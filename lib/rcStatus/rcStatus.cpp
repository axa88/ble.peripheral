// rcStatus.cpp
// Drop into your ESP32 NimBLE project. Call rcDescription(status).

#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif

	/* Declare as weak so linking succeeds if NimBLE host doesn't export it */
	extern const char *ble_hs_err_to_str(int err) __attribute__((weak));

#ifdef __cplusplus
}
#endif

/* -------------------- Condition descriptions --------------------
	Human-readable "Condition" descriptions.
*/

/* ATT (base 0x100) */
static const char att_00[] = "Success";
static const char att_01[] = "Invalid handle";
static const char att_02[] = "Read not permitted";
static const char att_03[] = "Write not permitted";
static const char att_04[] = "Invalid PDU";
static const char att_05[] = "Insufficient authentication";
static const char att_06[] = "Request not supported";
static const char att_07[] = "Invalid offset";
static const char att_08[] = "Insufficient authorization";
static const char att_09[] = "Prepare queue full";
static const char att_0A[] = "Attribute not found";
static const char att_0B[] = "Attribute not long";
static const char att_0C[] = "Insufficient encryption key size";
static const char att_0D[] = "Invalid attribute value length";
static const char att_0E[] = "Unlikely error";
static const char att_0F[] = "Insufficient encryption";
static const char att_10[] = "Unsupported group type";
static const char att_11[] = "Insufficient resources";
static const char att_unknown[] = "ATT: Unknown error";

static const char *const nimble_att_table[] = {att_00, att_01, att_02, att_03, att_04, att_05, att_06, att_07, att_08,
											   att_09, att_0A, att_0B, att_0C, att_0D, att_0E, att_0F, att_10, att_11};

/* HCI (base 0x200) -- documented condition text for common HCI codes */
static const char hci_00[] = "Success";
static const char hci_01[] = "Unknown HCI command";
static const char hci_02[] = "Unknown connection identifier";
static const char hci_03[] = "Hardware failure";
static const char hci_04[] = "Page timeout";
static const char hci_05[] = "Authentication failure";
static const char hci_06[] = "PIN or key missing";
static const char hci_07[] = "Memory capacity exceeded";
static const char hci_08[] = "Connection timeout";
static const char hci_09[] = "Connection limit exceeded";
static const char hci_0A[] = "Synchronous connection limit to a device exceeded";
static const char hci_0B[] = "ACL connection already exists";
static const char hci_0C[] = "Command disallowed";
static const char hci_0D[] = "Connection rejected due to limited resources";
static const char hci_0E[] = "Connection rejected due to security reasons";
static const char hci_0F[] = "Connection rejected due to unacceptable BD_ADDR";
static const char hci_10[] = "Connection accept timeout exceeded";
static const char hci_11[] = "Unsupported feature or parameter value";
static const char hci_12[] = "Invalid HCI command parameters";
static const char hci_13[] = "Remote user terminated connection";
static const char hci_14[] = "Remote device terminated connection due to low resources";
static const char hci_15[] = "Remote device terminated connection due to power off";
static const char hci_16[] = "Local host terminated connection";
static const char hci_17[] = "Repeated attempts";
static const char hci_18[] = "Pairing not allowed";
static const char hci_19[] = "Unknown LMP PDU";
static const char hci_1A[] = "Unsupported remote feature / unsupported LMP feature";
static const char hci_1B[] = "SCO offset rejected";
static const char hci_1C[] = "SCO interval rejected";
static const char hci_1D[] = "SCO air mode rejected";
static const char hci_1E[] = "Invalid LMP parameters / LL parameters";
static const char hci_1F[] = "Unspecified error";
static const char hci_20[] = "Unsupported LMP parameter value / unsupported LL parameter value";
static const char hci_21[] = "Role change not allowed";
static const char hci_22[] = "LMP response timeout / LL response timeout";
static const char hci_23[] = "LMP error transaction collision";
static const char hci_24[] = "LMP PDU not allowed";
static const char hci_25[] = "Encryption mode not acceptable";
static const char hci_26[] = "Link key cannot be changed";
static const char hci_27[] = "Requested QoS not supported";
static const char hci_28[] = "Instant passed";
static const char hci_29[] = "Pairing with unit key not supported";
static const char hci_2A[] = "Different transaction collision";
static const char hci_2B[] = "Reserved";
static const char hci_2C[] = "QoS unacceptable parameter";
static const char hci_2D[] = "QoS rejected";
static const char hci_2E[] = "Channel classification not supported";
static const char hci_2F[] = "Insufficient security";
static const char hci_30[] = "Parameter out of mandatory range";
static const char hci_31[] = "Reserved";
static const char hci_32[] = "Role switch pending";
static const char hci_33[] = "Reserved";
static const char hci_34[] = "Reserved slot violation";
static const char hci_35[] = "Role switch failed";
static const char hci_36[] = "Extended inquiry response too large";
static const char hci_37[] = "Secure Simple Pairing not supported by host";
static const char hci_38[] = "Host busy - pairing";
static const char hci_39[] = "Connection rejected due to no suitable channel";
static const char hci_3A[] = "Controller busy";
static const char hci_3B[] = "Unacceptable connection parameters";
static const char hci_3C[] = "Directed advertising timeout";
static const char hci_3D[] = "Connection terminated due to MIC failure";
static const char hci_3E[] = "Connection establishment failed";
static const char hci_3F[] = "MAC connection failed";
static const char hci_40[] = "Coarse clock adjustment rejected";
static const char hci_unknown[] = "HCI: Unknown error";

static const char *const nimble_hci_table[] = {
	hci_00, hci_01, hci_02, hci_03, hci_04, hci_05, hci_06, hci_07, hci_08, hci_09, hci_0A, hci_0B, hci_0C, hci_0D, hci_0E, hci_0F, hci_10,
	hci_11, hci_12, hci_13, hci_14, hci_15, hci_16, hci_17, hci_18, hci_19, hci_1A, hci_1B, hci_1C, hci_1D, hci_1E, hci_1F, hci_20, hci_21,
	hci_22, hci_23, hci_24, hci_25, hci_26, hci_27, hci_28, hci_29, hci_2A, hci_2B, hci_2C, hci_2D, hci_2E, hci_2F, hci_30, hci_31, hci_32,
	hci_33, hci_34, hci_35, hci_36, hci_37, hci_38, hci_39, hci_3A, hci_3B, hci_3C, hci_3D, hci_3E, hci_3F, hci_40};

/* L2CAP (base 0x300) */
static const char l2c_00[] = "Success";
static const char l2c_01[] = "Signaling: command not understood";
static const char l2c_02[] = "MTU exceeded";
static const char l2c_03[] = "Invalid CID";
static const char l2c_unknown[] = "L2CAP: Unknown error";

static const char *const nimble_l2c_table[] = {l2c_00, l2c_01, l2c_02, l2c_03};

/* SM (us) (base 0x400) */
static const char sm_us_00[] = "Success";
static const char sm_us_01[] = "User input of passkey failed";
static const char sm_us_02[] = "OOB data not available";
static const char sm_us_03[] = "Authentication requirements cannot be met due to IO capabilities";
static const char sm_us_04[] = "Confirm value does not match the calculated compare value";
static const char sm_us_05[] = "Pairing is not supported by the device";
static const char sm_us_06[] = "Resultant encryption key size is insufficient for the security requirements of this device";
static const char sm_us_07[] = "SMP command received is not supported on this device";
static const char sm_us_08[] = "Pairing failed due to an unspecified reason";
static const char sm_us_09[] = "Pairing/auth disallowed: too little time since last pairing/security request";
static const char sm_us_0A[] = "Invalid parameters: command length invalid or parameter out of range";
static const char sm_us_0B[] = "DHKey check value received doesn't match local calculation";
static const char sm_us_0C[] = "Numeric comparison confirm values do not match";
static const char sm_us_0D[] = "Pairing over LE failed due to BR/EDR pairing in process";
static const char sm_us_0E[] = "BR/EDR Link Key cannot be used to derive/distribute keys for LE transport";
static const char sm_us_unknown[] = "SM(us): Unknown error";

static const char *const nimble_sm_us_table[] = {sm_us_00, sm_us_01, sm_us_02, sm_us_03, sm_us_04, sm_us_05, sm_us_06, sm_us_07,
												 sm_us_08, sm_us_09, sm_us_0A, sm_us_0B, sm_us_0C, sm_us_0D, sm_us_0E};

/* SM (peer) (base 0x500) */
static const char sm_peer_00[] = "Success";
static const char sm_peer_01[] = "User input of passkey failed, for example, the user cancelled the operation";
static const char sm_peer_02[] = "The OOB data is not available";
static const char sm_peer_03[] =
	"The pairing procedure cannot be performed as authentication requirements cannot be met due to IO capabilities of one or both devices";
static const char sm_peer_04[] = "The confirm value does not match the calculated compare value";
static const char sm_peer_05[] = "Pairing is not supported by the device";
static const char sm_peer_06[] = "The resultant encryption key size is insufficient for the security requirements of this device";
static const char sm_peer_07[] = "The SMP command received is not supported on this device";
static const char sm_peer_08[] = "Pairing failed due to an unspecified reason";
static const char sm_peer_09[] =
	"Pairing or authentication procedure is disallowed because too little time has elapsed since last pairing request or security request";
static const char sm_peer_0A[] =
	"The Invalid Parameters error code indicates that the command length is invalid or that a parameter is outside of the specified range";
static const char sm_peer_0B[] =
	"Indicates to the remote device that the DHKey Check value received doesn’t match the one calculated by the local device";
static const char sm_peer_0C[] = "Indicates that the confirm values in the numeric comparison protocol do not match";
static const char sm_peer_0D[] =
	"Indicates that the pairing over the LE transport failed due to a Pairing Request sent over the BR/EDR transport in process";
static const char sm_peer_0E[] =
	"Indicates that the BR/EDR Link Key generated on the BR/EDR transport cannot be used to derive and distribute keys for the LE transport";
static const char sm_peer_unknown[] = "SM(peer): Unknown error";

static const char *const nimble_sm_peer_table[] = {sm_peer_00, sm_peer_01, sm_peer_02, sm_peer_03, sm_peer_04, sm_peer_05, sm_peer_06, sm_peer_07,
												   sm_peer_08, sm_peer_09, sm_peer_0A, sm_peer_0B, sm_peer_0C, sm_peer_0D, sm_peer_0E};

/* -------------------- Helpers -------------------- */
static inline const char *table_lookup(const char *const table[], size_t table_len, int idx, const char *unknown)
{
	if (idx >= 0 && (size_t)idx < table_len)
		return table[idx];

	return unknown;
}

/* Host error fallback: short numeric message */
static const char *host_fallback_msg(int code)
{
	static char buf[32];
	snprintf(buf, sizeof(buf), "Host error 0x%02X", code & 0xFF);
	return buf;
}

/* Try NimBLE helper; if not available or returns NULL, use fallback. */
/* If the build does not export ble_hs_err_to_str, remove the call and use host_fallback_msg. */
static const char *nimble_host_description(int code)
{
	const char *s = NULL;

	/* If the symbol is not present, ble_hs_err_to_str will be null (weak),
	   so check before calling. */
	if (ble_hs_err_to_str)
	{
		s = ble_hs_err_to_str(code);
		if (s && s[0] != '\0')
			return s;
	}

	return host_fallback_msg(code);
}

/* Public API: returns the condition description for a NimBLE status value. */
const char *rcDescription(int status)
{
	int base = status & ~0xFF;
	int code = status & 0xFF;

	if (base == 0x0)
		return nimble_host_description(code); /* Host/stack error (no base) */

	switch (base)
	{
		case 0x100: return table_lookup(nimble_att_table, sizeof(nimble_att_table) / sizeof(nimble_att_table[0]), code, att_unknown);
		case 0x200: return table_lookup(nimble_hci_table, sizeof(nimble_hci_table) / sizeof(nimble_hci_table[0]), code, hci_unknown);
		case 0x300: return table_lookup(nimble_l2c_table, sizeof(nimble_l2c_table) / sizeof(nimble_l2c_table[0]), code, l2c_unknown);
		case 0x400: return table_lookup(nimble_sm_us_table, sizeof(nimble_sm_us_table) / sizeof(nimble_sm_us_table[0]), code, sm_us_unknown);
		case 0x500: return table_lookup(nimble_sm_peer_table, sizeof(nimble_sm_peer_table) / sizeof(nimble_sm_peer_table[0]), code, sm_peer_unknown);
		default: return "Unknown base";
	}
}
