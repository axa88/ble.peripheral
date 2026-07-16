// BluetoothManager.h
#pragma once
#include <NimBLEDevice.h>
#include "advertising.h"
#include <mutex>
#include <functional>
#include <vector>
#include <array>
#include <atomic>
#include <optional>
#include <string>

class BluetoothManager
{
public:
	using ConnectionHandler = std::function<void(const NimBLEConnInfo&)>;
	enum class Event { Connect, Disconnect, AuthComplete, Count };

	struct PhyUpdate { uint8_t txPhysMask; uint8_t rxPhysMask; uint16_t phyOptions; };
	struct PhyResult { uint8_t txPhy; uint8_t rxPhy; };

	// Public API
	static BluetoothManager& Instance() noexcept;

	void SetupBluetooth();
	void SubscribeToEvent(Event event, ConnectionHandler&& h);

	uint8_t Capabilities(std::optional<uint8_t> capabilities = std::nullopt) noexcept;
	uint8_t Authentication(std::optional<uint8_t> authentication = std::nullopt) noexcept;
	uint8_t Encryption(std::optional<uint8_t> encryption = std::nullopt) noexcept;

	bool AdvertisingRestart(std::optional<bool> enable = std::nullopt) noexcept;
	// Advertising instance management now delegates to the Advertising:: module (advertising.h/.cpp).
	// These wrappers exist for callers (processMenu.cpp) that don't need the full InstanceConfig API.
	bool AdvertisingState(uint8_t instance, std::optional<bool> enable = std::nullopt);
	uint32_t AdvertisingInterval(uint8_t instance, std::optional<uint32_t> intervalMs = std::nullopt) noexcept;
	DiscoverableMode AdvertisingDiscoverable(uint8_t instance, std::optional<DiscoverableMode> mode = std::nullopt) noexcept;
	bool AdvertisingConnectable(uint8_t instance, std::optional<bool> connectable = std::nullopt) noexcept;
	const std::string& AdvertisingName() const noexcept { return deviceName_; }

	uint16_t GetPeerMtu(uint16_t connHandle) noexcept;
	std::optional<PhyResult> Phy(uint16_t connHandle, std::optional<PhyUpdate> params = std::nullopt) noexcept;
	void UpdateConnectionParams(uint16_t connHandle, std::chrono::milliseconds minIntervalMs, std::chrono::milliseconds maxIntervalMs, uint16_t latency, std::chrono::milliseconds supervisionTimeoutMs);
	void RequestDataLength(uint16_t connHandle, uint16_t octets);
	int8_t GetPeerRssi(uint16_t connHandle) noexcept;
	void DeleteBond(uint16_t connHandle);
	void DeleteAllBonds();

	// Expose if needed
	NimBLEServer* Server() const noexcept;
	NimBLECharacteristic* Characteristic() const noexcept;

private:
	BluetoothManager();
	~BluetoothManager() = default;

	BluetoothManager(const BluetoothManager&) = delete;
	BluetoothManager& operator=(const BluetoothManager&) = delete;
	BluetoothManager(BluetoothManager&&) = delete;
	BluetoothManager& operator=(BluetoothManager&&) = delete;

	bool initialized_ = false;
	std::string deviceName_;
	inline static constexpr char SERVICE_UUID[]        = "12345678-1234-5678-1234-56789abcdef0";
	inline static constexpr char CHARACTERISTIC_UUID[] = "12345678-1234-5678-1234-56789abcdef1";

	NimBLEServer* server_ = nullptr;
	NimBLECharacteristic* characteristic_ = nullptr;

	std::atomic<uint8_t> capabilities_{ BLE_HS_IO_KEYBOARD_DISPLAY };
	std::atomic<uint8_t> authentication_{ BLE_SM_PAIR_AUTHREQ_SC | BLE_SM_PAIR_AUTHREQ_MITM | BLE_SM_PAIR_AUTHREQ_BOND };
	std::atomic<uint8_t> encryption_{ BLE_SM_PAIR_KEY_DIST_SIGN | BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC };
	// Per-instance advertising config (name, discoverable mode, connectable, interval, etc.)
	// now lives in the Advertising:: module - see advertising.h/.cpp.

	// forward declarations
	class ServerCallbacks;
	class AdvertisingCallbacks;

#if !CONFIG_BT_NIMBLE_EXT_ADV
	NimBLEAdvertising* advertising_ = nullptr;
#else
	NimBLEExtAdvertising* advertising_ = nullptr;
#endif
	std::atomic<bool> advertRestarting_{ true };
};
