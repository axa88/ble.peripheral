// configMenuHelp.h
#pragma once
#include <NimBLEDevice.h>
#include <cstdint>
#include <string_view>

namespace ConfigMenuHelp
{
	inline constexpr uint8_t encToggleLTK(uint8_t ltk) noexcept { return static_cast<uint8_t>(ltk ^ BLE_SM_PAIR_KEY_DIST_ENC); }
	inline constexpr uint8_t encToggleIRK(uint8_t irk) noexcept { return static_cast<uint8_t>(irk ^ BLE_SM_PAIR_KEY_DIST_ID); }
	inline constexpr uint8_t encToggleCSRK(uint8_t csrk) noexcept { return static_cast<uint8_t>(csrk ^ BLE_SM_PAIR_KEY_DIST_SIGN); }
	inline constexpr uint8_t encToggleLK(uint8_t dualLinkKey) noexcept { return static_cast<uint8_t>(dualLinkKey ^ BLE_SM_PAIR_KEY_DIST_LINK); }

	inline constexpr std::string_view encToString(uint8_t enc) noexcept
	{
		switch (enc & (BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_SIGN | BLE_SM_PAIR_KEY_DIST_LINK))
		{
			case 0x00: return "✗ LTK / ✗ IRK / ✗ CSRK / ✗ LINK";
			case BLE_SM_PAIR_KEY_DIST_ENC: return "✓ LTK / ✗ IRK / ✗ CSRK / ✗ LINK";
			case BLE_SM_PAIR_KEY_DIST_ID: return "✗ LTK / ✓ IRK / ✗ CSRK / ✗ LINK";
			case (BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID): return "✓ LTK / ✓ IRK / ✗ CSRK / ✗ LINK";
			case BLE_SM_PAIR_KEY_DIST_SIGN: return "✗ LTK / ✗ IRK / ✓ CSRK / ✗ LINK";
			case (BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_SIGN): return "✓ LTK / ✗ IRK / ✓ CSRK / ✗ LINK";
			case (BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_SIGN): return "✗ LTK / ✓ IRK / ✓ CSRK / ✗ LINK";
			case (BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_SIGN): return "✓ LTK / ✓ IRK / ✓ CSRK / ✗ LINK";
			case BLE_SM_PAIR_KEY_DIST_LINK: return "✗ LTK / ✗ IRK / ✗ CSRK / ✓ LINK";
			case (BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_LINK): return "✓ LTK / ✗ IRK / ✗ CSRK / ✓ LINK";
			case (BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_LINK): return "✗ LTK / ✓ IRK / ✗ CSRK / ✓ LINK";
			case (BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_LINK): return "✓ LTK / ✓ IRK / ✗ CSRK / ✓ LINK";
			case (BLE_SM_PAIR_KEY_DIST_SIGN | BLE_SM_PAIR_KEY_DIST_LINK): return "✗ LTK / ✗ IRK / ✓ CSRK / ✓ LINK";
			case (BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_SIGN | BLE_SM_PAIR_KEY_DIST_LINK): return "✓ LTK / ✗ IRK / ✓ CSRK / ✓ LINK";
			case (BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_SIGN | BLE_SM_PAIR_KEY_DIST_LINK): return "✗ LTK / ✓ IRK / ✓ CSRK / ✓ LINK";
			case (BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_SIGN | BLE_SM_PAIR_KEY_DIST_LINK):
				return "✓ LTK / ✓ IRK / ✓ CSRK / ✓ LINK";
			default: return "✗ LTK / ✗ IRK / ✗ CSRK / ✗ LINK";
		}
	}

	inline constexpr uint8_t capIoNext(uint8_t ioCap) noexcept
	{
		switch (ioCap)
		{
			case BLE_HS_IO_DISPLAY_ONLY: return BLE_HS_IO_DISPLAY_YESNO;
			case BLE_HS_IO_DISPLAY_YESNO: return BLE_HS_IO_KEYBOARD_ONLY;
			case BLE_HS_IO_KEYBOARD_ONLY: return BLE_HS_IO_NO_INPUT_OUTPUT;
			case BLE_HS_IO_NO_INPUT_OUTPUT: return BLE_HS_IO_KEYBOARD_DISPLAY;
			case BLE_HS_IO_KEYBOARD_DISPLAY: return BLE_HS_IO_DISPLAY_ONLY;
			default: return BLE_HS_IO_DISPLAY_ONLY;
		}
	}

	inline constexpr std::string_view capIoToString(uint8_t ioCap) noexcept
	{
		switch (ioCap)
		{
			case BLE_HS_IO_NO_INPUT_OUTPUT: return "No input No output (cannot display output or accept input)";
			case BLE_HS_IO_KEYBOARD_ONLY: return "Keyboard input only (can accept passkey from user)";
			case BLE_HS_IO_DISPLAY_ONLY: return "Display output only (can display passkey to user)";
			case BLE_HS_IO_DISPLAY_YESNO: return "Display + Confirmation (can display numeric passkey to user and accept binary input confirmation)";
			case BLE_HS_IO_KEYBOARD_DISPLAY: return "Keyboard + Display (can both display passkey to user and accept passkey from user)";
			default: return "Unknown pairing input/output capability";
		}
	}

	inline constexpr uint8_t authToggleBond(uint8_t bond) noexcept { return static_cast<uint8_t>(bond ^ BLE_SM_PAIR_AUTHREQ_BOND); }
	inline constexpr uint8_t authToggleMitm(uint8_t mitm) noexcept { return static_cast<uint8_t>(mitm ^ BLE_SM_PAIR_AUTHREQ_MITM); }
	inline constexpr uint8_t authToggleSC(uint8_t sc) noexcept { return static_cast<uint8_t>(sc ^ BLE_SM_PAIR_AUTHREQ_SC); }
	inline constexpr uint8_t authToggleKP(uint8_t kp) noexcept { return static_cast<uint8_t>(kp ^ BLE_SM_PAIR_AUTHREQ_KEYPRESS); }

	inline constexpr std::string_view authToString(uint8_t auth) noexcept
	{
		switch (auth & (BLE_SM_PAIR_AUTHREQ_BOND | BLE_SM_PAIR_AUTHREQ_MITM | BLE_SM_PAIR_AUTHREQ_SC | BLE_SM_PAIR_AUTHREQ_KEYPRESS))
		{
			case 0x00: return "✗ Bonding / ✗ MitM / ✗ Sec Conn / ✗ Keypress";
			case BLE_SM_PAIR_AUTHREQ_BOND: return "✓ Bonding / ✗ MitM / ✗ Sec Conn / ✗ Keypress";
			case BLE_SM_PAIR_AUTHREQ_MITM: return "✗ Bonding / ✓ MITM / ✗ Sec Conn / ✗ Keypress";
			case (BLE_SM_PAIR_AUTHREQ_BOND | BLE_SM_PAIR_AUTHREQ_MITM): return "✓ Bonding / ✓ MITM / ✗ Sec Conn / ✗ Keypress";
			case BLE_SM_PAIR_AUTHREQ_SC: return "✗ Bonding / ✗ MitM / ✓ Sec Conn / ✗ Keypress";
			case (BLE_SM_PAIR_AUTHREQ_SC | BLE_SM_PAIR_AUTHREQ_BOND): return "✓ Bonding / ✗ MitM / ✓ Sec Conn / ✗ Keypress";
			case (BLE_SM_PAIR_AUTHREQ_SC | BLE_SM_PAIR_AUTHREQ_MITM): return "✗ Bonding / ✓ MitM / ✓ Sec Conn / ✗ Keypress";
			case (BLE_SM_PAIR_AUTHREQ_SC | BLE_SM_PAIR_AUTHREQ_MITM | BLE_SM_PAIR_AUTHREQ_BOND): return "✓ Bonding / ✓ MitM / ✓ Sec Conn / ✗ Keypress";
			case BLE_SM_PAIR_AUTHREQ_KEYPRESS: return "✗ Bonding / ✗ MitM / ✗ Sec Conn / ✓ Keypress";
			case (BLE_SM_PAIR_AUTHREQ_KEYPRESS | BLE_SM_PAIR_AUTHREQ_BOND): return "✓ Bonding / ✗ MitM / ✗ Sec Conn / ✓ Keypress";
			case (BLE_SM_PAIR_AUTHREQ_KEYPRESS | BLE_SM_PAIR_AUTHREQ_MITM): return "✗ Bonding / ✓ MITM / ✗ Sec Conn / ✓ Keypress";
			case (BLE_SM_PAIR_AUTHREQ_KEYPRESS | BLE_SM_PAIR_AUTHREQ_MITM | BLE_SM_PAIR_AUTHREQ_BOND): return "✓ Bonding / ✓ MITM / ✗ Sec Conn / ✓ Keypress";
			case (BLE_SM_PAIR_AUTHREQ_KEYPRESS | BLE_SM_PAIR_AUTHREQ_SC): return "✗ Bonding / ✗ MitM / ✓ Sec Conn / ✓ Keypress";
			case (BLE_SM_PAIR_AUTHREQ_KEYPRESS | BLE_SM_PAIR_AUTHREQ_SC | BLE_SM_PAIR_AUTHREQ_BOND): return "✓ Bonding / ✗ MitM / ✓ Sec Conn / ✓ Keypress";
			case (BLE_SM_PAIR_AUTHREQ_KEYPRESS | BLE_SM_PAIR_AUTHREQ_SC | BLE_SM_PAIR_AUTHREQ_MITM): return "✗ Bonding / ✓ MitM / ✓ Sec Conn / ✓ Keypress";
			case (BLE_SM_PAIR_AUTHREQ_KEYPRESS | BLE_SM_PAIR_AUTHREQ_SC | BLE_SM_PAIR_AUTHREQ_MITM | BLE_SM_PAIR_AUTHREQ_BOND):
				return "✓ Bonding / ✓ MitM / ✓ Sec Conn / ✓ Keypress";
			default: return "✗ Bonding / ✗ MitM / ✗ Sec Conn / ✗ Keypress";
		}
	}

	// void printConnInfo(const NimBLEConnInfo &connInfo);
	inline void printConnInfo(const NimBLEConnInfo& connInfo)
	{
		Serial.printf("=== NimBLEConnInfo ===\n");
		Serial.printf("OTA Address: %s\n", connInfo.getAddress().toString().c_str());
		Serial.printf("ID Address: %s\n", connInfo.getIdAddress().toString().c_str());
		Serial.printf("Conn Handle: %u\n", connInfo.getConnHandle());
		Serial.printf("Conn Interval: %.2f ms\n", connInfo.getConnInterval() * 1.25f);
		Serial.printf("Supervision Timeout: %u ms\n", connInfo.getConnTimeout() * 10u);
		Serial.printf("Conn Latency: %u\n", connInfo.getConnLatency());
		Serial.printf("MTU: %u\n", connInfo.getMTU());
		Serial.printf("Role: %s\n", (connInfo.isMaster() ? "Master" : (connInfo.isSlave() ? "Slave" : "Unknown")));
		Serial.printf("Bonded: %s\n", connInfo.isBonded() ? "Yes" : "No");
		Serial.printf("Encrypted: %s\n", connInfo.isEncrypted() ? "Yes" : "No");
		Serial.printf("Authenticated: %s\n", connInfo.isAuthenticated() ? "Yes" : "No");
		Serial.printf("Security Key Size: %u\n", connInfo.getSecKeySize());
		Serial.printf("======================\n");
	}
}
