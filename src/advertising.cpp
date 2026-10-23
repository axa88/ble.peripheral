// advertising.cpp
#include "advertising.h"
#include <Arduino.h>
#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <cstdint>
#include <utility>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>

namespace
{
	std::mutex configMutex_;
	std::map<uint8_t, Advertising::InstanceConfig> configs_;
	bool directedActive_ = false;
	std::optional<uint8_t> directedInstance_;
	std::mutex restartMutex_;
	std::map<uint8_t, TimerHandle_t> restartTimers_;
	std::map<uint8_t, uint32_t> restartTimerGenerations_;
	std::set<uint8_t> pendingRestartInstances_;
	std::map<uint8_t, uint32_t> restartGenerations_;
	constexpr uint32_t kRestartDelayMs = 5000;
#if CONFIG_BT_NIMBLE_EXT_ADV
	// Explicit extended-advertising stop completion is the successful NimBLE HCI
	// disable return plus a verified inactive state. NimBLE-Arduino does not
	// emit ADV_COMPLETE for that explicit stop path; callbacks remain useful for
	// spontaneous completion notifications but never complete this generation.
	std::map<uint8_t, uint32_t> stopRequestGenerations_;
	std::map<uint8_t, uint32_t> stopCompletionGenerations_;
	uint32_t nextStopGeneration_ = 0;
	constexpr uint32_t kStopCompletionTimeoutMs = 5000;
#endif

#if !CONFIG_BT_NIMBLE_EXT_ADV
	NimBLEAdvertising* Adv() { return NimBLEDevice::getAdvertising(); }
	std::optional<Advertising::InstanceConfig> legacyOrdinaryCache_;
#else
	NimBLEExtAdvertising* Adv() { return NimBLEDevice::getAdvertising(); }
#endif

#if CONFIG_BT_NIMBLE_EXT_ADV
	uint32_t beginStopRequestLocked(uint8_t instanceId)
	{
		uint32_t generation = ++nextStopGeneration_;
		if (generation == 0)
			generation = ++nextStopGeneration_;
		stopRequestGenerations_[instanceId] = generation;
		Serial.printf("[BT] Advertising stop requested | Adv[%u] | generation:%u\n", instanceId, generation);
		return generation;
	}

	bool stopCompletionObservedLocked(uint8_t instanceId, uint32_t generation)
	{
		auto it = stopCompletionGenerations_.find(instanceId);
		return it != stopCompletionGenerations_.end() && it->second == generation;
	}

	bool publishExplicitStopCompletionLocked(uint8_t instanceId, uint32_t generation, bool stopSucceeded, bool inactive)
	{
		auto request = stopRequestGenerations_.find(instanceId);
		if (request == stopRequestGenerations_.end() || request->second != generation)
			return false;

		if (!stopSucceeded || !inactive)
		{
			stopRequestGenerations_.erase(request);
			return false;
		}

		stopCompletionGenerations_[instanceId] = generation;
		stopRequestGenerations_.erase(request);
		return true;
	}

	bool waitForStopCompletion(uint8_t instanceId)
	{
		uint32_t generation = 0;
		{
			std::lock_guard<std::mutex> lk(configMutex_);
			auto request = stopRequestGenerations_.find(instanceId);
			if (request == stopRequestGenerations_.end())
				return true;
			generation = request->second;
		}

		TimeOut_t timeoutState;
		TickType_t ticksToWait = pdMS_TO_TICKS(kStopCompletionTimeoutMs);
		vTaskSetTimeOutState(&timeoutState);
		for (;;)
		{
			{
				std::lock_guard<std::mutex> lk(configMutex_);
				auto request = stopRequestGenerations_.find(instanceId);
				if (request == stopRequestGenerations_.end())
					return true;
				generation = request->second;
				if (stopCompletionObservedLocked(instanceId, generation))
					return true;
			}

			if (xTaskCheckForTimeOut(&timeoutState, &ticksToWait) != pdFALSE)
			{
				Serial.printf("[ERR] Advertising stop completion wait timeout | Adv[%u] | generation:%u | timeout:%ums\n",
					instanceId, generation, kStopCompletionTimeoutMs);
				return false;
			}
			vTaskDelay(std::min(ticksToWait, pdMS_TO_TICKS(10)));
		}
	}
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

#if CONFIG_BT_NIMBLE_EXT_ADV
		auto pendingStop = stopRequestGenerations_.find(instanceId);
		if (pendingStop != stopRequestGenerations_.end() &&
			!stopCompletionObservedLocked(instanceId, pendingStop->second))
		{
			Serial.printf("[ERR] Advertising start blocked by pending stop completion | Adv[%u] | generation:%u\n",
				instanceId, pendingStop->second);
			return false;
		}
#endif

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

	bool makeModeTransitionCandidateLocked(
		const Advertising::InstanceConfig& current,
		Advertising::AdvertisementMode requestedMode,
		Advertising::InstanceConfig& candidate)
	{
		if (!Advertising::modeIsSupported(requestedMode))
			return false;

		candidate = current;
		const bool wasDirected = Advertising::modeIsDirected(current.mode);
		const bool willBeDirected = Advertising::modeIsDirected(requestedMode);

#if !CONFIG_BT_NIMBLE_EXT_ADV
		if (wasDirected && !willBeDirected && legacyOrdinaryCache_.has_value())
		{
			candidate = *legacyOrdinaryCache_;
			candidate.restartAfterConnection = false;
		}
		else if (!wasDirected && willBeDirected)
			candidate.restartAfterConnection = false;
#else
		if (!wasDirected && willBeDirected)
		{
			candidate.discoverable = DiscoverableMode::None;
			candidate.restartAfterConnection = false;
		}
		else if (wasDirected && !willBeDirected)
			candidate.discoverable = DiscoverableMode::None;
#endif

		candidate.mode = requestedMode;
		return discoverabilityAllowed(candidate.mode, candidate.discoverable);
	}

	bool setModeLocked(uint8_t instanceId, Advertising::AdvertisementMode requestedMode)
	{
		auto it = configs_.find(instanceId);
		if (it == configs_.end())
		{
			Serial.printf("[ERR] No such advertising instance: %u\n", instanceId);
			return false;
		}
		if (!Advertising::modeIsSupported(it->second.mode))
		{
			Serial.printf("[ERR] Adv[%u] has an unsupported advertising mode\n", instanceId);
			return false;
		}
		if (!Advertising::modeIsSupported(requestedMode))
		{
			Serial.printf("[ERR] Adv[%u] requested an unsupported advertising mode\n", instanceId);
			return false;
		}
		if (Advertising::IsActive(instanceId))
		{
			Serial.printf("[ERR] Adv[%u] mode change rejected because advertising is active\n", instanceId);
			return false;
		}
		if (it->second.mode == requestedMode)
			return true;

		Advertising::InstanceConfig candidate;
		if (!makeModeTransitionCandidateLocked(it->second, requestedMode, candidate))
		{
			Serial.printf("[ERR] Adv[%u] mode is incompatible with its discoverability value\n", instanceId);
			return false;
		}

		const bool wasDirected = Advertising::modeIsDirected(it->second.mode);
		const bool willBeDirected = Advertising::modeIsDirected(requestedMode);
		cancelRestartLocked(instanceId, "mode changed");
		cancelRestartTimer(instanceId);
#if !CONFIG_BT_NIMBLE_EXT_ADV
		if (!wasDirected && willBeDirected)
			legacyOrdinaryCache_ = it->second;
		else if (wasDirected && !willBeDirected)
			legacyOrdinaryCache_.reset();
#endif
		it->second = std::move(candidate);
		return true;
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
	#if CONFIG_BT_NIMBLE_EXT_ADV
		// A manual E command can follow R while the explicit stop call is still
		// publishing its HCI-return/inactive boundary. Wait outside configMutex_
		// so Stop can close the generation, then let startInstanceLocked reject
		// any new race that is still pending.
		const bool stopReady = waitForStopCompletion(instanceId);
		if (!stopReady)
			return false;
	#endif
		std::lock_guard<std::mutex> lk(configMutex_);
		cancelRestartLocked(instanceId, "manual start");
		cancelRestartTimer(instanceId);
	#if CONFIG_BT_NIMBLE_EXT_ADV
		// A completed explicit stop belongs to the preceding R command. Do not
		// let its retained generation suppress a later connection callback.
		stopCompletionGenerations_.erase(instanceId);
	#endif
		return startInstanceLocked(instanceId);
	}

	StartAllResult StartAll()
	{
		std::vector<uint8_t> instanceIds;
		{
			std::lock_guard<std::mutex> lk(configMutex_);
			instanceIds.reserve(configs_.size());
			for (const auto& [id, config] : configs_)
			{
				(void)config;
				instanceIds.push_back(id);
			}
		}

		std::vector<uint8_t> stopReadyIds;
		stopReadyIds.reserve(instanceIds.size());
#if CONFIG_BT_NIMBLE_EXT_ADV
		for (uint8_t instanceId : instanceIds)
		{
			if (waitForStopCompletion(instanceId))
				stopReadyIds.push_back(instanceId);
		}
#else
		stopReadyIds = instanceIds;
#endif

		StartAllResult result;
		std::vector<uint8_t> startAccepted;
		startAccepted.reserve(instanceIds.size());
		std::lock_guard<std::mutex> lk(configMutex_);

		bool hasDirectedMode = false;
		for (const auto& [id, config] : configs_)
		{
			(void)id;
			hasDirectedMode = hasDirectedMode || modeIsDirected(config.mode);
		}
		if (configs_.size() > 1 && hasDirectedMode)
		{
			result.failed = static_cast<unsigned>(configs_.size());
			result.blockedByDirectedMode = true;
			Serial.println("[ERR] Start all rejected because directed advertising is exclusive across configured instances");
			return result;
		}

		for (const auto& [instanceId, config] : configs_)
		{
			(void)config;
			if (std::find(instanceIds.begin(), instanceIds.end(), instanceId) == instanceIds.end() ||
				std::find(stopReadyIds.begin(), stopReadyIds.end(), instanceId) == stopReadyIds.end())
			{
				++result.failed;
				continue;
			}

#if CONFIG_BT_NIMBLE_EXT_ADV
			auto pendingStop = stopRequestGenerations_.find(instanceId);
			if (pendingStop != stopRequestGenerations_.end() &&
				!stopCompletionObservedLocked(instanceId, pendingStop->second))
			{
				Serial.printf("[ERR] Advertising start blocked by pending stop completion | Adv[%u] | generation:%u\n",
					instanceId, pendingStop->second);
				++result.failed;
				continue;
			}
#endif

			cancelRestartLocked(instanceId, "manual start");
			cancelRestartTimer(instanceId);
#if CONFIG_BT_NIMBLE_EXT_ADV
			// Match Start(): a completed stop belongs to the preceding request.
			stopCompletionGenerations_.erase(instanceId);
#endif
			if (Advertising::IsActive(instanceId) || startInstanceLocked(instanceId))
				startAccepted.push_back(instanceId);
			else
				++result.failed;
		}

		for (uint8_t instanceId : startAccepted)
		{
			if (Advertising::IsActive(instanceId))
				++result.started;
			else
				++result.failed;
		}
		return result;
	}

	bool Stop(uint8_t instanceId)
	{
		auto* adv = Adv();
		if (!adv)
			return false;
		bool wasActive = false;
		uint32_t stopGeneration = 0;
		{
			std::lock_guard<std::mutex> lk(configMutex_);
			cancelRestartLocked(instanceId, "manual stop");
			cancelRestartTimer(instanceId);
#if CONFIG_BT_NIMBLE_EXT_ADV
			wasActive = adv->isActive(instanceId);
			if (wasActive)
				stopGeneration = beginStopRequestLocked(instanceId);
#else
			wasActive = adv->isAdvertising();
#endif
		}

		bool stopSucceeded = false;
#if CONFIG_BT_NIMBLE_EXT_ADV
		stopSucceeded = !wasActive || adv->stop(instanceId);
#else
		stopSucceeded = !wasActive || adv->stop();
#endif
		bool ok = stopSucceeded;
#if CONFIG_BT_NIMBLE_EXT_ADV
		const bool inactive = !adv->isActive(instanceId);
		bool stopCompletionPublished = stopGeneration == 0 && inactive;
		if (stopGeneration != 0)
		{
			{
				std::lock_guard<std::mutex> lk(configMutex_);
				stopCompletionPublished = publishExplicitStopCompletionLocked(instanceId, stopGeneration, stopSucceeded, inactive);
			}
			if (stopCompletionPublished)
			{
				Serial.printf("[BT] Advertising stop complete | Adv[%u] | generation:%u | api:1 | active:0 | source:stop-return\n",
					instanceId, stopGeneration);
			}
			else
			{
				Serial.printf("[ERR] Advertising stop completion failed | Adv[%u] | generation:%u | api:%u | active:%u\n",
					instanceId, stopGeneration, stopSucceeded ? 1u : 0u, inactive ? 0u : 1u);
			}
		}
		ok = stopSucceeded && inactive && stopCompletionPublished;
#else
		const bool inactive = !adv->isAdvertising();
		ok = stopSucceeded && inactive;
#endif

		{
			std::lock_guard<std::mutex> lk(configMutex_);
			if (inactive && directedActive_ && directedInstance_.has_value() && *directedInstance_ == instanceId)
			{
#if !CONFIG_BT_NIMBLE_EXT_ADV
				restoreLegacyOrdinaryLocked();
#else
				clearDirectedStateLocked();
#endif
			}
		}
		Serial.printf(ok ? "[BT] Advertising stopped | Adv[%u]\n" : "[ERR] Failed to stop advertising | Adv[%u]\n", instanceId);
		return ok;
	}

	StopAllResult StopAll()
	{
		CancelAllRestartAfterConnection();

		std::vector<uint8_t> instanceIds;
		{
			std::lock_guard<std::mutex> lk(configMutex_);
			instanceIds.reserve(configs_.size());
			for (const auto& [id, config] : configs_)
			{
				(void)config;
				instanceIds.push_back(id);
			}
		}

		StopAllResult result;
		for (uint8_t instanceId : instanceIds)
		{
			if (!IsActive(instanceId))
				continue;
			if (Stop(instanceId))
				++result.stopped;
			else
				++result.failed;
		}
		return result;
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

	bool TrySnapshot(std::vector<InstanceConfig>& out)
	{
		std::unique_lock<std::mutex> lk(configMutex_, std::try_to_lock);
		if (!lk.owns_lock())
			return false;

		std::vector<InstanceConfig> snapshot;
		snapshot.reserve(configs_.size());
		for (const auto& [id, config] : configs_)
		{
			(void)id;
			snapshot.push_back(config);
		}
		out = std::move(snapshot);
		return true;
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

		AdvertisementMode next = nextMode(it->second.mode);
		for (size_t i = 0; i < supportedModes.size(); ++i)
		{
			InstanceConfig candidate;
			if (makeModeTransitionCandidateLocked(it->second, next, candidate))
				return setModeLocked(instanceId, next);
			next = nextMode(next);
		}
		Serial.printf("[ERR] Adv[%u] has no compatible supported mode for its discoverability value\n", instanceId);
		return false;
	}

	bool SetMode(uint8_t instanceId, AdvertisementMode mode)
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		return setModeLocked(instanceId, mode);
	}

	bool SetInterval(uint8_t instanceId, uint32_t intervalMs)
	{
		if (intervalMs < 20 || intervalMs > 10240)
		{
			Serial.printf("[ERR] Adv[%u] interval must be from 20 to 10240 ms\n", instanceId);
			return false;
		}

		std::lock_guard<std::mutex> lk(configMutex_);
		auto it = configs_.find(instanceId);
		if (it == configs_.end())
		{
			Serial.printf("[ERR] No such advertising instance: %u\n", instanceId);
			return false;
		}

		cancelRestartLocked(instanceId, "interval changed");
		cancelRestartTimer(instanceId);
		InstanceConfig previous = it->second;
		InstanceConfig updated = previous;
		updated.intervalMs = intervalMs;
		const auto props = modeProperties(updated.mode);
#if !CONFIG_BT_NIMBLE_EXT_ADV
		if (props.directed)
		{
			it->second.intervalMs = intervalMs;
			return true;
		}
#endif
		if (props.directed && !updated.directedTarget.has_value())
		{
			it->second.intervalMs = intervalMs;
			return true;
		}

		it->second = updated;
		if (!rebuildAdvertisement(it->second, true))
		{
			it->second = std::move(previous);
			Serial.printf("[ERR] Adv[%u] interval update could not be applied\n", instanceId);
			return false;
		}
		return true;
	}

	bool SetDirectedTarget(uint8_t instanceId, const NimBLEAddress& target)
	{
		return TrySetDirectedTarget(instanceId, target) == DirectedTargetUpdateResult::Applied;
	}

	DirectedTargetUpdateResult TrySetDirectedTarget(uint8_t instanceId, const NimBLEAddress& target)
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		auto it = configs_.find(instanceId);
		if (it == configs_.end())
			return DirectedTargetUpdateResult::InstanceNotFound;
		if (target.isNull())
			return DirectedTargetUpdateResult::InvalidTarget;
		// Validate under the same lock as assignment. A concurrent shared bond
		// deletion always reconciles targets after changing the native store.
		if (!isBonded(target))
			return DirectedTargetUpdateResult::TargetNotBonded;
		if (IsActive(instanceId))
		{
			Serial.printf("[ERR] Adv[%u] target cannot change while advertising is active\n", instanceId);
			return DirectedTargetUpdateResult::AdvertisingActive;
		}
		cancelRestartLocked(instanceId, "directed target changed");
		cancelRestartTimer(instanceId);
		it->second.directedTarget = target;
		return DirectedTargetUpdateResult::Applied;
	}

	bool RestartAfterConnection(uint8_t instanceId, std::optional<bool> enable)
	{
		if (!enable.has_value())
		{
			std::lock_guard<std::mutex> lk(configMutex_);
			auto it = configs_.find(instanceId);
			return it != configs_.end() && it->second.restartAfterConnection;
		}

		if (SetRestartAfterConnection(instanceId, *enable))
			return *enable;
		return RestartAfterConnection(instanceId, std::nullopt);
	}

	bool SetRestartAfterConnection(uint8_t instanceId, bool enable)
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		auto it = configs_.find(instanceId);
		if (it == configs_.end())
		{
			Serial.printf("[ERR] No such advertising instance: %u\n", instanceId);
			return false;
		}
		if (enable && it->second.discoverable == DiscoverableMode::Limited)
		{
			Serial.printf("[ERR] Adv[%u] restart after connection is unavailable with Limited discoverability\n", instanceId);
			return false;
		}
		if (enable && modeIsDirected(it->second.mode))
		{
			Serial.printf("[ERR] Adv[%u] Directed advertising does not restart after connection\n", instanceId);
			return false;
		}
		if (!enable)
		{
			cancelRestartLocked(instanceId, "policy disabled");
			cancelRestartTimer(instanceId);
		}
		it->second.restartAfterConnection = enable;
		return true;
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

	bool ReconcileDirectedTargets()
	{
		std::set<uint8_t> affectedInstances;
		std::set<uint8_t> activeInstancesToStop;
		{
			std::lock_guard<std::mutex> lk(configMutex_);
			for (auto& [id, cfg] : configs_)
			{
				if (cfg.directedTarget.has_value() && !isBonded(*cfg.directedTarget))
				{
					cfg.directedTarget.reset();
					if (modeIsDirected(cfg.mode))
					{
						cancelRestartLocked(id, "directed target no longer bonded");
						affectedInstances.insert(id);
					}
				}
			}
#if !CONFIG_BT_NIMBLE_EXT_ADV
			if (legacyOrdinaryCache_.has_value() && legacyOrdinaryCache_->directedTarget.has_value() &&
				!isBonded(*legacyOrdinaryCache_->directedTarget))
				legacyOrdinaryCache_->directedTarget.reset();
#endif
			if (directedActive_ && directedInstance_.has_value())
			{
				const uint8_t instanceId = *directedInstance_;
				const auto config = configs_.find(instanceId);
				if (config == configs_.end() || !config->second.directedTarget.has_value() ||
					!isBonded(*config->second.directedTarget))
				{
					affectedInstances.insert(instanceId);
					activeInstancesToStop.insert(instanceId);
					cancelRestartLocked(instanceId, "directed target no longer bonded");
				}
			}
			for (const uint8_t instanceId : affectedInstances)
			{
				const auto config = configs_.find(instanceId);
				if (config != configs_.end() && modeIsDirected(config->second.mode) && Advertising::IsActive(instanceId))
					activeInstancesToStop.insert(instanceId);
			}
		}

		for (const uint8_t instanceId : affectedInstances)
			cancelRestartTimer(instanceId);

		bool success = true;
		for (const uint8_t instanceId : activeInstancesToStop)
		{
			if (!Stop(instanceId))
				success = false;
		}
		return success;
	}

	bool IsDirectedActive()
	{
		std::lock_guard<std::mutex> lk(configMutex_);
		return directedActive_;
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
		bool activeAfterCallback = false;
		bool explicitStopPending = false;
		{
			std::lock_guard<std::mutex> lk(configMutex_);
			activeAfterCallback = Adv() != nullptr && Adv()->isActive(instanceId);
			explicitStopPending = stopRequestGenerations_.find(instanceId) != stopRequestGenerations_.end();
			if (directedActive_ && directedInstance_.has_value() && *directedInstance_ == instanceId)
			{
				if (reason != BLE_HS_EDONE && !activeAfterCallback)
					clearDirectedStateLocked();
			}
			else if (!explicitStopPending && !directedActive_ && (reason == 0 || reason == BLE_HS_EPREEMPTED))
			{
				shouldSchedule = markRestartPendingLocked(instanceId,
					reason == 0 ? "connection" : "host preemption");
			}
		}
		Serial.printf("[BT] Advertising stop complete | Adv[%u] | generation:0 | reason:0x%x | active:%u | source:adv-complete\n",
			instanceId, reason, activeAfterCallback ? 1u : 0u);
		if (shouldSchedule)
			scheduleRestartTimer(instanceId);
	}
	#endif

	DiscoverableMode Discoverable(uint8_t instanceId, std::optional<DiscoverableMode> mode)
	{
		if (mode.has_value())
			SetDiscoverability(instanceId, *mode);

		std::lock_guard<std::mutex> lk(configMutex_);
		auto it = configs_.find(instanceId);
		return it == configs_.end() ? DiscoverableMode::General : it->second.discoverable;
	}

	bool SetDiscoverability(uint8_t instanceId, DiscoverableMode mode)
	{
		if (mode != DiscoverableMode::None && mode != DiscoverableMode::Limited && mode != DiscoverableMode::General)
		{
			Serial.printf("[ERR] Adv[%u] requested an unsupported discoverability value\n", instanceId);
			return false;
		}

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

		InstanceConfig candidate = it->second;
#if CONFIG_BT_NIMBLE_EXT_ADV
		if (modeIsDirected(candidate.mode) && mode != DiscoverableMode::None)
			candidate.mode = undirectedMode(candidate.mode);
#else
		if (modeIsDirected(candidate.mode))
		{
			Serial.printf("[ERR] Adv[%u] Legacy Directed mode uses its cached discoverability\n", instanceId);
			return false;
		}
#endif
		if (!discoverabilityAllowed(candidate.mode, mode))
		{
			Serial.printf("[ERR] Adv[%u] discoverability is incompatible with the selected advertising mode\n", instanceId);
			return false;
		}

		candidate.discoverable = mode;
		if (mode == DiscoverableMode::Limited)
			candidate.restartAfterConnection = false;
		if (!modeIsDirected(candidate.mode) && !rebuildAdvertisement(candidate, false))
		{
			Serial.printf("[ERR] Adv[%u] discoverability update could not be applied\n", instanceId);
			return false;
		}

		cancelRestartLocked(instanceId, "discoverability changed");
		cancelRestartTimer(instanceId);
		it->second = std::move(candidate);
		return true;
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
	bool AddDefaultInstance(uint8_t instanceId, std::string_view name)
	{
		if (name.empty() || name.find('\0') != std::string_view::npos)
		{
			Serial.println("[ERR] Advertising instance name must be non-empty and contain no NUL characters");
			return false;
		}

		InstanceConfig config;
		config.id = instanceId;
		config.name.assign(name.data(), name.size());
		config.discoverable = DiscoverableMode::General;
		config.mode = defaultMode();
		config.intervalMs = 100;
		return AddInstance(config);
	}

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
