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
		Serial.printf("onStopped: Advert %u, reason: %s\n", instId, NimBLEUtils::returnCodeToString(reason));
		switch (reason) // seems there are only 2 posibilities, timeout and connect
		{
			case 0: Serial.println("Client connecting"); return;
			case BLE_HS_ETIMEOUT: Serial.println("Time expired"); break;
			case BLE_HS_EDONE: Serial.println("Solicited stop"); break;
			default: break;
		}
	}

	void onScanRequest(NimBLEExtAdvertising* pAdv, uint8_t instId, NimBLEAddress addr) override
	{
		Serial.printf("onScanRequest: Advert %u, address: %s\n", instId, ConfigMenuHelp::formatAddress(addr));
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
		Serial.println("onConnect");
		notify(BluetoothManager::Event::Connect, connInfo);
	}

	void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override
	{
		Serial.printf("onDisconnect reason: %s\n", NimBLEUtils::returnCodeToString(reason));
		notify(BluetoothManager::Event::Disconnect, connInfo);

		if (mgr_.advertising_ && mgr_.advertRestarting_)
		{
		#if !CONFIG_BT_NIMBLE_EXT_ADV
			bool started = mgr_.advertising_->start();
		#else
			bool started = mgr_.advertising_->start(0);
		#endif

			Serial.printf(started ? "Started advertising\n" : "Failed to start advertising\n");
		}
	}

	void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) override
	{
		Serial.printf("onMTUChange: %u\n", MTU);
		Serial.printf("%s\n", connInfo.toString());
	}

	uint32_t onPassKeyDisplay() override
	{
		Serial.println("onPassKeyDisplay");
		auto pk = esp_random() % 1000000;
		Serial.printf("Displayed passkey for entry on remote device: %06u\n", pk);
		return pk;
	}

	void onPassKeyEntry(NimBLEConnInfo& connInfo) override
	{
		Serial.println("onPassKeyEntry: Enter passkey displayed on remote device here: ");
		ProcessMenu::consoleMode = ConsoleMode::Passkey;
		ProcessInput::passkeyReady = false;
		ProcessInput::passkeyValue = 0;
		unsigned long start = millis();
		const unsigned long timeout = 25000;
		while (!ProcessInput::passkeyReady && (millis() - start) < timeout)
			vTaskDelay(pdMS_TO_TICKS(20));

		if (!ProcessInput::passkeyReady)
			Serial.println("passkey entry: timeout");

		Serial.printf("key entered: %u\n", ProcessInput::passkeyValue.load());
		NimBLEDevice::injectPassKey(connInfo, ProcessInput::passkeyValue);
	}

	void onConfirmPassKey(NimBLEConnInfo& connInfo, uint32_t pin) override
	{
		ProcessMenu::consoleMode = ConsoleMode::PinConfirm;
		ProcessInput::confirmReady = ProcessInput::confirmAccept = false;

		Serial.printf("Confirm passkey: %06u\n", pin);
		Serial.println("onConfirmPassKey: ('y'/'n')?");

		unsigned long start = millis();
		const unsigned long timeout = 25000;
		while (!ProcessInput::confirmReady && (millis() - start) < timeout)
			vTaskDelay(pdMS_TO_TICKS(20));

		if (!ProcessInput::confirmReady)
			Serial.println("passkey confirm: timeout");

		Serial.printf("onConfirmPassKey: %s\n", ProcessInput::confirmAccept ? "Accepted" : "Rejected");
		NimBLEDevice::injectConfirmPasskey(connInfo, ProcessInput::confirmAccept);
	}

	void onAuthenticationComplete(NimBLEConnInfo& connInfo) override
	{
		Serial.println("onAuthenticationComplete");
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

	uint8_t txPhy, rxPhy = 0;
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

	Serial.printf("UpdateConnectionParams (time) -> minUnits=%u maxUnits=%u latency=%u timeoutUnits=%u\n", minUnits, maxUnits, latency, timeoutUnits);
	server_->updateConnParams(connHandle, minUnits, maxUnits, latency, timeoutUnits);
}

void BluetoothManager::RequestDataLength(uint16_t connHandle, uint16_t octets)
{
	if (!server_) return;

	if (octets < 0x001B || octets > 0x00FB)
	{
		Serial.printf("RequestDataLength: out of range: %u\n", octets);
		return;
	}

	server_->setDataLen(connHandle, octets);
}

void BluetoothManager::SetupBluetooth()
{
	if (initialized_) return;
	initialized_ = true;
	Serial.println("Initializing NimBLE...");

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
		Serial.printf("Started advertising\n");
	else
		Serial.printf("Failed to start advertising\n");
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
			Serial.printf("Started advertising\n");
		else
			Serial.printf("Failed to start advertising\n");
	}
	else
		Serial.printf("Failed to register advertisement data\n");

	Serial.printf("isAdvertising(): %s\n", advertising_->isAdvertising() ? "true" : "false");
#endif
} // SetupBluetooth
