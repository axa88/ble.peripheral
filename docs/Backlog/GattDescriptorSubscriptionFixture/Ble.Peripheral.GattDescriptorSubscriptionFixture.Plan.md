# Ble.Peripheral — GATT Descriptor and Subscription Fixture Plan

| Plan field | Value |
| --- | --- |
| Version / date | 1.1 / 2026-10-29 |
| Status | Proposed — implementation pending |
| Branch | `codex/gatt-descriptor-subscription-fixtures` |
| Targets | ESP32-C3 and ESP32-WROVER |
| Pinned stack | NimBLE-Arduino 2.5.1 |

## 1. Purpose and boundaries

Add a small set of GATT fixtures that exercises writable User Description eligibility, CCCD-selected publication, and selected Valid Range presentation. Reuse the existing fixture, Web `publishValue` operation, Test Scenarios UI, BLE host event queue, and dynamic GATT lifecycle. This does not add a general GATT simulator or descriptor editor to the peripheral Web UI.

Dynamic GATT registration, rebuild, Service Changed, and protected-fixture verification already exist in `src/dynamicGatt.cpp`. Preserve that behavior and extend its existing fixture snapshot only as needed. Keep existing UUIDs, fixture indexes, values, seven read-only User Descriptions, routes, authentication, workers, and Service Changed semantics stable.

### Related client contract (separate project)

The Ble.Net Explorer v1.4 plan on `codex/gatt-scalar-interpretation` remains open. Current client code informed this fixture contract: description eligibility requires an explicit two-byte `0x2900` read; the client uses a conservative 20-byte fallback when peer MTU is unknown; the Android subscription path requires a discovered CCCD, while Windows uses its native characteristic CCCD operation, and both expose client-side logical subscription controls; and Valid Range requires authoritative metadata, with Aggregate semantics unresolved when its handles cannot be resolved. This firmware fixture supports the plan's later physical acceptance. Make no Ble.Net edits, do not close out the Explorer plan here, and do not make cross-repository tests a gate. Firmware software completion does not close the Explorer plan.

## 2. Source-backed baseline and stack contracts

| Current source | Finding and plan consequence |
| --- | --- |
| `src/fixtureGatt.cpp`, `src/fixtureGatt.h` | The descriptor service has seven read-only `0x2901` fixtures. Preserve all seven; append the writable fixture. The existing notification and indication write callbacks use explicit-payload overloads. |
| `src/dynamicGatt.cpp` | Protected services, characteristic values, descriptors (`0x2901`/`0x2904`/`0x2905`/`0x2906` and custom), and CCCDs are already captured and verified through rebuild. Add `0x2900` and the mutable fixture to this path; preserve actual current bytes and assigned handles. |
| `src/webInterface.cpp`, `src/fixtureGatt.cpp`, `web/index.html` | The existing authenticated `publishValue` route and Web selector serve three stable targets. Append a fourth target; keep the same operation and worker. Scenario selection currently provides instructions and optional prefill without executing GATT actions. |
| `platformio.ini` and installed `.pio/libdeps/<environment>/NimBLE-Arduino/src` | The dependency is pinned to 2.5.1. Its C++ `NIMBLE_PROPERTY` enum does not define AUX_WRITE; `createCharacteristic` accepts and forwards native flags. `BLE_GATT_CHR_F_AUX_WRITE` (`0x0100`) makes the native characteristic declaration carry the Extended Properties bit, but the stack does not create a `0x2900` descriptor. |
| Pinned `NimBLECharacteristic.cpp` and `ble_gatts.c` | Parameterless `notify()` and `indicate()` reach `ble_gatts_chr_updated`. The call result does not prove per-peer dispatch or receipt; CCCD configuration determines the update kind. `onSubscribe` is available; `onStatus` is asynchronous and connection-scoped where that overload supplies `NimBLEConnInfo`. |
| `test/` | Maintained tests live under `test/`, not `tests/`. Existing native fixture, startup, publication, status, topology, and browser regressions can be extended; no new test framework is needed. |

For characteristic creation, pass `BLE_GATT_CHR_F_AUX_WRITE` in the characteristic-property flag domain; do not refer to a nonexistent `NIMBLE_PROPERTY::AUX_WRITE`. Descriptor creation uses the wrapper's supported property inputs, while descriptor getters expose `BLE_ATT_F_*` permissions. Tests and finalization must keep those domains distinct.

## 3. Package A — User Description fixtures

Append one readable characteristic with AUX_WRITE and exactly one explicit Extended Properties descriptor:

| Attribute | Required value |
| --- | --- |
| Characteristic native flag | `BLE_GATT_CHR_F_AUX_WRITE`; native declaration must include Extended Properties |
| `0x2900` | Exactly one descriptor, read-only, maximum length 2, initial bytes `02 00` |
| `0x2901` | Read/write permissions, maximum length 128, short recognizable initial bytes |

Verify that the native declaration's Extended Properties bit agrees with the sole readable `0x2900` value and its Writable Auxiliaries bit. Deterministic production/adapter tests exercise the read/write permission contract and verify descriptor ownership, UUID, handle, maximum length, permission-domain conversion, exact bytes, native declaration bits, and local native-table readback. They validate the adapter contract but do not establish wire-level ATT behavior. Verify a successful ATT read and write with byte-for-byte readback during later physical acceptance. Do not append a NUL, normalize, or truncate accepted bytes. Do not add long-write support; writes must fit one supported ATT Write Request.

Keep the current seven `0x2901` descriptions read-only. They remain the no-`0x2900` comparison. A separate `0x2900 = 01 00` Reliable Write comparison is optional only if the first read-only preflight against the pinned stack proves a conforming declaration and descriptor configuration; otherwise omit it without blocking the feature. Do not fabricate an Extended Properties descriptor.

The firmware does not add editor behavior or infer descriptor permissions from text.

## 4. Package B — Subscription fixtures and truthful diagnostics

Correct both existing write-trigger callbacks in `src/fixtureGatt.cpp`: retain trigger `01` and the current event payload bytes, store the payload on the characteristic, then call the parameterless publication method. Do not use explicit-payload `notify(value, length)` or `indicate(value, length)` for subscription tests. Parameterless calls queue the normal characteristic-updated path; a true return is not proof of per-peer dispatch or client receipt.

Append characteristic `12345678-1234-5678-1234-56789abcdef5` with `READ | NOTIFY | INDICATE`, one NimBLE-managed CCCD, and one appended entry in the existing Web target list. This UUID follows the existing custom descriptor `...def4`; preserve the three current target IDs. Reuse `publishValue`; store and verify bytes before requesting the normal update. For this dual-property characteristic, let the CCCD determine each peer's effective update kind. Do not claim the method name (`notify()` or `indicate()`) forces that kind. Central-side tests observe the actual received notification or indication.

Use the existing `onSubscribe` callback to expose the last observed per-peer subscription value (`0000`, `0001`, `0002`, or `0003`), characteristic identity, peer identity, observation time, and connection freshness. Label these fields **Last observed subscription** and **Connection freshness**. Treat the value as a last-observed event, not an authoritative mirror or as Off when no event was seen. Reuse existing connection identity/lifecycle data and mark or clear an observation when its connection ends. The pinned connection-scoped `onStatus` callback is asynchronous: code `0` is notification TX status, `BLE_HS_EDONE` is indication acknowledgement, and other nonzero codes are errors. These statuses do not prove client-application receipt. Parameterless publication may return true when there are no subscribed peers, so that result is not per-peer dispatch evidence.

Add a required, bounded diagnostics object to the existing `GET /api/gatt` response. Preserve its existing response keys and the `Dispatched` result where possible, but define them truthfully as NimBLE accepting/queuing the update request; update descriptive text to say peer dispatch and client-application receipt are not proven. Update the matching response validator and focused regression tests. Add no route, worker, duplicate CCCD state machine, or event history. Bound observations to current configured peers and retain only the latest event for each relevant peer/characteristic.

## 5. Package C — Valid Range and Web scenarios

Preserve the existing signed Valid Range fixture byte-for-byte: parent `df04`, sole `0x2904 = 0E FE 2F 27 01 00 00`, and `0x2906 = 70 FE E8 03` (`-4.00` to `10.00 °C`). Existing fixtures do not cover an unsigned range or raw-only unresolved range; append both without changing `df01`–`df07`:

| Appended characteristic | Value and metadata | Expected use |
| --- | --- | --- |
| `df09` — Unsigned Range | READ value `0A`; sole `0x2904 = 04 00 00 27 01 00 00`; `0x2906 = 02 2A`; no `0x2905` | Positive unitless `uint8` case; inclusive bounds 2–42 and current value 10. |
| `df10` — Unresolved Range | READ value `0A 00`; `0x2906 = 02 00 2A 00`; no `0x2904` and no `0x2905` | Intentional negative raw-only case; retain the bytes and report unresolved type/units/bounds. |

Append the writable-description fixture at `df08`; do not renumber existing fixtures. Do not add decoder work or range enforcement. Do not use the existing Aggregate fixture as a same-platform positive Valid Range case.

Extend `web/index.html` scenarios for writable/ineligible descriptions, Notify/Indicate switching, subscription diagnostics, and any added range fixture. Central-only descriptor reads/writes remain written instructions; never route descriptor actions through GATT value publication controls. Prefill only controls that apply to a publication scenario. Selecting or prefilling a scenario must not write, publish, reset, or change a descriptor. Keep the generic controls and existing scenario IDs; do not add a peripheral-side descriptor editor.

## 6. Dynamic GATT compatibility

Extend `FixtureIdentity` in `src/dynamicGatt.cpp` to include `0x2900`; keep `0x2902` CCCD capture separate. Snapshot actual descriptor bytes before a supported rebuild, including the mutable `0x2901`, then verify the same object identity/parent relationship, UUID, handle, permissions, current bytes, and native ATT readback after rebuild. Validate the writable description's initial value at startup only; after a successful write, preserve and compare the captured current value rather than the original seed. Retain existing fixture handles and values and leave the existing dynamic service add/remove and recovery behavior unchanged.

## 7. One-pass implementation and software gates

Start with a short read-only preflight of the pinned 2.5.1 flag flow, descriptor registration/ATT permissions, subscription and status callback contracts, current fixtures, dynamic rebuild snapshot, and UI response schema. Then implement Packages A, B, and C together in this branch; do not split them into separately gated implementation phases.

For firmware source changes, run `python tools/firmware_revision.py prepare` before validation or handoff. Extend executable production-path startup and injected-failure coverage, and verify the pinned adapter's distinct characteristic-property and descriptor-permission domains. Extend focused checks for fixture finalization and ATT adapter contracts, UI/scenario behavior, status serialization, publication semantics, and mutable-descriptor rebuild preservation. Relevant maintained paths are:

- `test/descriptor_fixture_native/run.py`
- `test/descriptor_startup_native/run.py`
- `test/gatt_publication_native/run.py`
- `test/web_status_native/run.py`
- `test/dynamic_topology_native/run.py` and `test/dynamic_gatt_native/run.py`
- `test/web_ui_regression.cjs` and `test/verify-descriptor-fixture.py`

After the focused regressions pass, build ESP32-C3 and ESP32-WROVER in `BLE_INTERFACE_MODE` 0, 1, 2, and 3 (eight configurations), retaining the effective mode and results. These are software gates; do not upload during this work. Complete software implementation, tests, and builds before beginning any hardware or client physical acceptance.

## 8. Physical acceptance and completion record

All rows below are future physical acceptance checks; this plan records no hardware pass.

| Check | Required evidence |
| --- | --- |
| Writable description | Discover exactly one read-only `0x2900 = 02 00` and a read/write `0x2901`; write/read `Café` as `43 61 66 C3 A9`, then shorter `Cat` and longer `Longer fixture text`, each within the single ATT Write Request payload and the 128-byte capacity; verify exact readback after each write. |
| Read-only descriptions | Confirm all original seven `0x2901` writes are rejected and their bytes remain unchanged. Exercise the `01 00` Reliable Write comparison only if implemented. |
| Original and dual subscriptions | Exercise the original notification and indication fixtures. On the `...def5` dual fixture test Off → Notify, Notify → Indicate, Indicate → Off, Off → Indicate, Indicate → Notify, and repeated selection; publish while Off and record no client receipt, then record the actual received update kind for enabled modes. |
| Disconnect/reconnect | Disconnect and reconnect after subscription changes; rediscover, record the central's effective CCCD selection, and compare it with the peripheral's last-observed subscription and connection-freshness labels. Do not infer an active subscription from stale state. |
| Range fixtures | Read the existing signed fixture, unsigned `df09`, and raw-only unresolved `df10`; verify `df10` retains raw bytes and has no fabricated interpretation. |
| Dynamic rebuild | Rebuild through the supported path and verify the changed User Description bytes and protected fixture handles persist. |
| Device/client provenance | Exercise supported Windows and Android clients on both boards where behavior can differ; record only combinations actually tested. |

For each physical result, record board, client/platform, environment, and the **device-reported** source revision and fingerprint. A locally verified binary does not establish what a connected device is serving. Keep software results, upload/deployment, peripheral physical acceptance, and Explorer rendered/physical acceptance separate. No physical result is claimed by this plan.

## 9. Explicitly out of scope

General-purpose GATT simulation; a descriptor editor or manual CCCD in the peripheral UI; new HTTP routes/workers or subscription state machines; duplicate `0x2900`; Explorer combined-mode editing; long writes; broad descriptor/numeric fixture matrices; firmware value decoding or Valid Range enforcement; dynamic GATT redesign; Service Changed changes; Ble.Net source changes; and cross-repository test gates.

## 10. Source anchors

- `src/fixtureGatt.cpp`, `src/fixtureGatt.h` — fixture creation/finalization, publication callbacks, and existing Web target inventory.
- `src/dynamicGatt.cpp`, `src/dynamicGatt.h` — protected fixture capture, rebuild, and verification.
- `src/webInterface.cpp`, `src/webInterface.h`, `src/bluetoothManager.cpp`, `web/index.html` — existing status/GATT contract, connection lifecycle, target selector, and scenarios.
- `platformio.ini` and installed `.pio/libdeps/<environment>/NimBLE-Arduino/src` — pinned NimBLE-Arduino 2.5.1 wrapper and native contracts.
- `test/README`, `docs/Completed/DescriptorDrivenGattFixture/Completion.md` — current regression guidance and prior fixture guarantees.
