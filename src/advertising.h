// advertising.h
#pragma once
#include <NimBLEDevice.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <map>
#include <array>
#include <cstddef>

enum class DiscoverableMode{ None, Limited, General };

namespace Advertising
{
	enum class AdvertisementMode : uint8_t
	{
		ConnectableUndirectedScannable,
		ConnectableUndirectedNonScannable,
		NonconnectableUndirectedScannable,
		NonconnectableUndirectedNonScannable,
		ConnectableDirectedNonScannable,
		NonconnectableDirectedScannable,
		NonconnectableDirectedNonScannable
	};

	struct AdvertisementModeProperties
	{
		bool connectable;
		bool directed;
		bool scannable;
		std::string_view name;
	};

	inline constexpr AdvertisementModeProperties modeProperties(AdvertisementMode mode) noexcept
	{
		switch (mode)
		{
			case AdvertisementMode::ConnectableUndirectedScannable: return { true,  false, true, "Connectable [X] | Directed [ ] | Scannable [X]" };
			case AdvertisementMode::ConnectableUndirectedNonScannable: return { true,  false, false, "Connectable [X] | Directed [ ] | Scannable [ ]" };
			case AdvertisementMode::NonconnectableUndirectedScannable: return { false, false, true, "Connectable [ ] | Directed [ ] | Scannable [X]" };
			case AdvertisementMode::NonconnectableUndirectedNonScannable: return { false, false, false, "Connectable [ ] | Directed [ ] | Scannable [ ]" };
			case AdvertisementMode::ConnectableDirectedNonScannable: return { true,  true,  false, "Connectable [X] | Directed [X] | Scannable [ ]" };
			case AdvertisementMode::NonconnectableDirectedScannable: return { false, true,  true, "Connectable [ ] | Directed [X] | Scannable [X]" };
			case AdvertisementMode::NonconnectableDirectedNonScannable: return { false, true,  false, "Connectable [ ] | Directed [X] | Scannable [ ]" };
			default: return { false, false, false, "Unsupported" };
		}
	}

	inline constexpr std::string_view modeToString(AdvertisementMode mode) noexcept
	{
		return modeProperties(mode).name;
	}

	inline constexpr bool modeIsDirected(AdvertisementMode mode) noexcept
	{
		return modeProperties(mode).directed;
	}

	#if CONFIG_BT_NIMBLE_EXT_ADV
	inline constexpr std::array<AdvertisementMode, 6> supportedModes{
		AdvertisementMode::ConnectableUndirectedNonScannable,
		AdvertisementMode::NonconnectableUndirectedScannable,
		AdvertisementMode::NonconnectableUndirectedNonScannable,
		AdvertisementMode::ConnectableDirectedNonScannable,
		AdvertisementMode::NonconnectableDirectedScannable,
		AdvertisementMode::NonconnectableDirectedNonScannable
	};
	#else
	inline constexpr std::array<AdvertisementMode, 4> supportedModes{
		AdvertisementMode::ConnectableUndirectedScannable,
		AdvertisementMode::NonconnectableUndirectedScannable,
		AdvertisementMode::NonconnectableUndirectedNonScannable,
		AdvertisementMode::ConnectableDirectedNonScannable
	};
	#endif

	inline constexpr bool modeIsSupported(AdvertisementMode mode) noexcept
	{
		for (auto supported : supportedModes)
		{
			if (supported == mode)
				return true;
		}
		return false;
	}

	inline constexpr AdvertisementMode undirectedMode(AdvertisementMode mode) noexcept
	{
		switch (mode)
		{
			case AdvertisementMode::ConnectableDirectedNonScannable:
				#if CONFIG_BT_NIMBLE_EXT_ADV
				return AdvertisementMode::ConnectableUndirectedNonScannable;
				#else
				return AdvertisementMode::ConnectableUndirectedScannable;
				#endif
			case AdvertisementMode::NonconnectableDirectedScannable:
				return AdvertisementMode::NonconnectableUndirectedScannable;
			case AdvertisementMode::NonconnectableDirectedNonScannable:
				return AdvertisementMode::NonconnectableUndirectedNonScannable;
			default:
				return mode;
		}
	}

	inline constexpr AdvertisementMode defaultMode() noexcept
	{
#if CONFIG_BT_NIMBLE_EXT_ADV
		return AdvertisementMode::ConnectableUndirectedNonScannable;
#else
		return AdvertisementMode::ConnectableUndirectedScannable;
#endif
	}

	inline constexpr AdvertisementMode nextMode(AdvertisementMode mode) noexcept
	{
		for (size_t i = 0; i < supportedModes.size(); ++i)
		{
			if (supportedModes[i] == mode)
				return supportedModes[(i + 1) % supportedModes.size()];
		}
		return supportedModes[0];
	}

	// BLE spec (Vol 3, Part C, 9.2.3): a Limited Discoverable device must stop advertising after a maximum of TGAP(lim_adv_timeout) = 180 s.
	inline constexpr uint32_t kLimitedDiscoverableTimeoutSec = 10;

	inline constexpr DiscoverableMode discModeToggle(DiscoverableMode mode) noexcept
	{
		switch (mode)
		{
			case DiscoverableMode::None:	return DiscoverableMode::Limited;
			case DiscoverableMode::Limited:	return DiscoverableMode::General;
			default:						return DiscoverableMode::None;
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
		AdvertisementMode mode = defaultMode();
		// Restart this ordinary instance five seconds after NimBLE reports a connection stop.
		bool restartAfterConnection = true;
		std::optional<NimBLEAddress> directedTarget;
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
	bool CycleMode(uint8_t instanceId);
	bool SetDirectedTarget(uint8_t instanceId, const NimBLEAddress& target);
	std::optional<NimBLEAddress> GetDirectedTarget(uint8_t instanceId);
	bool RestartAfterConnection(uint8_t instanceId, std::optional<bool> enable = std::nullopt);
	void ClearDirectedTargets();
	bool IsDirectedActive();
	std::optional<NimBLEAddress> DirectedTarget();

	// Lifecycle notifications from the NimBLE callbacks. Pairing/authentication is intentionally
	// not part of the advertising restart boundary.
	void OnConnectionEstablished();
#if CONFIG_BT_NIMBLE_EXT_ADV
	void OnAdvertisingStopped(uint8_t instanceId, int reason);
#endif
	void CancelRestartAfterConnection(uint8_t instanceId);
	void CancelAllRestartAfterConnection();

	// Get/set discoverable mode for an instance (updates stored config + re-applies live).
	DiscoverableMode Discoverable(uint8_t instanceId, std::optional<DiscoverableMode> mode = std::nullopt);
	bool CycleDiscoverability(uint8_t instanceId);

	std::optional<InstanceConfig> GetConfig(uint8_t instanceId);

	// --- Extended advertising only (multi-instance) ---
#if CONFIG_BT_NIMBLE_EXT_ADV
	// Adds and applies a new advertising instance. Fails if the instance id already exists.
	bool AddInstance(const InstanceConfig& config);
	bool RemoveInstance(uint8_t instanceId);
	std::vector<uint8_t> ListInstances();
#endif

} // namespace Advertising
