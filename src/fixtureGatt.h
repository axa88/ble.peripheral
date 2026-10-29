#pragma once

#include <NimBLEDevice.h>
#include <cstdint>
#include "bleConfig.h"
#if BLE_ENABLE_WEB_INTERFACE
#include "connectionId.h"
#include "gattValue.h"
#include <vector>
#include <string>
#endif

namespace FixtureGatt
{
	inline constexpr char ServiceUuid[] = "12345678-1234-5678-1234-56789abcdef0";
	inline constexpr char ExistingCharacteristicUuid[] = "12345678-1234-5678-1234-56789abcdef1";
	inline constexpr char NotificationCharacteristicUuid[] = "12345678-1234-5678-1234-56789abcdef2";
	inline constexpr char IndicationCharacteristicUuid[] = "12345678-1234-5678-1234-56789abcdef3";
	inline constexpr char CustomDescriptorUuid[] = "12345678-1234-5678-1234-56789abcdef4";
	inline constexpr char DualUpdateCharacteristicUuid[] = "12345678-1234-5678-1234-56789abcdef5";

	inline constexpr char ExistingCharacteristicValue[] = "Ima Characteristic";
	inline constexpr char NotificationCharacteristicValue[] = "Notify Characteristic";
	inline constexpr char IndicationCharacteristicValue[] = "Indicate Characteristic";
	inline constexpr char CustomDescriptorValue[] = "Custom Descriptor";
	inline constexpr uint8_t UpdateTrigger = 0x01;
	inline constexpr char NotificationPayload[] = "notify-event";
	inline constexpr char IndicationPayload[] = "indicate-event";
	inline constexpr char DescriptorServiceUuid[] = "12345678-1234-5678-1234-56789abcdf00";
	inline constexpr char TemperatureUuid[] = "12345678-1234-5678-1234-56789abcdf05";
	inline constexpr char WritableDescriptionUuid[] = "12345678-1234-5678-1234-56789abcdf08";
	inline constexpr char UnsignedRangeUuid[] = "12345678-1234-5678-1234-56789abcdf09";
	inline constexpr char UnresolvedRangeUuid[] = "12345678-1234-5678-1234-56789abcdf10";

	NimBLECharacteristic* Create(NimBLEServer& server);
	bool Finalize();
	// Caller owns DynamicGatt entry; refuse to invalidate while a host job is queued/running.
	bool BeginRebuild();
	bool FinalizeAfterRebuild();
	bool Ready();

#if BLE_ENABLE_WEB_INTERFACE
	struct ValueSnapshot
	{
		ConnectionId id{};
		const char* name = nullptr;
		const char* serviceUuid = nullptr;
		const char* characteristicUuid = nullptr;
		uint16_t properties = 0;
		size_t maxLength = 0;
		size_t publicationLimit = 0;
		size_t minimumPeerMtu = 0;
		size_t connectedPeerCount = 0;
		std::vector<uint8_t> current;
	};
	enum class Outcome { Ready, Dispatched, NotReady, NotFound, InvalidArgument, Busy, Failed };
	struct WebResult
	{
		Outcome outcome = Outcome::Failed;
		std::string detail = "GATT operation failed.";
		bool storageVerified = false;
		bool publicationAttempted = false;
		bool publicationDispatched = false;
		std::vector<ValueSnapshot> values{};
	};
	// Called only from HTTP/worker tasks. Value access is marshalled to the BLE host.
	WebResult GetValues();
	WebResult ChangeValue(ConnectionId id, const GattValue::Buffer& bytes, size_t length);
#endif
}
