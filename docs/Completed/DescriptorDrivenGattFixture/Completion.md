# Descriptor-driven GATT fixture completion record

**Status:** Software implementation and validation complete. The owner authorized local promotion and scoped cleanup on 2026-10-27. Detailed physical/platform acceptance is unverified and is not reported as passed.

**Source:** Feature branch `descriptor-interpretations`, based on `d38a56fb67d2c48f1ad9c691b2700e38d407affe`. Exact final source, image, test, cleanup, and Git results are retained locally in `.pio/descriptor-fixture/validation-manifest.json` and `.pio/descriptor-fixture/closeout/`.

## Delivered behavior

The [canonical plan](Ble.Peripheral.DescriptorDrivenGattFixture.Plan.md) defines seven additive characteristics in service `12345678-1234-5678-1234-56789abcdf00`: five scalar CPF cases, an Aggregate value referencing assigned CPF handles, and an acknowledged-write update trigger. New metadata is READ-only; the stack owns CCCDs. The new service is not advertised.

Web controls expose only the original notification/indication characteristics and Temperature Scalar. Publishing stores and verifies bytes before requesting ordinary subscriber delivery; restoration verifies original bytes without publishing. Boot-scoped target IDs, the existing authenticated single worker, CSRF/Host/Origin guards, and conservative all-peer `ATT_MTU - 3` bounds remain in effect. Scenarios provide instructions/prefill without executing tests or changing values.

Value snapshots/mutations run on the NimBLE host event queue alongside ATT callbacks. Owned queued work survives caller timeout and cancels before execution if it has not started. The one-second wait is a soft deadline after enqueue: queue backpressure or an already-running callback can extend it. Dispatch is not proof of client receipt or indication acknowledgement.

The page header shows the serving device's name, IP, and Bluetooth address. The hex editor shows payload bytes, send limit, excess, and the binding constraint. Invalid/oversized drafts disable Send while preserving Restore availability subject to readiness and the worker lock. A baseline that exceeds the send limit is explicitly distinguished from a value that cannot be stored; peer-limited baseline hints name the necessary negotiated MTU. Drafts survive polling and temporary snapshot loss.

Original UUIDs, values, ATT callbacks, advertising payloads, security, pairing, and serial behavior are preserved. No Ble.Net code, semantic decoding, fragmentation, automatic MTU exchange, or persistent override was added. [User guidance](../../WebInterface.md) and [test guidance](../../../test/README) describe operation and validation.

## Regressions corrected

- Descriptor getters return ATT permission flags (`BLE_ATT_F_READ`, `0x01`), while characteristic getters use characteristic flags (`NIMBLE_PROPERTY::READ`, `0x02`). The original comparison rejected valid metadata and prevented advertising setup. Default instance 0 is now stored before failure branches; radio configuration/start remains readiness-gated, including C3's implicit server-start path.
- Completed publication/restoration serialized three GATT result fields that the status validator rejected. The stored last operation then broke every status refresh and was mislabeled an allocation failure. The validator now requires the complete Boolean shape, and incomplete response validation has its own diagnostic.
- GATT snapshots could report HTTP 200 after individual cJSON field-allocation failures, and an explicit snapshot failure was labeled Busy. Every construction step is now checked; allocation/explicit failure produces HTTP 500 while Busy/NotReady remains retryable HTTP 409.

The native tests include deliberately broken variants that reproduce the descriptor-domain, status-schema, and incomplete-GATT-response defects. Earlier successful builds/static/browser checks did not execute these backend contracts; they were insufficient to catch those regressions.

## Software validation

The final local evidence index records exact commands, source hashes, toolchain versions, image SHA-256 values, OTA slot sizes, and the scope/limits of each check.

| Check | Evidence and limit |
| --- | --- |
| Fixture native regression | Seven success/failure cases execute production fixture code against a narrow adapter checked against pinned NimBLE 2.5.1 contracts; the wrong-enum variant fails normal finalization. |
| Startup native regression | Six legacy/extended success and injected-failure cases exercise extracted production orchestration and both UI inventory access paths. |
| Backend status/GATT regression | Extracted production serializers, validators, and routes; 94 real cJSON allocation-failure injections; broken variants reproduce status rejection and incomplete HTTP 200. Snapshot producer, BLE execution, and HTTP transport are stubbed. |
| Browser and generated asset | Passed. Node regressions cover 23-byte baseline at limits 20/23, valid/empty/invalid/oversized drafts, overlapping limiting factors, Restore, worker locking, and polling/recovery. The verified asset is 16,027 gzip bytes from 56,776 HTML bytes. |
| Firmware qualification | Eight configurations passed with zero warnings: modes 0/1 on both boards from `20261026T193753Z-6aa18b09`, and final modes 2/3 on both boards from `20261026T201121Z-34f8dab1`. All three native gates passed before the final four builds; all 35 source-input hashes stayed unchanged during that run. |

Modes 0/1 reuse their qualified images after verification that only HTML and the generated Web asset changed, and that current/archived Web assets produce identical preprocessor output with no declarations or data in both non-Web modes. The archived pre-limit HTML/asset copies reproduce the earlier firmware-input snapshot. Web modes are rebuilt for the final page. This is explicitly combined qualification, not a claim that all eight images share one textual source snapshot. Both application images must fit both 2,031,616-byte OTA slots; Web assets must occur only in modes 2/3.

Earlier 71-check pure Web-validation and 8-check pairing results concern unchanged helpers and are historical evidence, not new physical acceptance. No upload or device operation is part of this closeout.

Current mode-3 application/OTA images (paths relative to `.pio/descriptor-fixture`):

| Board | Image | Bytes | SHA-256 |
| --- | --- | --- | --- |
| C3 | `20261026T201121Z-34f8dab1/c3_mode3.bin` | 1,474,128 | `3d853486b7e7ab6c749c5522eea92b817f08d3a1c9b16df224423df03266d695` |
| WROVER | `20261026T201121Z-34f8dab1/wrover_mode3.bin` | 1,414,160 | `6dcfaa95b1d424ecdb33d623e4050e25eaa0b14f7a59f72338c147b17faf3210` |

The final binaries contain the exact verified compressed page. Source/asset/browser/static checks complement native execution; no check here proves over-the-air receipt or live HTTP/security behavior. Shared PlatformIO outputs are idle for the owner's flashing.


## Physical/platform evidence and follow-up

The owner reported the earlier WROVER advertising/instance-0 failure and notification-triggered Web status failure on both boards. Those have deterministic software reproductions and corrections. Exact uploaded-image provenance and detailed acceptance captures were not supplied.

The later indication report is consistent with the required transport guard: the restored ASCII `Indicate Characteristic` baseline is 23 bytes, while the copied UI line showed stored capacity 512 and publication limit 20. Restoration is permitted; publishing that baseline is correctly blocked. The owner reported a one-byte operation working, but no independently captured client receipt or board/image identity was supplied for that report. The requested UI clarification now makes these sizes and actions explicit.

The owner requested feature closure and local promotion on 2026-10-27. The [physical acceptance matrix](Ble.Peripheral.DescriptorDrivenGattFixture.Plan.md#8-physical-acceptance-and-blenet-handoff) remains follow-up: both-board descriptor bytes/ownership/handles/permissions, transport and subscription cases, restoration without updates, original behavior/security, and Windows/Android raw/native observations are not exhaustively verified. Software/build results do not substitute for that evidence. The owner retains flashing and physical verification.

## Artifact and Git disposition

The final index and exact cleanup receipts identify retained and removed paths. The two reviewed allowlists removed 100 generated files totaling 41,837,426 logical bytes, plus the empty former Backlog directory. The no-PlatformIO regeneration check passed and rewrote exactly the five expected Web-status native outputs; those are intentionally retained, not unresolved leftovers. Retain current qualification images/logs/manifests and the previous owner-handoff images until no longer needed; retain compact historical provenance and the two unique uncommitted regression-source patches. Obsolete defective images, duplicate logs, and reproducible native scratch outputs are retired by reviewed literal allowlist, with containment/reparse/tracked checks. The native Web-status regeneration pilot verifies only its expected outputs return; those regenerated outputs have a named reproduction purpose. Shared toolchains, dependencies, existing v1.4 evidence, and current upload build trees are preserved.

Local promotion uses one reviewed feature commit, a fast-forward-only update of `master`, and retirement of the merged local feature branch after verification. The exact commit/refs and resulting clean working-tree state are recorded in the local promotion receipt. Remote push is separate and is not authorized by this closeout. Retention and promotion follow the [feature closeout procedure](../../Feature-Closeout.md).
