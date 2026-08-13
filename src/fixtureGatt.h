#pragma once

#include <NimBLEDevice.h>
#include <cstdint>

namespace FixtureGatt
{
	inline constexpr char ServiceUuid[] = "12345678-1234-5678-1234-56789abcdef0";
	inline constexpr char ExistingCharacteristicUuid[] = "12345678-1234-5678-1234-56789abcdef1";
	inline constexpr char NotificationCharacteristicUuid[] = "12345678-1234-5678-1234-56789abcdef2";
	inline constexpr char IndicationCharacteristicUuid[] = "12345678-1234-5678-1234-56789abcdef3";
	inline constexpr char CustomDescriptorUuid[] = "12345678-1234-5678-1234-56789abcdef4";

	inline constexpr char ExistingCharacteristicValue[] = "Ima Characteristic";
	inline constexpr char NotificationCharacteristicValue[] = "Notify Characteristic";
	inline constexpr char IndicationCharacteristicValue[] = "Indicate Characteristic";
	inline constexpr char CustomDescriptorValue[] = "Custom Descriptor";
	inline constexpr uint8_t UpdateTrigger = 0x01;
	inline constexpr char NotificationPayload[] = "notify-event";
	inline constexpr char IndicationPayload[] = "indicate-event";

	NimBLECharacteristic* Create(NimBLEServer& server);
}
