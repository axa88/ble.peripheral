// advertising.cpp
#include "advertising.h"
#include <Arduino.h>
#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

namespace
{
	std::mutex configMutex_;
	std::map<uint8_t, Advertising::InstanceConfig> configs_;
	bool directedActive_ = false;
	std::optional<uint8_t> directedInstance_;
	std::optional<NimBLEAddress> directedTarget_;
	std::mutex restartMutex_;
	std::map<uint8_t, TimerHandle_t> restartTimers_;
	std::map<uint8_t, uint32_t> restartTimerGenerations_;
	std::set<uint8_t> pendingRestartInstances_;
	std::map<uint8_t, uint32_t> restartGenerations_;
	constexpr uint32_t kRestartDelayMs = 5000;

#if !CONFIG_BT_NIMBLE_EXT_ADV
	NimBLEAdvertising* Adv() { return NimBLEDevice::getAdvertising(); }
	std::optional<Advertising::InstanceConfig> legacyOrdinaryCache_;
#else
	NimBLEExtAdvertising* Adv() { return NimBLEDevice::getAdvertising(); }
#endif

	uint16_t msToUnits(uint32_t ms) noexcept
	{
		return static_cast<uint16_t>((ms * 8 + 2) / 5);
	}

	uint32_t limitedTimeoutMs(DiscoverableMode mode) noexcept
	{
		uint32_t ms = mode == DiscoverableMode::Limited ? Advertising::kLimitedDiscoverableTimeoutSec * 1000u : 0u;
		return std::min<uint32_t>(ms, 180u * 1000u);
	}

	bool discoverabilityAllowed(Advertising::AdvertisementMode mode, DiscoverableMode discoverable) noexcept
	{
#if CONFIG_BT_NIMBLE_EXT_ADV
		(void)mode;
		(void)discoverable;
		return true;
#else
		const auto props = Advertising::modeProperties(mode);
		if (props.directed)
			return true;
		if (!props.connectable && props.scannable)
			return discoverable != DiscoverableMode::None;
		if (!props.connectable && !props.scannable)
			return discoverable == DiscoverableMode::None;
		return true;
#endif
	}

	bool isBonded(const NimBLEAddress& address)
	{
		if (address.isNull())
			return false;
		for (int i = 0; i < NimBLEDevice::getNumBonds(); ++i)
		{
			if (NimBLEDevice::getBondedAddress(i) == address)
				return true;
		}
		return false;
	}

#if !CONFIG_BT_NIMBLE_EXT_ADV
	uint8_t discModeToLegacyMode(DiscoverableMode mode) noexcept
	{
		switch (mode)
		{
			case DiscoverableMode::None: return BLE_GAP_DISC_MODE_NON;
			case DiscoverableMode::Limited: return BLE_GAP_DISC_MODE_LTD;
			case DiscoverableMode::General: return BLE_GAP_DISC_MODE_GEN;
			default: return BLE_GAP_DISC_MODE_GEN;
		}
	}

	bool rebuildLegacyAdvertisement(const Advertising::InstanceConfig& cfg, bool restartIfActive)
	{
		auto* adv = Adv();
		if (!adv || cfg.id != 0)
			return false;

		const auto props = Advertising::modeProperties(cfg.mode);
		if (props.directed)
			return false;

		bool wasOn = adv->isAdvertising();
		if (wasOn)
			adv->stop();

		adv->clearData();
		adv->enableScanResponse(false);

		NimBLEAdvertisementData primaryData;
		if (!primaryData.setName(cfg.name.c_str()))
			Serial.println("[ERR] Advertised name did not fit in primary payload");

		NimBLEAdvertisementData scanData;
		if (props.scannable)
		{
			if (!scanData.addTxPower())
				Serial.println("[ERR] TX power did not fit in scan response");
			if (!cfg.serviceUuid.empty() && !scanData.addServiceUUID(cfg.serviceUuid.c_str()))
				Serial.println("[ERR] Service UUID did not fit in scan response");
			if (!cfg.manufacturerData.empty() &&
				!scanData.setManufacturerData(std::string(reinterpret_cast<const char*>(cfg.manufacturerData.data()), cfg.manufacturerData.size())))
				Serial.println("[ERR] Manufacturer data did not fit in scan response");
			if (!adv->setScanResponseData(scanData))
				Serial.println("[ERR] Failed to apply scan response data");
			adv->enableScanResponse(true);
		}
		else
		{
			if (!cfg.serviceUuid.empty() && !primaryData.addServiceUUID(cfg.serviceUuid.c_str()))
				Serial.println("[ERR] Service UUID did not fit in primary payload");
			if (!cfg.manufacturerData.empty() &&
				!primaryData.setManufacturerData(std::string(reinterpret_cast<const char*>(cfg.manufacturerData.data()), cfg.manufacturerData.size())))
				Serial.println("[ERR] Manufacturer data did not fit in primary payload");
		}
		if (!adv->setAdvertisementData(primaryData))
			Serial.println("[ERR] Failed to apply primary advertising data");

		uint16_t units = msToUnits(cfg.intervalMs);
		adv->setMinInterval(units);
		adv->setMaxInterval(units);

		if (!adv->setConnectableMode(props.connectable ? BLE_GAP_CONN_MODE_UND : BLE_GAP_CONN_MODE_NON))
			Serial.println("[ERR] Failed to set connectable mode");

		uint8_t discoverableMode = discModeToLegacyMode(cfg.discoverable);
		if (!adv->setDiscoverableMode(discoverableMode))
			Serial.println("[ERR] Failed to set discoverable mode");

		if (wasOn && restartIfActive)
			return adv->start(limitedTimeoutMs(cfg.discoverable));
		return true;
	}

	bool startLegacyDirectedLocked(const Advertising::InstanceConfig& cfg, const NimBLEAddress& target)
	{
		auto* adv = Adv();
		if (!adv || cfg.id != 0)
			return false;

		if (adv->isAdvertising())
		{
			Serial.println("[ERR] Legacy advertising must be stopped before directed start");
			return false;
		}

		adv->enableScanResponse(false);
		if (!adv->setConnectableMode(BLE_GAP_CONN_MODE_DIR))
			return false;
		if (!adv->setDiscoverableMode(BLE_GAP_DISC_MODE_NON))
			return false;
		return adv->start(0, &target);
	}

	void restoreLegacyOrdinaryLocked()
	{
		if (!legacyOrdinaryCache_.has_value())
			return;

		Advertising::InstanceConfig restored = *legacyOrdinaryCache_;
		legacyOrdinaryCache_.reset();
		restored.restartAfterConnection = false;
		configs_[0] = restored;
		directedActive_ = false;
		directedInstance_.reset();
		directedTarget_.reset();
		rebuildLegacyAdvertisement(restored, false);
	}
#else
	// Deterministic service-data payload reused from the legacy fixture: "BeBo" = 0x42 0x65 0x42 0x6F.
	constexpr char kServiceDataPayload[] = { 'B', 'e', 'B', 'o' };

	uint8_t discModeToExtFlags(DiscoverableMode mode) noexcept
	{
		uint8_t flags = BLE_HS_ADV_F_BREDR_UNSUP;
		switch (mode)
		{
			case DiscoverableMode::None: break;
			case DiscoverableMode::Limited: flags |= BLE_HS_ADV_F_DISC_LTD; break;
			case DiscoverableMode::General: flags |= BLE_HS_ADV_F_DISC_GEN; break;
		}
		return flags;
	}

	bool buildExtendedAdvertisement(NimBLEExtAdvertisement& extAdv, const Advertising::InstanceConfig& cfg)
	{
		const auto props = Advertising::modeProperties(cfg.mode);
		if (props.directed && !cfg.directedTarget.has_value())
			return false;

		extAdv.setFlags(discModeToExtFlags(cfg.discoverable));
		extAdv.setConnectable(props.connectable);
		extAdv.setScannable(props.scannable);
		if (props.directed)
			extAdv.setDirectedPeer(*cfg.directedTarget);
		extAdv.setDirected(props.directed, false);
		extAdv.setName(cfg.name.c_str());
		if (!cfg.manufacturerData.empty())
			extAdv.setManufacturerData(std::string(reinterpret_cast<const char*>(cfg.manufacturerData.data()), cfg.manufacturerData.size()));
		if (!cfg.serviceUuid.empty())
		{
			const std::string serviceData(kServiceDataPayload, sizeof(kServiceDataPayload));
			if (!extAdv.setServiceData(NimBLEUUID(cfg.serviceUuid.c_str()), serviceData))
			{
				Serial.printf("[ERR] Failed to configure service data for advertising instance %u\n", cfg.id);
				return false;
			}
		}
		uint16_t units = msToUnits(cfg.intervalMs);
		extAdv.setMinInterval(units);
		extAdv.setMaxInterval(units);
		return true;
	}

	bool applyExtendedDataLocked(const Advertising::InstanceConfig& cfg, bool restartIfActive)
	{
		auto* adv = Adv();
		if (!adv)
			return false;

		const auto props = Advertising::modeProperties(cfg.mode);
		if (props.directed && !cfg.directedTarget.has_value())
			return false;

		bool wasOn = adv->isActive(cfg.id);
		if (wasOn && !adv->stop(cfg.id))
			return false;

		NimBLEExtAdvertisement extAdv(BLE_HCI_LE_PHY_1M, BLE_HCI_LE_PHY_1M);
		if (!buildExtendedAdvertisement(extAdv, cfg))
			return false;
		if (!adv->setInstanceData(cfg.id, extAdv))
			return false;

		if (wasOn && restartIfActive)
			return adv->start(cfg.id, static_cast<int>(limitedTimeoutMs(cfg.discoverable)), 0);
		return true;
	}
#endif

	bool rebuildAdvertisement(const Advertising::InstanceConfig& cfg, bool restartIfActive)
	{
#if CONFIG_BT_NIMBLE_EXT_ADV
		return applyExtendedDataLocked(cfg, restartIfActive);
#else
		return rebuildLegacyAdvertisement(cfg, restartIfActive);
#endif
	}

	void clearDirectedStateLocked()
	{
		directedActive_ = false;
		directedInstance_.reset();
		directedTarget_.reset();
	}

	void restartTimerCallback(TimerHandle_t timer);
	bool startInstanceLocked(uint8_t instanceId);
	void tryRestart(uint8_t instanceId, uint32_t generation);

	bool restartPolicyEnabledLocked(const Advertising::InstanceConfig& cfg) noexcept
	{
		return cfg.restartAfterConnection &&
			cfg.discoverable != DiscoverableMode::Limited &&
			!Advertising::modeIsDirected(cfg.mode);
	}

	bool invalidateRestartLocked(uint8_t instanceId)
	{
		const bool wasPending = pendingRestartInstances_.erase(instanceId) != 0;
		++restartGenerations_[instanceId];
		return wasPending;
	}

	bool invalidateAllRestartsLocked()
	{
		const bool hadPending = !pendingRestartInstances_.empty();
		for (const auto instanceId : pendingRestartInstances_)
			++restartGenerations_[instanceId];
		pendingRestartInstances_.clear();
		return hadPending;
	}

	void cancelRestartLocked(uint8_t instanceId, const char* reason)
	{
		if (invalidateRestartLocked(instanceId))
			Serial.printf("[BT] Advertising restart cancelled | Adv[%u] | %s\n", instanceId, reason);
	}

	void cancelAllRestartsLocked(const char* reason)
	{
		if (invalidateAllRestartsLocked())
			Serial.printf("[BT] Advertising restarts cancelled | all instances | %s\n", reason);
	}

	bool markRestartPendingLocked(uint8_t instanceId, const char* trigger)
	{
		auto it = configs_.find(instanceId);
		if (it == configs_.end() || !restartPolicyEnabledLocked(it->second))
			return false;

		if (pendingRestartInstances_.insert(instanceId).second)
		{
			++restartGenerations_[instanceId];
			Serial.printf("[BT] Advertising restart pending | Adv[%u] | trigger:%s\n", instanceId, trigger);
		}
		return true;
	}

	void cancelRestartTimer(uint8_t instanceId)
	{
		TimerHandle_t timer = nullptr;
		{
			std::lock_guard<std::mutex> lk(restartMutex_);
			auto it = restartTimers_.find(instanceId);
			if (it == restartTimers_.end())
				return;
			timer = it->second;
			restartTimers_.erase(it);
			restartTimerGenerations_.erase(instanceId);
		}

		xTimerStop(timer, 0);
		xTimerDelete(timer, 0);
	}

	void cancelAllRestartTimers()
	{
		std::vector<TimerHandle_t> timers;
		{
			std::lock_guard<std::mutex> lk(restartMutex_);
			timers.reserve(restartTimers_.size());
			for (const auto& [instanceId, timer] : restartTimers_)
			{
				(void)instanceId;
				timers.push_back(timer);
			}
			restartTimers_.clear();
			restartTimerGenerations_.clear();
		}

		for (auto timer : timers)
		{
			xTimerStop(timer, 0);
			xTimerDelete(timer, 0);
		}
	}

	void scheduleRestartTimer(uint8_t instanceId)
	{
		uint32_t generation = 0;
		{
			std::lock_guard<std::mutex> lk(configMutex_);
			auto it = configs_.find(instanceId);
			if (it == configs_.end() || pendingRestartInstances_.find(instanceId) == pendingRestartInstances_.end() ||
				directedActive_ || !restartPolicyEnabledLocked(it->second) || Advertising::IsActive(instanceId))
				return;
			generation = restartGenerations_[instanceId];
		}

		TimerHandle_t staleTimer = nullptr;
		TimerHandle_t timer = nullptr;
		BaseType_t timerStartResult = pdFAIL;
		{
			std::lock_guard<std::mutex> lk(restartMutex_);
			auto existing = restartTimers_.find(instanceId);
			if (existing != restartTimers_.end())
			{
				if (restartTimerGenerations_[instanceId] == generation)
					return;
				staleTimer = existing->second;
				restartTimers_.erase(existing);
				restartTimerGenerations_.erase(instanceId);
			}

			timer = xTimerCreate("advRestart", pdMS_TO_TICKS(kRestartDelayMs), pdFALSE, nullptr, restartTimerCallback);
			if (!timer)
			{
				Serial.printf("[ERR] Failed to create advertising restart timer for Adv[%u]\n", instanceId);
			}
			else
			{
				restartTimers_[instanceId] = timer;
				restartTimerGenerations_[instanceId] = generation;
				timerStartResult = xTimerStart(timer, 0);
				if (timerStartResult != pdPASS)
				{
					restartTimers_.erase(instanceId);
					restartTimerGenerations_.erase(instanceId);
				}
			}
		}

		if (staleTimer)
		{
			xTimerStop(staleTimer, 0);
			xTimerDelete(staleTimer, 0);
		}
		if (!timer)
			return;

		if (timerStartResult != pdPASS)
		{
			xTimerDelete(timer, 0);
			Serial.printf("[ERR] Failed to start advertising restart timer for Adv[%u]\n", instanceId);
			return;
		}
		Serial.printf("[BT] Advertising restart scheduled | Adv[%u] | delay:%ums\n", instanceId, kRestartDelayMs);
	}

	void restartTimerCallback(TimerHandle_t timer)
	{
		std::optional<uint8_t> instanceId;
		uint32_t generation = 0;
		{
			std::lock_guard<std::mutex> lk(restartMutex_);
			for (auto it = restartTimers_.begin(); it != restartTimers_.end(); ++it)
			{
				if (it->second == timer)
				{
					instanceId = it->first;
					generation = restartTimerGenerations_[it->first];
					restartTimers_.erase(it);
					restartTimerGenerations_.erase(it->first);
					break;
				}
			}
		}

		xTimerDelete(timer, 0);
		if (instanceId.has_value())
			tryRestart(*instanceId, generation);
	}

	bool startInstanceLocked(uint8_t instanceId)
	{
		auto it = configs_.find(instanceId);
		if (it == configs_.end())
		{
			Serial.printf("[ERR] No such advertising instance: %u\n", instanceId);
			return false;
		}

		Advertising::InstanceConfig cfg = it->second;
		if (!Advertising::modeIsSupported(cfg.mode))
		{
			Serial.printf("[ERR] Adv[%u] has an unsupported advertising mode\n", instanceId);
			return false;
		}
		const auto props = Advertising::modeProperties(cfg.mode);
		if (directedActive_)
		{
			Serial.println("[ERR] Directed advertising is already active");
			return false;
		}
		if (Advertising::IsActive(instanceId))
			return true;

		if (props.directed)
		{
			if (!cfg.directedTarget.has_value() || !isBonded(*cfg.directedTarget))
			{
				Serial.println("[ERR] Directed mode requires a selected bonded target");
				return false;
			}

#if !CONFIG_BT_NIMBLE_EXT_ADV
			if (!startLegacyDirectedLocked(cfg, *cfg.directedTarget))
			{
				Serial.printf("[ERR] Failed to start directed advertising to %s\n", cfg.directedTarget->toString().c_str());
				return false;
			}
#else
			if (!applyExtendedDataLocked(cfg, false))
			{
				Serial.printf("[ERR] Failed to configure directed advertising instance %u\n", instanceId);
				return false;
			}
			if (!Adv()->start(instanceId, static_cast<int>(limitedTimeoutMs(cfg.discoverable)), 0))
			{
				Serial.printf("[ERR] Failed to start directed advertising instance %u\n", instanceId);
				return false;
			}
			cancelAllRestartsLocked("directed mode");
			cancelAllRestartTimers();
			for (const auto& [id, other] : configs_)
			{
				(void)other;
				if (id != instanceId && Adv()->isActive(id) && !Adv()->stop(id))
					Serial.printf("[ERR] Failed to stop advertising instance %u while directed mode is active\n", id);
			}
#endif
			directedActive_ = true;
			directedInstance_ = instanceId;
			directedTarget_ = cfg.directedTarget;
			Serial.printf("[BT] Directed advertising started | Adv[%u] | Target=%s\n", instanceId, cfg.directedTarget->toString().c_str());
			return true;
		}

		if (!rebuildAdvertisement(cfg, false))
		{
			Serial.printf("[ERR] Failed to configure advertising instance %u\n", instanceId);
			return false;
		}

#if CONFIG_BT_NIMBLE_EXT_ADV
		bool ok = Adv()->start(instanceId, static_cast<int>(limitedTimeoutMs(cfg.discoverable)), 0);
#else
		bool ok = Adv()->start(limitedTimeoutMs(cfg.discoverable));
#endif
		Serial.printf(ok ? "[BT] Advertising started | Adv[%u]\n" : "[ERR] Failed to start advertising | Adv[%u]\n", instanceId);
		return ok;
	}

	void tryRestart(uint8_t instanceId, uint32_t generation)
	{
		bool retry = false;
		{
			std::lock_guard<std::mutex> lk(configMutex_);
			auto it = configs_.find(instanceId);
			if (it == configs_.end() || pendingRestartInstances_.find(instanceId) == pendingRestartInstances_.end() ||
				restartGenerations_[instanceId] != generation)
				return;

			if (directedActive_)
			{
				cancelRestartLocked(instanceId, "directed advertising active");
				return;
			}
			if (!restartPolicyEnabledLocked(it->second))
			{
				cancelRestartLocked(instanceId, "restart policy no longer eligible");
				return;
			}
			if (Advertising::IsActive(instanceId))
			{
				cancelRestartLocked(instanceId, "advertising already active");
				return;
			}

			Serial.printf("[BT] Advertising restart attempted | Adv[%u]\n", instanceId);
			if (startInstanceLocked(instanceId) && Advertising::IsActive(instanceId))
			{
				invalidateRestartLocked(instanceId);
				Serial.printf("[BT] Advertising restarted | Adv[%u]\n", instanceId);
			}
			else
			{
				retry = true;
				Serial.printf("[ERR] Advertising restart failed; retrying | Adv[%u]\n", instanceId);
			}
		}

		if (retry)
			scheduleRestartTimer(instanceId);
	}
}

namespace Advertising
{
	void Setup(const InstanceConfig& config)
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		if (!modeIsSupported(config.mode))
		{
			Serial.printf("[ERR] Unsupported advertising mode for instance %u\n", config.id);
			return;
		}
		InstanceConfig stored = config;
		if (stored.discoverable == DiscoverableMode::Limited && stored.restartAfterConnection)
		{
			Serial.printf("[ERR] Adv[%u] Limited discoverability cannot use restart after connection\n", stored.id);
			stored.restartAfterConnection = false;
		}
		if (modeIsDirected(stored.mode))
			stored.restartAfterConnection = false;
#if CONFIG_BT_NIMBLE_EXT_ADV
		if (modeIsDirected(stored.mode))
		{
			stored.discoverable = DiscoverableMode::None;
			stored.restartAfterConnection = false;
		}
#endif
		cancelRestartLocked(stored.id, "configuration changed");
		cancelRestartTimer(stored.id);
		configs_[stored.id] = stored;
		if (!modeIsDirected(stored.mode) || stored.directedTarget.has_value())
			rebuildAdvertisement(stored, false);
	}

	bool ApplyConfig(const InstanceConfig& config)
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		if (!modeIsSupported(config.mode))
		{
			Serial.printf("[ERR] Unsupported advertising mode for instance %u\n", config.id);
			return false;
		}
#if CONFIG_BT_NIMBLE_EXT_ADV
		if (config.id >= CONFIG_BT_NIMBLE_MAX_EXT_ADV_INSTANCES)
		{
			Serial.printf("[ERR] Instance %u is unavailable\n", config.id);
			return false;
		}
#else
		if (config.id != 0)
		{
			Serial.println("[ERR] Legacy advertising only supports instance 0");
			return false;
		}
#endif
		auto previous = configs_.find(config.id);
		bool modeChanged = previous != configs_.end() && previous->second.mode != config.mode;
		if (modeChanged && IsActive(config.id))
		{
			Serial.printf("[ERR] Adv[%u] mode change rejected because advertising is active\n", config.id);
			return false;
		}

		InstanceConfig stored = config;
		if (stored.discoverable == DiscoverableMode::Limited && stored.restartAfterConnection)
		{
			Serial.printf("[ERR] Adv[%u] Limited discoverability cannot use restart after connection\n", stored.id);
			return false;
		}
		if (modeIsDirected(stored.mode))
			stored.restartAfterConnection = false;
#if !CONFIG_BT_NIMBLE_EXT_ADV
		if (modeChanged && !modeIsDirected(previous->second.mode) && modeIsDirected(stored.mode))
			legacyOrdinaryCache_ = previous->second;
		else if (modeChanged && modeIsDirected(previous->second.mode) && !modeIsDirected(stored.mode) && legacyOrdinaryCache_.has_value())
			stored.discoverable = legacyOrdinaryCache_->discoverable;
#endif
		if (!discoverabilityAllowed(stored.mode, stored.discoverable))
		{
			Serial.printf("[ERR] Adv[%u] discoverability is incompatible with the selected advertising mode\n", config.id);
			return false;
		}

#if CONFIG_BT_NIMBLE_EXT_ADV
		if (modeIsDirected(stored.mode))
		{
			stored.discoverable = DiscoverableMode::None;
			stored.restartAfterConnection = false;
		}
#else
		if (modeChanged && modeIsDirected(previous->second.mode) && !modeIsDirected(stored.mode))
			legacyOrdinaryCache_.reset();
#endif
		cancelRestartLocked(stored.id, "configuration changed");
		cancelRestartTimer(stored.id);
		configs_[stored.id] = stored;
		if (modeIsDirected(stored.mode) && !stored.directedTarget.has_value())
			return true;
	#if !CONFIG_BT_NIMBLE_EXT_ADV
		if (modeIsDirected(stored.mode))
			return true;
	#endif

		if (directedActive_ && directedInstance_.has_value() && *directedInstance_ == stored.id)
		{
#if CONFIG_BT_NIMBLE_EXT_ADV
			return rebuildAdvertisement(stored, true);
#else
			return true;
#endif
		}
		return rebuildAdvertisement(stored, true);
	}

	bool Start(uint8_t instanceId)
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		cancelRestartLocked(instanceId, "manual start");
		cancelRestartTimer(instanceId);
		return startInstanceLocked(instanceId);
	}

	bool Stop(uint8_t instanceId)
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		cancelRestartLocked(instanceId, "manual stop");
		cancelRestartTimer(instanceId);
		auto* adv = Adv();
		if (!adv)
			return false;

		bool ok = false;
#if CONFIG_BT_NIMBLE_EXT_ADV
		ok = !adv->isActive(instanceId) || adv->stop(instanceId);
#else
		ok = !adv->isAdvertising() || adv->stop();
#endif
		if (directedActive_ && directedInstance_.has_value() && *directedInstance_ == instanceId)
		{
#if !CONFIG_BT_NIMBLE_EXT_ADV
			restoreLegacyOrdinaryLocked();
#else
			clearDirectedStateLocked();
#endif
		}
		Serial.printf(ok ? "[BT] Advertising stopped | Adv[%u]\n" : "[ERR] Failed to stop advertising | Adv[%u]\n", instanceId);
		return ok;
	}

	bool IsActive(uint8_t instanceId)
	{
		auto* adv = Adv();
		if (!adv)
			return false;
#if CONFIG_BT_NIMBLE_EXT_ADV
		return adv->isActive(instanceId);
#else
		return instanceId == 0 && adv->isAdvertising();
#endif
	}

	bool CycleMode(uint8_t instanceId)
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		auto it = configs_.find(instanceId);
		if (it == configs_.end())
		{
			Serial.printf("[ERR] No such advertising instance: %u\n", instanceId);
			return false;
		}
		if (!modeIsSupported(it->second.mode))
		{
			Serial.printf("[ERR] Adv[%u] has an unsupported advertising mode\n", instanceId);
			return false;
		}
		if (IsActive(instanceId))
		{
			Serial.printf("[ERR] Adv[%u] mode change rejected because advertising is active\n", instanceId);
			return false;
		}

		AdvertisementMode previous = it->second.mode;
		AdvertisementMode next = nextMode(previous);
		bool foundCompatibleMode = false;
		for (size_t i = 0; i < supportedModes.size(); ++i)
		{
			if (discoverabilityAllowed(next, it->second.discoverable))
			{
				foundCompatibleMode = true;
				break;
			}
			next = nextMode(next);
		}
		if (!foundCompatibleMode)
		{
			Serial.printf("[ERR] Adv[%u] has no compatible supported mode for its discoverability value\n", instanceId);
			return false;
		}
		cancelRestartLocked(instanceId, "mode changed");
		cancelRestartTimer(instanceId);
#if !CONFIG_BT_NIMBLE_EXT_ADV
		if (!modeIsDirected(previous) && modeIsDirected(next))
		{
			legacyOrdinaryCache_ = it->second;
			it->second.restartAfterConnection = false;
		}
		else if (modeIsDirected(previous) && !modeIsDirected(next))
		{
			if (legacyOrdinaryCache_.has_value())
			{
				InstanceConfig restored = *legacyOrdinaryCache_;
				restored.mode = next;
				restored.restartAfterConnection = false;
				it->second = restored;
				legacyOrdinaryCache_.reset();
				return true;
			}
		}
#else
		if (!modeIsDirected(previous) && modeIsDirected(next))
		{
			it->second.discoverable = DiscoverableMode::None;
			it->second.restartAfterConnection = false;
		}
		else if (modeIsDirected(previous) && !modeIsDirected(next))
			it->second.discoverable = DiscoverableMode::None;
#endif
		it->second.mode = next;
		return true;
	}

	bool SetDirectedTarget(uint8_t instanceId, const NimBLEAddress& target)
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		auto it = configs_.find(instanceId);
		if (it == configs_.end() || target.isNull())
			return false;
		if (IsActive(instanceId))
		{
			Serial.printf("[ERR] Adv[%u] target cannot change while advertising is active\n", instanceId);
			return false;
		}
		cancelRestartLocked(instanceId, "directed target changed");
		cancelRestartTimer(instanceId);
		it->second.directedTarget = target;
		return true;
	}

	std::optional<NimBLEAddress> GetDirectedTarget(uint8_t instanceId)
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		auto it = configs_.find(instanceId);
		return it == configs_.end() ? std::nullopt : it->second.directedTarget;
	}

	bool RestartAfterConnection(uint8_t instanceId, std::optional<bool> enable)
	{
		if (!enable.has_value())
		{
			std::lock_guard<std::mutex> lk(configMutex_);
			auto it = configs_.find(instanceId);
			return it != configs_.end() && it->second.restartAfterConnection;
		}

		if (!*enable)
		{
			{
				std::lock_guard<std::mutex> lk(configMutex_);
				cancelRestartLocked(instanceId, "policy disabled");
			}
			cancelRestartTimer(instanceId);
		}

		std::lock_guard<std::mutex> lk(configMutex_);
		auto it = configs_.find(instanceId);
		if (it == configs_.end())
		{
			Serial.printf("[ERR] No such advertising instance: %u\n", instanceId);
			return false;
		}
		if (*enable && it->second.discoverable == DiscoverableMode::Limited)
		{
			Serial.printf("[ERR] Adv[%u] restart after connection is unavailable with Limited discoverability\n", instanceId);
			return it->second.restartAfterConnection;
		}
		if (*enable && modeIsDirected(it->second.mode))
		{
			Serial.printf("[ERR] Adv[%u] Directed advertising does not restart after connection\n", instanceId);
			return it->second.restartAfterConnection;
		}
		it->second.restartAfterConnection = *enable;
		return it->second.restartAfterConnection;
	}

	void CancelRestartAfterConnection(uint8_t instanceId)
	{
		{
			std::lock_guard<std::mutex> lk(configMutex_);
			cancelRestartLocked(instanceId, "explicit stop");
		}
		cancelRestartTimer(instanceId);
	}

	void CancelAllRestartAfterConnection()
	{
		{
			std::lock_guard<std::mutex> lk(configMutex_);
			cancelAllRestartsLocked("stop all");
		}
		cancelAllRestartTimers();
	}

	void ClearDirectedTargets()
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		for (auto& [id, cfg] : configs_)
		{
			cancelRestartLocked(id, "directed targets cleared");
			cancelRestartTimer(id);
			cfg.directedTarget.reset();
		}
	}

	bool IsDirectedActive()
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		return directedActive_;
	}

	std::optional<NimBLEAddress> DirectedTarget()
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		return directedActive_ ? directedTarget_ : std::nullopt;
	}

	void OnConnectionEstablished()
	{
		bool shouldSchedule = false;
		{
			std::lock_guard<std::mutex> lk(configMutex_);
			if (directedActive_)
			{
#if !CONFIG_BT_NIMBLE_EXT_ADV
				restoreLegacyOrdinaryLocked();
#else
				clearDirectedStateLocked();
#endif
			}
#if !CONFIG_BT_NIMBLE_EXT_ADV
			else
				shouldSchedule = markRestartPendingLocked(0, "connection");
#endif
		}

#if !CONFIG_BT_NIMBLE_EXT_ADV
		if (shouldSchedule)
			scheduleRestartTimer(0);
#else
		(void)shouldSchedule;
#endif
	}

	#if CONFIG_BT_NIMBLE_EXT_ADV
	void OnAdvertisingStopped(uint8_t instanceId, int reason)
	{
		bool shouldSchedule = false;
		{
			std::lock_guard<std::mutex> lk(configMutex_);
			if (directedActive_ && directedInstance_.has_value() && *directedInstance_ == instanceId)
			{
				if (reason != BLE_HS_EDONE)
					clearDirectedStateLocked();
			}
			else if (!directedActive_ && (reason == 0 || reason == BLE_HS_EPREEMPTED))
			{
				shouldSchedule = markRestartPendingLocked(instanceId,
					reason == 0 ? "connection" : "host preemption");
			}
		}
		if (shouldSchedule)
			scheduleRestartTimer(instanceId);
	}
	#endif

	DiscoverableMode Discoverable(uint8_t instanceId, std::optional<DiscoverableMode> mode)
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		auto it = configs_.find(instanceId);
		if (it == configs_.end())
			return DiscoverableMode::General;
		if (mode.has_value())
		{
			if (IsActive(instanceId))
			{
				Serial.printf("[ERR] Adv[%u] discoverability change rejected because advertising is active\n", instanceId);
				return it->second.discoverable;
			}

#if CONFIG_BT_NIMBLE_EXT_ADV
			if (modeIsDirected(it->second.mode) && *mode != DiscoverableMode::None)
				it->second.mode = undirectedMode(it->second.mode);
#else
			if (modeIsDirected(it->second.mode))
			{
				Serial.printf("[ERR] Adv[%u] Legacy Directed mode uses its cached discoverability\n", instanceId);
				return it->second.discoverable;
			}
#endif
			if (!discoverabilityAllowed(it->second.mode, *mode))
			{
				Serial.printf("[ERR] Adv[%u] discoverability is incompatible with the selected advertising mode\n", instanceId);
				return it->second.discoverable;
			}
			cancelRestartLocked(instanceId, "discoverability changed");
			cancelRestartTimer(instanceId);
			it->second.discoverable = *mode;
			if (*mode == DiscoverableMode::Limited)
				it->second.restartAfterConnection = false;
			if (!modeIsDirected(it->second.mode))
				rebuildAdvertisement(it->second, false);
		}
		return it->second.discoverable;
	}

	bool CycleDiscoverability(uint8_t instanceId)
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		auto it = configs_.find(instanceId);
		if (it == configs_.end())
		{
			Serial.printf("[ERR] No such advertising instance: %u\n", instanceId);
			return false;
		}
		if (IsActive(instanceId))
		{
			Serial.printf("[ERR] Adv[%u] discoverability change rejected because advertising is active\n", instanceId);
			return false;
		}
#if !CONFIG_BT_NIMBLE_EXT_ADV
		if (modeIsDirected(it->second.mode))
		{
			Serial.printf("[ERR] Adv[%u] Legacy Directed mode uses its cached discoverability\n", instanceId);
			return false;
		}
#endif

		DiscoverableMode candidate = it->second.discoverable;
		for (size_t i = 0; i < 3; ++i)
		{
			candidate = discModeToggle(candidate);
			AdvertisementMode candidateMode = it->second.mode;
#if CONFIG_BT_NIMBLE_EXT_ADV
			if (modeIsDirected(candidateMode) && candidate != DiscoverableMode::None)
				candidateMode = undirectedMode(candidateMode);
#endif
			if (!discoverabilityAllowed(candidateMode, candidate))
				continue;

			cancelRestartLocked(instanceId, "discoverability changed");
			cancelRestartTimer(instanceId);
			it->second.mode = candidateMode;
			it->second.discoverable = candidate;
			if (candidate == DiscoverableMode::Limited)
				it->second.restartAfterConnection = false;
			if (!modeIsDirected(candidateMode))
				rebuildAdvertisement(it->second, false);
			return true;
		}

		Serial.printf("[ERR] Adv[%u] has no other valid discoverability value for its mode\n", instanceId);
		return false;
	}

	std::optional<InstanceConfig> GetConfig(uint8_t instanceId)
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		auto it = configs_.find(instanceId);
		return it == configs_.end() ? std::nullopt : std::optional<InstanceConfig>(it->second);
	}

#if CONFIG_BT_NIMBLE_EXT_ADV
	bool AddInstance(const InstanceConfig& config)
	{
		if (!modeIsSupported(config.mode))
		{
			Serial.printf("[ERR] Unsupported advertising mode for instance %u\n", config.id);
			return false;
		}
		if (config.id >= CONFIG_BT_NIMBLE_MAX_EXT_ADV_INSTANCES)
		{
			Serial.printf("[ERR] Instance id %u is unavailable\n", config.id);
			return false;
		}
		InstanceConfig stored = config;
		if (stored.discoverable == DiscoverableMode::Limited && stored.restartAfterConnection)
		{
			Serial.printf("[ERR] Adv[%u] Limited discoverability cannot use restart after connection\n", stored.id);
			return false;
		}
		if (modeIsDirected(stored.mode))
		{
			stored.discoverable = DiscoverableMode::None;
			stored.restartAfterConnection = false;
		}
		{
			std::lock_guard<std::mutex> lk(configMutex_);
			if (configs_.find(config.id) != configs_.end())
			{
				Serial.printf("[ERR] Instance %u already exists\n", config.id);
				return false;
			}
			configs_[stored.id] = stored;
		}
		if (!modeIsDirected(stored.mode) || stored.directedTarget.has_value())
		{
			if (!ApplyConfig(stored))
			{
				std::lock_guard<std::mutex> lk(configMutex_);
				configs_.erase(stored.id);
				return false;
			}
		}
		Serial.printf("[BT] Added advertising instance %u\n", config.id);
		return true;
	}

	bool RemoveInstance(uint8_t instanceId)
	{
		if (instanceId == 0)
		{
			Serial.println("[ERR] Instance 0 cannot be removed");
			return false;
		}
		std::lock_guard<std::mutex> lk(configMutex_);
		if (directedActive_ && directedInstance_.has_value() && *directedInstance_ == instanceId)
		{
			Serial.println("[ERR] Directed instance cannot be removed while active");
			return false;
		}
		auto it = configs_.find(instanceId);
		if (it == configs_.end())
		{
			Serial.printf("[ERR] No such advertising instance: %u\n", instanceId);
			return false;
		}
		cancelRestartLocked(instanceId, "instance removed");
		cancelRestartTimer(instanceId);
		auto* adv = Adv();
		if (adv->isActive(instanceId) && !adv->stop(instanceId))
			return false;
		if (!adv->removeInstance(instanceId))
			return false;
		configs_.erase(it);
		Serial.printf("[BT] Removed advertising instance %u\n", instanceId);
		return true;
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
}
