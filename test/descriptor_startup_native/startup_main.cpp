#include "startup_support.h"

#include <cstdio>
#include <string>

namespace
{
	bool require(bool condition, const char* message)
	{
		if (!condition)
			std::fprintf(stderr, "FAIL %s\n", message);
		return condition;
	}

	bool checkConfig(const Advertising::InstanceConfig& config, const std::string& expectedName)
	{
		const std::vector<uint8_t> manufacturer{0xFF, 0xFF, 'B', 'e', 'B', 'o'};
		return config.id == 0 && config.name == expectedName &&
			config.discoverable == DiscoverableMode::General &&
			config.mode == Advertising::defaultMode() && config.intervalMs == 100 &&
			config.restartAfterConnection && !config.directedTarget &&
			config.serviceUuid == FixtureGatt::ServiceUuid && config.manufacturerData == manufacturer;
	}

	bool checkService(bool expectRegistered)
	{
		auto* server = NimBLEDevice::getServer();
		if (!server || server->servicesForTest().size() != 2) return false;
		auto* service = server->getServiceByUUID(FixtureGatt::ServiceUuid);
		if (!service || service->characteristicsForTest().size() != 3) return false;
		auto* original = service->getCharacteristic(FixtureGatt::ExistingCharacteristicUuid);
		if (!original || (original->getHandle() != 0) != expectRegistered) return false;
		const auto value = original->getValue();
		return std::string(reinterpret_cast<const char*>(value.data()), value.size()) == FixtureGatt::ExistingCharacteristicValue;
	}
}

int main(int argc, char** argv)
{
	if (argc != 2) return 2;
	const std::string scenario = argv[1];
	if (scenario != "success" && scenario != "registration-failure" && scenario != "finalization-failure") return 2;

	StartupTest::reset();
	auto& controls = NimBLETest::controls();
	if (scenario == "registration-failure") controls.registrationSucceeds = false;
	if (scenario == "finalization-failure") controls.postRegistrationFault = NimBLETest::Fault::DescriptionWritePermission;

	Advertising::InstanceConfig probe;
	probe.id = 0;
	if (!require(!FixtureGatt::Ready(), "fixture starts not ready")) return 1;
	if (!require(!StartupTest::tryProductionRebuild(probe), "production rebuild refuses pre-ready configuration")) return 1;
	if (!require(StartupTest::state().legacyRebuildCalls == 0 && StartupTest::state().extendedDataCalls == 0 &&
		NimBLETest::controls().counters.serverStarts == 0, "pre-ready rebuild causes no radio apply or implicit server registration")) return 1;

	auto& manager = BluetoothManager::Instance();
	manager.SetupBluetooth();

	const bool succeeds = scenario == "success";
	const bool registered = scenario != "registration-failure";
	const char* expectedMode = CONFIG_BT_NIMBLE_EXT_ADV ? "Modern" : "Legacy";
	const std::string expectedName = std::string(expectedMode) + "-0201";
	if (!require(manager.initialized_ && manager.deviceName_ == expectedName, "production startup initializes the expected device name")) return 1;
	if (!require(StartupTest::state().initCalls == 1 && StartupTest::state().pairingBeginCalls == 1, "NimBLE and pairing initialize once")) return 1;
	if (!require(StartupTest::state().configVisibleAtCreateServer && StartupTest::state().serverStartsAtCreateServer == 0,
		"default advertising config is stored before server creation without an implicit start")) return 1;
	if (!require(StartupTest::state().createServerCalls == 1, "startup creates one server")) return 1;
	if (!require(Advertising::GetConfig(0).has_value(), "instance 0 remains visible through GetConfig")) return 1;

	const auto config = Advertising::GetConfig(0);
	if (!require(checkConfig(*config, expectedName), "instance 0 retains the default mode, name, UUID, interval and manufacturer payload")) return 1;
	std::vector<Advertising::InstanceConfig> snapshot;
	if (!require(Advertising::TrySnapshot(snapshot) && snapshot.size() == 1 && checkConfig(snapshot.front(), expectedName),
		"instance 0 remains visible through the advertising snapshot used by Web")) return 1;

	if (!require(FixtureGatt::Ready() == succeeds, "fixture readiness matches registration and finalization outcome")) return 1;
	if (!require(NimBLETest::controls().registrationComplete == registered, "registration state matches selected startup outcome")) return 1;
	const size_t expectedStartAttempts = 1 + ((succeeds && CONFIG_BT_NIMBLE_EXT_ADV) ? 1U : 0U);
	if (!require(NimBLETest::controls().counters.serverStarts == expectedStartAttempts &&
		NimBLETest::controls().counters.actualRegistrations == (registered ? 1U : 0U),
		"server registration is explicit; the extended post-ready apply is an idempotent start attempt")) return 1;
	if (!require(checkService(registered), "the original fixture service remains present without duplication")) return 1;
	if (!require(StartupTest::state().startCalls == (succeeds ? 1U : 0U) &&
		StartupTest::state().radioStartCalls == (succeeds ? 1U : 0U), "advertising starts only after successful finalization")) return 1;
	if (succeeds && !require(StartupTest::state().startSawReady && StartupTest::state().hasStartConfig &&
		StartupTest::state().startConfig && checkConfig(*StartupTest::state().startConfig, expectedName),
		"start observes ready fixture and the stored instance 0 config")) return 1;
	const size_t expectedLegacyRebuilds = succeeds && !CONFIG_BT_NIMBLE_EXT_ADV ? 1U : 0U;
	const size_t expectedExtendedApplies = succeeds && CONFIG_BT_NIMBLE_EXT_ADV ? 1U : 0U;
	if (!require(StartupTest::state().postReadyRebuildCalls == (succeeds ? 1U : 0U) &&
		StartupTest::state().legacyRebuildCalls == expectedLegacyRebuilds &&
		StartupTest::state().extendedDataCalls == expectedExtendedApplies,
		"advertisement data is applied once, after finalization")) return 1;
	if (!require(StartupTest::state().aggregateStableAfterRebuild == succeeds,
		"post-ready rebuild preserves aggregate descriptor handle and CPF bytes")) return 1;

	const auto before = NimBLETest::controls().counters;
	const auto factoryCallsBefore = NimBLETest::controls().factoryCalls;
	const auto serverCreationsBefore = StartupTest::state().createServerCalls;
	const auto pairingBeginsBefore = StartupTest::state().pairingBeginCalls;
	manager.SetupBluetooth();
	if (!require(NimBLETest::controls().counters.serviceCreates == before.serviceCreates &&
		NimBLETest::controls().counters.characteristicCreates == before.characteristicCreates &&
		NimBLETest::controls().counters.descriptorCreates == before.descriptorCreates &&
		NimBLETest::controls().counters.presentationFormatCreates == before.presentationFormatCreates &&
		NimBLETest::controls().factoryCalls == factoryCallsBefore &&
		NimBLETest::controls().counters.serverStarts == before.serverStarts &&
		StartupTest::state().createServerCalls == serverCreationsBefore &&
		StartupTest::state().pairingBeginCalls == pairingBeginsBefore &&
		StartupTest::state().startCalls == (succeeds ? 1U : 0U), "repeated SetupBluetooth does not duplicate services or start again")) return 1;
	if (!require(Advertising::TrySnapshot(snapshot) && snapshot.size() == 1,
		"repeated startup retains exactly one visible default instance")) return 1;

	std::printf("PASS %s (CONFIG_BT_NIMBLE_EXT_ADV=%d)\n", scenario.c_str(), CONFIG_BT_NIMBLE_EXT_ADV);
	return 0;
}
