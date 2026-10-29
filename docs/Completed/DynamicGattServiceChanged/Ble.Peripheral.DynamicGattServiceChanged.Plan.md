# Ble.Peripheral - Dynamic GATT Services and Service Changed Support

| Field | Value |
|---|---|
| Revision | 1.15 |
| Status | Implementation and software qualification complete; physical acceptance deferred and unrecorded |
| Branch | `feature/dynamic-gatt-service-changed` |
| Baseline | `1ca416abdfbe0f5fbafa220072bdef743f0e73a2` |
| Review snapshot | 2026-10-29 |
| Worktree | `C:\Git\esp-ble\ble.peripheral-dynamic-gatt-service-changed` |
| Original repository | `C:\Git\esp-ble\ble.peripheral` |
| Targets | ESP32-C3 and ESP32-WROVER |
| BLE library | NimBLE-Arduino 2.5.1 |

### Revision note

Revision 1.15 records software closeout for the disconnected-only structural-mutation scope. Physical C3/WROVER acceptance and device-reported identity evidence remain deferred and unrecorded. Live connected structural mutation and Robust Caching remain follow-up work; caching and dynamic-service configuration stay intentionally disabled.

## 1. Scope and workspace

### Goal

Add three operations through the existing Web API and UI: Add Service, Remove Service, and Send Service Changed. NimBLE's automatic Service Changed behavior after structural changes remains enabled. Preserve the existing static fixtures and their runtime values, descriptors, advertising behavior, security/pairing, serial commands, and other Web functionality.

### Managed services

- Allow at most four temporary services at once. Each has one user-specified canonical 128-bit service UUID and one canonical 128-bit characteristic UUID.
- The characteristic is plain READ only, with no CCCD or write, notify, indicate, encryption, authentication, or authorization flags.
- Initial hexadecimal value may be empty and is limited to 256 decoded bytes.
- Temporary services are volatile. Do not auto-advertise their UUIDs or edit/remove built-in services. This is not a general GATT editor.
- Structural add/remove is supported only with zero connected peers and no active directed advertising. This disconnected-only scope is accepted for now.
- Manual Service Changed dispatch does not alter the database and may be invoked while connected if GATT is Ready.

Do not add routes, serial commands, worker queues, frameworks, or configuration switches. Exclude stack patches/private production internals, bond migration or new automatic bond deletion, general GATT history/editing, resource-reporting APIs, and automatic restoration of changed fixture values.

### Setup, evidence, and authorization

The owner accepted clean `master` at `1ca416abdfbe0f5fbafa220072bdef743f0e73a2` as the setup baseline. Work is isolated at the recorded worktree and branch above; the original `master` worktree remains preserved. The worktree was established with:

```powershell
git worktree add C:\Git\esp-ble\ble.peripheral-dynamic-gatt-service-changed -b feature/dynamic-gatt-service-changed master
```

This baseline acceptance is a setup decision, not feature hardware evidence. It does not require unrelated historical physical matrices to be repeated. Planning, workspace setup, implementation, and software qualification are complete. The completion record separates historical r19 software evidence from the final closeout snapshot and identifies unrecorded physical checks.

The owner authorized implementation, deterministic checks, builds, commit cleanup, and local promotion. Local promotion is authorized and in progress. No flash/device deployment has been performed, and remote push remains unauthorized. Keep private local configuration out of evidence and documentation; do not read or reproduce credential values. Keep build-tree use coordinated and idle during owner upload operations.

## 2. Pinned-stack feasibility

### Configuration and known behavior

Preserve NVS bonding. Keep GATT caching and dynamic-service configuration disabled on both targets using `CONFIG_BT_NIMBLE_GATT_CACHING` / `MYNEWT_VAL_BLE_GATT_CACHING` and `CONFIG_BT_NIMBLE_DYNAMIC_SERVICE` / `MYNEWT_VAL_BLE_DYNAMIC_SERVICE`. Keep the related compile-time invariants in `src/NimBLEConfigChecks.cpp`. In the pinned `esp_nimble_cfg.h`, undefined effective aliases map to zero; reject enabled values and use supported preprocessor checks. Add no configuration switch.

On NimBLE-Arduino 2.5.1, `start()` can return success without rebuilding while peers remain connected; `removeService()` is `void` and can silently leave the native service registered; and the wrapper ignores the `ble_gatts_reset` return. GAP initialization and `add_svcs` can assert on `EBUSY` after an invalid reset attempt, so return codes alone do not prevent a crash. Guard every reset precondition and never call `start()` when any precondition has failed. Stage 1 must verify the pinned assertion behavior and effective assertion configuration. Treat add/remove wrapper outcomes as candidates for silent failure and verify actual native service/characteristic/descriptor presence and absence, UUIDs, assigned handles, properties, ATT permissions, and ownership after mutation. A boolean return is insufficient.

The native mutability guard requires no active GAP advertising, discovery, or connect procedure and no connections. Use the application-visible `server->getConnectedCount()` in a verified serialized host context, and fence the in-flight start/connection race. Stage 1 must identify and prove the permitted execution context for reset and rebuild. Do not call private/static `ble_gatts_mutable()` or invent a private peer-count API. At the reset boundary all relevant GAP procedures and extended advertising sets must be inactive. Ordinary active advertising is allowed before mutation and must be captured, stopped, then restored; reject active directed advertising.

### Repeated-reset resource gate

Pinned source review found `NimBLEService::start_internal` invokes `ble_gatts_count_cfg` on each GATT start/reset. The count call accumulates service, attribute, and client-configuration maxima; reset does not clear them, and start reallocates using those maxima. Establish the baseline from the registrations/counters contributed during GAP/GATT initialization, then include every repeated `NimBLEService::start_internal` contribution. Repeated rebuilds therefore need a source-backed bound on allocation growth before production rebuild is enabled.

Use the existing native harness pattern to verify pinned counter/reset/assertion behavior and bound the supported rebuild lifecycle. Stage 1 exits only after it documents the permitted reset/rebuild execution context; proves precondition guards prevent reset and `start()` after any failed precondition; verifies the effective assertion behavior for `EBUSY`; accounts for GAP/GATT initialization and repeated `NimBLEService::start_internal` counter contributions; exercises native lookup behavior after a silent failed add; and verifies extended-advertising clear/restore against GAP/host state. This is a feasibility gate, not hardware acceptance: record fixture-handle stability as a Stage 4 physical test requirement. Exercise production orchestration and the pinned adapter contract; a copied model or source-string check is not evidence. The exported `ble_gatts_stop` is only a candidate mitigation: its normal caller is host shutdown, and export alone does not establish live-reset safety. Do not call it unless its context, ownership, lock ordering, and wrapper-cache effects are proven. If safe bounded behavior requires a NimBLE/ESP-IDF change, private stack access, bond migration, or another excluded feature, stop before enabling production structural mutation and report the blocker. Hardware allocator, handle, subscription, and operating-system behavior remain final-acceptance evidence, not proven by the host gate.

## 3. Firmware safety properties and lifecycle

### Ownership, identity, and admission

`BluetoothManager` owns the NimBLE server and manual indication entry point. Keep temporary service ownership and structural work in the BLE layer; `FixtureGatt` owns the static fixture. Use boot-scoped monotonic service IDs that are never reused, including after failed allocation or removal; handle overflow explicitly. Never expose pointers or ATT handles as operation IDs.

Accept only the canonical 36-character hyphenated 128-bit UUID form. Normalize case and compare 16-byte values. Reject malformed, short, 16/32-bit, and Bluetooth Base UUID aliases; protected fixture service/characteristic UUIDs; existing or duplicate service UUIDs; and a characteristic UUID equal to its service UUID. Derive protected fixture UUIDs from production definitions, not a duplicate hand-maintained list. Parse initial values with existing `GattValue` helpers and the 256-byte decoded limit.

Keep a minimal lifecycle state outside Web-only compilation: Ready, Applying, RecoveryRequired. Initialize lifecycle to Ready independently of initial fixture readiness; `FixtureGatt::Ready()` remains false until fixture finalization. The two readiness states are independent. Add/Remove/Send admission requires both Ready and fixture readiness, using local state; do not use `GetValues()` as a readiness probe. Revalidate UUIDs, capacity, IDs/removal ownership, peer state, and directed advertising under transaction ownership before native mutation.

### Shared exclusion and structural operation

Use the smallest verified transaction/entry exclusion that prevents a readiness check from racing its call. It must coordinate structural work with:

- Web GATT host jobs, including the `runHost` readiness-check-to-`hostWorkPending` CAS. Use a narrow barrier or reservation covering the full readiness-check/CAS sequence; exclude new snapshots before invalidating fixture readiness. A GET that wins admission completes before mutation; a GET that loses returns unavailable. If an existing job cannot drain safely, fail before native mutation. Do not publish a completed failure while queued work may mutate later.
- Serial advertising mutators at `ProcessMenu::handleConfigInput` and relevant `processInput.cpp` completions, including buffered-line submission. Recheck a buffered mutation when submitted and restore `consoleMode` to Config on rejection. Keep `Q` and `W` available. Reject only advertising mutations while Applying/RecoveryRequired; do not add a bypass or guards inside every public `Advertising` method.
- Existing serial `D`/`S` bond-deletion handlers, whose reconciliation can change directed targets, stop an instance, or cancel a restart. Hold the same entry lease across deletion and reconciliation and return Busy for these two handlers during Applying/RecoveryRequired. Preserve their Ready behavior and unrelated security commands. Existing Web bond operations remain serialized by the Web worker.
- Pending restart state and automatic callbacks. Add a narrow `Advertising` accessor for pending-restart state and a rearm operation for preserved eligible intent. NimBLE host callbacks must never block waiting for the structural gate; defer nonblockingly or use generation fencing. Timer-daemon callbacks must not perform conflicting work during Applying. Account for a running callback, intent changes before StopAll, and callbacks expiring after re-arm. A timer delay is not synchronization.

Keep lock ordering narrow. Never hold `Advertising::configMutex_` across a native helper that takes it, timer scheduling, or a wait. Never hold a lock needed by a host callback while waiting for that callback. Prove the native execution context against the pinned stack; do not perform synchronous host round trips from a host callback.

### Structural lifecycle

Use this ordered property flow; implementation may choose the simplest safe internal mechanism:

1. Reserve the existing worker and exclusive transaction entry. Verify lifecycle Ready and `FixtureGatt::Ready()` before changing either. Establish the host-job barrier/reservation across the complete readiness-check/CAS; let an existing job finish safely or fail before mutation.
2. Revalidate request identity/capacity and reject active directed advertising. Ordinary active advertising is permitted. Atomically enter Applying and invalidate fixture readiness before unsafe database access.
3. Snapshot authoritative advertising configurations, active states, pending restart intent, and generations coherently. Call `Advertising::StopAll()` and verify stop/cancellation. Fence new starts and running callbacks.
4. Immediately before mutation, require `server->getConnectedCount() == 0` in a verified serialized context and all GAP advertising/discovery/connect procedures and extended sets inactive. Perform native work in the execution context proven safe for the pinned stack. Add/remove/rebuild, then verify registration, fresh assigned handles, UUIDs, properties, ATT permissions, ownership, presence, and removed-service absence using exported lookup APIs. A successful `start()` alone is not proof. Do not call private `ble_gatts_mutable()`.
5. Verify the complete fixture and stack topology, including `0x1801`, `0x2A05`, and stack CCCD where present. Rebuild `0x2905` from actual handles; preserve `0x2901`, `0x2904`, `0x2906`, and legitimate runtime characteristic values. Rebuild finalization validates structure, assigned handles, and descriptor metadata but skips initial startup-seed value checks. Do not reset changed fixture values to startup seeds.
6. After native database and fixture verification, restore `FixtureGatt::Ready()` while lifecycle remains Applying and transaction/host-job ownership is held; this allows existing advertising readiness checks to pass. Then reconstruct advertising from current authoritative configuration, restore previously active eligible sets, and re-arm eligible pending restarts last. Report timer creation/start failures. Finish only after registration and restoration attempts are truthful; then transition to Ready or RecoveryRequired and release ownership. A timeout must retain operation lifetime/ownership until completion or cancellation before native work.

`Advertising::configs_` is authoritative. C3 GATT reset clears all extended advertising sets through `NimBLEServer::resetGATT` -> no-argument `NimBLEDevice::stopAdvertising` -> `NimBLEExtAdvertising::stop` -> `ble_gap_ext_adv_clear`. A by-value `std::vector<bool>` exposes proxy references that can mutate the returned copy; do not characterize its clear as broken on that basis. The wrapper status remains a cache, so inspect GAP/host extended-set state before reset and verify host state after clear. Rebuild every configured set, including previously inactive sets. Preserve active and eligible pending intent/generations using the `Advertising` pending-restart accessor and rearm operation. Snapshot before StopAll; reconstruct and Start before final re-arm; schedule outside `configMutex_`. Make timer allocation/start failures observable as incomplete restoration. Do not fabricate advertising events or restore a stale directed target after D/S reconciliation.

Invalidate the managed service pointer as soon as the wrapper may delete it; handle zero is never ordinary removal cleanup. A failure known to leave the database unchanged may restore readiness/advertising and return Failed without registration. A retry is optional and allowed only if separately proven safe. If any native state is uncertain, invalidate affected handles, prevent advertising/new GATT work, and enter RecoveryRequired; return `Failed` with `gatt_recovery_required` and put the required action first in bounded `detail` (at most 255 UTF-8 bytes). Require restart when safe recovery has not been demonstrated. Do not promise atomic rollback. If GATT is verified but advertising restoration is partial, return `Applied` with `advertising_restore_incomplete` and refresh the service inventory.

Manual indication calls `sendServiceChangedIndication()` for the full attribute range. It requires Ready and fixture readiness at execution, may be invoked while connected, and reports Dispatched after the `void` API call. It proves neither radio delivery nor acknowledgement. Preserve NimBLE's automatic Service Changed behavior. If relevant fixture handles move, check the risk of bonded subscriptions mapping to unrelated attributes; do not expand into bond migration/deletion.

## 4. Web API and UI contracts

Keep `/api/gatt`; add no route, worker queue, status-data family, or serial command. Reuse authentication, CSRF, Host/Origin checks, strict JSON parsing, the single-slot worker, operation IDs, and existing result states. Add/Remove/Send Service Changed emit no `OperationData` family and no `data` object.

### Requests and inventory

Each line below is a separate POST request.

```json
{"op":"addService","serviceUuid":"12345678-1234-5678-1234-56789abce100","characteristicUuid":"12345678-1234-5678-1234-56789abce101","valueHex":""}
{"op":"removeService","serviceId":"<boot-scoped-service-id>"}
{"op":"sendServiceChanged"}
```

`valueHex` is required but may be empty. Use bounded binary UUIDs and the service-ID rules in section 3. Do not expose native pointers or handles.

GET retains existing `values` and adds only `state`, `services` (protected fixtures plus temporary services), `serviceLimit`, `valuesAvailable`, `servicesAvailable`, and `reason`. `servicesAvailable` is true only when lifecycle is Ready, fixture readiness is true, and the managed inventory is valid. `valuesAvailable` is true only after a safe successful host snapshot; do not infer it from lifecycle state. The transaction's host-job exclusion makes new value snapshots unavailable during Applying. For unavailable collections, return `null` and a reason, not an empty successful collection. Omit `reason` when both collections are available. A Busy or NotReady GATT snapshot remains HTTP 200 with `valuesAvailable:false`; a genuine Failed snapshot is HTTP 500. Keep this mapping specific to GATT snapshots.

Use existing `/api/status` connection/advertising fields for connected peers and directed state; do not duplicate them in `/api/gatt`. Manual Service Changed admission uses local readiness, not `GetValues()`, and remains available when a value snapshot is independently Busy.

Retain the 1024-byte POST body limit and 6144-byte HTTP/worker stacks. Bound worst-case inventory serialization at four services. Keep error detail within the existing 255 UTF-8 byte limit; use the existing result/error fields.

### UI

Extend existing GATT Controls and Test Scenarios with service/characteristic UUID, initial value, Add/Remove, and Send Service Changed. Keep the four focused scenarios: manual indication while connected; add then reconnect/discover; remove then reconnect/discover; bonded cache behavior. Scenarios instruct/prefill but do not mutate automatically.

Reuse polling, operation feedback, dirty-field protection, and existing error handling. Disable Add/Remove while peers are connected or directed advertising is active; manual indication remains available while connected when GATT is Ready. When collections are null/unavailable, preserve but visibly mark prior values/inventory as stale, display `state`/`reason`, disable only controls that depend on unavailable data, and preserve unfinished input. Do not treat null as empty. Manual indication depends on GATT readiness, not an unrelated snapshot. After `Applied` with `advertising_restore_incomplete`, refresh inventory and visibly show the warning. For `gatt_recovery_required`, show the required action, disable GATT mutations, and do not present stale inventory as current. Keep existing characteristic value publication unchanged; regenerate `src/webAssets.cpp` with the current generator.

## 5. Proportionate verification and physical acceptance

### Deterministic/software checks

Include executable successful-startup and injected-failure tests that exercise production orchestration and validate adapter contracts against the pinned stack.

Extend existing Python/C++/Node harnesses and `test/README`; use the smallest meaningful production-oriented adapters needed to test these contract groups. Test production orchestration against pinned behavior, not a parallel algorithm or source-string checks.

| Contract group | Required evidence |
|---|---|
| Registry and inputs | Add/remove/recreate, four-service capacity, non-reused IDs, overflow, canonical/case-normalized UUIDs, protected UUID derivation, duplicate rejection, 256-byte values, and properties distinct from ATT permissions. |
| Pinned GATT lifecycle | Repeated-reset counters include initialization and rebuild contributions and are bounded or the unsafe path is disabled; successful registration is verified through native lookups; false-success start, silent failed-add/removal, registration failure, pointer invalidation, and recovery outcomes are truthfully handled. |
| Concurrency and entry | GET readiness/CAS race and queued-job lifetime; execution-time revalidation; serial advertising and D/S reconcile entry, buffered resubmission and Config restoration; nonblocking host callbacks; pending timer/callback races and timer-allocation/start failures. |
| Fixture and advertising | Complete fixture/stack topology and handle lookup; rebuild finalization checks structure, handles, and descriptor metadata while skipping initial seed-value checks; publish a changed fixture value, rebuild, and verify the value and `FixtureGatt::Ready()`; refresh `0x2905`; preserve ordinary active, inactive, and eligible pending advertising; reconstruct all C3 sets; check bonded subscription risk if handles move. |
| API/UI | Initial readiness, independent collection availability, Busy/NotReady vs Failed HTTP mapping, null/stale state, preserved drafts, and warning/recovery feedback. Existing publication and authentication behavior remains intact. |
| Targets and bounds | Serial-only and Web-disabled compatibility; all eight C3/WROVER mode combinations; bounded four-service serialization and existing stack/body limits. |

Use applicable existing suites (`descriptor_fixture_native`, `descriptor_startup_native`, `web_status_native`, `gatt_publication_native`, WebValidation, pairing/advertising regressions, and `web_ui_regression.cjs`). Add a focused check only when existing harnesses cannot express a required contract. Update `tools/validate-descriptor-fixture.ps1` input hashing only for new owned inputs that it must cover; do not create a general validation framework. Follow `test/README` for suite-specific commands. Preserve the generated-asset check:

```powershell
python tools/generate-web-assets.py --check
```

Focused checks may run during implementation. For the final unchanged snapshot, run applicable host suites and one sequential eight-build matrix:

```powershell
pwsh -NoProfile -File tools/validate-descriptor-fixture.ps1
```

The matrix covers modes 0-3 on C3 and WROVER. It does not replace standalone host suites. Keep Web code compiled out when disabled; keep serial code independent of Web. Do not run concurrent PlatformIO commands in one build tree. Build/static evidence is not physical acceptance.

### Firmware-level physical acceptance

Physical C3/WROVER acceptance remains deferred and unrecorded. No physical matrix or device-reported identity evidence is present in this closeout. If physical acceptance resumes, use the cases below; they describe future checks and do not claim results.

| Case | Evidence |
|---|---|
| Service lifecycle | Add, read, remove, and recreate up to the limit. Disconnect for structural changes, reconnect, explicitly rediscover, and verify added service/value and removed service absence. |
| Service Changed and bonded state | Invoke manual indication while connected and record dispatch separately from any observed Windows event and rediscovery. Observe automatic behavior on reconnect after structural change. Exercise bonded and unbonded Windows connections and verify existing fixture descriptors/subscriptions remain valid. No packet is required from a disconnected client. |
| Advertising and bounded stability | Verify ordinary active advertising is stopped/restored, inactive configuration is rebuilt, eligible pending restart intent survives, and C3 sets return. Record fixture handle stability across a rebuild for each target and check bonded subscription mapping if handles move. Choose and record a short bounded rebuild count based on the source/capacity risk findings; observe heap and relevant task-stack headroom. Host-task stack high-water is not exposed through `/api/status.resources`; measure it with a temporary uncommitted probe. Investigate persistent growth or unsafe headroom, not normal measurement noise. |
| Provenance and failures | Record device-reported firmware revision, full fingerprint, environment, tested image/client identity, and category results. Registration failure injection is required in deterministic tests, not as a forced physical fault. Add a diagnostic image only to resolve a concrete unknown; identify its evidence separately and verify a production image's basic operation afterward. |

If Windows does not report a Service Changed event, record it as not observed and distinguish firmware dispatch from client/OS behavior; do not claim receipt or automatically fail firmware acceptance on an absent OS event. Resolve evidence of firmware failure; client/OS limitations remain a separate follow-up. Firmware acceptance requires verified database/lifecycle behavior, safe failure handling, and basic Windows discoverability. Device-reported identity is required; a local image hash alone is insufficient.

## 6. Execution, closeout, and follow-up

| Stage | Work and exit condition |
|---|---|
| **1. Feasibility** | Verify pinned counter/reset/assertion behavior and a bounded safe application lifecycle. Document the permitted reset/rebuild execution context; prove guards prevent reset and `start()` after failed preconditions; verify effective `EBUSY` assertions; include GAP/GATT initialization plus repeated `NimBLEService::start_internal` counter contributions; exercise silent failed-add lookup semantics; and verify extended-advertising clear/restore against host state. Identify fixture-handle stability risk and its Stage 4 physical test, without requiring hardware evidence at this stage. If a safe path requires excluded stack/bond work, stop before enabling structural mutation and report the blocker. This is an internal dependency gate, not a separate owner-signoff milestone. |
| **2. Implementation** | In one continuous pass, implement the registry, lifecycle exclusion/recovery, Web contract, and focused existing-harness coverage, including the `Advertising` pending-restart accessor/rearm operation and rebuild-aware fixture finalization. Root retains architecture, scope, evidence, and Git topology; Luna Max may handle bounded non-overlapping work. Coordinate file ownership and build trees. |
| **3. Software qualification** | Once docs/source are final, run `python tools/firmware_revision.py prepare` before validation/handoff; an unchanged rerun must not increment revision. Run applicable existing host suites, asset check, and the eight-build matrix on one unchanged snapshot. Repeat only affected checks after changes. |
| **4. Physical acceptance** | Complete the firmware-level C3/WROVER and basic Windows cases above after software qualification. Keep diagnostic and production-image claims separate. |
| **5. Review and closeout** | Follow `AGENTS.md` and `docs/Feature-Closeout.md`; review evidence, scrub sensitive logs, and inventory the branch/worktree/artifacts. Software-qualified, physically accepted, and promotion-ready are separate states. Commit cleanup and local promotion are authorized and in progress; remote push remains a separate action requiring authorization. |

Implementation and software qualification are complete for the disconnected-only structural-mutation scope. Commit cleanup and local promotion are authorized and in progress. No flash/device deployment has been performed; no remote push is authorized. See the [completion record](Completion.md) for historical evidence, final-manifest location, and deferred acceptance.

### Non-gating client/platform follow-up

Current Windows and Android Ble.Net backends already implement the Service Changed notification contract; no client change is expected for this firmware feature. Broader Windows/Android suites, instrumentation, integration, or physical event-receipt work remains follow-up. Live connected structural mutation is unsupported in this accepted scope; Add and Remove require zero connected peers. Robust Caching is also deferred, and current GATT caching/dynamic-service configuration remains intentionally disabled. Manual Service Changed can be dispatched while connected, but dispatch does not prove client or operating-system receipt.

### Implementation record (2026-10-29)

`src/dynamicGatt.cpp` owns the bounded registry and the Ready/Applying/RecoveryRequired lifecycle. The existing single Web worker dispatches the three new operations; native mutation is synchronous within one event on NimBLE's existing parent event queue. A nonblocking application entry lease excludes serial advertising/bond reconciliation, host-job readiness/CAS admission, and timer/host advertising callbacks. Deferred callback payloads and intent generations preserve restart intent without blocking a host callback on that lease.

The source-backed native sequence is idle precheck, wrapper mutation, checked `ble_gatts_reset()`, exported `ble_gatts_stop()`, wrapper `start()`, and native/fixture verification. Removal must be marked and checked while its registered service handle is still valid, before reset. Reset empties visible/hidden ATT lists using the live pool; only then may stop free the pools and clear all three capacity counters. Both stack services and application services are recounted into fresh pools. Production uses no private host-lock or mutability helper. A native failure or uncertain baseline quarantines GATT and requires restart. Known unchanged precondition rejection restores readiness/advertising. A verified database with partial advertising restoration reports Applied with a warning.

The deterministic harness executes production lifecycle/transaction/admission code and extracted pinned stack functions. Its 128-cycle counter exercise establishes a bounded software allocation model; it is not device allocator, radio, task-stack, subscription, or operating-system evidence. GAP, fixture characteristic/descriptor/CCCD, and stack Service Changed handles are checked for stability at each rebuild to protect handle-keyed bonded CCCDs. Device observation of those contracts remains part of Stage 4.

The native fixture regression publishes changed Temperature bytes, reassigns fixture handles through its registration adapter, and verifies rebuild finalization reaches Ready without replacing runtime bytes with startup seeds. Advertising reconstruction restores all configured sets before active starts and pending-restart re-arm; timer allocation/start failures remain observable. GET and UI tests cover independent null collections, stale displays, preserved drafts, initial readiness, and warning/recovery feedback.

Run the focused additions before the existing host suites and final eight-build matrix:

```powershell
python test/dynamic_gatt_native/run.py
python test/dynamic_entry_native/run.py
python test/dynamic_recovery_native/run.py
python test/dynamic_topology_native/run.py
python test/dynamic_advertising_native/run.py
```

Private local configuration is excluded from evidence and documentation; this record contains no credential values. No flash/device deployment or bonding changes were performed. Commit cleanup and local promotion are authorized and in progress; no remote push is authorized. Physical C3/WROVER acceptance and host-task stack measurement remain deferred and unrecorded; use the temporary uncommitted probe described above if measurement is pursued.
