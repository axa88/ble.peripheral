# Ble.Peripheral — GATT Descriptor and Subscription Fixture Plan

**Version:** 1.8\
**Date:** 2026-11-03\
**Status:** Implementation changes present; software completion follows §7 gates; physical acceptance remains separate\
**Reported WIP commit:** `80f6450` — verify against current HEAD\
**Branch:** `codex/gatt-descriptor-subscription-fixtures`\
**Targets:** ESP32-C3 and ESP32-WROVER\
**Pinned stack:** NimBLE-Arduino 2.5.1\
**Supersedes:** v1.7

---

## 1. Objective and scope

Complete the existing GATT test fixtures needed to validate three areas of Ble.Net Explorer v1.4:

1. **Package A — User Description:** Writable `0x2901` and `0x2900` eligibility metadata.
2. **Package B — Subscriptions:** Notification/indication behavior, a dual-capability characteristic, and truthful diagnostics.
3. **Package C — Valid Range and Web scenarios:** Additional `0x2906` fixtures, accurate characteristic properties, and test guidance.

Reuse the existing firmware architecture, Web `publishValue`, Test Scenarios UI, BLE host event queue, dynamic GATT lifecycle, and regression infrastructure.

This is a completion and defect-correction plan, not a new architecture or general-purpose GATT simulator.

Preserve existing UUIDs, seven original read-only User Descriptions, fixture indexes, routes, workers, authentication, dynamic service behavior, and Service Changed semantics.

### 1.1 Repository baseline

The reported WIP commit is `80f6450`, but its identity and current implementation state must be confirmed from the working tree.

Before editing:

- Read `AGENTS.md`.
- Confirm branch, HEAD, and Git status.
- Inspect existing changes and WIP implementation.
- Identify completed, incomplete, and defective requirements.
- Preserve correctly implemented functionality.
- Avoid duplicate fixture registration or unnecessary refactoring.

Use the actual working tree and pinned NimBLE source as authoritative.

### 1.2 Current WIP and static review checkpoint

At the 2026-11-03 read-only review checkpoint, the current branch was
`codex/gatt-descriptor-subscription-fixtures` at HEAD
`80f64508dbcac63d558b01092e1d7b94cbb84205`. The reported WIP commit and branch
therefore matched. Before the v1.8 plan edit, pre-existing working-tree changes were
present in this plan and `firmware-revision.json`; no source or test files were
modified in that checkpoint.
The revision file showed revision 28. This records observed state only and does not
certify that the current identity is synchronized.

The inspected WIP already contains the Package A descriptor fixtures, original
publication triggers, the dual Notify/Indicate characteristic, the `df09`/`df10`
Valid Range fixtures, and dynamic fixture identity capture/verification for mutable
descriptor values. The remaining implementation gaps are subscription counts for
all Web targets, per-characteristic publication status, Web `read`/`write` fields
and property rendering, and the focused validation described below.

This checkpoint was static source and test review only. No tests, builds, upload, or
physical acceptance were run or established.

### 1.3 Related Explorer plan

The separate Ble.Net Explorer v1.4 plan remains open.

Relevant client behavior includes:

- Explicit `0x2900` reads for Writable Auxiliaries eligibility.
- Conservative 20-byte write fallback when peer MTU is unavailable.
- Native Windows CCCD operations and descriptor-dependent Android subscriptions.
- Logical Off, Notify, and Indicate controls.
- Valid Range interpretation requiring authoritative metadata.

Do not modify Ble.Net, close its plan, or require cross-repository tests for firmware software completion.

---

## 2. Verified NimBLE contracts

### 2.1 Extended Properties

NimBLE-Arduino 2.5.1 does not expose `NIMBLE_PROPERTY::AUX_WRITE`.

Use native `BLE_GATT_CHR_F_AUX_WRITE` (`0x0100`) in the characteristic-property flag domain.

The native flag sets the declaration's Extended Properties bit, but the stack does not automatically register `0x2900`.

Create one explicit `0x2900` descriptor.

Keep characteristic properties distinct from descriptor `BLE_ATT_F_*` permissions.

### 2.2 Publication

Parameterless `notify()` and `indicate()` invoke `ble_gatts_chr_updated()`.

The operation:

- Uses CCCD configuration to select effective update behavior.
- Can report success without any subscribed peer.
- Schedules transmission asynchronously.
- May coalesce rapid updates, particularly while indications are outstanding.

A successful request does not prove peer transmission, indication acknowledgement, or client-application receipt.

### 2.3 Callbacks

`onSubscribe()` supplies peer information when the connection can be resolved.

The pinned `onStatus()` implementation:

- Does not provide usable peer identity.
- Calls both overloads for qualifying events.
- Suppresses the initial unacknowledged indication TX event.
- Uses independent default overload implementations.

Use only the two-argument overload for publication diagnostics, recording each callback event once.

Do not modify the pinned NimBLE library.

### 2.4 Dynamic GATT

`FixtureIdentity::capture()` and `verify()` already traverse protected services and capture:

- Characteristic identities, properties, handles, and values.
- Descriptor identities, permissions, handles, and current values.
- `0x2900`, `0x2901`, `0x2904`, `0x2905`, `0x2906`, and the custom descriptor.
- NimBLE-managed CCCDs separately.

Mutable descriptor values are already compared across supported rebuilds.

No structural redesign of this mechanism is required.

---

## 3. Package A — User Description fixtures

### 3.1 Writable fixture

Use characteristic:

`12345678-1234-5678-1234-56789abcdf08`

| Attribute | Requirement |
|---|---|
| Characteristic | READ with native AUX_WRITE |
| Declaration | Extended Properties bit set |
| `0x2900` | Exactly one; read-only; two bytes |
| `0x2900` value | `02 00` |
| `0x2901` | READ and WRITE |
| `0x2901` capacity | 128 bytes |
| Initial description | Short, recognizable text |

Verify declaration/descriptor consistency, ownership, UUIDs, handles, permissions, capacities, exact values, and native ATT registration.

Support exact byte-for-byte readback without NUL insertion, normalization, truncation, or automatic restoration.

Do not implement long writes.

### 3.2 Negative comparison

Preserve the seven existing read-only `0x2901` descriptors, including their original permissions, lengths, and initial values.

These provide the required comparison without Writable Auxiliaries.

Do not add another Reliable Write fixture or `0x2900 = 01 00` comparison. Reliable Write is outside this feature.

### 3.3 Verification

Extend existing deterministic tests for registration and permission contracts.

Reserve actual remote ATT write/readback verification for physical acceptance.

---

## 4. Package B — Subscription fixtures and diagnostics

### 4.1 Existing publication triggers

For original characteristics `ef2` and `ef3`, preserve trigger byte `01`.

On a matching trigger:

1. Store the predefined event payload.
2. Verify stored bytes.
3. Invoke parameterless `notify()` or `indicate()`.

Subsequent reads must return:

| Characteristic | Stored value |
|---|---|
| `ef2` | `notify-event` |
| `ef3` | `indicate-event` |

The available WIP already implements this behavior. Verify rather than repeat it.

Update tests that assume the trigger byte persists.

Leave `TemperatureTrigger` and the existing Web publication path unchanged unless another defect is demonstrated.

### 4.2 Dual Notify/Indicate fixture

Use:

`12345678-1234-5678-1234-56789abcdef5`

Required properties:

`READ | NOTIFY | INDICATE`

Requirements:

- One NimBLE-managed CCCD.
- No WRITE property.
- Fourth entry in the existing Web publication target list.
- Existing `publishValue` operation.
- Stored-value verification before publication.
- CCCD-selected effective update behavior.

Preserve the original three Web target positions and their within-boot opaque identity mapping.

Do not infer update type solely from the parameterless method name.

### 4.3 Per-peer subscription observations

Use the existing `onSubscribe()` callback and connection identity.

Maintain bounded, last-observed state for each current peer and each of the four Web
publication targets:

- `ef2` — original notification characteristic.
- `ef3` — original indication characteristic.
- `df05` — Temperature Scalar.
- `ef5` — dual Notify/Indicate characteristic.

Add subscription observation to `df05` without changing its Notify property or Web
publication behavior. Keep `TemperatureTrigger::onWrite` attached to `df07` and
preserve its existing behavior.

Keep the subscription-observation allowlist separate from the publication-status
allowlist. Subscription observations cover `ef2`, `ef3`, `df05`, and `ef5`.

Each peer/characteristic record contains:

- Characteristic UUID.
- Connection ID.
- Observed CCCD value.
- Observation uptime.
- Connection freshness.

Recognize `0000` Off, `0001` Notify, `0002` Indicate, and `0003` both.

Display **Last observed subscription** and **Connection freshness**.

An absent observation does not establish Off.

The existing `BluetoothManager::Disconnected()` already clears observations for a connection. Verify that behavior and preserve it.

Bound subscription storage for at least four target observations per connection
(`CONFIG_BT_NIMBLE_MAX_CONNECTIONS * 4`). Keep publication-status storage separate;
its three-record bound is specified below.

Do not add another connection registry or subscription state machine.

### 4.4 Per-characteristic publication outcomes

The reported WIP incorrectly uses the three-argument `onStatus()` and attempts to associate its empty connection information with a peer.

Correct this by:

1. Overriding only `onStatus(NimBLECharacteristic*, int)`.
2. Recording outcomes by characteristic identity.
3. Keeping publication status outside per-peer subscription records.
4. Removing the erroneous peer-attributed status fields from publication-status
   records and their serializer. Keep subscription peer identity in its existing
   subscription records.
5. Testing that each callback is counted once.

Maintain one bounded status record for each of:

- `ef2`
- `ef3`
- `ef5`

Use a separate publication-status allowlist containing only `ef2`, `ef3`, and `ef5`.
Do not share the four-target subscription predicate or allowlist with status recording;
`df05` must not acquire publication-status records through the subscription path.

Record:

- Last status code.
- Last status classification.
- Observation uptime.
- Counts by classification.

Interpret:

| Status | Classification |
|---|---|
| `0` | Notification TX |
| `BLE_HS_EDONE` | Indication acknowledged |
| Other nonzero | Error or timeout |

Counts represent observed callback events, not requested publications or verified client deliveries.

Do not attribute these outcomes to peers.

Keep publication status outside per-peer subscription observations. Preserve the
`lastPublicationStatuses` array and its characteristic UUID, status code,
classification, and observation uptime; its records must not contain peer address,
connection ID, or connection freshness. Update the Web renderer, validators, and
tests with this intentional record-shape change.

### 4.5 Web publication diagnostics

Extend the existing `GET /api/gatt` response with bounded diagnostics.

Maintain compatibility with existing response fields.

Preserve the `Dispatched` result identifier where required, but define it as acceptance of the characteristic-update request—not proof of transmission.

**Preserve the existing `connectedPeerCount` contract.**

`connectedPeerCount` already represents all connected peers used to calculate the publication limit and minimum connected-peer MTU. It includes peers regardless of whether they are subscribed.

Do not reuse, redefine, or change the meaning of `connectedPeerCount`.

Add a distinct per-target field:

`observedSubscribedPeerCount`

Emit this field for all four existing Web targets: `ef2`, `ef3`, `df05`, and `ef5`.

Derive this count from the existing per-peer subscription observations for the target characteristic.

Count only currently connected peers with a matching last-observed subscription:

- `0001`: eligible for Notify.
- `0002`: eligible for Indicate.
- `0003`: eligible for both.
- `0000`: not subscribed.

For a dual-capability target, a peer with either enabled update type counts once, not twice.

This metric is an observation count, not an authoritative CCCD query, an exact transmission count, or proof of delivery.

If no subscribed peer is observed, report:

`No subscribed peer observed; peer dispatch and client receipt are not established.`

Do not infer that zero transmissions occurred merely because the recorded subscription count is zero.

Update the existing serializer, Web display, response validators, and focused tests.

Preserve the original `connectedPeerCount` assertions and add separate assertions for `observedSubscribedPeerCount`.

Do not introduce new routes, workers, subscription state machines, or event-history infrastructure.

---

## 5. Package C — Valid Range and Web scenarios

### 5.1 Valid Range fixtures

Preserve the original signed range:

| Attribute | Value |
|---|---|
| Characteristic | `df04` |
| `0x2904` | `0E FE 2F 27 01 00 00` |
| `0x2906` | `70 FE E8 03` |
| Interpretation | −4.00 to 10.00 °C |

Use the following appended fixtures:

| Fixture | Configuration | Expected behavior |
|---|---|---|
| `df09` | READ `0A`; `0x2904 = 04 00 00 27 01 00 00`; `0x2906 = 02 2A`; no `0x2901`/`0x2905` | Unitless `uint8`, inclusive bounds 2–42 |
| `df10` | READ `0A 00`; `0x2906 = 02 00 2A 00`; no `0x2901`/`0x2904`/`0x2905` | Preserve raw bytes; interpretation unresolved |

Preserve `df01`–`df07`; `df08` remains the writable-description fixture.

Do not add decoding, range enforcement, or additional numeric matrices.

### 5.2 Correct the Web target property contract

The existing `FixtureGatt::ValueSnapshot` already contains `properties`, but `webInterface.cpp` serializes only `notify` and `indicate`.

The browser cannot reliably determine READ or WRITE support from those booleans.

In the existing GATT target serializer, add:

```cpp
cJSON_AddBoolToObject(
    value, "read",
    (entry.properties & NIMBLE_PROPERTY::READ) != 0);

cJSON_AddBoolToObject(
    value, "write",
    (entry.properties & NIMBLE_PROPERTY::WRITE) != 0);
```

Integrate these checks into the existing serializer's allocation/failure handling rather than introducing a separate path.

Keep the existing `notify` and `indicate` fields.

Do not add new fields to `ValueSnapshot`; its `properties` member already supplies the information.

This is an additive change to the existing target response schema. No new route or operation is required.

Update the matching response checks in:

- `test/web_status_native`
- `test/web_ui_regression.cjs`

In particular, update the target-object assertions in `web_status_native/status_main.cpp` and the four mock target objects in `web_ui_regression.cjs`.

Use actual repository test paths.

### 5.3 Correct characteristic property rendering

Update `renderGattSelection()` in `web/index.html` to construct its identity line from:

- `item.read`
- `item.write`
- `item.notify`
- `item.indicate`

Remove the hard-coded UUID-specific `df05` WRITE exception and the assumption that a target supports only one publication type.

Expected displays:

- `ef2`: `READ | WRITE | NOTIFY`
- `ef3`: `READ | WRITE | INDICATE`
- `df05`: `READ | NOTIFY`
- `ef5`: `READ | NOTIFY | INDICATE`

Preserve existing publication controls and button behavior.

Do not introduce additional GATT reads or another API.

### 5.4 Test Scenarios UI

Extend existing scenarios for:

- Writable User Description.
- Read-only User Description.
- Dual Notify/Indicate switching.
- Subscription/publication diagnostics.
- Unsigned Valid Range.
- Unresolved Valid Range.

Scenario selection may prefill applicable generic controls.

It must not automatically write, publish, reset, or change descriptor values.

Descriptor reads and writes remain central-side operations.

Preserve existing scenario IDs and controls.

Do not add a descriptor editor to the peripheral Web UI.

---

## 6. Dynamic GATT compatibility

Preserve existing protected-fixture capture and verification.

No structural changes to `dynamicGatt.cpp` are expected.

### 6.1 Fixture finalization

Preserve the original seven fixtures, indexes, and read-only invariants.

For the writable `df08` description:

- Require `BLE_ATT_F_READ | BLE_ATT_F_WRITE`.
- Require 128-byte capacity.
- Verify seed bytes at startup.
- Verify current bytes during subsequent finalization.
- Never reset or compare modified bytes to the seed during rebuild.

Validate `df09` and `df10` according to their actual metadata.

Preserve existing array structures where they already work. Do not refactor solely to separate old and new fixtures.

### 6.2 Rebuild test

Add focused deterministic coverage that exercises the production disconnected
dynamic-GATT add and remove orchestration together with fixture capture/finalization.
The existing model-only topology test and lifecycle tests with registration/reset
stubs are not sufficient on their own for this combined path.

1. Set `df08` description to non-seed bytes.
2. Verify the new value.
3. Ensure no connection remains active.
4. Exercise the supported production dynamic GATT add operation and remove operation
   while idle, causing the supported rebuild path.
5. Exercise production `FixtureIdentity::capture()`/`verify()` and
   `FixtureGatt::BeginRebuild()`/`FinalizeAfterRebuild()` in that path.
6. Verify descriptor identity, handles, permissions, and the changed bytes after
   each rebuild, including native ATT readback.

Physical sequence:

`Write → Disconnect → Rebuild → Reconnect → Rediscover → Read`

Do not weaken `nativeIdle()` or recovery safeguards.

### 6.3 ATT handle migration

`ef5` is created after original characteristic `ef3`.

`ef4` is the custom descriptor UUID attached to `ef2`, not a characteristic.

Adding `ef5` introduces new ATT attributes before the descriptor service, so subsequent handles are expected to shift.

The appended `df08`–`df10` fixtures do not independently shift earlier descriptor-service attributes.

Before physical acceptance, remove old bonds as applicable and perform fresh GATT rediscovery.

Verify actual handle assignments on hardware.

Do not implement historical-handle compatibility workarounds or change Service Changed behavior.

---

## 7. Implementation and software validation

### 7.1 Execution

Perform one short read-only preflight covering:

- Current branch and WIP.
- Pinned NimBLE contracts.
- Fixture registration and finalization.
- Subscription and status callbacks.
- Existing Web target serialization, including `connectedPeerCount`.
- Web rendering and scenarios.
- Dynamic rebuild verification.
- Existing tests and firmware revision state.

Then complete the three packages together.

Do not introduce independently gated implementation phases.

### 7.2 Required deterministic testing

Reuse maintained tests, including:

- `test/descriptor_fixture_native/run.py`
- `test/descriptor_startup_native/run.py`
- `test/gatt_publication_native/run.py`
- `test/web_status_native/run.py`
- `test/dynamic_topology_native/run.py`
- `test/dynamic_gatt_native/run.py`
- `test/web_ui_regression.cjs`
- `test/verify-descriptor-fixture.py`

Extend focused coverage for:

- Production startup success and injected failures, including advertising remaining
  blocked after registration/finalization failure.
- Descriptor properties, permissions, capacity, and native registration.
- Adapter contracts against pinned NimBLE-Arduino 2.5.1, keeping characteristic
  property flags distinct from descriptor ATT permissions.
- AUX_WRITE/`0x2900` consistency.
- Trigger stored-value behavior.
- Dual-characteristic registration.
- Two-argument `onStatus()` and single-event counting.
- Subscription observations and disconnect cleanup.
- Subscription counts for all four targets, including `df05`; explicit Off, no
  observation, Notify, Indicate, and combined CCCD cases; disconnect and connection
  handle reuse must not retain a stale count.
- Publication status and response serialization.
- Exactly one characteristic-level status callback record per event, with status
  records for `ef2`, `ef3`, and `ef5` only.
- Existing `connectedPeerCount` behavior.
- New `observedSubscribedPeerCount` behavior, including zero subscribers and mixed CCCD configurations.
- Added `read`/`write` response fields.
- Correct property rendering for all four Web targets.
- Scenario prefill without mutation.
- Mutable descriptor preservation through rebuild.

Confirm actual test paths in the working tree.

Reuse the maintained tests; do not create a new test framework. `test/web_status_native`
exercises response serialization with synthetic snapshots. `test/gatt_publication_native`
extracts production callback and diagnostic logic and covers subscription observations
for all four Web targets, disconnect cleanup, and characteristic-level publication
status event counting. The production rebuild coverage in §6.2 must exercise the
integrated dynamic-GATT path; model-only topology checks and lifecycle stubs do not
satisfy that gate. Keep `test/README` aligned with the four publication targets.

The maintained `tools/validate-descriptor-fixture.ps1` runner prepares firmware
identity, executes all deterministic checks listed above, and then builds the eight
configurations in §7.4 serially. It retains the source snapshot, per-check logs,
effective-mode evidence, and copied build artifacts under
`.pio/descriptor-fixture/<runId>/`. Passing a subset of the listed checks or builds
does not satisfy the complete validation gate.

### 7.3 Firmware identity

For firmware source changes, run:

`python tools/firmware_revision.py prepare`

Run it before validation or handoff as required by `AGENTS.md`.

The current identity tooling fingerprints its included project inputs, including
documentation, tests, and tools. After the final plan or other included-input edit,
run `prepare` before validation or handoff. Do not change the fingerprint exclusion
policy as part of this work.

Do not hand-edit the fingerprint.

Commit synchronized tracked revision state with the source and other included-input
changes that produced it.

### 7.4 Build matrix

Build ESP32-C3 and ESP32-WROVER in every `BLE_INTERFACE_MODE`:

| Mode | C3 | WROVER |
|---|---|---|
| 0 | Required | Required |
| 1 | Required | Required |
| 2 | Required | Required |
| 3 | Required | Required |

Eight builds total.

Run PlatformIO builds serially.

Reuse `tools/validate-descriptor-fixture.ps1` where applicable. The append behavior
for `PLATFORMIO_BUILD_FLAGS` was verified with the PATH PlatformIO Core 6.2.0
executable. The maintained runner invokes the separate
`%USERPROFILE%\.platformio\penv\Scripts\pio.exe`, which reported Core 6.1.19 during
the 2026-11-03 preflight. Preserve board-specific defines, required forced NimBLE
headers, and inherited flags; ensure a conflicting inherited `BLE_INTERFACE_MODE`
definition cannot override the requested mode. Verify the effective compiler
arguments and selected mode for each environment using the runner's executable;
the requested environment-variable string alone is not verification. Do not modify
tracked project configuration to select modes. Keep validation evidence free of
credentials and other secrets.

Modes 0 and 1 exclude Web support. Core fixtures and publication behavior must compile without Web diagnostics.

Modes 2 and 3 include Web support and must exercise the response and diagnostics contracts.

Record effective interface mode separately from firmware revision and fingerprint.

### 7.5 Completion boundary

Software completion requires:

- Applicable implementation requirements satisfied.
- Focused regressions passing.
- Eight required builds passing.
- Firmware identity synchronized.
- Documentation updated.
- Implementation committed on the current branch.

Do not upload firmware or claim physical acceptance during software implementation.

### 7.6 Validation output

The maintained runner retains the source snapshot, deterministic-check results, effective-mode evidence, and per-build artifacts under `.pio/descriptor-fixture/<runId>/`. These software records do not establish which firmware is running on a device or physical acceptance.

---

## 8. Physical acceptance

Physical acceptance remains separate and unperformed.

Before testing, remove old bonds as applicable and rediscover GATT attributes.

| Test | Required observation |
|---|---|
| Writable description | Discover `0x2900 = 02 00` and writable `0x2901`. |
| UTF-8 writes | Write/read `Café` (`43 61 66 C3 A9`), `Cat`, and `Longer fixture text`. |
| Read-only descriptions | Original seven remain unchanged; actual unauthorized ATT writes are rejected. |
| Write boundary | Test effective single-request maximum and one byte beyond, without long writes. |
| Original publication | Confirm Notify-only and Indicate-only behavior and post-trigger stored values. |
| Dual subscription | Exercise Off → Notify → Indicate → Off, reverse transitions, and repeated selection. |
| Diagnostics | Compare `connectedPeerCount`, `observedSubscribedPeerCount`, observed CCCD state, characteristic status, and actual client receipt without equating them. |
| Disconnect | Verify old peer observations cannot be reused after reconnect. |
| Valid Range | Verify `df04`, `df09`, and unresolved `df10`. |
| Rebuild | Verify modified `df08` description survives disconnected rebuild. |
| Handle migration | Fresh bond/discovery state and actual handle mapping confirmed. |
| Web properties | Verify accurate property display for `ef2`, `ef3`, `df05`, and `ef5`. |

### ATT error boundaries

Distinguish client-side prevention from actual server ATT errors.

Expected server responses, when an ATT request actually reaches the attribute:

- Read-only descriptor write: `0x03` — Write Not Permitted.
- Write exceeding descriptor capacity: `0x0D` — Invalid Attribute Value Length.

The effective single ATT Write Request limit is
`min(128, negotiated_MTU - 3, central_backend_limit)`. The descriptor's 128-byte
capacity is separate from the negotiated transport and central backend limits. A
129-byte capacity test is meaningful only when the central backend and negotiated
MTU can carry a 129-byte value in one request (MTU at least 132). A client may refuse
an oversized write before sending it; record that separately from a server ATT
`0x0D` response. Do not implement long writes merely to provoke a capacity error.

### Publication timing

NimBLE may delay or coalesce rapid updates.

Use appropriate separation or indication acknowledgement when testing distinct publications.

Do not require one received update per rapid publish request.

### Physical evidence

Record for each executed test:

- Board.
- Central client/platform.
- Firmware environment.
- Effective `BLE_INTERFACE_MODE`.
- Device-reported revision and fingerprint.
- Actual observation and result.

Do not treat a locally built image as proof of the firmware running on the device.

Keep peripheral acceptance and Explorer acceptance separate.

---

## 9. Explicitly out of scope

- Generic GATT simulation.
- Peripheral Web descriptor editing.
- Manual CCCDs.
- New Web routes, workers, or operations.
- Duplicate connection/subscription state machines.
- Per-peer publication status tracking.
- General event-history or telemetry infrastructure.
- Reliable Write fixture implementation.
- New long-write transport.
- Additional descriptor or numeric fixture matrices.
- Firmware value decoding or Valid Range enforcement.
- Dynamic GATT redesign.
- Service Changed changes.
- Historical ATT handle compatibility workarounds.
- Ble.Net source modifications.
- Cross-repository completion gates.
- Unrelated Web UI redesign.

---

## 10. Implementation principle

**Finish the existing WIP, correct confirmed defects, and validate the result.**

Retain Packages A, B, and C without introducing new feature areas.

Prefer small changes to existing callbacks, serializers, fixtures, and tests over new infrastructure.

The required Web response changes are limited to:

1. `read` and `write` booleans derived from the existing characteristic properties.
2. `observedSubscribedPeerCount`, distinct from the existing `connectedPeerCount`.
3. Bounded per-characteristic publication status diagnostics.

Preserve all existing response fields and meanings except the intentionally corrected
publication-status record shape: remove its erroneous peer-attributed fields as
specified in §4.4. Keep the array and characteristic-level status fields, and update
its consumers and tests in the same change.

Do not expand scope unless a verified defect directly prevents the intended Explorer feature testing.
