# Ble.Peripheral — Descriptor-Driven GATT Fixture Plan

**Revision:** 1.9 (owner-directed software closeout, 2026-10-27)

**Status:** Implementation complete; software closeout and local promotion authorized by the owner on 2026-10-27. Detailed physical/platform acceptance remains unverified; this archive does not claim those tests passed. See the [completion record](Completion.md).

**Reviewed:** 2026-10-27 (closeout; initial source review 2026-10-24)

**Repository baseline:** `master`, `d38a56fb67d2c48f1ad9c691b2700e38d407affe`, clean working tree before this documentation change

**Targets:** ESP32-C3 and ESP32-WROVER; pinned NimBLE-Arduino 2.5.1

## 1. Purpose and boundaries

Provide deterministic physical GATT data for Ble.Net's **BLE Presentation Format, Value Interpretation, and Valid Range Support Plan**, currently at `C:\Git\dotnet-ble-win\docs\Backlog\BLE Descriptor driven Value Interpretation\BLE Presentation Format, Value Interpretation, and Valid Range Support Plan.md` (companion checkout, not a dependency of this repository).

Deliver one additive service, generic browser publication/restoration for three explicitly registered characteristics, and static scenario instructions with optional prefill. Firmware supplies bytes and metadata; Ble.Net owns semantic resolution and decoding. No Ble.Net source or API changes belong to this task.

Preserve existing service/characteristic UUIDs, initial values, custom descriptor, ATT write callbacks, advertising payloads, security, pairing, and serial controls. Do not advertise the new service UUID. New GATT fixtures exist in all four interface modes; HTTP and browser controls compile only when `BLE_ENABLE_WEB_INTERFACE` is enabled. Overrides are runtime-only, with explicit restoration and no NVS persistence.

The owner authorized implementation after this plan review. The owner subsequently authorized commits, local promotion, and scoped cleanup on 2026-10-27. Flashing remains with the owner and remote push is not authorized. These actions follow the [feature closeout procedure](../../Feature-Closeout.md).

## 2. Verification findings and changes from revision 1.6

| Finding | Disposition |
| --- | --- |
| Five scalar fixtures already include TemperatureScalar; the implementation list counted it twice. | Exactly **seven** new characteristics: five scalars, one AggregateValue, one trigger. |
| The editable page is `web/index.html`, not `src/web/index.html`. | Use the existing generator and tracked `src/webAssets.cpp`. |
| `FixtureGatt::Create()` currently returns the original READ/WRITE characteristic; the caller discards that result. | Preserve original fixture construction; add only minimal owned references/inventory and finalization. No broad return-type/API redesign is required. |
| `SetupBluetooth()` marks `initialized_` early and ignores the Boolean return from `server_->start()`. Advertising can also start through reconfiguration and restart paths. | Add a separate readiness invariant after successful registration/finalization; guard every actual advertising start. |
| Installed NimBLE 2.5.1 exposes `create2904()` and `createDescriptor(const NimBLEUUID&, uint32_t properties, uint16_t maxLen)`. Defaults permit descriptor writes. | Explicit READ permissions and lengths for all new descriptors; binary, length-aware initialization. |
| Descriptor creation accepts `NIMBLE_PROPERTY` flags but converts them to ATT permissions. Descriptor getters return `BLE_ATT_F_*`; characteristic getters retain characteristic flags. | Compare READ-only descriptor getters to `BLE_ATT_F_READ` (`0x01`), never characteristic `NIMBLE_PROPERTY::READ` (`0x02`). Verify the pinned stack's conversion in deterministic executable tests. |
| `setValue(const uint8_t*, size_t)` on local attributes returns `void`; the underlying value implementation can clear the old length before rejecting oversize data. | Validate before mutation; compare stored bytes after every set/restore. Never infer success from the setter call. |
| No-argument `notify()` and `indicate()` both reach `ble_gatts_chr_updated(handle)`; custom-value broadcast iterates connected peers. | Use stored-value broadcast only. Its Boolean result is dispatch evidence, not subscriber count, delivery, or indication acknowledgement. |
| Existing ATT trigger callbacks send fixed custom payloads on the original characteristics and do not replace the stored value with that event payload. | Preserve that behavior; generic local publication must not invoke `onWrite` or pretend to be an ATT write. Subscription guarantees below apply to the new generic path and new trigger. |
| HTTP body limit is 1,024 bytes, with one deferred worker and nine registered URI handlers. | Bound payloads explicitly, keep the request limit, and allow eleven URI handlers for the two added routes. |
| The test tree has pairing, validation, browser, and advertising regression checks; no maintained central-side descriptor test runner or source-owned build-matrix helper was found. | Extend focused checks and record eight explicit builds; reuse external tooling only when actually available. Do not build a test framework. |
| Android's public descriptor API does not expose an ATT-handle getter. | Raw Aggregate readability is required; authoritative resolution is conditional on backend capability. No reflection/private API workaround. |
| Ble.Net phases distinguish native metadata, raw metadata, Aggregate resolution, scalar interpretation, Valid Range, and Explorer presentation. | Firmware acceptance and downstream phase acceptance are separate; later decoder/UI work does not block fixture delivery. |

Reviewed local sources: [`fixtureGatt.h`](../../../src/fixtureGatt.h), [`fixtureGatt.cpp`](../../../src/fixtureGatt.cpp), [`bluetoothManager.cpp`](../../../src/bluetoothManager.cpp), [`advertising.cpp`](../../../src/advertising.cpp), [`webInterface.cpp`](../../../src/webInterface.cpp), [`bleConfig.h`](../../../src/bleConfig.h), [`platformio.ini`](../../../platformio.ini), [`web/index.html`](../../../web/index.html), [`generate-web-assets.py`](../../../tools/generate-web-assets.py), and [test guidance](../../../test/README). Installed C3/WROVER headers and implementation were inspected under `.pio/libdeps/<environment>/NimBLE-Arduino/src`; they are verification inputs, not tracked deliverables. Recheck these facts when implementation starts.

## 3. Canonical GATT manifest

### 3.1 UUIDs and exact inventory

Use new service UUID `12345678-1234-5678-1234-56789abcdf00`. In the table, suffixes replace the final four digits of that UUID. These proposed UUIDs do not collide with current fixture constants; freeze them with implementation.

| Characteristic | UUID suffix | Properties | Initial value | CPF bytes (`0x2904`) | Exact `0x2901` text |
| --- | --- | --- | --- | --- | --- |
| ExponentZero | `df01` | READ | `EB 00` | `0E 00 2F 27 01 00 00` | `Exponent Zero` |
| ExponentPositive | `df02` | READ | `EB 00` | `0E 7F 2F 27 01 00 00` | `Exponent Positive` |
| ExponentNegativeMin | `df03` | READ | `EB 00` | `0E 80 2F 27 01 00 00` | `Exponent Negative Minimum` |
| ExponentNegativeTwo | `df04` | READ | `EB 00` | `0E FE 2F 27 01 00 00` | `Exponent Negative Two` |
| TemperatureScalar | `df05` | READ \| NOTIFY | `EB 00` | `0E FF 2F 27 01 00 00` | `Temperature Scalar` |
| AggregateValue | `df06` | READ | `EB 00 2A` | `04 00 00 27 01 00 00` | `Aggregate Value` |
| TemperatureUpdateTrigger | `df07` | WRITE | Empty | None | `Temperature Update Trigger` |

New descriptor totals: **seven `0x2901`, six `0x2904`, one `0x2905`, one `0x2906`, and one NimBLE-managed `0x2902`**. Do not manually create a CCCD. No other new descriptor types or characteristics are required.

Create each CPF with `create2904()` and explicitly set format, signed exponent, unit, namespace, and description. Formats `0x0E` and `0x04` mean sint16 and uint8; units `0x272F` and `0x2700` mean degrees Celsius and unitless. Namespace is `0x01`; description is `0x0000`. See [Bluetooth Assigned Numbers, Presentation Format and units](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Assigned_Numbers/out/en/index-en.html).

The five scalar values encode raw integer 235. Expected mathematical values are, respectively, `235`, `235 × 10^127`, `235 × 10^-128`, `2.35`, and `23.5`, all in degrees Celsius. Extreme exponents are projection/precision tests, not physically plausible temperatures. Firmware does no scaling. Client evidence must retain the signed exponent and must not silently overflow, underflow, or claim a rounded result is exact.

All seven descriptions are READ-only ASCII UTF-8 with no terminating NUL. Set maximum length to the exact encoded length and use `setValue(reinterpret_cast<const uint8_t*>(text), sizeof(text) - 1)` for array constants. Create one per new characteristic, before registration. They are labels, not value semantics. No Writable Auxiliaries or `0x2900` is needed. Core GATT defines one User Description per characteristic and permits descriptor order to vary: [Core 5.4, Vol 3, Part G, §3.3.3.2](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host/generic-attribute-profile--gatt-.html).

Scalar maximum stored lengths: two bytes for the four READ-only scalars; TemperatureScalar may retain the normal characteristic maximum so generic controls can supply arbitrary test bytes. AggregateValue has maximum length three; trigger maximum length one. Unsupported trigger writes that reach the callback are ignored; oversized writes may be rejected by ATT.

### 3.2 AggregateValue

Create exactly one READ-only `0x2905`, maximum length four, with a four-byte placeholder before registration. After successful registration replace it with the two actual assigned descriptor handles, each uint16 little-endian:

1. `TemperatureScalar.0x2904` — sint16, exponent -1, Celsius.
2. `AggregateValue.0x2904` — uint8, exponent 0, unitless.

This describes the constant AggregateValue payload as `[23.5 °C, 42]`. Its local CPF describes the second component, not the entire three-byte payload. The Aggregate list includes that local CPF and a sibling reference, which GATT permits: [Core 5.4, Vol 3, Part G, §3.3.3.6](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host/generic-attribute-profile--gatt-.html).

References share **format metadata**, not live characteristic values: changing TemperatureScalar never updates AggregateValue. The Aggregate fixture deliberately detects clients that incorrectly take a one-local-CPF fast path while `0x2905` exists. A client unable to map ATT handles must report unresolved Aggregate semantics rather than interpret the whole payload as uint8 or guess discovery order.

Never hardcode handles or use adjacent descriptor positions. Retain pointers to the intended CPF objects; verify nonzero distinct handles, UUID `0x2904`, parent ownership, four-byte list order, and readback equality. Adding User Descriptions changes assigned handles without changing these rules.

### 3.3 Valid Range

Attach exactly one READ-only `0x2906`, maximum length four, to ExponentNegativeTwo; initialize before registration with binary bytes `70 FE E8 03`. It contains lower raw sint16 `-400` followed by upper raw sint16 `1000`, giving inclusive bounds `-4.00 °C` and `10.00 °C`. The baseline is `2.35 °C`, inside the range.

Bounds reuse the parent's datatype, units, and represented-value transform, as specified in [GATT Specification Supplement, §4.1](https://btprodspecificationrefs.blob.core.windows.net/gatt-specification-supplement/GATT_Specification_Supplement.pdf). This is a single-field metadata fixture, not firmware range enforcement. Do not attach Valid Range to AggregateValue or make its parent writable. The different exponent/range from the companion plan's illustrative vector is intentional; evidence must use this manifest. Boundary and malformed-data decoder tests remain deterministic Ble.Net tests.

### 3.4 Absence and original fixtures

Keep the original service `12345678-1234-5678-1234-56789abcdef0` unchanged:

| Existing target | Initial value | Required preservation |
| --- | --- | --- |
| `...def1` | ASCII `Ima Characteristic` | READ/WRITE; no `0x2901`, `0x2904`, `0x2905`, or `0x2906` |
| `...def2` | ASCII `Notify Characteristic` | READ/WRITE/NOTIFY, existing callback and custom READ/WRITE descriptor `...def4` (`Custom Descriptor`) |
| `...def3` | ASCII `Indicate Characteristic` | READ/WRITE/INDICATE and existing callback |

TemperatureUpdateTrigger has `0x2901` but no CPF/Aggregate. Use readable `...def1` as the primary no-format value fixture; the write-only trigger is only a metadata-absence check. ExponentZero has CPF/User Description but no Valid Range. Descriptors must remain associated with their own parent, never a neighboring characteristic.

### 3.5 Notification trigger

On a central-originated acknowledged ATT write of exactly `01` to TemperatureUpdateTrigger, invoke **no-argument** `TemperatureScalar.notify()` without changing TemperatureScalar's stored bytes. This works in headless and serial-only modes as well as web modes. No valid write restores baseline implicitly. Other accepted payloads produce no publication. Use normal CCCD handling; require positive delivery with an enabled subscription and no delivery after unsubscribe. Log dispatch failure without treating it as ATT semantic validation.

## 4. Registration and advertising readiness

Keep the lifecycle small:

```text
Store default advertising instance 0 for Web/serial inventory (no radio apply)
  -> Create original and new services, characteristics, descriptors, and baseline constants
  -> server_->start() must succeed
  -> FixtureGatt::Finalize() resolves CPF handles and fills Aggregate bytes
  -> verify descriptor bytes, lengths, permissions, identity, and ownership
  -> publish fixture-ready state
  -> apply radio configuration and start through existing Advertising module
```

`Finalize()` returns `bool`, creates no attributes, and is safe to check repeatedly without re-registering the table. No schema changes or reset/re-registration are permitted after readiness. Descriptor **value** finalization after registration is necessary for `0x2905`; it is not a schema mutation.

Keep readiness false by default and on any creation, registration, or finalization failure. Log the failing stage and remain non-advertising until reboot; no new recovery framework. `initialized_` alone is insufficient. A manual web/serial start must not bypass failure.

Default instance 0 must remain present in both UI inventories on GATT failure. Store its original configuration before GATT failure branches. Defer applying advertisement data until ready: NimBLE 2.5.1's C3 `setInstanceData()` implicitly calls `server->start()`. Guard the shared rebuild path as well as actual radio starts so configuration cannot register an incomplete GATT table.

Audit and gate actual `Adv()->start(...)` calls, including `startInstanceLocked`, directed starts, active reconfiguration helpers, Start/StartAll, and delayed restarts. Publish readiness across tasks safely. Keep NimBLE's independent advertise-on-disconnect path disabled (currently false by default). Do not hold advertising/configuration locks while finalizing or introduce lock-order dependencies into GATT construction. Preserve existing stop-completion behavior.

## 5. Generic publication and restoration

### 5.1 Fixed eligible inventory and baselines

Register only original `...def2`, original `...def3`, and TemperatureScalar. Each entry holds its application-owned characteristic reference, service/characteristic UUIDs, friendly name, properties, maximum stored length, immutable baseline bytes, and an opaque boot-scoped identity. A boot token plus a small fixed entry ID is sufficient; raw pointers and client-supplied handles are not identities.

Do not scan internal NimBLE collections, enumerate arbitrary future services, or add general GATT introspection. Capture baselines from constants/initialization before any external writes; never redefine baseline from current bytes. Compare actual stored bytes to show Baseline, Modified, or Unavailable. Central ATT writes can also modify the original fixtures.

### 5.2 Input and transport bounds

Accept empty values and hexadecimal byte pairs, compact or separated by ASCII whitespace **between bytes**. Reject odd digits, invalid characters, whitespace splitting a byte, and decoded oversize values before mutation. Normalize browser display as uppercase byte pairs and submit compact hex.

Use a fixed **256-byte decoded Web payload cap**, additionally limited by the characteristic's maximum stored length. This keeps compact hex plus the small command identity inside the existing 1,024-byte HTTP body limit. Raw requests above that limit still fail; whitespace is not grounds to enlarge it. The worker owns the decoded buffer and explicit length, including embedded zeroes. No request/JSON pointer may outlive the handler.

Before publication, reject payloads exceeding `ATT_MTU - 3` on any currently connected peer. This conservative bound avoids a new per-peer subscription inventory and prevents knowingly truncated publication; it can also reject a long payload because of an unsubscribed peer's smaller MTU. At MTU 23 the limit is 20 bytes. Expose that limitation in the UI. Recheck at execution; connection changes can still affect actual delivery. Do not fragment, truncate, negotiate MTU automatically, or add long-notification machinery. Restoration has no MTU restriction because it does not publish.

### 5.3 Operations and truthful results

Add authenticated `GET /api/gatt` for the three-entry inventory/current snapshots and `POST /api/gatt` for only `publishValue` and `restoreValue`. Reuse the existing single worker, admission errors, boot-scoped operation identity, `/api/status` result polling, Basic authentication, CSRF and Host/Origin validation. Update route capacity from nine to eleven and verify route registration. Do not introduce another queue or command framework.

Revalidate readiness, boot/entry identity, properties, lengths, and MTU bounds in the worker. For publication, set the stored bytes, read back and verify equality, then call the no-argument stored-value `notify()` or `indicate()`. Current targets support exactly one of those properties, so no dual-capability policy is needed. Do not call their ATT callbacks or use custom-value/per-connection publication overloads.

Report storage verification separately from publication dispatch in existing result detail/data: a successful dispatch is `Dispatched`, not proof of receipt. No connected peers/subscribers can still produce a successful library return. A later indication acknowledgement is not established by that return. If setting succeeded but dispatch failed, retain and report the modified value; no silent rollback. Readback failure must not be reported as success or followed by publication.

Restoration sets the immutable baseline and verifies readback; return `Applied` only on equality. It never publishes or changes descriptors. Selection, polling, scenario prefill, browser refresh, and reconnect never restore automatically.

Worker serialization covers HTTP mutations only. Existing central ATT writes are independent; preserve their callbacks and avoid claiming transactional ordering with them. Review shared value access against NimBLE's actual synchronization, and serialize any new application-owned compound state appropriately without holding application locks across re-entrant BLE calls. Acceptance sequences must avoid simultaneous central writes to the same target. Snapshots show observed current bytes, not guaranteed per-peer transmitted bytes. Concurrent-write transaction semantics and legacy callback repairs are outside scope.

## 6. Browser controls and scenarios

Edit [`web/index.html`](../../../web/index.html). With just three targets, use one selector showing friendly name and UUIDs, properties, current/baseline hex, modified state, a hex editor, capability-specific Send notification/Send indication, and Restore baseline. Preserve edited input during polling; refresh observed values separately. Display maximum accepted length and the transport limit. Explain that dispatch requires client-side receipt verification.

Add one collapsible **GATT Test Scenarios** section backed by static manifest data, not firmware-specific scenario operations:

| Scenario | Central action and expected evidence | Browser prefill |
| --- | --- | --- |
| Descriptions | Read seven `0x2901`; compare exact bytes/text and parents; original `...def1` has none | None |
| Scalar metadata/exponents | Read five scalar values/CPFs; compare manifest; record signed/native exponent projection separately from decoding | None |
| Aggregate | Read `EB 00 2A` and four-byte handle list; verify authoritative mapping when available, otherwise unresolved semantics | None |
| Valid Range | Read parent/CPF and `70 FE E8 03`; expected raw bounds -400/1000 and represented bounds -4/10 | None |
| Absence | Inspect original `...def1`, trigger, and ExponentZero as specified above | None |
| Temperature read/update | Restore baseline; read `EB 00`; subscribe, then write trigger `01` or publish `F5 00`; expected update raw 245 / 24.5 °C | TemperatureScalar, `F5 00` |
| Original notification/indication | Subscribe to the selected original target; publish known bytes; compare exact receipt and later read; restore baseline | Corresponding target, `01 02 03` |

Each scenario states setup, target UUIDs/descriptors, action, expected bytes, and pass/fail evidence. **Fill GATT Controls** changes browser selection/input only; it does not send or restore. Central-only actions remain clearly labeled. Baseline-dependent instructions explicitly require restoration. Static scenario text must not imply the browser has observed remote metadata or verified decoding.

Regenerate with `python tools/generate-web-assets.py`; verify with `python tools/generate-web-assets.py --check`. Never edit gzip bytes manually. Extend the current [Web Interface guide](../../WebInterface.md) for these controls and limits. Keep the canonical manifest in fixture constants plus this plan; no manifest generation subsystem is required.

## 7. Implementation sequence and software validation

1. Recheck checkout, installed stack APIs, descriptor permissions, configuration, and all advertising start paths. Confirm original fixture baselines and capture comparable warning/size evidence. Do not read credential values into evidence.
2. Add the exact seven-characteristic service and descriptor constants. Integrate minimal finalization and readiness gates; review original-fixture preservation and no new service in advertising.
3. Add the three-entry inventory, bounded parser/commands, storage readback, publication/restoration, and two HTTP routes. Review worker copies/stack, request security, shared access, and failure reporting.
4. Add browser controls/static scenarios, regenerate assets, and update the user guide. Review the complete scoped diff before deployment.
5. Run focused checks below and the eight-board/mode build matrix. Retain concise reproducible evidence and explicitly list remaining physical work.

Extend existing pure validation/browser checks where they cover hex boundaries (empty, zero bytes, malformed, exact cap/over-cap), unknown/stale targets, immutable baseline comparison, restore-without-publish, dispatch-vs-storage feedback, and prefill/polling preservation. Verify authentication/CSRF/Origin rejection and route registration against the running HTTP server; pure helper tests alone do not establish those properties.

Run existing native pairing-state and direct web-validation checks per [test guidance](../../../test/README), `node test/web_ui_regression.cjs`, asset `--check`, and `pwsh -NoProfile -File test/verify-advertising-stop-completion.ps1`.

Require narrow executable native regressions that compile production fixture code and execute the production startup orchestration with source-grounded adapters. Cover successful registration/finalization/advertising, exact metadata and original fixtures, READ-only descriptor ATT flags, rejected invalid permissions/handles, failure retaining instance 0, no early radio configuration or implicit server registration, and repeat initialization. Demonstrate that restoring the shipped wrong-enum comparison makes the normal-startup test fail. Structural source scans alone cannot satisfy these gates. Keep adapters bounded to the exercised fixture/startup contracts; no general mock stack or central automation framework. Native adapters do not establish actual radio, ATT transport, or subscription behavior.

Build C3 and WROVER in `BLE_INTERFACE_MODE` **0, 1, 2, 3**: eight distinct configurations, not eight repetitions of default mode 3. Use PlatformIO Core 6.1.19 with an explicit per-mode compiler definition and separate/rebuilt outputs; retain commands and evidence of the effective mode. A temporary local override/build helper may be used without changing tracked defaults. Do not assume a deleted historical matrix helper is available. Additional `*-com` packaging checks are needed only if upload-specific configuration changes.

Require no new warnings against comparable baseline logs; unchanged `no_fs.csv`; firmware fitting the actual application partition; web code/assets excluded from modes 0/1; and source/generated asset equality. The installed `no_fs.csv` currently has two `0x1F0000` (2,031,616-byte) OTA slots; verify generated partition output at implementation time rather than relying on that number alone.

Record source revision plus dirty patch/hash, effective flags/mode, toolchain/stack versions, build result, image size, exact BIN SHA-256, and board. Historical `.pio/closeout` or v1.4 records may guide format but are not acceptance evidence for new images. No build-identity characteristic is needed.

## 8. Physical acceptance and Ble.Net handoff

Deploy only after source/software review and separate owner authorization. Test both boards with exact uploaded BIN hashes and known GATT-cache state. Record central OS/device/app versions, firmware mode, discovery/read cache policy, test actions, raw results, and limitations. Use supported fresh/uncached discovery; if stale state persists, stop and establish an owner-approved cache reset. Do not automatically delete bonds or alter security to obtain evidence.

| Acceptance group | Required evidence on both boards |
| --- | --- |
| Startup/readiness | Successful registration/finalization before advertising; manual/restart/reconfiguration paths remain gated on failure; normal advertising/stop/restart regression behavior preserved |
| Metadata | Seven exact READ-only User Descriptions, six exact CPFs, one READ-only four-byte Aggregate, one READ-only Valid Range, correct ownership/absence and CCCD membership |
| Assigned handles | Device-side manifest/log maps both referenced handles to intended CPF objects; central raw `0x2905` agrees. Never substitute build success for runtime handles |
| Permissions | A central write request to representative new READ-only descriptors is rejected with unchanged bytes; discovery alone does not establish write permissions |
| New trigger | Baseline and modified-value reads; enabled CCCD receives unchanged current TemperatureScalar after trigger `01`; unsupported accepted trigger data has no effect; unsubscribe prevents receipt |
| Generic controls | Exact stored/published bytes for all three targets; indication and notification delivery; no delivery to connected unsubscribed peers; invalid/oversize/stale-target rejection before mutation; honest dispatch/error feedback |
| Restoration/UI | Exact baseline bytes return without an update event; actual central-written state is reflected on polling; edits survive polling; scenario prefill causes no operation; descriptor bytes remain unchanged |
| Original behavior | Original reads/writes/custom descriptor, legacy ATT event payload behavior, advertising, pairing, and interface operations remain as before |

Exercise at least one connection at MTU 23 for 20-byte success/21-byte rejection and a larger negotiated MTU for a supported longer payload. Test empty publication where supported and separate library dispatch from client receipt. Do not claim multipeer subscription acceptance from a one-central test; retain a connected unsubscribed peer for that case. Full device testing need not repeat every case in all eight build modes; explicitly smoke-test central-trigger independence in a non-web mode on each board and record the image used.

Windows observations: native PresentationFormats count/order and signed exponent projection for the five scalars; native Aggregate behavior; raw User Description/Valid Range access where exposed; absence and subscription behavior. Distinguish raw descriptor visibility, native projection, and later Ble.Net decoding.

Android observations: raw descriptor bytes and parent ownership, reads, subscription updates, and absence. The [public BluetoothGattDescriptor API](https://developer.android.com/reference/android/bluetooth/BluetoothGattDescriptor) exposes UUID/parent but no public ATT-handle accessor. Require raw Aggregate bytes and record whether the tested backend has an authoritative handle map; otherwise unresolved Aggregate semantics is the expected downstream result. Do not use hidden APIs, reflection, or firmware logs to manufacture a runtime Ble.Net handle capability.

Handoff coverage maps to companion phases: scalar native metadata → 1A; raw CPF → 1B; authoritative Aggregate mapping → 1C; reads/updates → 2A; Valid Range → 2B; descriptions/scenario data support later Explorer verification. Client numeric precision, Aggregate resolution, range decoding, and UI presentation are downstream acceptance, not firmware implementation gates. Capture Windows/Android raw/native findings now with a capable existing tool; rerun semantic assertions when the respective Ble.Net phase exists. Reuse existing automation when available; otherwise retain explicit manual procedures and results.

## 9. Completion and exclusions

Software closeout reviews the exact manifest, readiness behavior, three generic controls, static scenarios, guide/assets, focused software checks, and eight builds. On 2026-10-27 the owner requested feature closure, local promotion, and cleanup. Detailed both-board physical evidence and Windows/Android observations in section 8 remain unverified and are retained as follow-up acceptance; they are not reported as passed by this software closeout. Retain a concise completion record separating software, upload, physical checks, and platform limitations. Closeout inventory/retention follows the existing [procedure](../../Feature-Closeout.md).

Deferred or excluded: additional services/fixtures, a second Aggregate or Valid Range, malformed/duplicate metadata, `0x2909`/Digital and unrelated descriptors, runtime descriptor editing, writable/multilingual/invalid UTF-8 descriptions, CPF decoding/scaling in firmware, semantic writes/range enforcement, a general GATT browser, dynamic service inventory, per-peer publication/CCCD controls, delivery tracking, a new test framework, automatic scenario execution, serial equivalents, persistent overrides, security/pairing redesign, legacy callback repairs, build-identity GATT metadata, and Ble.Net/Explorer changes. Deterministic decoder edge cases remain in Ble.Net tests.
