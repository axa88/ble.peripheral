// BluetoothManager.h
#pragma once
#include <NimBLEDevice.h>
#include <mutex>
#include <functional>
#include <vector>
#include <array>
#include <atomic>
#include <optional>

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

	bool AdvertisingRestartOnDisconnect(std::optional<bool> enable = std::nullopt) noexcept;
	bool AdvertisingState(uint8_t instance, std::optional<bool> enable = std::nullopt);

	uint16_t GetPeerMtu(uint16_t connHandle) noexcept;
	std::optional<PhyResult> Phy(uint16_t connHandle, std::optional<PhyUpdate> params = std::nullopt) noexcept;
	void UpdateConnectionParams(uint16_t connHandle, std::chrono::milliseconds minIntervalMs, std::chrono::milliseconds maxIntervalMs, uint16_t latency, std::chrono::milliseconds supervisionTimeoutMs);
	void RequestDataLength(uint16_t connHandle, uint16_t octets);

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
	inline static constexpr char SERVICE_UUID[] = "0000xxxx-0000-1000-8000-00805f9b34fb";
	inline static constexpr char CHARACTERISTIC_UUID[] = "0000yyyy-0000-1000-8000-00805f9b34fb";

	NimBLEServer* server_ = nullptr;
	NimBLECharacteristic* characteristic_ = nullptr;

	std::atomic<uint8_t> capabilities_{ BLE_HS_IO_KEYBOARD_DISPLAY };
	std::atomic<uint8_t> authentication_{ BLE_SM_PAIR_AUTHREQ_SC | BLE_SM_PAIR_AUTHREQ_MITM | BLE_SM_PAIR_AUTHREQ_BOND };
	std::atomic<uint8_t> encryption_{ BLE_SM_PAIR_KEY_DIST_SIGN | BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC };

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
