// advertising.h
#pragma once
#include <NimBLEDevice.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <map>

enum class DiscoverableMode{ None, Limited, General };

namespace Advertising
{
	// BLE spec (Vol 3, Part C, 9.2.3): a Limited Discoverable device must stop advertising after a maximum of TGAP(lim_adv_timeout) = 180 s.
	inline constexpr uint32_t kLimitedDiscoverableTimeoutSec = 10;

	inline constexpr DiscoverableMode discModeToggle(DiscoverableMode mode) noexcept
	{
		// Cycles Limited <-> General only, per current console command scope.
		// `None` is defined for future use (e.g. explicit non-discoverable instances) but is intentionally not part of this toggle.
		switch (mode)
		{
			case DiscoverableMode::Limited:	return DiscoverableMode::General;
			default:						return DiscoverableMode::Limited;
		}
	}

	inline constexpr std::string_view discModeToString(DiscoverableMode mode) noexcept
	{
		switch (mode)
		{
			case DiscoverableMode::None:	return "None (non-discoverable)";
			case DiscoverableMode::Limited:	return "Limited";
			case DiscoverableMode::General:	return "General";
			default:						return "Unknown";
		}
	}

	// Full config for a single advertising instance. This is the single source of truth for what gets broadcast
	// - every live update rebuilds the complete advertisement (name, mfr data, flags, connectable) from this struct before
	// calling setInstanceData/applying to the legacy advertising object, rather than replacing it with a partially-populated object.
	struct InstanceConfig
	{
		uint8_t id = 0;
		std::string name;
		DiscoverableMode discoverable = DiscoverableMode::General;
		bool connectable = true;
		uint32_t intervalMs = 100;
		std::vector<uint8_t> manufacturerData;
		std::string serviceUuid; // empty = none
	};

	// One-time setup of instance 0 using the legacy or extended advertising object obtained from NimBLEDevice::getAdvertising().
	// Mirrors the existing SetupBluetooth() advertising block, but goes through the shared apply path.
	void Setup(const InstanceConfig& config);

	// --- Common (both legacy [instance 0 only] and extended adv) ---

	// Applies (or re-applies) the full config for an instance: rebuilds the advertisement from scratch and calls setInstanceData (ext)
	// the individual legacy setters, preserving all fields every time.
	bool ApplyConfig(const InstanceConfig& config);

	// Starts an instance. If its stored config has DiscoverableMode::Limited, this passes a timeout (kLimitedDiscoverableTimeoutSec) to NimBLE's own start()
	// duration parameter so the stack auto-stops it - no manual timer needed. For None/General, duration is 0 (advertise indefinitely until Stop()).
	bool Start(uint8_t instanceId);
	bool Stop(uint8_t instanceId);
	bool IsActive(uint8_t instanceId);

	// Get/set discoverable mode for an instance (updates stored config + re-applies live).
	DiscoverableMode Discoverable(uint8_t instanceId, std::optional<DiscoverableMode> mode = std::nullopt);

	// Get/set connectable flag for an instance (updates stored config + re-applies live).
	bool Connectable(uint8_t instanceId, std::optional<bool> connectable = std::nullopt);

	std::optional<InstanceConfig> GetConfig(uint8_t instanceId);

	// --- Extended advertising only (multi-instance) ---
#if CONFIG_BT_NIMBLE_EXT_ADV
	// Adds and applies a new advertising instance. Fails if the instance id already exists.
	bool AddInstance(const InstanceConfig& config);
	bool RemoveInstance(uint8_t instanceId);
	std::vector<uint8_t> ListInstances();
#endif

} // namespace Advertising
