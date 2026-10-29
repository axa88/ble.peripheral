# Dynamic GATT Services and Service Changed: Completion

[Archived implementation plan](Ble.Peripheral.DynamicGattServiceChanged.Plan.md) · [Web Interface user guide](../../WebInterface.md)

## Delivered

The existing GATT API and page support adding and removing temporary services and dispatching Service Changed. The device holds up to four temporary services at once. Each has one READ-only characteristic, a canonical 128-bit service UUID and characteristic UUID, and an initial value of up to 256 bytes. Services are volatile, are not automatically advertised, and cannot replace protected fixture services.

Structural add/remove requires no connected peers and no active directed advertising. The UI disables those actions under either condition, and the device rechecks readiness before mutation. This disconnected-only scope is accepted for now. Manual Service Changed remains available while connected when GATT is Ready. Dispatched reports that the server called the NimBLE API; it does not establish radio delivery, peer receipt, or acknowledgement.

The UI retains stale inventory/value observations with a visible unavailable reason, preserves unfinished input, reports partial advertising restoration, and presents the required restart action after GATT recovery becomes necessary.

## Software evidence

The historical revision 19 qualification passed the eight C3/WROVER builds across modes 0–3, the recorded native checks, and the Web/UI regression evidence. Its source ID was 5c3aefe07013bca7124025c870bba0c08d37125a046b4fcc1bbfe1f0190fd639. Compact retained records are under .pio/dynamic-gatt-closeout/20261029T101203Z/historical-r19 and include the qualification summary, manifests, and source snapshot; earlier raw per-build logs are not retained.

The final closeout snapshot result is recorded separately at .pio/dynamic-gatt-closeout/validation-manifest.json. That manifest is authoritative for the final reviewed snapshot; the historical r19 result above is not a claim about a later snapshot.

## Deferred and unrecorded

No physical C3/WROVER matrix, Windows device observations, or device-reported firmware identity evidence was recorded. Physical acceptance is deferred and unrecorded.

Live connected structural mutation remains unsupported. Robust Caching is deferred; the current GATT caching and dynamic-service configuration remains intentionally disabled. Broader client and operating-system event receipt checks remain separate follow-up.

No device flashing or deployment occurred. Commit cleanup and local promotion were authorized and in progress at this documentation closeout; no remote push was authorized. Private local configuration and its credential values are excluded from this record.