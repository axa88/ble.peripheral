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

### Overview and operation details

Overview and Operation are collapsible sections. Operation feedback describes accepted, rejected, queued, and completed work. Resource observations under Overview are read-only measurements; they are not a substitute for checking runtime headroom under the intended workload.

## Network and security limits

The application validates authentication, CSRF, Host, and Origin data for web requests. The ESP-IDF public header accessor returns only the first matching header when a request contains duplicate header names. The application strictly parses the returned value and rejects malformed or comma-combined values, but it cannot inspect later duplicate fields.

Because traffic uses plain HTTP, authentication does not provide transport encryption. Keep the device on a trusted local network and do not place the service behind a public port forward or an untrusted proxy. Keep the local web credential, authorization data, CSRF values, and passkeys out of documentation and logs. Record a device or peer identifier when needed to identify an acceptance result; keep that evidence local and access-controlled, and omit identifiers from general documentation.

See the [completion record](Completed/WebInterface/Completion.md) for implementation evidence and the checks that remain unverified.
