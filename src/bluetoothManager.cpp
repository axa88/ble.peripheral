// BluetoothManager.cpp
#include "BluetoothManager.h"
#include "fixtureGatt.h"
#include <Arduino.h>
#include "pairingCoordinator.h"
#include "bleValidation.h"
#include <functional>
#include <utility>
#include <mutex>
#include <cmath>
#include <algorithm>
#include <climits>
#include <cstdint>
#include <freertos/FreeRTOS.h>

#ifdef USING_NIMBLE_ARDUINO_HEADERS
# include "nimble/nimble/host/include/host/ble_gap.h"
#else
# include "host/ble_gap.h"
#endif

namespace
{
	using LockGuard = std::lock_guard<std::mutex>;
	using ConnectionHandlers = std::vector<BluetoothManager::ConnectionHandler>;
	constexpr uint8_t supportedAuthenticationMask = BLE_SM_PAIR_AUTHREQ_BOND | BLE_SM_PAIR_AUTHREQ_MITM | BLE_SM_PAIR_AUTHREQ_SC;
	constexpr uint8_t supportedEncryptionMask = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_SIGN;

	std::array<ConnectionHandlers, static_cast<size_t>(BluetoothManager::Event::Count)> subscriptions_;
	std::array<std::mutex, static_cast<size_t>(BluetoothManager::Event::Count)> subscriptionMutex_;

	void notify(BluetoothManager::Event event, const NimBLEConnInfo& info, std::string_view tag = "BLE Event")
	{
		if (event >= BluetoothManager::Event::Count)
			return;
		auto idx = static_cast<size_t>(event);

		ConnectionHandlers copy;
		{
			LockGuard lk(subscriptionMutex_[idx]);
			copy = subscriptions_[idx];
		}

		// invoke outside of lock
		for (const auto& h : copy)
		{
			if (!h)
				continue;
			try { std::invoke(h, info); }
			catch (const std::exception& e) { Serial.printf("%s subscriber threw: %s\n", tag.data(), e.what()); }
			catch (...) { Serial.printf("%s subscriber threw unknown exception type\n", tag.data()); }
		}
	}
} // anonymous namespace


#if CONFIG_BT_NIMBLE_EXT_ADV
class BluetoothManager::AdvertisingCallbacks : public NimBLEExtAdvertisingCallbacks
{
public:
	explicit AdvertisingCallbacks(BluetoothManager& mgr) : mgr_(mgr) {}

	void onStopped(NimBLEExtAdvertising* pAdv, int reason, uint8_t instId) override
	{
		Advertising::OnAdvertisingStopped(instId, reason);
		Serial.printf("[BT] Advertising stopped (instance %u), reason:0x%x %s\n", instId, reason, NimBLEUtils::returnCodeToString(reason));
		switch (reason) // GAP completion reasons include connection, host preemption, timeout, and solicited stop.
		{
			case 0: Serial.println("[BT] Client connecting"); return;
			case BLE_HS_EPREEMPTED: Serial.println("[BT] Advertising preempted by host"); break;
			case BLE_HS_ETIMEOUT: Serial.println("[BT] Advertising timed out"); break;
			case BLE_HS_EDONE: Serial.println("[BT] Advertising stopped (solicited)"); break;
			default: break;
		}
	}

	void onScanRequest(NimBLEExtAdvertising* pAdv, uint8_t instId, NimBLEAddress addr) override
	{
		Serial.printf("[BT] Scan request: instance %u, address: %s\n", instId, addr.toString().c_str());
	}
private:
	BluetoothManager& mgr_;
};
#endif

class BluetoothManager::ServerCallbacks : public NimBLEServerCallbacks
{
public:
	explicit ServerCallbacks(BluetoothManager& mgr) : mgr_(mgr) {}

	void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override
	{
		mgr_.Connected(connInfo);
		Advertising::OnConnectionEstablished();
		Serial.println("[BT] Client connected");
		notify(BluetoothManager::Event::Connect, connInfo);
	}

	void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override
	{
		mgr_.Disconnected(connInfo);
		Serial.printf("[BT] Client disconnected, reason:0x%x %s\n", reason, NimBLEUtils::returnCodeToString(reason));
		notify(BluetoothManager::Event::Disconnect, connInfo);
	}

	void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) override
	{
		Serial.printf("[BT] MTU changed: %u\n", MTU);
	}

	uint32_t onPassKeyDisplay() override
	{
		auto pk = esp_random() % 1000000;
		PairingCoordinator::publish({}, Pairing::Type::Display, pk);
		return pk;
	}

	void onPassKeyEntry(NimBLEConnInfo& connInfo) override
	{
		auto id = mgr_.ConnectionIdentity(connInfo.getConnHandle());
		if (!id || !PairingCoordinator::publish(*id, Pairing::Type::PasskeyEntry))
			mgr_.server_->disconnect(connInfo.getConnHandle());
	}

	void onConfirmPassKey(NimBLEConnInfo& connInfo, uint32_t pin) override
	{
		auto id = mgr_.ConnectionIdentity(connInfo.getConnHandle());
		if (!id || !PairingCoordinator::publish(*id, Pairing::Type::NumericConfirmation, pin))
			NimBLEDevice::injectConfirmPasskey(connInfo, false);
	}

	void onAuthenticationComplete(NimBLEConnInfo& connInfo) override
	{
		if (auto id = mgr_.ConnectionIdentity(connInfo.getConnHandle()))
			PairingCoordinator::invalidate(*id, true);
		Serial.println("[PAIR] Authentication complete");
		notify(BluetoothManager::Event::AuthComplete, connInfo);
	}

	void onPhyUpdate(NimBLEConnInfo& connInfo, uint8_t txPhy, uint8_t rxPhy) override
	{
		Serial.printf("[BT] PHY updated for %s — tx:%u rx:%u\n", connInfo.getAddress().toString().c_str(), static_cast<unsigned>(txPhy), static_cast<unsigned>(rxPhy));
	}

private:
	BluetoothManager& mgr_;
}; // BluetoothManager::ServerCallbacks


BluetoothManager& BluetoothManager::Instance() noexcept
{
	static BluetoothManager instance;
	return instance;
}

BluetoothManager::BluetoothManager() = default;

void BluetoothManager::Connected(const NimBLEConnInfo& info)
{
	std::lock_guard<std::recursive_mutex> lock(connectionMutex_);
	if (connectionSequence_ == UINT32_MAX) { server_->disconnect(info.getConnHandle()); return; }
	for (auto& entry : connections_)
	{
		if (!entry.id)
		{
			entry = {{bootId_, ++connectionSequence_}, info.getConnHandle(), info.getAddress()};
			return;
		}
	}
	server_->disconnect(info.getConnHandle());
}

void BluetoothManager::Disconnected(const NimBLEConnInfo& info)
{
	ConnectionId id;
	{
		std::lock_guard<std::recursive_mutex> lock(connectionMutex_);
		for (auto& entry : connections_)
			if (entry.id && entry.handle == info.getConnHandle() && entry.peer == info.getAddress())
			{
				id = entry.id;
				entry = {};
				break;
			}
	}
	PairingCoordinator::invalidate(id);
}

std::optional<ConnectionId> BluetoothManager::ConnectionIdentity(uint16_t handle)
{
	std::lock_guard<std::recursive_mutex> lock(connectionMutex_);
	for (const auto& entry : connections_)
		if (entry.id && entry.handle == handle) return entry.id;
	return std::nullopt;
}

std::optional<NimBLEConnInfo> BluetoothManager::ResolveConnection(ConnectionId id)
{
	if (!server_ || !id || id.boot != bootId_) return std::nullopt;
	for (const auto& entry : connections_)
		if (entry.id == id)
		{
			auto info = server_->getPeerInfoByHandle(entry.handle);
			if (info.getConnHandle() == entry.handle && info.getAddress() == entry.peer) return info;
			break;
		}
	return std::nullopt;
}

bool BluetoothManager::IsCurrentConnectionLocked(ConnectionId id, uint16_t handle, const NimBLEAddress& peer)
{
	if (!server_ || !id || id.boot != bootId_)
		return false;
	for (const auto& entry : connections_)
	{
		if (entry.id != id)
			continue;
		if (entry.handle != handle || !(entry.peer == peer))
			return false;
		const auto info = server_->getPeerInfoByHandle(handle);
		return info.getConnHandle() == handle && info.getAddress() == peer;
	}
	return false;
}

bool BluetoothManager::InjectPasskey(ConnectionId connection, uint32_t passkey)
{
	std::lock_guard<std::recursive_mutex> lock(connectionMutex_);
	auto info = ResolveConnection(connection);
	return info && passkey <= 999999 && NimBLEDevice::injectPassKey(*info, passkey);
}

bool BluetoothManager::InjectConfirmation(ConnectionId connection, bool accepted)
{
	std::lock_guard<std::recursive_mutex> lock(connectionMutex_);
	auto info = ResolveConnection(connection);
	return info && NimBLEDevice::injectConfirmPasskey(*info, accepted);
}

bool BluetoothManager::Disconnect(ConnectionId connection)
{
	std::lock_guard<std::recursive_mutex> lock(connectionMutex_);
	auto info = ResolveConnection(connection);
	return info && server_->disconnect(info->getConnHandle());
}

std::vector<BluetoothManager::ConnectedPeerSnapshot> BluetoothManager::GetConnectedPeerSnapshots()
{
	std::vector<ConnectedPeerSnapshot> snapshots;
	std::lock_guard<std::recursive_mutex> lock(connectionMutex_);
	CollectConnectedPeerSnapshots(snapshots);
	return snapshots;
}

bool BluetoothManager::TryGetConnectedPeerSnapshots(std::vector<ConnectedPeerSnapshot>& snapshots)
{
	snapshots.clear();
	std::unique_lock<std::recursive_mutex> lock(connectionMutex_, std::try_to_lock);
	if (!lock.owns_lock())
		return false;
	CollectConnectedPeerSnapshots(snapshots);
	return true;
}


void BluetoothManager::CollectConnectedPeerSnapshots(std::vector<ConnectedPeerSnapshot>& snapshots)
{
	if (!server_)
		return;
	snapshots.reserve(connections_.size());
	for (const auto& entry : connections_)
	{
		if (!entry.id)
			continue;
		auto info = ResolveConnection(entry.id);
		if (!info)
			continue;
		snapshots.push_back({
			entry.id,
			entry.handle,
			info->getAddress().toString(),
			info->getIdAddress().toString(),
			static_cast<double>(info->getConnInterval()) * 1.25,
			static_cast<uint32_t>(info->getConnTimeout()) * 10U,
			info->getConnLatency(),
			info->getMTU(),
			info->isBonded(),
			info->isEncrypted(),
			info->isAuthenticated(),
			info->getSecKeySize(),
			info->getIdAddress().getType()
		});
	}
}

bool BluetoothManager::ConnectionExists(ConnectionId connection)
{
	std::lock_guard<std::recursive_mutex> lock(connectionMutex_);
	return ResolveConnection(connection).has_value();
}

bool BluetoothManager::ValidConnectionParameters(uint32_t minIntervalMs, uint32_t maxIntervalMs, uint16_t latency, uint32_t supervisionTimeoutMs)
{
	return BleValidation::connectionParameters(minIntervalMs, maxIntervalMs, latency, supervisionTimeoutMs).has_value();
}

BluetoothManager::Outcome BluetoothManager::SetPreferredMtu(uint16_t mtu)
{
	if (mtu < 23 || mtu > 517)
		return Outcome::InvalidArgument;
	return NimBLEDevice::setMTU(mtu) ? Outcome::Applied : Outcome::Failed;
}

BluetoothManager::Outcome BluetoothManager::RequestConnectionParams(ConnectionId connection, uint32_t minIntervalMs, uint32_t maxIntervalMs, uint16_t latency, uint32_t supervisionTimeoutMs)
{
	const auto parameters = BleValidation::connectionParameters(minIntervalMs, maxIntervalMs, latency, supervisionTimeoutMs);
	if (!parameters)
		return Outcome::InvalidArgument;

	std::lock_guard<std::recursive_mutex> lock(connectionMutex_);
	if (!server_)
		return Outcome::Unsupported;
	auto info = ResolveConnection(connection);
	if (!info)
		return Outcome::InvalidArgument;

	server_->updateConnParams(info->getConnHandle(), parameters->minIntervalUnits, parameters->maxIntervalUnits,
		parameters->latency, parameters->supervisionTimeoutUnits);
	return Outcome::Dispatched;
}

BluetoothManager::Outcome BluetoothManager::RequestDataLength(ConnectionId connection, uint16_t octets)
{
	if (octets < 27 || octets > 251)
		return Outcome::InvalidArgument;
	std::lock_guard<std::recursive_mutex> lock(connectionMutex_);
	if (!server_)
		return Outcome::Unsupported;
	auto info = ResolveConnection(connection);
	if (!info)
		return Outcome::InvalidArgument;
	server_->setDataLen(info->getConnHandle(), octets);
	return Outcome::Dispatched;
}

BluetoothManager::Outcome BluetoothManager::RequestPhy(ConnectionId connection, PhyUpdate params)
{
	std::lock_guard<std::recursive_mutex> lock(connectionMutex_);
	if (!server_)
		return Outcome::Unsupported;
	auto info = ResolveConnection(connection);
	if (!info)
		return Outcome::InvalidArgument;
	return server_->updatePhy(info->getConnHandle(), params.txPhysMask, params.rxPhysMask, params.phyOptions) ? Outcome::Accepted : Outcome::Failed;
}

BluetoothManager::PeerLinkResult BluetoothManager::ReadPeerLink(ConnectionId connection)
{
	uint16_t handle = BLE_HS_CONN_HANDLE_NONE;
	NimBLEAddress peer;
	{
		std::lock_guard<std::recursive_mutex> lock(connectionMutex_);
		if (!server_)
			return { Outcome::Unsupported, std::nullopt, std::nullopt };
		auto info = ResolveConnection(connection);
		if (!info)
			return { Outcome::InvalidArgument, std::nullopt, std::nullopt };
		handle = info->getConnHandle();
		peer = info->getAddress();
	}

	const int8_t rssi = GetPeerRssi(handle);
	{
		std::lock_guard<std::recursive_mutex> lock(connectionMutex_);
		if (!IsCurrentConnectionLocked(connection, handle, peer))
			return { Outcome::Failed, std::nullopt, std::nullopt };
	}

	const auto phy = Phy(handle);
	{
		std::lock_guard<std::recursive_mutex> lock(connectionMutex_);
		if (!IsCurrentConnectionLocked(connection, handle, peer))
			return { Outcome::Failed, std::nullopt, std::nullopt };
	}

	PeerLinkResult result{ Outcome::Failed, std::nullopt, std::nullopt };
	if (rssi != INT8_MIN)
		result.rssiDbm = rssi;
	if (phy.has_value())
		result.phy = phy;
	if (result.rssiDbm.has_value() && result.phy.has_value())
		result.outcome = Outcome::Applied;
	return result;
}

BluetoothManager::Outcome BluetoothManager::DisconnectAll()
{
	std::lock_guard<std::recursive_mutex> lock(connectionMutex_);
	if (!server_)
	{
		return Outcome::Unsupported;
	}
	unsigned requested = 0;
	unsigned failed = 0;
	for (const auto& entry : connections_)
	{
		if (!entry.id)
			continue;
		auto info = ResolveConnection(entry.id);
		if (!info)
			continue;
		++requested;
		if (!server_->disconnect(info->getConnHandle()))
			++failed;
	}
	if (requested == 0)
		return Outcome::Applied;
	return failed == 0 ? Outcome::Accepted : Outcome::Failed;
}

NimBLEServer* BluetoothManager::Server() const noexcept { return server_; }

uint8_t BluetoothManager::Capabilities(std::optional<uint8_t> capabilities) noexcept
{
	std::lock_guard<std::mutex> lock(securityMutex_);
#if !BLE_ENABLE_SERIAL_CONSOLE && !BLE_ENABLE_WEB_INTERFACE
	capabilities = BLE_HS_IO_NO_INPUT_OUTPUT;
#endif
	if (capabilities.has_value())
	{
		capabilities_.store(*capabilities, std::memory_order_relaxed);
		NimBLEDevice::setSecurityIOCap(*capabilities);
	}
	return capabilities_.load(std::memory_order_relaxed);
}

uint8_t BluetoothManager::Authentication(std::optional<uint8_t> authentication) noexcept
{
	std::lock_guard<std::mutex> lock(securityMutex_);
	if (authentication.has_value())
	{
		const uint8_t value = static_cast<uint8_t>(*authentication & supportedAuthenticationMask);
		authentication_.store(value, std::memory_order_relaxed);
		NimBLEDevice::setSecurityAuth(value);
	}
	return authentication_.load(std::memory_order_relaxed);
}

uint8_t BluetoothManager::Encryption(std::optional<uint8_t> encryption) noexcept
{
	std::lock_guard<std::mutex> lock(securityMutex_);
	if (encryption.has_value())
	{
		const uint8_t value = static_cast<uint8_t>(*encryption & supportedEncryptionMask);
		encryption_.store(value, std::memory_order_relaxed);
		NimBLEDevice::setSecurityRespKey(value);
		NimBLEDevice::setSecurityInitKey(value);
	}
	return encryption_.load(std::memory_order_relaxed);
}

bool BluetoothManager::SetAuthenticationFlag(uint8_t mask, bool enabled)
{
	if (mask == 0 || (mask & supportedAuthenticationMask) != mask)
		return false;
	std::lock_guard<std::mutex> lock(securityMutex_);
	const uint8_t current = authentication_.load(std::memory_order_relaxed);
	const uint8_t value = enabled ? static_cast<uint8_t>(current | mask) : static_cast<uint8_t>(current & ~mask);
	authentication_.store(value, std::memory_order_relaxed);
	NimBLEDevice::setSecurityAuth(value);
	return true;
}

bool BluetoothManager::SetKeyDistributionFlag(uint8_t mask, bool enabled)
{
	if (mask == 0 || (mask & supportedEncryptionMask) != mask)
		return false;
	std::lock_guard<std::mutex> lock(securityMutex_);
	const uint8_t current = encryption_.load(std::memory_order_relaxed);
	const uint8_t value = enabled ? static_cast<uint8_t>(current | mask) : static_cast<uint8_t>(current & ~mask);
	encryption_.store(value, std::memory_order_relaxed);
	NimBLEDevice::setSecurityRespKey(value);
	NimBLEDevice::setSecurityInitKey(value);
	return true;
}

uint8_t BluetoothManager::ToggleAuthenticationFlag(uint8_t mask)
{
	if (mask == 0 || (mask & supportedAuthenticationMask) != mask)
		return Authentication();
	std::lock_guard<std::mutex> lock(securityMutex_);
	const uint8_t value = static_cast<uint8_t>(authentication_.load(std::memory_order_relaxed) ^ mask);
	authentication_.store(value, std::memory_order_relaxed);
	NimBLEDevice::setSecurityAuth(value);
	return value;
}

uint8_t BluetoothManager::ToggleKeyDistributionFlag(uint8_t mask)
{
	if (mask == 0 || (mask & supportedEncryptionMask) != mask)
		return Encryption();
	std::lock_guard<std::mutex> lock(securityMutex_);
	const uint8_t value = static_cast<uint8_t>(encryption_.load(std::memory_order_relaxed) ^ mask);
	encryption_.store(value, std::memory_order_relaxed);
	NimBLEDevice::setSecurityRespKey(value);
	NimBLEDevice::setSecurityInitKey(value);
	return value;
}

uint8_t BluetoothManager::CycleCapabilities()
{
	std::lock_guard<std::mutex> lock(securityMutex_);
#if !BLE_ENABLE_SERIAL_CONSOLE && !BLE_ENABLE_WEB_INTERFACE
	const uint8_t value = BLE_HS_IO_NO_INPUT_OUTPUT;
#else
	uint8_t value;
	switch (capabilities_.load(std::memory_order_relaxed))
	{
		case BLE_HS_IO_DISPLAY_ONLY: value = BLE_HS_IO_DISPLAY_YESNO; break;
		case BLE_HS_IO_DISPLAY_YESNO: value = BLE_HS_IO_KEYBOARD_ONLY; break;
		case BLE_HS_IO_KEYBOARD_ONLY: value = BLE_HS_IO_NO_INPUT_OUTPUT; break;
		case BLE_HS_IO_NO_INPUT_OUTPUT: value = BLE_HS_IO_KEYBOARD_DISPLAY; break;
		case BLE_HS_IO_KEYBOARD_DISPLAY: value = BLE_HS_IO_DISPLAY_ONLY; break;
		default: value = BLE_HS_IO_DISPLAY_ONLY; break;
	}
#endif
	capabilities_.store(value, std::memory_order_relaxed);
	NimBLEDevice::setSecurityIOCap(value);
	return value;
}

void BluetoothManager::SubscribeToEvent(Event event, ConnectionHandler&& h)
{
	if (event >= Event::Count) return;
	auto idx = static_cast<size_t>(event);
	LockGuard lk(subscriptionMutex_[idx]);
	subscriptions_[idx].reserve(subscriptions_[idx].size() + 1);
	subscriptions_[idx].push_back(std::move(h));
}

bool BluetoothManager::AdvertisingState(uint8_t instance, std::optional<bool> enable)
{
	if (!enable.has_value())
		return Advertising::IsActive(instance);

	bool desired = *enable;
	bool active = Advertising::IsActive(instance);
	Advertising::CancelRestartAfterConnection(instance);
	if (active == desired)
		return desired;

	return desired ? Advertising::Start(instance) : Advertising::Stop(instance);
}

std::optional<BluetoothManager::PhyResult> BluetoothManager::Phy(uint16_t connHandle, std::optional<PhyUpdate> params) noexcept
{
	if (!server_)
		return std::nullopt;

	if (params.has_value())
	{
		// updatePhy is async — result arrives via onPhyUpdate callback; just fire and check rc
		const auto& p = *params;
		bool ok = server_->updatePhy(connHandle, p.txPhysMask, p.rxPhysMask, p.phyOptions);
		if (!ok)
		{
			Serial.println("[ERR] PHY update request failed (may not be supported on this PHY/peer)");
			return std::nullopt;
		}
		Serial.println("[BT] PHY update requested — result will follow via callback");
		return std::nullopt; // result comes asynchronously
	}

	// Read-only: return current PHY
	uint8_t txPhy = 0, rxPhy = 0;
	if (!server_->getPhy(connHandle, &txPhy, &rxPhy))
	{
		Serial.println("[ERR] PHY read failed");
		return std::nullopt;
	}
	return PhyResult{ txPhy, rxPhy };
}

uint32_t BluetoothManager::AdvertisingInterval(uint8_t instance, std::optional<uint32_t> intervalMs) noexcept
{
	auto cfg = Advertising::GetConfig(instance);
	if (!cfg.has_value())
		return 0;

	if (intervalMs.has_value())
	{
		uint32_t ms = *intervalMs;
		// BLE spec: min 20 ms, max 10240 ms; clamp to a reasonable range
		if (ms < 20)
			ms = 20;
		if (ms > 10240)
			ms = 10240;
		cfg->intervalMs = ms;
		Advertising::ApplyConfig(*cfg);
	}
	return cfg->intervalMs;
}

DiscoverableMode BluetoothManager::AdvertisingDiscoverable(uint8_t instance, std::optional<DiscoverableMode> mode) noexcept
{
	return Advertising::Discoverable(instance, mode);
}

int8_t BluetoothManager::GetPeerRssi(uint16_t connHandle) noexcept
{
	int8_t rssi = 0;
	int rc = ble_gap_conn_rssi(connHandle, &rssi);
	if (rc != 0)
	{
		Serial.printf("[ERR] RSSI read failed, rc=0x%x\n", rc);
		return INT8_MIN;
	}
	return rssi;
}

std::optional<int8_t> BluetoothManager::GetPeerRssi(ConnectionId connection) noexcept
{
	uint16_t handle = BLE_HS_CONN_HANDLE_NONE;
	NimBLEAddress peer;
	{
		std::lock_guard<std::recursive_mutex> lock(connectionMutex_);
		auto info = ResolveConnection(connection);
		if (!info)
			return std::nullopt;
		handle = info->getConnHandle();
		peer = info->getAddress();
	}

	const int8_t rssi = GetPeerRssi(handle);
	{
		std::lock_guard<std::recursive_mutex> lock(connectionMutex_);
		if (!IsCurrentConnectionLocked(connection, handle, peer))
			return std::nullopt;
	}
	if (rssi == INT8_MIN)
		return std::nullopt;
	return rssi;
}

BluetoothManager::BondDeleteResult BluetoothManager::DeleteConnectedPeerBond(ConnectionId connection)
{
	BondDeleteResult result{ Outcome::InvalidArgument, 0, NimBLEDevice::getNumBonds() };
	{
		std::lock_guard<std::recursive_mutex> lock(connectionMutex_);
		if (!server_)
		{
			result.outcome = Outcome::Unsupported;
			return result;
		}
		auto info = ResolveConnection(connection);
		if (!info)
			return result;

		const int before = NimBLEDevice::getNumBonds();
		const bool success = NimBLEDevice::deleteBond(info->getIdAddress());
		result.remaining = NimBLEDevice::getNumBonds();
		result.deleted = before > result.remaining ? before - result.remaining : 0;
		result.outcome = success ? Outcome::Applied : Outcome::Failed;
	}
	const bool reconciled = Advertising::ReconcileDirectedTargets();
	if (!reconciled)
		result.outcome = Outcome::Failed;
	return result;
}

BluetoothManager::BondDeleteResult BluetoothManager::DeleteAllBonds()
{
	const int count = NimBLEDevice::getNumBonds();
	if (count == 0)
	{
		const bool reconciled = Advertising::ReconcileDirectedTargets();
		return { reconciled ? Outcome::Applied : Outcome::Failed, 0, 0 };
	}

	const bool success = NimBLEDevice::deleteAllBonds();
	const int remaining = NimBLEDevice::getNumBonds();
	const int deleted = count > remaining ? count - remaining : 0;
	const bool reconciled = Advertising::ReconcileDirectedTargets();
	return { success && reconciled ? Outcome::Applied : Outcome::Failed, deleted, remaining };
}

void BluetoothManager::SetupBluetooth()
{
	if (initialized_)
		return;

	bootId_ = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
	if (!bootId_) bootId_ = 1;
	if (!PairingCoordinator::begin(bootId_, {
		[](ConnectionId id, uint32_t value) { return Instance().InjectPasskey(id, value); },
		[](ConnectionId id, bool value) { return Instance().InjectConfirmation(id, value); },
		[](ConnectionId id) { return Instance().Disconnect(id); }}))
	{
		Serial.println("[ERR] Pairing deadline executor allocation failed; BLE initialization stopped");
		return;
	}
	initialized_ = true;
	Serial.println("[BT] Initializing NimBLE...");

	// Init with the plain device name, then build the MAC-suffixed name for advertising only.
	// getAddress() is valid after init(); store the NimBLEAddress to keep getVal() pointer alive.
#if !CONFIG_BT_NIMBLE_EXT_ADV
	constexpr const char* mode = "Legacy";
#else
	constexpr const char* mode = "Modern";
#endif

	NimBLEDevice::init(mode);
	NimBLEAddress addr = NimBLEDevice::getAddress();
	const uint8_t* mac = addr.getVal();
	char buf[24];
	snprintf(buf, sizeof(buf), "%s-%02X%02X", mode, mac[1], mac[0]);
	deviceName_ = buf;

	// Configure security
	Capabilities(capabilities_);
	Authentication(authentication_);
	Encryption(encryption_);

	// NimBLEDevice::setCustomGapHandler(evalGapHandler, nullptr);

	// Create server and the deterministic GATT fixture.
	server_ = NimBLEDevice::createServer();
	server_->setCallbacks(new ServerCallbacks(*this));
	(void)FixtureGatt::Create(*server_);
	server_->start();

	// Advertising - instance 0 setup delegated to the Advertising:: module (advertising.h/.cpp),
	// which owns the full instance config and is the single place that rebuilds/applies
	// advertisement data (fixes the previous setInstanceData() partial-replace issue).
	advertising_ = NimBLEDevice::getAdvertising();
#if CONFIG_BT_NIMBLE_EXT_ADV
	advertising_->setCallbacks(new AdvertisingCallbacks(*this));
#else
	// Legacy API has no NimBLEExtAdvertisingCallbacks equivalent; setAdvertisingCompleteCallback
	// does not report a reason code. Connection handling therefore remains in onConnect.
	advertising_->setAdvertisingCompleteCallback([this](NimBLEAdvertising*)
		{
			Serial.println("[BT] Advertising stopped (instance 0)");
		});
#endif

	Advertising::InstanceConfig instance0;
	instance0.id = 0;
	instance0.name = deviceName_;
	instance0.discoverable = DiscoverableMode::General;
	instance0.mode = Advertising::defaultMode();
	instance0.intervalMs = 100;
	instance0.serviceUuid = FixtureGatt::ServiceUuid;
	// Manufacturer-specific data: company ID 0xFFFF (test/internal) + "BeBo" payload
	instance0.manufacturerData = { 0xFF, 0xFF, 'B', 'e', 'B', 'o' };

	Advertising::Setup(instance0);

	if (Advertising::Start(0))
		Serial.printf("[BT] Advertising started as \"%s\"\n", deviceName_.c_str());
	else
		Serial.println("[ERR] Failed to start advertising");
} // SetupBluetooth
