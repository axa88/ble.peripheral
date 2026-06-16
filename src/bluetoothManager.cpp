// BluetoothManager.cpp
#include "BluetoothManager.h"
#include <Arduino.h>
#include "configMenuHelp.h"
#include "processInput.h"
#include "processMenu.h"
#include <functional>
#include <utility>
#include <chrono>
#include <cmath> // for std::lround
#include <limits>

namespace
{
	using LockGuard = std::lock_guard<std::mutex>;
	using ConnectionHandlers = std::vector<BluetoothManager::ConnectionHandler>;

	std::array<ConnectionHandlers, static_cast<size_t>(BluetoothManager::Event::Count)> subscriptions_;
	std::array<std::mutex, static_cast<size_t>(BluetoothManager::Event::Count)> subscriptionMutex_;

	void notify(BluetoothManager::Event event, const NimBLEConnInfo& info, std::string_view tag = "BLE Event")
	{
		if (event >= BluetoothManager::Event::Count) return;
		auto idx = static_cast<size_t>(event);

		ConnectionHandlers copy;
		{
			LockGuard lk(subscriptionMutex_[idx]);
			copy = subscriptions_[idx];
		}

		// invoke outside lock
		for (const auto& h : copy)
		{
			if (!h) continue;
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
		Serial.printf("[BT] Advertising stopped (instance %u), reason:0x%x %s\n", instId, reason, NimBLEUtils::returnCodeToString(reason));
		switch (reason) // seems there are only 2 posibilities, timeout and connect
		{
			case 0: Serial.println("[BT] Client connecting"); return;
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
		Serial.println("[BT] Client connected");
		notify(BluetoothManager::Event::Connect, connInfo);
	}

	void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override
	{
		Serial.printf("[BT] Client disconnected, reason:0x%x %s\n", reason, NimBLEUtils::returnCodeToString(reason));
		notify(BluetoothManager::Event::Disconnect, connInfo);

		if (mgr_.advertising_ && mgr_.advertRestarting_)
		{
		#if !CONFIG_BT_NIMBLE_EXT_ADV
			bool advStarted = mgr_.advertising_->start();
		#else
			bool advStarted = mgr_.advertising_->start(0);
		#endif

			Serial.println(advStarted ? "[BT] Advertising restarted" : "[ERR] Failed to restart advertising");
			if (advStarted)
				Serial.println("[HINT] Waiting for a client to connect. Press Z to see the menu.");
		}
	}

	void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) override
	{
		Serial.printf("[BT] MTU changed: %u\n", MTU);
	}

	uint32_t onPassKeyDisplay() override
	{
		auto pk = esp_random() % 1000000;
		Serial.printf("[PAIR] Passkey for remote device entry: %06u\n", pk);
		return pk;
	}

	void onPassKeyEntry(NimBLEConnInfo& connInfo) override
	{
		ProcessMenu::consoleMode = ConsoleMode::Passkey;
		ProcessInput::passkeyReady = false;
		ProcessInput::passkeyValue = 0;

		Serial.println("[PAIR] ==================================================");
		Serial.println("[PAIR] Passkey required");
		Serial.println("[PAIR] Enter the 6-digit passkey shown on the remote device:");
		Serial.print("[PAIR] > ");

		unsigned long start = millis();
		const unsigned long timeout = 25000;
		while (!ProcessInput::passkeyReady && (millis() - start) < timeout)
			vTaskDelay(pdMS_TO_TICKS(20));

		if (!ProcessInput::passkeyReady)
			Serial.println("[PAIR] Passkey entry timed out");
		else
			Serial.printf("[PAIR] Passkey entered: %06u\n", ProcessInput::passkeyValue.load());

		Serial.println("[PAIR] ==================================================");
		NimBLEDevice::injectPassKey(connInfo, ProcessInput::passkeyValue);
	}

	void onConfirmPassKey(NimBLEConnInfo& connInfo, uint32_t pin) override
	{
		ProcessMenu::consoleMode = ConsoleMode::PinConfirm;
		ProcessInput::confirmReady = ProcessInput::confirmAccept = false;

		Serial.println("[PAIR] ==================================================");
		Serial.printf("[PAIR] Confirm this passkey matches the remote device: %06u\n", pin);
		Serial.print("[PAIR] Accept? (y/n) > ");

		unsigned long start = millis();
		const unsigned long timeout = 25000;
		while (!ProcessInput::confirmReady && (millis() - start) < timeout)
			vTaskDelay(pdMS_TO_TICKS(20));

		if (!ProcessInput::confirmReady)
			Serial.println("[PAIR] Passkey confirmation timed out");
		else
			Serial.printf("[PAIR] Passkey %s\n", ProcessInput::confirmAccept ? "accepted" : "rejected");

		Serial.println("[PAIR] ==================================================");
		NimBLEDevice::injectConfirmPasskey(connInfo, ProcessInput::confirmAccept);
	}

	void onAuthenticationComplete(NimBLEConnInfo& connInfo) override
	{
		Serial.println("[PAIR] Authentication complete");
		notify(BluetoothManager::Event::AuthComplete, connInfo);
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

NimBLEServer* BluetoothManager::Server() const noexcept { return server_; }

NimBLECharacteristic* BluetoothManager::Characteristic() const noexcept { return characteristic_; }

uint8_t BluetoothManager::Capabilities(std::optional<uint8_t> capabilities) noexcept
{
	if (capabilities.has_value())
		NimBLEDevice::setSecurityIOCap(capabilities_ = *capabilities);
	return capabilities_;
}

uint8_t BluetoothManager::Authentication(std::optional<uint8_t> authentication) noexcept
{
	if (authentication.has_value())
		NimBLEDevice::setSecurityAuth(authentication_ = *authentication);
	return authentication_;
}

uint8_t BluetoothManager::Encryption(std::optional<uint8_t> encryption) noexcept
{
	if (encryption.has_value())
	{
		NimBLEDevice::setSecurityRespKey(encryption_ = *encryption);
		NimBLEDevice::setSecurityInitKey(encryption_);
	}
	return encryption_;
}

void BluetoothManager::SubscribeToEvent(Event event, ConnectionHandler&& h)
{
	if (event >= Event::Count) return;
	auto idx = static_cast<size_t>(event);
	LockGuard lk(subscriptionMutex_[idx]);
	subscriptions_[idx].reserve(subscriptions_[idx].size() + 1);
	subscriptions_[idx].push_back(std::move(h));
}

bool BluetoothManager::AdvertisingRestartOnDisconnect(std::optional<bool> enable) noexcept
{
	if (enable.has_value())
		advertRestarting_ = *enable;
	return advertRestarting_;
}

bool BluetoothManager::AdvertisingState(uint8_t instance, std::optional<bool> enable)
{
#if !CONFIG_BT_NIMBLE_EXT_ADV
	if (!advertising_) return false;
	if (enable)
	{
		if (*enable && !advertising_->isAdvertising())
			advertising_->start();
		else if (!*enable && advertising_->isAdvertising())
			advertising_->stop();
	}

	return advertising_->isAdvertising();
#else
	if (!advertising_) return false;
	if (enable.has_value())
	{
		bool desired = *enable;
		if (advertising_->isActive(instance) == desired) return desired;
		if (desired)
			advertising_->start(instance);
		else
			advertising_->stop(instance);
	}
	return advertising_->isActive(instance);
#endif
}

uint16_t BluetoothManager::GetPeerMtu(uint16_t connHandle) noexcept
{
	if (!server_) return 0;
	return server_->getPeerMTU(connHandle);
}

std::optional<BluetoothManager::PhyResult> BluetoothManager::Phy(uint16_t connHandle, std::optional<PhyUpdate> params) noexcept
{
	if (!server_) return std::nullopt;

	if (params.has_value())
	{
		const auto& p = *params;
		bool ok = server_->updatePhy(connHandle, p.txPhysMask, p.rxPhysMask, p.phyOptions);
		if (!ok) return std::nullopt;
	}

	uint8_t txPhy = 0, rxPhy = 0;
	if (!server_->getPhy(connHandle, &txPhy, &rxPhy))
		return std::nullopt;

	return PhyResult{ txPhy, rxPhy };
}

void BluetoothManager::UpdateConnectionParams(uint16_t connHandle, std::chrono::milliseconds minIntervalMs, std::chrono::milliseconds maxIntervalMs, uint16_t latency, std::chrono::milliseconds supervisionTimeoutMs)
{
	if (!server_) return;

	auto toIntervalUnits = [](std::chrono::milliseconds ms) -> uint16_t
		{
			auto msv = ms.count();
			auto units = (static_cast<long long>(msv) * 4 + 2) / 5;
			if (units < 0) units = 0;
			if (units > static_cast<long long>(std::numeric_limits<uint16_t>::max()))
				units = static_cast<long long>(std::numeric_limits<uint16_t>::max());
			return static_cast<uint16_t>(units);
		};

	auto toTimeoutUnits = [](std::chrono::milliseconds ms) -> uint16_t
		{
			auto msv = ms.count();
			auto units = (static_cast<long long>(msv) + 5) / 10;
			if (units < 0) units = 0;
			if (units > static_cast<long long>(std::numeric_limits<uint16_t>::max()))
				units = static_cast<long long>(std::numeric_limits<uint16_t>::max());
			return static_cast<uint16_t>(units);
		};

	auto minUnits = toIntervalUnits(minIntervalMs);
	auto maxUnits = toIntervalUnits(maxIntervalMs);
	auto timeoutUnits = toTimeoutUnits(supervisionTimeoutMs);

	server_->updateConnParams(connHandle, minUnits, maxUnits, latency, timeoutUnits);
}

void BluetoothManager::RequestDataLength(uint16_t connHandle, uint16_t octets)
{
	if (!server_) return;

	if (octets < 0x001B || octets > 0x00FB)
	{
		Serial.printf("[ERR] RequestDataLength out of range: %u\n", octets);
		return;
	}

	server_->setDataLen(connHandle, octets);
}

void BluetoothManager::SetupBluetooth()
{
	if (initialized_) return;
	initialized_ = true;
	Serial.println("[BT] Initializing NimBLE...");

#if !CONFIG_BT_NIMBLE_EXT_ADV
	NimBLEDevice::init("ESP-Wroom");
#else
	NimBLEDevice::init("ESP-C3");
#endif

	// Configure security
	Capabilities(capabilities_);
	Authentication(authentication_);
	Encryption(encryption_);

	// NimBLEDevice::setCustomGapHandler(evalGapHandler, nullptr);

	// Create server/service/characteristics
	server_ = NimBLEDevice::createServer();
	server_->setCallbacks(new ServerCallbacks(*this));
	NimBLEService* service = server_->createService(SERVICE_UUID);
	characteristic_ = service->createCharacteristic(CHARACTERISTIC_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
	characteristic_->setValue("Ima Characteristic");
	server_->start();

	// Advertising
#if !CONFIG_BT_NIMBLE_EXT_ADV
	advertising_ = NimBLEDevice::getAdvertising();
	advertising_->setName("ESP-LegacyAdv"); // complete name
	advertising_->addTxPower();
	advertising_->addServiceUUID(SERVICE_UUID);
	advertising_->enableScanResponse(true);
	if (advertising_->start())
		Serial.println("[BT] Advertising started");
	else
		Serial.println("[ERR] Failed to start advertising");
#else
	uint8_t primaryPhy = BLE_HCI_LE_PHY_1M; /** for advertising, can be one of BLE_HCI_LE_PHY_1M or BLE_HCI_LE_PHY_CODED */
	uint8_t secondaryPhy = BLE_HCI_LE_PHY_1M; /** for advertising/connecting, can be one of BLE_HCI_LE_PHY_1M, BLE_HCI_LE_PHY_2M or BLE_HCI_LE_PHY_CODED */

	// Create an extended advertisement with the instance ID 0 and set the PHY's. Multiple instances can be added as long as the instance ID is incremented.
	NimBLEExtAdvertisement extAdvMent(primaryPhy, secondaryPhy);
	extAdvMent.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
	extAdvMent.setConnectable(true);
	extAdvMent.setName("ExtAdv");
	/** As per Bluetooth specification, extended advertising cannot be both scannable and connectable */
	// extAdvMent.setScannable(false); // The default is false, set here for demonstration.
	/** Extended advertising allows for 251 bytes (minus header bytes ~20) in a single advertisement or up to 1650 if chained */
	// extAdvMent.setServiceData(NimBLEUUID(SERVICE_UUID), std::string("test"));
	/* 	extAdvMent.setServiceData(NimBLEUUID(SERVICE_UUID), std::string("Extended Advertising Demo.\r\n"
																	"Extended advertising allows for "
																	"251 bytes of data in a single advertisement,\r\n"
																	"or up to 1650 bytes with chaining.\r\n"
																	"This example message is 226 bytes long "
																	"and is using CODED_PHY for long range."));
		*/

	advertising_ = NimBLEDevice::getAdvertising();
	advertising_->setCallbacks(new AdvertisingCallbacks(*this));

	// NimBLEExtAdvertising::setInstanceData takes the instance ID and a reference to a `NimBLEExtAdvertisement` object.
	// This sets the data that will be advertised for this instance ID, returns true if successful.
	// Note: It is safe to create the advertisement as a local variable if setInstanceData is called before exiting the code block as the data will be copied.
	if (advertising_->setInstanceData(0, extAdvMent))
	{
		if (advertising_->start(0))
			Serial.println("[BT] Advertising started");
		else
			Serial.println("[ERR] Failed to start advertising");
	}
	else
		Serial.println("[ERR] Failed to register advertisement data");

	Serial.printf("[BT] isAdvertising(): %s\n", advertising_->isAdvertising() ? "true" : "false");
#endif
} // SetupBluetooth
