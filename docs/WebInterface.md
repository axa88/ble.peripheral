# Web Interface User Guide

The web interface controls the peripheral firmware when the selected build includes web support. Build modes are:

| Mode | Web interface | Serial console |
| --- | --- | --- |
| 0 | Off | Off |
| 1 | Off | On |
| 2 | On | Off |
| 3 | On | On |

## Build for local testing

Copy `platformio.local.example.ini` to the ignored `platformio.local.ini` and fill its Wi-Fi, OTA, and web-credential placeholders locally. `BLE_INTERFACE_MODE` selects the mode in the table above. With PlatformIO Core installed in the user profile, build both boards without uploading:

```powershell
$pioExe = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe'
& $pioExe run -e wrover -e c3
if ($LASTEXITCODE -ne 0) { throw 'Firmware build failed.' }
```

## Connect

1. Confirm that the device is connected to a trusted local network and find its current address using the project's normal status or serial workflow.
2. Open the device root page in a browser at its current HTTP address. The page is served at the root path.
3. When requested, use the web credential provisioned in the ignored local configuration. Keep that value out of source control, logs, screenshots, and this guide.

The page header shows this peripheral's device name, IP address, Bluetooth address, source revision, source fingerprint, and PlatformIO environment. These identify the device serving the page; the Bluetooth address is not a connected central's address. Reload the page after uploading firmware so its JavaScript is refreshed.

The service uses plain HTTP. It does not encrypt browser traffic with TLS. Use it only on a trusted local network; do not expose or forward it to the public internet.

## Operate the device

### Advertising

In Advertising, choose an item under Advertising instances. Use Remove selected instance, or fill New instance ID and Name and choose Add instance. Additional instances are available only when extended advertising is supported and configured capacity remains. Instance 0 is reserved and cannot be removed.

Mode and Discoverability apply when selected. Interval changes only after you choose Set interval. Start selected and Stop selected control one instance; Start all advertising and Stop all control the configured set. Directed advertising is exclusive: when more than one instance is configured and any instance uses a directed mode, Start all advertising is rejected. Start the directed instance on its own.

Started and stopped styling reflects device-reported state; a queued change is not shown as complete until the device reports it. Restart after connection is a single Enabled/Disabled toggle based on the reported device setting. While a request is running, the button shows Applying without replacing the last confirmed state.

### Pairing and bonds

Under Security and bonds, Pairing I/O describes the peripheral's local input/output capability: Display only, Display and yes/no, Keyboard only, No input or output, or Keyboard and display. The Bonding, MITM protection, Secure Connections, LTK distribution, IRK distribution, and CSRK distribution controls configure subsequent pairing procedures. They do not change a pairing request already in progress.

Active requests appear under Pairing with an expiry countdown. For a Display request, enter the passkey shown on the page at the peer. For PasskeyEntry, enter the peer's six-digit value on the page and choose Send passkey. For NumericConfirmation, compare the displayed value with the peer and choose Accept match only when they match; choose Reject otherwise. A compatible central interaction is still required to complete pairing.

Delete selected connection's bond and Delete all bonds change the device's stored security state.

### Connections and link information

Under Connections, choose a Connected peer and use Disconnect peer, or choose Disconnect all. Use Refresh link information when you need an explicit RSSI or PHY read for the selected connection. Link information is requested on demand rather than gathered by ordinary status polling.

### Link settings

Preferred local MTU is the peripheral's local preference for subsequent ATT negotiation. Choose Set preferred MTU to change that preference. This action does not initiate an MTU exchange or change the MTU already negotiated with a connected peer.

Connection parameters has Min ms, Max ms, Latency, and Timeout ms fields; choose Request parameters to submit them. A request can be rejected by the peer or controller. Data length accepts 27 through 251 octets; choose Request data length to submit it. Request PHY submits the displayed transmit and receive PHY combination.

### GATT controls

GATT Controls offers the original notification and indication characteristics plus Temperature Scalar. The selector shows names and UUIDs; the details show a boot-scoped target identity, properties, and current stored bytes. Central ATT writes to the original characteristics are reflected on subsequent polling.

Enter complete hexadecimal byte pairs, compact or separated by ASCII whitespace. Empty input publishes an empty value. Send notification or Send indication stores and verifies the bytes, then requests an update through normal subscriber handling. This does not invoke an ATT write callback. A Dispatched result identifies the request path, not client receipt, subscriber presence, or indication acknowledgement.

The send limit is the maximum number of value bytes in one update, not the characteristic's storage capacity. It is the smallest of the 256-byte Web cap, the characteristic's stored maximum, and every connected peer's negotiated `ATT_MTU - 3`. The three bytes are ATT protocol overhead. All connected peers count, including unsubscribed peers. For example, MTU 23 permits 20-byte updates; MTU 517 permits 514 transport bytes, while this Web interface still caps requests at 256 bytes.

The editor displays the payload byte count and authoritative send limit. Oversize feedback states the attempted bytes, applicable maximum, and negotiated-MTU calculation or local cap without prescribing remedies. Send is disabled for invalid/oversized drafts; its defensive popup reports the same constraint if invoked. Peer identities are shown only when the status snapshot agrees with the GATT snapshot; the GATT snapshot's minimum MTU, peer count, and publication limit remain authoritative. With no connected peers, the UI makes the lack of recipients explicit. Drafts survive polling and temporary snapshot loss.

There is no runtime Restore operation, saved-baseline inventory, automatic reset, or baseline button. Initial characteristic bytes remain ordinary fixture initialization. Tests construct fresh fixture state; manual scenarios state their required values explicitly. Selection, scenario prefill, refresh, reconnect, and publication do not reset other values. Overrides disappear on reboot.

GATT Test Scenarios contains static instructions and expected bytes for User Description, scalar Presentation Formats and signed exponents, Aggregate Format, Valid Range, absence, and read/update cases. Fill GATT Controls only selects a target and fills input; Send is explicit. Descriptor inspection and acknowledged trigger writes are central-side actions. Use a fresh fixture or set the known sample bytes required by a canonical-value test, and avoid concurrent writes to the same target.

The additive service is `12345678-1234-5678-1234-56789abcdf00`; it is discoverable after connecting but not advertised. Temperature Scalar is `...df05` (initial `EB 00`, sint16/exponent -1, 23.5 degrees Celsius). Subscribe and write acknowledged `01` to `...df07` to request its current stored bytes, including in non-Web builds. The [fixture specification](Completed/DescriptorDrivenGattFixture/Ble.Peripheral.DescriptorDrivenGattFixture.Plan.md) documents the seven targets and metadata; its historical Restore contract was superseded by the owner-requested removal described here.

The authenticated API exposes GET /api/gatt snapshots and POST /api/gatt operations. This example publishes characteristic bytes:

```json
{"op":"publishValue","targetId":"<current opaque targetId>","hex":"F500"}
```

Snapshots include `minimumPeerMtu` and `connectedPeerCount` from the same host observation as `publicationLimit`. Zero peers is distinct from a connected peer whose MTU is not yet available. Use the current snapshot's target identity and normal CSRF header. Poll `/api/status` for the operation ID; `storageVerified`, `publicationAttempted`, and `publicationDispatched` distinguish outcomes. Invalid identity/hex/length cannot select an arbitrary attribute. Host execution rechecks current bounds and reports actual sizes on a late rejection. An unstarted timed-out request is canceled; contention does not publish automatically later. Transport delivery and live HTTP/security still require device acceptance.

### Temporary services and Service Changed

The Temporary services section lists protected fixtures and up to four temporary services. Each temporary service has one READ-only characteristic. Enter canonical 128-bit service and characteristic UUIDs and an initial hexadecimal value of at most 256 decoded bytes; an empty value is allowed. Choose Add service to register it, or select a temporary service and choose Remove service. Built-in services cannot be removed. Temporary services are volatile, are not added to advertising, and disappear after reboot.

Adding or removing a service requires every peer to be disconnected and directed advertising to be stopped. The removal selector and Add/Remove buttons are disabled while a peer is connected or directed advertising is active. The device rechecks the conditions when it handles the request. After a structural change, reconnect and explicitly rediscover services on the BLE central.

API clients use POST /api/gatt with the same authentication and CSRF requirements as other operations. Add requires valueHex, which may be empty; use the current services inventory to obtain a serviceId for Remove. Each line is a separate request.

    {"op":"addService","serviceUuid":"12345678-1234-5678-1234-56789abce100","characteristicUuid":"12345678-1234-5678-1234-56789abce101","valueHex":"0102"}
    {"op":"removeService","serviceId":"<current boot-scoped serviceId>"}
    {"op":"sendServiceChanged"}

GET /api/gatt includes the existing values plus state, services, serviceLimit, valuesAvailable, servicesAvailable, and reason. The values and services collections can be unavailable independently; the unavailable collection is null with a reason. Do not treat null as an empty collection. servicesAvailable is true only when GATT is Ready, fixture readiness is true, and the managed inventory is valid. valuesAvailable requires a safe successful host snapshot. Busy or NotReady value snapshots return HTTP 200 with valuesAvailable false; a genuine failed snapshot returns HTTP 500.

Send Service Changed can be used while connected when GATT is Ready. It dispatches an indication for the full attribute range without changing the database. A Dispatched result means the server called NimBLE's send API; it does not prove radio delivery, client receipt, or acknowledgement.

When a GATT inventory or value snapshot is unavailable, the page labels the last observation stale and preserves unfinished input. It disables controls that need current data. RecoveryRequired shows the restart action and disables GATT mutations. A partial advertising-restoration warning refreshes the inventory and remains visible.

### Source revision

`firmware-revision.json` records the source revision and its content fingerprint. A common PlatformIO pre-hook synchronizes the generated Web asset, detects project source changes, advances the revision once for a changed snapshot, and emits an environment-specific identity header. Rebuilding unchanged sources keeps the same revision; changing only the build invocation does not advance it. The tracked state survives `pio run --target clean` and should be committed with its source changes.

CLI and VS Code firmware builds use the same hook. The post hook stamps the resulting application binary and verifies the current source identity before an upload; a stale `nobuild upload` cannot silently upload a mismatched source snapshot. The owner remains responsible for uploading. To synchronize source identity explicitly from the repository root:

```powershell
python tools/firmware_revision.py prepare
```

The displayed revision identifies project source, not negotiated MTU or client subscriptions. Source fingerprints exclude generated build outputs, revision state/locks, and ignored local credential configuration. Read the displayed revision, environment, and source fingerprint when recording device acceptance.

### Overview and operation details

Overview and Operation are collapsible sections. Operation feedback describes accepted, rejected, queued, and completed work. Resource observations under Overview are read-only measurements; they are not a substitute for checking runtime headroom under the intended workload.

## Network and security limits

The application validates authentication, CSRF, Host, and Origin data for web requests. The ESP-IDF public header accessor returns only the first matching header when a request contains duplicate header names. The application strictly parses the returned value and rejects malformed or comma-combined values, but it cannot inspect later duplicate fields.

Because traffic uses plain HTTP, authentication does not provide transport encryption. Keep the device on a trusted local network and do not place the service behind a public port forward or an untrusted proxy. Keep the local web credential, authorization data, CSRF values, and passkeys out of documentation and logs. Record a device or peer identifier when needed to identify an acceptance result; keep that evidence local and access-controlled, and omit identifiers from general documentation.

See the [completion record](Completed/WebInterface/Completion.md) for implementation evidence and the checks that remain unverified.
