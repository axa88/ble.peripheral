#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Arduino.h"
#include "NimBLEDevice.h"

class NimBLEAddress
{
public:
	NimBLEAddress() = default;
	const uint8_t* getVal() const { return value_; }
	bool isNull() const { return false; }

private:
	uint8_t value_[6]{0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
};

class NimBLEAdvertising
{
public:
	template <typename Callback>
	void setAdvertisingCompleteCallback(Callback&&) {}
	bool start();
};

class NimBLEExtAdvertisingCallbacks
{
public:
	virtual ~NimBLEExtAdvertisingCallbacks() = default;
};

struct NimBLEExtAdvertisement {};

class NimBLEExtAdvertising
{
public:
	void setCallbacks(NimBLEExtAdvertisingCallbacks*) {}
	bool setInstanceData(uint8_t instanceId, const NimBLEExtAdvertisement& data);
	bool start(uint8_t instanceId);
};

#include "advertising.h"
#include "connectionId.h"
#include "fixtureGatt.h"

class NimBLEDevice
{
public:
	static void init(const char* mode);
	static NimBLEAddress getAddress();
	static NimBLEServer* createServer();
	static NimBLEServer* getServer();
#if CONFIG_BT_NIMBLE_EXT_ADV
	static NimBLEExtAdvertising* getAdvertising();
#else
	static NimBLEAdvertising* getAdvertising();
#endif
};

class BluetoothManager;

class AdvertisingCallbacks final : public NimBLEExtAdvertisingCallbacks
{
public:
	explicit AdvertisingCallbacks(BluetoothManager&) {}
};

class ServerCallbacks final : public NimBLEServerCallbacks
{
public:
	explicit ServerCallbacks(BluetoothManager&) {}
};

namespace PairingCoordinator
{
	struct Callbacks
	{
		std::function<bool(ConnectionId, uint32_t)> injectPasskey;
		std::function<bool(ConnectionId, bool)> injectConfirmation;
		std::function<bool(ConnectionId)> disconnect;
	};

	bool begin(uint64_t bootId, Callbacks callbacks);
}

class BluetoothManager
{
public:
	static BluetoothManager& Instance();
	void SetupBluetooth();
	bool InjectPasskey(ConnectionId, uint32_t) { return true; }
	bool InjectConfirmation(ConnectionId, bool) { return true; }
	bool Disconnect(ConnectionId) { return true; }
	uint8_t Capabilities(const std::atomic<uint8_t>& value) { return value.load(); }
	uint8_t Authentication(const std::atomic<uint8_t>& value) { return value.load(); }
	uint8_t Encryption(const std::atomic<uint8_t>& value) { return value.load(); }

	bool initialized_ = false;
	uint64_t bootId_ = 0;
	std::string deviceName_;
	NimBLEServer* server_ = nullptr;
#if CONFIG_BT_NIMBLE_EXT_ADV
	NimBLEExtAdvertising* advertising_ = nullptr;
#else
	NimBLEAdvertising* advertising_ = nullptr;
#endif
	std::atomic<uint8_t> capabilities_{0};
	std::atomic<uint8_t> authentication_{0};
	std::atomic<uint8_t> encryption_{0};
};

namespace StartupTest
{
	struct State
	{
		size_t initCalls = 0;
		size_t createServerCalls = 0;
		size_t pairingBeginCalls = 0;
		size_t startCalls = 0;
		size_t radioStartCalls = 0;
		size_t postReadyRebuildCalls = 0;
		size_t legacyRebuildCalls = 0;
		size_t extendedDataCalls = 0;
		size_t serverStartsAtCreateServer = 0;
		bool configVisibleAtCreateServer = false;
		bool startSawReady = false;
		bool aggregateStableAfterRebuild = false;
		bool hasStartConfig = false;
		std::string initializedMode;
		std::optional<Advertising::InstanceConfig> startConfig;
	};

	State& state();
	void reset();
	bool tryProductionRebuild(const Advertising::InstanceConfig& config);
}

uint32_t esp_random();
