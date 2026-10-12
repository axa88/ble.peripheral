# Connectable vs Discoverable

These are independent GAP concepts.

* **Connectable** determines whether the advertiser will accept an LE connection request.
* **Discoverable** indicates whether the advertiser is advertising itself as being in General or Limited Discoverable Mode.

An advertiser may be:
| Connectable | Discoverable | Valid |
| :---        | :---         | :---  |
| Yes         | Yes          | Yes   |
| Yes         | No           | Yes   |
| No          | Yes          | Yes   |
| No          | No           | Yes   |

The Bluetooth specification does not require these properties to be linked.

---

## Discoverable (Advertising Flags)

### Bluetooth Core Specification
* Vol. 3, Part C, Section 11.1.3 (Flags AD Type)

The Flags AD structure (AD Type `0x01`) is advertisement payload data.

* **Bit 0**: LE Limited Discoverable Mode
* **Bit 1**: LE General Discoverable Mode
* **Bit 2**: BR/EDR Not Supported
* **Bit 3**: Simultaneous LE + BR/EDR (Controller)
* **Bit 4**: Simultaneous LE + BR/EDR (Host)

The discoverability flags advertise the GAP discoverability mode of the device.

### General Discoverable
Indicates the advertiser is operating in General Discoverable Mode. The advertiser remains generally discoverable until advertising is stopped or its discoverability mode changes.

### Limited Discoverable
Indicates the advertiser is operating in Limited Discoverable Mode. Limited Discoverable Mode is intended for temporary discovery and is normally used only for a limited duration.

### Neither Flag Present
If neither discoverability flag is present, the advertiser is not declaring either General or Limited Discoverable Mode. 

This does not prevent scanners from receiving the advertisement. The Flags AD structure is informational; scanners may choose whether or not to interpret it.

---

## Connectable

Connectability determines whether the advertiser will accept an incoming LE connection request (`CONNECT_IND` for legacy advertising or an equivalent connection procedure for extended advertising).

* **Connectable = true**: Connection requests are accepted.
* **Connectable = false**: Connection requests are ignored.

Connectability is independent of discoverability.

---

## Legacy Advertising

For legacy advertising, connectability and scannability are encoded by the advertising PDU type.

| Advertising PDU       | Connectable   | Scannable     |
| :---                  | :---          | :---          |
| ADV_IND               | Yes           | Yes           |
| ADV_DIRECT_IND        | Yes           | No            |
| ADV_SCAN_IND          | No            | Yes           |
| ADV_NONCONN_IND       | No            | No            |

The discoverability flags are not encoded in the PDU type. They remain ordinary advertisement payload data contained within the advertising packet.

---

## Extended Advertising

Extended advertising separates advertising properties from advertisement payload. Advertising Event Properties determine whether the advertising event is:

* Connectable
* Scannable
* Directed
* High Duty Cycle Directed
* Legacy
* Anonymous
* Include Tx Power

The discoverability flags remain ordinary advertisement payload data exactly as they do for legacy advertising.

Therefore:
* **Advertising Event Properties** determine whether connections are permitted.
* **Flags AD structure** declares General or Limited Discoverable Mode.

These are independent.

---

## Practical Examples

| Connectable | Discoverable Flags | Meaning |
| :--- | :--- | :--- |
| **Yes** | General | Device advertises itself as generally discoverable and accepts connections. |
| **Yes** | Limited | Device advertises itself as temporarily discoverable and accepts connections. |
| **Yes** | None | Device accepts connections but does not declare a discoverability mode. This is valid but uncommon. |
| **No** | General | Device advertises itself as generally discoverable but does not accept connections (for example, some broadcast services). |
| **No** | Limited | Device temporarily advertises itself as discoverable without accepting connections. Valid, though uncommon. |
| **No** | None | Broadcast-only advertising with no declared discoverability mode. Typical examples include telemetry broadcasters, manufacturer beacons, periodic advertisers, or other non-connectable broadcast applications. |

---

## NimBLE-Arduino Mapping

### Legacy API
* **`setConnectableMode(...)`**: Selects the appropriate legacy advertising PDU type. Controls Connectable and Non-connectable modes.
* **`setDiscoverableMode(...)`**: Controls the discoverability flags placed into the advertising payload (General, Limited, or None).

### Extended API
* **`setConnectable(bool)`**: Sets the Connectable Advertising Event Property.
* **`setFlags(...)`**: Sets the Flags AD structure placed into the advertising payload. 
  * *Example*: `BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP`

---

## Relationship

The two concepts are orthogonal.

```text
Advertising Event Properties
	│
	├── Connectable?
	├── Scannable?
	├── Directed?
	└── ...

Advertisement Payload
	│
	├── Flags AD Structure
	│      ├── General Discoverable
	│      ├── Limited Discoverable
	│      └── BR/EDR Not Supported
	│
	├── Local Name
	├── Service UUIDs
	├── Manufacturer Data
	└── ...
```

* **Advertising Event Properties** determine how the controller behaves during advertising.
* **The advertisement payload** describes the advertiser to scanners.

---

## Implementation Choice (Limited Discoverable Timeout)

### Bluetooth Core Specification
* Vol. 3, Part C (GAP)
https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host/generic-access-profile.html

for peripheral/beacon:
TGAP(adv_fast_­period) 30 s
Minimum time to perform advertising when user initiated
Recommended value

TGAP(lim_adv_­timeout) 180s Maximum time to remain advertising when in the limited discoverable mode

The Bluetooth specification requires that a device not remain in Limited Discoverable Mode beyond this timeout. How this is achieved is implementation-specific. Typical implementations include:

for a central/observer:
TGAP(lim_disc_­scan_­min_­­coded) 30.72 s 
Minimum time to perform scanning when performing the limited discovery procedure on the LE Coded PHY
Recommended value

TGAP(lim_disc_­scan_­min) 10.24 s
Minimum time to perform scanning when performing the limited discovery procedure on the LE 1M PHY
Recommended value

TGAP(scan_fast_­period) 30.72 s
Minimum time to perform scanning when user initiated
Recommended value

TGAP(conn_param_­timeout) 30 s
Connection parameter update notification timer when performing the connection parameter update procedure
Recommended value

* Stopping advertising.


### NimBLE-Arduino Implementation
This library implements Limited Discoverable Mode by starting advertising with a finite advertising duration (e.g., `start(duration)`), causing advertising to stop automatically when the timeout expires. This is an implementation choice consistent with the GAP intent, rather than a requirement that advertising itself must stop.

---

## GATT Fixture Provenance

The WROVER and C3 images used for cross-platform Central physical acceptance must deploy the consolidated fixture service `12345678-1234-5678-1234-56789abcdef0` with:

* read/write characteristic `...def1`;
* read/write/notify characteristic `...def2`;
* read/write/indicate characteristic `...def3`; and
* a read/write descriptor `...def4` on `...def2`.

An older deployed WROVER image that exposes only `...def1` can connect normally, but cannot validate the notification characteristic or descriptor topology. Refresh the deployed fixture image and retain its build/upload provenance before relying on a Central acceptance result; this record does not claim validation of any particular deployment.
