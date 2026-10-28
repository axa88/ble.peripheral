#include "startup_support.h"

namespace FirmwareIdentity
{
	uint32_t revision() { return 7; }
	const char* sourceId() { return "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"; }
	const char* environment() { return "native-startup"; }
	const char* summary() { return "Firmware revision 7 | source 0123456789ab | environment native-startup"; }
}

namespace
{
	NimBLEServer server;
#if CONFIG_BT_NIMBLE_EXT_ADV
	NimBLEExtAdvertising extendedAdvertising;
#else
	NimBLEAdvertising legacyAdvertising;
#endif
	uint32_t randomValue = 0x10203040;

	struct AggregateSnapshot
	{
		uint16_t temperatureCpf = 0;
		uint16_t aggregateCpf = 0;
		uint16_t aggregateHandle = 0;
		std::vector<uint8_t> aggregateBytes;
	};

	bool captureAggregateSnapshot(AggregateSnapshot& snapshot)
	{
		auto* service = server.getServiceByUUID(FixtureGatt::DescriptorServiceUuid);
		if (!service) return false;
		auto* temperature = service->getCharacteristic(FixtureGatt::TemperatureUuid);
		auto* aggregateValue = service->getCharacteristic("12345678-1234-5678-1234-56789abcdf06");
		if (!temperature || !aggregateValue) return false;
		auto* temperatureCpf = temperature->getDescriptorByUUID(NimBLEUUID(uint16_t{0x2904}));
		auto* aggregateCpf = aggregateValue->getDescriptorByUUID(NimBLEUUID(uint16_t{0x2904}));
		auto* aggregate = aggregateValue->getDescriptorByUUID(NimBLEUUID(uint16_t{0x2905}));
		if (!temperatureCpf || !aggregateCpf || !aggregate) return false;
		snapshot.temperatureCpf = temperatureCpf->getHandle();
		snapshot.aggregateCpf = aggregateCpf->getHandle();
		snapshot.aggregateHandle = aggregate->getHandle();
		const auto value = aggregate->getValue();
		snapshot.aggregateBytes.assign(value.begin(), value.end());
		if (!snapshot.temperatureCpf || !snapshot.aggregateCpf || !snapshot.aggregateHandle || snapshot.aggregateBytes.size() != 4)
			return false;
		const uint16_t encodedTemperature = static_cast<uint16_t>(snapshot.aggregateBytes[0]) |
			static_cast<uint16_t>(snapshot.aggregateBytes[1]) << 8;
		const uint16_t encodedAggregate = static_cast<uint16_t>(snapshot.aggregateBytes[2]) |
			static_cast<uint16_t>(snapshot.aggregateBytes[3]) << 8;
		return encodedTemperature == snapshot.temperatureCpf && encodedAggregate == snapshot.aggregateCpf;
	}

	bool sameAggregateSnapshot(const AggregateSnapshot& left, const AggregateSnapshot& right)
	{
		return left.temperatureCpf == right.temperatureCpf && left.aggregateCpf == right.aggregateCpf &&
			left.aggregateHandle == right.aggregateHandle && left.aggregateBytes == right.aggregateBytes;
	}
}

namespace StartupTest
{
	State& state()
	{
		static State value;
		return value;
	}

	void reset()
	{
		state() = State{};
		NimBLETest::reset();
	}
}

namespace PairingCoordinator
{
	bool begin(uint64_t bootId, Callbacks callbacks)
	{
		++StartupTest::state().pairingBeginCalls;
		return bootId != 0 && static_cast<bool>(callbacks.injectPasskey) &&
			static_cast<bool>(callbacks.injectConfirmation) && static_cast<bool>(callbacks.disconnect);
	}
}

BluetoothManager& BluetoothManager::Instance()
{
	static BluetoothManager manager;
	return manager;
}

void NimBLEDevice::init(const char* mode)
{
	auto& test = StartupTest::state();
	++test.initCalls;
	test.initializedMode = mode ? mode : "";
}

NimBLEAddress NimBLEDevice::getAddress()
{
	return NimBLEAddress{};
}

NimBLEServer* NimBLEDevice::createServer()
{
	auto& test = StartupTest::state();
	++test.createServerCalls;
	test.serverStartsAtCreateServer = NimBLETest::controls().counters.serverStarts;
	test.configVisibleAtCreateServer = Advertising::GetConfig(0).has_value();
	return &server;
}

NimBLEServer* NimBLEDevice::getServer()
{
	return &server;
}

#if CONFIG_BT_NIMBLE_EXT_ADV
NimBLEExtAdvertising* NimBLEDevice::getAdvertising()
{
	return &extendedAdvertising;
}
#else
NimBLEAdvertising* NimBLEDevice::getAdvertising()
{
	return &legacyAdvertising;
}
#endif

bool NimBLEAdvertising::start()
{
	++StartupTest::state().radioStartCalls;
	return FixtureGatt::Ready();
}

bool NimBLEExtAdvertising::setInstanceData(uint8_t, const NimBLEExtAdvertisement&)
{
	++StartupTest::state().extendedDataCalls;
	// The installed extended-advertising API starts the host server as part of
	// setInstanceData(). Model that side effect so readiness-gate regressions fail.
	return NimBLEDevice::getServer()->start();
}

bool NimBLEExtAdvertising::start(uint8_t)
{
	++StartupTest::state().radioStartCalls;
	return FixtureGatt::Ready();
}

namespace Advertising
{
	bool Start(uint8_t instanceId)
	{
		auto& test = StartupTest::state();
		++test.startCalls;
		test.startSawReady = FixtureGatt::Ready();
		test.startConfig = GetConfig(instanceId);
		test.hasStartConfig = test.startConfig.has_value();
		if (!test.startSawReady || !test.startConfig)
			return false;
		AggregateSnapshot before;
		if (!captureAggregateSnapshot(before))
			return false;
		++test.postReadyRebuildCalls;
		if (!StartupTest::tryProductionRebuild(*test.startConfig))
			return false;
		AggregateSnapshot after;
		test.aggregateStableAfterRebuild = captureAggregateSnapshot(after) && sameAggregateSnapshot(before, after);
		if (!test.aggregateStableAfterRebuild)
			return false;
#if CONFIG_BT_NIMBLE_EXT_ADV
		return NimBLEDevice::getAdvertising()->start(instanceId);
#else
		return NimBLEDevice::getAdvertising()->start();
#endif
	}
}

uint32_t esp_random()
{
	randomValue += 0x01010101;
	return randomValue;
}
