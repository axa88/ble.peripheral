# Ble.Peripheral — Modular Web Interface Implementation Plan

**Revision:** 1.4.1 (execution amendment, 2026-10-22)
**Status:** Completed. On 2026-10-23, the user reported physical acceptance on both boards since feedback-05. No separate device verification is recorded for the two later closeout fixes or current rebuilt images; see the [completion record](Completion.md).
**Targets:** ESP32-WROVER and ESP32-C3
**Framework:** Arduino / PlatformIO, pioarduino 55.03.38-1
**BLE library:** NimBLE-Arduino 2.5.1
**Source:** `Library > Ble.Peripheral Web`

## Revision history

| Revision | Principal changes |
| -------- | ----------------- |
| 0.1–0.5  | Initial architecture, pairing, connection identity and security |
| 0.6–0.9  | Removed unnecessary abstractions; simplified builds and web implementation |
| 1.0–1.1  | Reduced build matrix, cJSON, HTTP contracts and result semantics |
| 1.2      | Single-slot worker, mDNS compatibility, mandatory pairing spike |
| 1.3      | Pre-submission validation, worker result corrections, disposable spike worktree |
| 1.4      | On-demand RSSI/PHY reads; 10-second and 25-second unanswered-pairing verification |
| **1.4.1** | User-authorized removal of the mandatory pre-implementation pairing spike; reuse existing evidence and validate remaining behavior during integration |

---

## 1. Objective and scope

Add a web interface to Ble.Peripheral while preserving the existing serial console.

Support four independently selectable configurations:

| Mode | Serial   | Web      |
| ---- | -------- | -------- |
| 0    | Disabled | Disabled |
| 1    | Enabled  | Disabled |
| 2    | Disabled | Enabled  |
| 3    | Enabled  | Enabled  |

Requirements:

- Either interface operates independently.
- Both interfaces operate simultaneously.
- Neither interface depends on the other.
- The BLE core operates without either interface.
- Both interfaces use the same authoritative BLE state.
- Existing serial commands and serial-controlled hardware tests remain compatible.

Preserve existing advertising, GATT, security, bonding, connections, preferred MTU, Wi-Fi, ArduinoOTA and target-specific capabilities.

**Scope constraint:** Introduce only mechanisms required for independently optional serial and web interfaces, safe BLE operations, and preservation of existing functionality.

Do not introduce a general-purpose device-management framework.

## 2. Architecture

```text
                       main.cpp
                          |
              +-----------+-----------+
              |                       |
         Serial Console          Web Interface
              |                       |
              +-----------+-----------+
                          |
                 Existing BLE APIs
                 + targeted helpers
                          |
              +-----------+-----------+
              |                       |
       BluetoothManager           Advertising
              |                       |
              +-----------+-----------+
                          |
                        NimBLE


                  connectionId.h
                        |
              +---------+---------+
              |                   |
       BluetoothManager    PairingCoordinator
              |                   |
              +-- Registered BLE -+
                  action callbacks
```

### Component ownership

**BluetoothManager**

- BLE initialization and callbacks.
- Connection identity and lifecycle.
- Security configuration.
- Connection operations.
- Native pairing injection and disconnection.
- Explicit RSSI and PHY reads.

**Advertising**

- Advertising instances and configurations.
- Start/stop and restart behavior.
- Advertising modes and discoverability.
- Directed advertising.
- Existing advertising synchronization.

**PairingCoordinator**

- Pending pairing requests.
- Request identity and deadlines.
- Response arbitration.
- Expiration and cancellation.

**Serial interface**

- Existing commands and menus.
- Input parsing.
- Console selection and presentation.

**Web interface**

- HTTP and JSON.
- Authentication and CSRF protection.
- Browser assets.
- Web-specific operation execution.

Reuse existing BLE APIs directly wherever practical.

Extract console-owned shared behavior into its natural owning component.

Do not introduce a comprehensive `PeripheralControl` abstraction.

---

## 3. Immediate prerequisite — Bond-deletion reporting bug

`BluetoothManager::DeleteAllBonds()` currently interprets the boolean result from `NimBLEDevice::deleteAllBonds()` as a deleted-bond count.

Correct this independently:

1. Read the number of stored bonds before deletion.
2. Call the existing NimBLE deletion API.
3. Read the number remaining afterward.
4. Report the observed net decrease.
5. Preserve the native success/failure result.
6. Preserve existing public signatures and serial behavior.

Do not reimplement NimBLE's internal deletion loop.

Defer return-type changes and shared directed-target cleanup to Phase 2.

Verify zero, one and multiple bonds, plus failure handling where reproducible.

**Gate A:** Existing bond-deletion reporting defect fixed.

---

## 4. Historical pairing evidence and implementation validation

**Execution amendment authorized by the user on 2026-10-22:** Proceed from Gate A directly to production Phases 1–4. A disposable pairing spike was optional diagnosis, not an implementation prerequisite. This replaced the original mandatory Gate B and its stop-before-implementation requirement, including the earlier handoff's execution order.

### 4.1 Interpretation of exploratory evidence

The exploratory WROVER work observed a successful user-reported deferred passkey ceremony after a separate task injected the value. A separate unanswered 10-second trial observed the application deadline and host events, but did not complete the required same-central recovery trial. These observations informed implementation decisions only. They do not establish production pairing, C3 behavior, numeric comparison, HTTP behavior, or physical acceptance.

The [completion record](Completion.md) documents retained artifacts, final software validation, the user's physical-acceptance report, and focused checks for later closeout fixes. Do not treat exploratory evidence as per-case production acceptance evidence.

### 4.2 Implementation approach

Use direct cross-task NimBLE injection initially, based on the WROVER result. Keep production pairing callbacks bounded and deadlines independent of interface polling. Implement the minimal connection identity, pure pairing state and coordinator described in Phase 1; do not merge experimental spike source.

Test host-event-queue dispatch only if an observed problem with direct injection warrants it. Fix concrete failures in the affected implementation and retest; do not recreate a general feasibility campaign before ordinary source work.

No new spike branch, worktree, central application feature or fixture framework is required. Retain the existing disposable tree only while its evidence or rollback inputs are needed; preserve findings before authorized cleanup.

### 4.3 Integrated physical acceptance

During Phase 4, validate the actual production implementation on WROVER and C3:

- Deferred passkey entry and numeric confirmation.
- Explicit numeric rejection and deadline expiration.
- Unanswered passkey entry at the production deadline, initially approximately 25 seconds.
- Actual application/native/peer termination ordering, observable host events during the pending interval, and successful same-central retry without peripheral reboot, NimBLE reset or manual pending-state cleanup.
- Independent serial/web operation, including responses and expiration without browser polling.

Use a separate approximately 10-second diagnostic only when needed to isolate termination behavior; reuse existing evidence when applicable. Do not require a duplicate 10/25-second spike matrix before implementation.

Coordinate only the manual actions actually needed for an integration test. State the selected peripheral I/O capability, central request mode, operator input required at each end, and expected success or failure before each trial. Do not treat expected pairing failure, a build, or a timeout-task heartbeat as full acceptance.

On 2026-10-23, the user reported physical acceptance on both boards since feedback-05. See the [completion record](Completion.md) for the scope of that report and the two later closeout fixes that still need focused device checks. The criteria above describe the original acceptance scope, not per-case evidence. Do not infer physical acceptance from a build or timeout-task heartbeat. Preserve credentials, security requirements, OTA behavior, provenance and all existing scope boundaries.

---

## 5. Phase 1 — Core independence and pairing

Begin immediately after the isolated Gate A correction. Section 4 evidence is reusable context; no pre-implementation Gate B approval is required.

### 5.1 Connection identity

Introduce:

```text
src/connectionId.h
```

Use a small platform-independent, boot-scoped connection identifier.

`BluetoothManager` owns the registry.

Requirements:

- Assign identity during `onConnect`.
- Associate it with the native handle and peer.
- Invalidate it during `onDisconnect`.
- Distinguish connections when native handles are reused.
- Revalidate the current native peer before mutations.
- Reject stale identities from earlier connections or boots.

Do not retain `NimBLEConnInfo` as an authoritative long-lived connection reference.

Serialize opaque identifiers as JSON strings.

### 5.2 Pairing state

Introduce:

```text
src/pairingState.h
src/pairingState.cpp

src/pairingCoordinator.h
src/pairingCoordinator.cpp
```

`pairingState` contains pure C++17 state logic.

No dependency on NimBLE, Arduino, FreeRTOS, HTTP or Serial.

`PairingCoordinator` adapts that logic to the embedded runtime.

Maintain a bounded collection of requests containing:

- Request ID.
- Connection ID where available.
- Pairing type.
- Required display or response value.
- Monotonic deadline.
- Resolution state.

Support first-response-wins behavior.

Reject duplicate, expired and stale responses.

Invalidate requests on disconnection or authentication completion.

### 5.3 Registered BLE actions

`BluetoothManager` registers three actions with `PairingCoordinator`:

```cpp
struct PairingActions
{
    bool (*injectPasskey)(
        ConnectionId connection,
        uint32_t passkey);

    bool (*injectConfirmation)(
        ConnectionId connection,
        bool accepted);

    bool (*disconnect)(
        ConnectionId connection);
};
```

Equivalent small stateless callbacks are acceptable.

The coordinator must not include `bluetoothManager.h`.

Each BLE-owned action resolves and validates the live connection immediately before calling NimBLE.

Use direct cross-task injection initially, as described in section 4; introduce host-context dispatch only if an observed defect requires it.

### 5.4 Non-blocking callbacks

Refactor:

```text
onPassKeyEntry()
onConfirmPassKey()
```

Each callback publishes its challenge and returns promptly.

Neither waits for console or web input in the NimBLE host task.

Serial observes changes through a lightweight change counter.

Web observes requests through authenticated HTTP polling.

Do not introduce responder-registration frameworks or browser-presence leases.

### 5.5 Independent pairing deadlines

Pairing deadlines must not depend on either interface's polling.

Use one lightweight coordinator-owned deadline executor in builds with at least one interface enabled.

The executor waits until a request changes or the nearest deadline.

On expiration:

- Numeric confirmation: inject rejection while the connection remains valid.
- Passkey entry: disconnect the corresponding live peer.
- Stale/disconnected request: invalidate without affecting another peer.

Never inject passkey zero as a timeout sentinel.

Initially use an approximately 25-second application deadline; verify actual termination ordering during integrated acceptance.

Do not create a task or timer per request.

### 5.6 Headless behavior

When neither interface is enabled:

- Configure non-interactive I/O capability.
- Preserve MITM/Secure Connections requirements.
- Do not silently downgrade security.
- Allow BLE initialization and advertising.
- Permit interactive authenticated pairing to fail when no operator is available.
- Do not allocate the interactive pairing executor.

### 5.7 Display-only pairing

`onPassKeyDisplay()` has no peer parameter.

Publish a time-bounded device-level passkey notification.

Do not invent a peer association.

Return promptly and protect web access to the displayed value.

### 5.8 Serial compatibility

Preserve existing six-digit passkey entry, `y/n` confirmation and unrelated console commands.

When multiple requests are pending, require unambiguous request selection.

### 5.9 Native pairing tests

Add a narrow PlatformIO native environment:

```ini
[env:native]
platform = native
framework =
lib_deps =
build_flags = -std=gnu++17
build_src_filter =
    -<*>
    +<pairingState.cpp>
test_build_src = yes
test_filter = test_pairing_state
```

Test location:

```text
test/test_pairing_state/test_main.cpp
```

Run:

```powershell
pio test -e native -f test_pairing_state
```

Use a supported Windows C++ compiler.

Keep native tests independent of NimBLE and FreeRTOS.

Test challenge creation, deadline expiration, duplicate responses, stale IDs, first-response-wins, disconnection, completion and capacity.

**Gate 1:** Native pairing-state tests and both target builds pass; callbacks do not wait for operator input, deadlines are independent of interface polling, and the BLE core no longer depends on interactive console input. Complete physical pairing acceptance against the integrated implementation in Phase 4.

---

## 6. Build configuration

### 6.1 Interface selection

Introduce:

```text
src/bleConfig.h
```

```cpp
#pragma once

#ifndef BLE_INTERFACE_MODE
#define BLE_INTERFACE_MODE 3
#endif

#if BLE_INTERFACE_MODE < 0 || BLE_INTERFACE_MODE > 3
#error Invalid BLE_INTERFACE_MODE
#endif

#define BLE_ENABLE_SERIAL_CONSOLE ((BLE_INTERFACE_MODE & 1) != 0)
#define BLE_ENABLE_WEB_INTERFACE  ((BLE_INTERFACE_MODE & 2) != 0)
```

Modes:

```text
0 = Headless
1 = Serial only
2 = Web only
3 = Both
```

Default to mode 3.

### 6.2 Optional source

Guard interface-specific translation units with the corresponding compile flag.

Guard initialization in `main.cpp`.

Disabled interfaces must contribute no functionality or assets.

Physically removing a disabled interface's source files must not break the selected build.

Keep existing source-file locations.

Move interactive C3 USB CDC input/menu gating into optional serial code.

### 6.3 Firmware build verification

Existing environments:

```text
wrover
wrover-com
c3
c3-com
```

Routine verification:

- Eight primary firmware builds: two boards × four interface modes.
- Two upload-environment compatibility builds in mode 3.

Use `PLATFORMIO_BUILD_FLAGS` to select the interface mode.

Preserve the established PowerShell `try/finally` process-state restoration.

Explicitly select mode 3 for upload-variant checks.

No custom interface-mode script or duplicate source-filter system.

### 6.4 Native test exclusion

Set:

```ini
[platformio]
default_envs = wrover, c3
```

Keep the native test environment outside normal firmware builds.

---

## 7. Phase 2 — Shared operations and authenticated web backend

### 7.1 Extract shared console logic

Move reusable behavior out of console-only implementations.

| Console | Shared behavior |
| ------- | --------------- |
| `R` | Stop all advertising and cancel pending restarts |
| `P` | Add instance using existing defaults |
| `I` | Assign directed target |
| `S` | Delete connected peer's bond and reconcile targets |
| `D` | Delete all bonds and reconcile targets |
| `X` | Disconnect all connections |
| `C` | Validate/set preferred MTU |
| `N` | Validate/request connection parameters |
| `M` | Validate/request data length |
| `B` | Reuse existing PHY update request behavior |
| `,` | Read current peer RSSI on demand |
| `=` | Gather device status |
| Connection listing | Gather connected-peer snapshots |

Place shared operations in existing owning components or small helpers.

Keep console selection/presentation and browser serialization separate.

### 7.2 Advertising mode

Add explicit `Advertising::SetMode(instanceId, mode)`.

Reuse the existing locked transition logic from `CycleMode()`.

Preserve active-state restrictions, supported modes, directed-mode restoration and restart behavior.

Do not implement selection through repeated cycles or stale whole-configuration updates.

### 7.3 Validation and synchronization

Centralize:

- Preferred MTU: existing minimum and shared maximum, currently 517.
- Advertising interval: reject outside 20–10240 ms.
- Data length: reject outside 27–251 octets.
- Connection parameters: preserve existing validation and interdependent constraints.

Synchronize security-flag read-modify-write operations inside `BluetoothManager`.

Preserve unrelated flags and applied NimBLE state.

### 7.4 Bond operations

Expose shared outcomes and directed-target reconciliation for:

- Deleting the bond of a connected peer.
- Deleting all stored bonds.

Do not introduce arbitrary stored-bond deletion.

### 7.5 Cheap status versus on-demand native reads

**Periodic `/api/status` must not perform controller RSSI or PHY reads.**

The ordinary device snapshot may include inexpensive, locally available state:

- Device identity.
- Network state.
- Preferred MTU.
- Advertising configuration and activity.
- Security configuration.
- Stored bond identities.
- Connected peer identities and locally available negotiated parameters.
- Supported capabilities.

Specifically exclude automatic calls to:

```text
BluetoothManager::GetPeerRssi(...)
BluetoothManager::Phy(handle, std::nullopt)
```

These paths invoke native BLE controller reads and must not run for every connection on every browser refresh.

#### On-demand peer link read

Reuse the existing route:

```text
POST /api/connections
```

Add one operation:

```json
{
  "op": "readPeerLink",
  "connectionId": "opaque-connection-id"
}
```

When explicitly requested:

1. Validate the current connection ID.
2. Read RSSI using the existing `GetPeerRssi` operation.
3. Read current PHY using the existing read-only `Phy` operation.
4. Report available values and any read failures.
5. Return the result for the requested connection only.

Use the existing single web worker for this controller-read operation so that synchronous HCI reads do not block HTTP request handling.

No additional worker, queue or HTTP route is required.

The completed operation record may include:

```json
{
  "operationId": "opaque-operation-id",
  "op": "readPeerLink",
  "state": "Done",
  "result": "Applied",
  "data": {
    "connectionId": "opaque-connection-id",
    "rssiDbm": -58,
    "txPhy": 2,
    "rxPhy": 2
  }
}
```

The values are illustrative.

If a controller read fails, report `Failed` and identify unavailable fields without inventing measurements.

The browser invokes `readPeerLink` when the operator selects **Refresh link information** for a connected peer.

Do not invoke it automatically during ordinary 1–2-second status polling.

The console retains its existing explicit RSSI command and manual status behavior.

### 7.6 HTTP server and JSON

Use ESP-IDF `esp_http_server`.

Use framework-provided cJSON on both targets.

Perform a minimal compile/link smoke check before implementing the API.

Configure:

- URI handler capacity.
- Socket limits.
- Server task stack.
- Receive/send timeouts.
- Request body limits.
- Header and URI limits.
- Appropriate client-purging behavior.

The supplied SDK configurations specify 1,024-byte request-header and 512-byte URI limits.

Use supported runtime configuration fields where appropriate.

Keep handlers short and avoid unnecessary head-of-line blocking.

### 7.7 HTTP routes

Use nine URI/method registrations:

```text
GET  /
GET  /api/status

POST /api/advertising
POST /api/security
POST /api/bonds
POST /api/connections
POST /api/mtu

GET  /api/pairing
POST /api/pairing
```

`GET /` serves the embedded HTML.

All API routes require authentication.

`GET /api/status` returns inexpensive local state, the CSRF token and the latest worker operation.

`GET /api/pairing` returns protected pairing challenges.

RSSI/PHY reads use `POST /api/connections` with `op: readPeerLink`.

### 7.8 Request contract

Every POST uses:

```json
{
  "op": "operationName"
}
```

Additional fields provide required parameters and target IDs.

Each `op` maps directly to existing or extracted shared BLE behavior.

Connection, pairing request and operation IDs are JSON strings.

Validate exact fields, types and value ranges using cJSON.

Do not create a second command-dispatch framework.

### 7.9 Web result vocabulary

```text
Queued
Applied
Accepted
Dispatched
InvalidArgument
Unsupported
Busy
Failed
```

Meanings:

- `Queued`: accepted into the web worker but not executed.
- `Applied`: synchronous local operation completed.
- `Accepted`: native BLE API reported request acceptance.
- `Dispatched`: native operation invoked without observable acceptance/completion.
- `InvalidArgument`: input rejected.
- `Unsupported`: unavailable operation or capability.
- `Busy`: conflicting unresolved work.
- `Failed`: underlying operation failed.

`Queued` appears only in the immediate HTTP 202 response.

Native `Accepted` must never describe merely entering the worker.

### 7.10 HTTP status mapping

| HTTP | Meaning |
| ---- | ------- |
| 200 | Request processed; inspect JSON result, including ordinary `Failed` |
| 202 | Deferred worker operation submitted, with `result: Queued` |
| 400 | Invalid input or unsupported operation |
| 401 | Authentication failure |
| 403 | CSRF, Host/Origin or authorization failure |
| 404 | Unknown/stale connection or pairing request |
| 409 | Busy/conflicting operation |
| 500 | Unexpected internal server failure |

Ordinary native BLE failures are not automatically HTTP 500.

### 7.11 `/api/status`

The authenticated status response contains:

```json
{
  "device": {},
  "network": {},
  "preferredMtu": 0,
  "advertising": [],
  "security": {},
  "bonds": [],
  "connections": [],
  "capabilities": {},
  "csrfToken": "opaque-token",
  "lastOperation": null
}
```

Values are illustrative placeholders.

`connections` contains locally available connection state only. It must not trigger current RSSI or PHY controller reads.

Pending pairing passkeys do not appear in general status.

On-demand `readPeerLink` results appear in the latest completed operation record.

### 7.12 Single-slot web worker

**Use exactly one worker task and one operation slot. No separate command queue.**

Create the task during web initialization, only in web-enabled builds.

All web-initiated `Advertising` mutations use the worker:

- Start/stop.
- Stop all.
- Mode and discoverability.
- Interval.
- Restart policy.
- Directed targets.
- Add/remove instances.

The explicit `readPeerLink` operation also uses this worker.

Other operations may execute inline only when verified short and non-blocking.

Pairing responses remain independent of the worker.

#### Worker record

```text
operationId
operation
target
state
result
detail
optional result data
```

**State is exactly:**

```text
Queued
Running
Done
```

**Stored result is:**

```text
null                 (while Queued or Running)

Applied              (after Done)
Accepted             (after Done)
Dispatched           (after Done)
Failed               (after Done)
```

Do not store `Queued`, `InvalidArgument`, `Unsupported` or `Busy` as terminal worker results.

Do not use `Succeeded` or `Failed` as worker states.

#### Admission validation

Validate requests before occupying the slot:

1. Authentication and CSRF.
2. JSON syntax and required fields.
3. Parameter types and ranges.
4. Valid operation name.
5. Target identifiers and known target existence.
6. Known device capability restrictions.

Return 400 or 404 for invalid requests without occupying the slot.

Known-invalid requests must not enter the worker.

Admission validation does not guarantee execution success. Owning components still validate live state when work executes.

#### Submission

Under the slot mutex:

1. If state is `Queued` or `Running`, return Busy (HTTP 409).
2. Otherwise assign a new boot-scoped operation ID.
3. Copy bounded command parameters.
4. Set `state = Queued`.
5. Set `result = null`.
6. Notify the worker.
7. Return HTTP 202 with `result: Queued` and the operation ID.

Do not hold the mutex while waiting for execution.

#### Execution

The worker:

1. Wakes from task notification.
2. Sets state to `Running`.
3. Executes the selected shared operation without holding the slot mutex.
4. Records the observed result and any bounded result data.
5. Sets state to `Done`.

The record persists until the next accepted submission.

Existing advertising operations may run for approximately five seconds.

While `Queued` or `Running`, further deferred submissions return Busy.

No cancellation mechanism, separate timeout, priority scheduler or persistent history is required.

Measure the worker task stack in Phase 3.

---

## 8. HTTP security and existing hostname

### 8.1 Authentication and CSRF

Use HTTP Basic authentication for the trusted-LAN evaluation fixture.

Require authentication for all API routes, including pairing, connections, bonds and security information.

Generate one cryptographically random CSRF token at boot.

Return it through authenticated `/api/status`.

Every mutation and explicit controller-read POST must:

1. Use POST.
2. Use JSON content type.
3. Present the correct CSRF header.
4. Pass Host/Origin validation.
5. Reject cross-origin requests.
6. Avoid permissive CORS.
7. Never mutate state through GET.

Plain HTTP does not protect credentials or pairing data from network observers.

Do not expose the service publicly.

### 8.2 Host and Origin validation

The existing network implementation sets:

```cpp
ArduinoOTA.setHostname(OTA_HOSTNAME);
ArduinoOTA.begin();
```

ArduinoOTA uses mDNS by default in the pinned framework, subject to successful initialization.

Accept exactly:

1. The configured/current device IP address.
2. `<OTA_HOSTNAME>.local`.

Do not introduce another hostname or mDNS service.

Normalize hostname case and optional HTTP port.

When Origin is supplied, require scheme, hostname and effective port to match the validated Host.

Reject unknown hosts and cross-origin mutations.

Verify IP and mDNS access on the actual LAN.

### 8.3 Credentials

Keep the web credential separate from OTA credentials.

Use a gitignored local PlatformIO configuration and a tracked placeholder example.

Move existing committed Wi-Fi/OTA credentials out of tracked configuration and rotate exposed values.

Preserve working build and upload settings.

Do not rewrite Git history without separate authorization.

Do not log passwords, Authorization headers, CSRF tokens or entered passkeys.

---

## 9. Phase 3 — Browser UI and embedded asset

### 9.1 Single browser asset

Use:

```text
web/index.html
```

Include minimal CSS and JavaScript inline.

Serve one gzip-compressed HTML asset at `GET /`.

No frontend framework, separate CSS/JS files, filesystem partition or WebSockets.

### 9.2 Asset generation and verification

Commit generated:

```text
src/webAssets.cpp
```

Use Python's standard library to compress the HTML and emit a C++ byte array.

Use `mtime=0` for gzip metadata.

Do not require identical compressed bytes across zlib versions.

Verify by decompressing the committed array and comparing it byte-for-byte with `web/index.html`.

Guard the generated asset with `BLE_ENABLE_WEB_INTERFACE`.

No PlatformIO pre-build generator is required.

### 9.3 Partition layout

Preserve:

```ini
board_build.partitions = no_fs.csv
```

Embed web assets in the application image.

Change the partition layout only if measured firmware size requires it while preserving OTA.

### 9.4 Browser functionality

Provide equivalent access to existing console controls.

**Overview:** Device identity, network, preferred MTU, advertising, security and connections.

**Advertising:** Instance selection, start/stop, stop-all, explicit mode selection, discoverability, interval, restart policy, directed targets and extended instances.

**Security:** I/O capability, MITM, Secure Connections, bonding, LTK/IRK/CSRK.

**Bonds:** List bonds, delete connected peer's bond and delete all bonds.

**Connections:** Peer details, negotiated MTU, connection parameters, data length and disconnection.

**Peer link information:** Explicit on-demand RSSI and current PHY read for the selected connection. Do not repeat those native reads during general status polling.

**Pairing:** Passkey display/entry, numeric confirmation/rejection and pending challenge status.

**Operation feedback:** Worker state, final operation result and on-demand peer-link measurements.

Use lightweight polling without overlapping requests.

Keep pairing expiration independent of the browser.

---

## 10. Implementation phases and gates

| Stage | Work | Exit gate |
| ----- | ---- | --------- |
| A | Correct existing bond-deletion reporting | Existing bug fixed |
| B (optional) | Reuse existing evidence; diagnose only concrete pairing defects | No pre-implementation gate; remaining physical checks belong to Phase 4 |
| 1 | Connection identity, minimal pairing coordinator, native tests and optional compilation | Pairing works independently of serial input |
| 2 | Extract shared operations, authenticated HTTP API, single-slot worker, on-demand link reads | Web API works without console implementation |
| 3 | Embedded HTML interface, memory and partition verification | Browser covers existing console functionality |
| 4 | Firmware build matrix and focused regression tests | Final acceptance criteria satisfied |

Proceed through production Phases 1–4 after Gate A. The earlier mandatory Gate B is superseded by section 4; preserve final acceptance requirements and report unperformed checks as pending.

Do not expand phases with speculative infrastructure.

---

## 11. Final acceptance criteria

This is the original detailed acceptance checklist, not a per-case evidence record. See the [completion record](Completion.md) for the user's report and the later-fix verification boundary.

### Architecture and builds

- Serial-only, web-only, combined and headless configurations work.
- Neither interface is required by the BLE core.
- Existing serial commands and hardware tests remain compatible.
- Eight primary firmware builds and both upload-variant checks pass.
- Native pairing-state tests pass.
- Disabled interface code and web assets are absent from applicable builds.

### BLE correctness

- Production deferred pairing is validated on WROVER and C3 during integrated acceptance.
- Reuse the observed WROVER 10-second termination evidence; run further short-deadline diagnostics only when needed to isolate a concrete failure.
- The 25-second test records which mechanism terminates pairing and confirms successful retry.
- NimBLE host responsiveness is verified during pending production pairing through actual host events.
- Existing spike findings are retained outside the disposable worktree; no further spike is required to begin implementation.
- Pairing callbacks do not block awaiting operator input.
- Pairing deadlines operate independently of interface polling.
- Stale connection and pairing IDs cannot affect another connection.
- Explicit advertising-mode selection is synchronized.
- Shared validation and security mutations are consistent.
- Bond deletion and directed-target cleanup use shared operations.
- Existing GATT, advertising, MTU and connection behavior remain compatible.

### HTTP and browser

- Periodic `/api/status` gathers only inexpensive locally available state.
- Periodic status does not invoke controller RSSI or PHY reads.
- `readPeerLink` performs RSSI/PHY reads only for an explicitly selected connection.
- On-demand link reads reuse the existing route and single worker.
- The web worker uses one task and one operation slot.
- Admission validation runs before the slot is occupied.
- Invalid, unsupported and stale requests do not consume the slot.
- Worker states are only `Queued`, `Running` and `Done`.
- Stored result is `null` until `Done`.
- Terminal worker results are only `Applied`, `Accepted`, `Dispatched` or `Failed`.
- HTTP 202 `Queued` remains distinct from native `Accepted`.
- Ordinary BLE failures are not misclassified as HTTP server faults.
- Authentication, CSRF and Host/Origin validation work.
- Device IP and existing OTA `.local` hostname are accepted when available.
- Embedded HTML decompresses to the current source.
- cJSON and the HTTP server work on both targets.

### Compatibility and resources

- Firmware fits the selected application partitions.
- Heap and task stacks remain sufficient under representative BLE/Wi-Fi/HTTP load.
- Wi-Fi reconnection and ArduinoOTA continue working.
- Serial console remains independently usable.
- Either interface can subsequently be removed without redesigning the BLE core or the other interface.

---

## 12. Scope exclusions

Do not introduce:

- New BLE evaluation features.
- Replacement of the serial console.
- Migration of existing serial-controlled tests to HTTP.
- GATT fixture rewrites.
- NimBLE internal rewrites.
- A general `PeripheralControl` framework.
- Generic RPC, job scheduling or event infrastructure.
- Multiple web workers or operation histories.
- Automatic high-frequency RSSI/PHY monitoring.
- Browser operator-presence management.
- New mDNS or hostname-management services.
- Cloud connectivity or public internet access.
- Browser firmware flashing.
- New filesystem partitions without measured need.
- Frontend frameworks or WebSockets.
- Cosmetic source relocation.
- Unrelated repository cleanup.

---

**Revision 1.4.1 — Architecture retained; execution prerequisites amended**

The on-demand RSSI/PHY design and production architecture remain as specified in revision 1.4. At the user's instruction, the mandatory pre-implementation pairing spike is removed. Reuse established central and WROVER evidence, implement Phases 1–4, and resolve concrete defects when found. Remaining production pairing and web behavior must be validated during integrated acceptance before completion is claimed.
