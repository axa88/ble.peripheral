# Native startup regression

Run `python test/descriptor_startup_native/run.py`. The runner extracts the current production bodies for `BluetoothManager::SetupBluetooth`, `Advertising::Setup`, the advertising snapshot readers, and the fixture readiness rebuild guard. It links the actual `src/fixtureGatt.cpp` with the shared native NimBLE object adapter and the repository-local g++ compiler. Generated files stay under `.pio/descriptor-fixture/native-startup`.

The runner covers legacy and extended advertising builds, successful boot, server registration failure, descriptor finalization failure, default instance visibility through `GetConfig` and `TrySnapshot`, pre-ready rebuild safety, and repeated setup idempotency. On successful boot, the `Start` adapter invokes the extracted production rebuild path after readiness; the extended-advertising stub models `setInstanceData()` calling `server.start()` and checks that the aggregate descriptor handles and bytes remain stable.

This is native software coverage. It does not validate the NimBLE radio implementation, advertising over the air, device HTTP/security behavior, client subscriptions, or physical-device acceptance.
