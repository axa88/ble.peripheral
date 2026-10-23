// BluetoothManager.h
#pragma once
#include <NimBLEDevice.h>
#include "advertising.h"
#include <functional>
#include <vector>
#include <array>
#include <atomic>
#include <optional>
#include <string>
#include <mutex>
#include "bleConfig.h"
#include "connectionId.h"

class BluetoothManager
{
public:
	using ConnectionHandler = std::function<void(const NimBLEConnInfo&)>;
	enum class Event { Connect, Disconnect, AuthComplete, Count };

	struct PhyUpdate { uint8_t txPhysMask; uint8_t rxPhysMask; uint16_t phyOptions; };
	struct PhyResult { uint8_t txPhy; uint8_t rxPhy; };
	enum class Outcome { Applied, Accepted, Dispatched, InvalidArgument, Unsupported, Failed };

	struct ConnectedPeerSnapshot
	{
		ConnectionId connectionId;
		uint16_t handle;
		std::string address;
		std::string identityAddress;
		double intervalMs;
		uint32_t timeoutMs;
		uint16_t latency;
		uint16_t mtu;
		bool bonded;
		bool encrypted;
		bool authenticated;
		uint8_t keySize;
		uint8_t identityAddressType;
	};

	struct PeerLinkResult
	{
		Outcome outcome;
		std::optional<int8_t> rssiDbm;
		std::optional<PhyResult> phy;
	};

	struct BondDeleteResult
	{
		Outcome outcome;
		int deleted;
		int remaining;
	};

	// Public API
	static BluetoothManager& Instance() noexcept;

	void SetupBluetooth();
	void SubscribeToEvent(Event event, ConnectionHandler&& h);

	uint8_t Capabilities(std::optional<uint8_t> capabilities = std::nullopt) noexcept;
	uint8_t Authentication(std::optional<uint8_t> authentication = std::nullopt) noexcept;
	uint8_t Encryption(std::optional<uint8_t> encryption = std::nullopt) noexcept;
	bool SetAuthenticationFlag(uint8_t mask, bool enabled);
	bool SetKeyDistributionFlag(uint8_t mask, bool enabled);
	uint8_t ToggleAuthenticationFlag(uint8_t mask);
	uint8_t ToggleKeyDistributionFlag(uint8_t mask);
	uint8_t CycleCapabilities();

	// Advertising instance management now delegates to the Advertising:: module (advertising.h/.cpp).
	// These wrappers exist for callers (processMenu.cpp) that don't need the full InstanceConfig API.
	bool AdvertisingState(uint8_t instance, std::optional<bool> enable = std::nullopt);
	uint32_t AdvertisingInterval(uint8_t instance, std::optional<uint32_t> intervalMs = std::nullopt) noexcept;
	DiscoverableMode AdvertisingDiscoverable(uint8_t instance, std::optional<DiscoverableMode> mode = std::nullopt) noexcept;
	const std::string& AdvertisingName() const noexcept { return deviceName_; }

	std::optional<PhyResult> Phy(uint16_t connHandle, std::optional<PhyUpdate> params = std::nullopt) noexcept;
	int8_t GetPeerRssi(uint16_t connHandle) noexcept;
	std::optional<int8_t> GetPeerRssi(ConnectionId connection) noexcept;
	std::vector<ConnectedPeerSnapshot> GetConnectedPeerSnapshots();
	bool TryGetConnectedPeerSnapshots(std::vector<ConnectedPeerSnapshot>& snapshots);
	bool ConnectionExists(ConnectionId connection);
	static bool ValidConnectionParameters(uint32_t minIntervalMs, uint32_t maxIntervalMs, uint16_t latency, uint32_t supervisionTimeoutMs);
	Outcome SetPreferredMtu(uint16_t mtu);
	Outcome RequestConnectionParams(ConnectionId connection, uint32_t minIntervalMs, uint32_t maxIntervalMs, uint16_t latency, uint32_t supervisionTimeoutMs);
	Outcome RequestDataLength(ConnectionId connection, uint16_t octets);
	Outcome RequestPhy(ConnectionId connection, PhyUpdate params);
	PeerLinkResult ReadPeerLink(ConnectionId connection);
	Outcome DisconnectAll();
	BondDeleteResult DeleteConnectedPeerBond(ConnectionId connection);
	BondDeleteResult DeleteAllBonds();
	std::optional<ConnectionId> ConnectionIdentity(uint16_t handle);
	bool InjectPasskey(ConnectionId connection, uint32_t passkey);
	bool InjectConfirmation(ConnectionId connection, bool accepted);
	bool Disconnect(ConnectionId connection);

	// Expose if needed
	NimBLEServer* Server() const noexcept;

private:
	BluetoothManager();
	~BluetoothManager() = default;

	BluetoothManager(const BluetoothManager&) = delete;
	BluetoothManager& operator=(const BluetoothManager&) = delete;
	BluetoothManager(BluetoothManager&&) = delete;
	BluetoothManager& operator=(BluetoothManager&&) = delete;

	bool initialized_ = false;
	std::string deviceName_;
	NimBLEServer* server_ = nullptr;
	struct LiveConnection
	{
		ConnectionId id;
		uint16_t handle = BLE_HS_CONN_HANDLE_NONE;
		NimBLEAddress peer;
	};
	std::recursive_mutex connectionMutex_;
	std::array<LiveConnection, CONFIG_BT_NIMBLE_MAX_CONNECTIONS> connections_{};
	uint64_t bootId_ = 0;
	uint32_t connectionSequence_ = 0;
	void Connected(const NimBLEConnInfo& info);
	void Disconnected(const NimBLEConnInfo& info);
	std::optional<NimBLEConnInfo> ResolveConnection(ConnectionId id); // connectionMutex_ held
	bool IsCurrentConnectionLocked(ConnectionId id, uint16_t handle, const NimBLEAddress& peer); // connectionMutex_ held
	void CollectConnectedPeerSnapshots(std::vector<ConnectedPeerSnapshot>& snapshots); // connectionMutex_ held

	std::mutex securityMutex_;
	std::atomic<uint8_t> capabilities_{ (BLE_ENABLE_SERIAL_CONSOLE || BLE_ENABLE_WEB_INTERFACE) ? BLE_HS_IO_KEYBOARD_DISPLAY : BLE_HS_IO_NO_INPUT_OUTPUT };
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
};
