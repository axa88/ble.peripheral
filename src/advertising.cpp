// advertising.cpp
#include "advertising.h"
#include <Arduino.h>
#include <map>
#include <mutex>
#include <algorithm>

namespace
{
	std::mutex configMutex_;
	std::map<uint8_t, Advertising::InstanceConfig> configs_;

#if !CONFIG_BT_NIMBLE_EXT_ADV
	NimBLEAdvertising* Adv() { return NimBLEDevice::getAdvertising(); }

	uint8_t discModeToLegacyMode(DiscoverableMode mode) noexcept
	{
		switch (mode)
		{
			case DiscoverableMode::None:    return BLE_GAP_DISC_MODE_NON;
			case DiscoverableMode::Limited: return BLE_GAP_DISC_MODE_LTD;
			case DiscoverableMode::General: return BLE_GAP_DISC_MODE_GEN;
			default:                        return BLE_GAP_DISC_MODE_GEN;
		}
	}
#else
	NimBLEExtAdvertising* Adv() { return NimBLEDevice::getAdvertising(); }

	// Builds the flags byte for extended advertising from a DiscoverableMode.
	uint8_t discModeToExtFlags(DiscoverableMode mode) noexcept
	{
		uint8_t flags = BLE_HS_ADV_F_BREDR_UNSUP; // set as this implementation is BLE only
		switch (mode)
		{
			case DiscoverableMode::None: break;
			case DiscoverableMode::Limited: flags |= BLE_HS_ADV_F_DISC_LTD; break;
			case DiscoverableMode::General: flags |= BLE_HS_ADV_F_DISC_GEN; break;
		}
		return flags;
	}
#endif

	uint16_t msToUnits(uint32_t ms) noexcept
	{
		return static_cast<uint16_t>((ms * 8 + 2) / 5); // round(ms / 0.625)
	}

	// Limited Discoverable Mode is intended to be temporary.
	// This implementation stops advertising after the GAP recommended maximum of 180 seconds.
	uint32_t limitedTimeoutMs(DiscoverableMode mode) noexcept
	{
		uint32_t ms = (mode == DiscoverableMode::Limited) ? Advertising::kLimitedDiscoverableTimeoutSec * 1000u : 0u;
		return std::min<uint32_t>(ms, 180u * 1000u); // never exceed the BLE spec max
	}

	bool rebuildAdvertisement(const Advertising::InstanceConfig& cfg)
	{
		auto* adv = Adv();
		if (!adv)
			return false;

#if !CONFIG_BT_NIMBLE_EXT_ADV
		if (cfg.id != 0)
		{
			Serial.println("[ERR] Legacy advertising only supports single instance/0");
			return false;
		}

		bool wasOn = adv->isAdvertising();
		if (wasOn)
			adv->stop();

		// Full rebuild every time: clear to not accumulate duplicate (service UUID / TX power entries) in payload, to not exceed the 31-byte legacy advert limit.
		adv->clearData();

		// disable Scan Response forces subsequent setName() to write into the PRIMARY payload deterministically:
		// NimBLEAdvertising::setName() checks m_scanResp first and, if true, routes the name into scan-response data instead of primary -
		// unlike addTxPower()/addServiceUUID()/setManufacturerData(), which only fall back to scan data if the primary add fails.
		// Since m_scanResp persists across rebuilds (clearData() doesn't reset it),
		// leaving this unset made name placement differ between the first rebuild (boot, m_scanResp still false) and every later toggle 
		// (m_scanResp already true from the previous rebuild's enable call) - an order-dependent bug. Forcing it false makes every rebuild land the name consistently.
		adv->enableScanResponse(false);

		// PRIMARY advertising payload: keep minimal - Flags (mandatory to spec) + Name.
		// The legacy PDU is capped at 31 bytes total; Service UUID (18 bytes for our 128-bit UUID) + TX power + manufacturer data no longer fit alongside Name+Flags here,
		// so they're placed in the scan response instead, which has its own independent 31-byte payload.
		if (!adv->setName(cfg.name.c_str()))
			Serial.println("[ERR] Advertised name did not fit in primary payload");

		// SCAN RESPONSE payload: built explicitly on a local NimBLEAdvertisementData and pushed as its own object via setScanResponseData(),
		// rather than calling NimBLEAdvertising::addTxPower() etc. on the advertising object itself
		// (whose scan-data fallback only triggers on primary-add failure, unlike setName()'s eager routing) - keeps the split between the two payloads explicit and
		// predictable instead of depending on NimBLE's inconsistent per-field fallback behavior.
		{
			NimBLEAdvertisementData scanData;
			if (!scanData.addTxPower())
				Serial.println("[ERR] TX power did not fit in scan response");
			if (!cfg.serviceUuid.empty() && !scanData.addServiceUUID(cfg.serviceUuid.c_str()))
				Serial.println("[ERR] Service UUID did not fit in scan response");
			if (!cfg.manufacturerData.empty() &&
				!scanData.setManufacturerData(std::string(reinterpret_cast<const char*>(cfg.manufacturerData.data()), cfg.manufacturerData.size())))
				Serial.println("[ERR] Manufacturer data did not fit in scan response");
			if (!adv->setScanResponseData(scanData))
				Serial.println("[ERR] Failed to apply scan response data");
			// Mark scan response as enabled now that we've deliberately populated it - this is what signals a scanner requesting SCAN_REQ actually gets SCAN_RSP data back.
			adv->enableScanResponse(true);
		}

		uint16_t units = msToUnits(cfg.intervalMs);
		adv->setMinInterval(units);
		adv->setMaxInterval(units);

		// Connectable mode BEFORE discoverable mode: setConnectableMode(BLE_GAP_CONN_MODE_NON) calls setFlags(0) internally,
		// wiping any flags already set - if this ran after setDiscoverableMode() it would silently erase the Flags AD entry whenever connectable=false.
		// Running it first means/ setDiscoverableMode() always has the final, correct word on the Flags entry.
		if (!adv->setConnectableMode(cfg.connectable ? BLE_GAP_CONN_MODE_UND : BLE_GAP_CONN_MODE_NON))
			Serial.println("[ERR] Failed to set connectable mode");
		if (!adv->setDiscoverableMode(discModeToLegacyMode(cfg.discoverable)))
			Serial.println("[ERR] Failed to set discoverable mode");

		if (wasOn)
			adv->start(limitedTimeoutMs(cfg.discoverable));

		Serial.printf("[BT] Instance 0 applied: name=\"%s\" disc=%.*s connectable=%s interval=%ums\n",
			cfg.name.c_str(),
			static_cast<int>(Advertising::discModeToString(cfg.discoverable).size()), Advertising::discModeToString(cfg.discoverable).data(),
			cfg.connectable ? "yes" : "no",
			cfg.intervalMs);
		return true;
#else
		bool wasOn = adv->isActive(cfg.id);
		if (wasOn)
			adv->stop(cfg.id);

		NimBLEExtAdvertisement extAdv(BLE_HCI_LE_PHY_1M, BLE_HCI_LE_PHY_1M);
		extAdv.setFlags(discModeToExtFlags(cfg.discoverable));
		extAdv.setConnectable(cfg.connectable);
		extAdv.setName(cfg.name.c_str());
		if (!cfg.manufacturerData.empty())
			extAdv.setManufacturerData(std::string(reinterpret_cast<const char*>(cfg.manufacturerData.data()), cfg.manufacturerData.size()));
		if (!cfg.serviceUuid.empty())
			extAdv.setServiceData(NimBLEUUID(cfg.serviceUuid.c_str()), std::string());
		uint16_t units = msToUnits(cfg.intervalMs);
		extAdv.setMinInterval(units);
		extAdv.setMaxInterval(units);

		if (!adv->setInstanceData(cfg.id, extAdv))
		{
			Serial.printf("[ERR] Failed to set instance data for id %u\n", cfg.id);
			return false;
		}

		if (wasOn)
			adv->start(cfg.id, static_cast<int>(limitedTimeoutMs(cfg.discoverable)), 0);

		Serial.printf("[BT] Instance %u applied: name=\"%s\" disc=%.*s connectable=%s interval=%ums\n",
			cfg.id, cfg.name.c_str(),
			static_cast<int>(Advertising::discModeToString(cfg.discoverable).size()), Advertising::discModeToString(cfg.discoverable).data(),
			cfg.connectable ? "yes" : "no",
			cfg.intervalMs);
		return true;
#endif
	}
} // namespace

namespace Advertising
{
	void Setup(const InstanceConfig& config)
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		configs_[config.id] = config;
		rebuildAdvertisement(configs_[config.id]);
	}

	bool ApplyConfig(const InstanceConfig& config)
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		configs_[config.id] = config;
		return rebuildAdvertisement(configs_[config.id]);
	}

	bool Start(uint8_t instanceId)
	{
		auto* adv = Adv();
		if (!adv)
			return false;

		std::optional<InstanceConfig> cfg;
		{
			std::lock_guard<std::mutex> lk(configMutex_);
			auto it = configs_.find(instanceId);
			if (it == configs_.end())
			{
				Serial.printf("[ERR] No such advertising instance: %u\n", instanceId);
				return false;
			}
			cfg = it->second;
		}

		// Auto-stop duration ONLY applies when this instance is configured Limited.
		// This implementation advertises indefinitely for None and General modes until Stop() is called.
		uint32_t durationMs = limitedTimeoutMs(cfg->discoverable);

#if !CONFIG_BT_NIMBLE_EXT_ADV
		bool ok = adv->start(durationMs);
#else
		bool ok = adv->start(instanceId, static_cast<int>(durationMs), 0);
#endif
		if (ok)
		{
			if (durationMs)
				Serial.printf("[BT] Advertising started (instance %u) [Limited, auto-stop in %us]\n", instanceId, durationMs / 1000u);
			else
				Serial.printf("[BT] Advertising started (instance %u)\n", instanceId);
		}
		else
			Serial.printf("[ERR] Failed to start advertising (instance %u)\n", instanceId);
		return ok;
	}

	bool Stop(uint8_t instanceId)
	{
		auto* adv = Adv();
		if (!adv)
			return false;

#if !CONFIG_BT_NIMBLE_EXT_ADV
		bool ok = adv->stop();
#else
		bool ok = adv->stop(instanceId);
#endif
		Serial.printf(ok ? "[BT] Advertising stopped (instance %u)\n" : "[ERR] Failed to stop advertising (instance %u)\n", instanceId);
		return ok;
	}

	bool IsActive(uint8_t instanceId)
	{
		auto* adv = Adv();
		if (!adv)
			return false;
#if !CONFIG_BT_NIMBLE_EXT_ADV
		return adv->isAdvertising();
#else
		return adv->isActive(instanceId);
#endif
	}

	DiscoverableMode Discoverable(uint8_t instanceId, std::optional<DiscoverableMode> mode)
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		auto it = configs_.find(instanceId);
		if (it == configs_.end())
			return DiscoverableMode::General;

		if (mode.has_value())
		{
			it->second.discoverable = *mode;
			rebuildAdvertisement(it->second);
		}
		return it->second.discoverable;
	}

	bool Connectable(uint8_t instanceId, std::optional<bool> connectable)
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		auto it = configs_.find(instanceId);
		if (it == configs_.end())
			return true;

		if (connectable.has_value())
		{
			it->second.connectable = *connectable;
			rebuildAdvertisement(it->second);
		}
		return it->second.connectable;
	}

	std::optional<InstanceConfig> GetConfig(uint8_t instanceId)
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		auto it = configs_.find(instanceId);
		if (it == configs_.end())
			return std::nullopt;
		return it->second;
	}

#if CONFIG_BT_NIMBLE_EXT_ADV
	bool AddInstance(const InstanceConfig& config)
	{
		{
			std::lock_guard<std::mutex> lk(configMutex_);
			if (configs_.find(config.id) != configs_.end())
			{
				Serial.printf("[ERR] Instance %u already exists\n", config.id);
				return false;
			}
			configs_[config.id] = config;
		}
		bool ok = ApplyConfig(config);
		if (ok)
			Serial.printf("[BT] Added advertising instance %u (\"%s\")\n", config.id, config.name.c_str());
		return ok;
	}

	bool RemoveInstance(uint8_t instanceId)
	{
		if (instanceId == 0)
		{
			Serial.println("[ERR] Instance 0 cannot be removed");
			return false;
		}

		auto* adv = Adv();
		if (!adv)
			return false;

		std::lock_guard<std::mutex> lk(configMutex_);
		auto it = configs_.find(instanceId);
		if (it == configs_.end())
		{
			Serial.printf("[ERR] No such advertising instance: %u\n", instanceId);
			return false;
		}

		bool ok = adv->removeInstance(instanceId);
		if (ok)
		{
			configs_.erase(it);
			Serial.printf("[BT] Removed advertising instance %u\n", instanceId);
		}
		else
			Serial.printf("[ERR] Failed to remove advertising instance %u\n", instanceId);
		return ok;
	}

	std::vector<uint8_t> ListInstances()
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		std::vector<uint8_t> ids;
		ids.reserve(configs_.size());
		for (const auto& [id, cfg] : configs_)
			ids.push_back(id);
		return ids;
	}
#endif

} // namespace Advertising
