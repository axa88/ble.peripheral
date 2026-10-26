#include "fixtureGatt.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	bool check(bool condition, const std::string& message)
	{
		if (!condition) std::cerr << "FAIL: " << message << '\n';
		return condition;
	}

	std::vector<uint8_t> bytes(const NimBLELocalValueAttribute* attribute)
	{
		if (!attribute) return {};
		const auto& value = attribute->getValue();
		return {value.begin(), value.end()};
	}

	NimBLEService* fixtureService(NimBLEServer& server)
	{
		return server.getServiceByUUID(FixtureGatt::DescriptorServiceUuid);
	}

	NimBLECharacteristic* fixtureCharacteristic(NimBLEServer& server, size_t index)
	{
		char uuid[37];
		std::snprintf(uuid, sizeof(uuid), "12345678-1234-5678-1234-56789abcdf%02u", static_cast<unsigned>(index + 1));
		auto* service = fixtureService(server);
		return service ? service->getCharacteristic(uuid) : nullptr;
	}

	NimBLEDescriptor* descriptor(NimBLECharacteristic* characteristic, uint16_t uuid)
	{
		return characteristic ? characteristic->getDescriptorByUUID(NimBLEUUID(uuid)) : nullptr;
	}

	bool approvedConstantsAreExact()
	{
		bool ok = true;
		ok &= check(std::strcmp(FixtureGatt::ServiceUuid, "12345678-1234-5678-1234-56789abcdef0") == 0 &&
			std::strcmp(FixtureGatt::ExistingCharacteristicUuid, "12345678-1234-5678-1234-56789abcdef1") == 0 &&
			std::strcmp(FixtureGatt::NotificationCharacteristicUuid, "12345678-1234-5678-1234-56789abcdef2") == 0 &&
			std::strcmp(FixtureGatt::IndicationCharacteristicUuid, "12345678-1234-5678-1234-56789abcdef3") == 0 &&
			std::strcmp(FixtureGatt::CustomDescriptorUuid, "12345678-1234-5678-1234-56789abcdef4") == 0,
			"approved original service, characteristic, and custom descriptor UUIDs");
		ok &= check(std::strcmp(FixtureGatt::DescriptorServiceUuid, "12345678-1234-5678-1234-56789abcdf00") == 0 &&
			std::strcmp(FixtureGatt::TemperatureUuid, "12345678-1234-5678-1234-56789abcdf05") == 0,
			"approved descriptor-fixture service and temperature UUIDs");
		ok &= check(std::strcmp(FixtureGatt::ExistingCharacteristicValue, "Ima Characteristic") == 0 &&
			std::strcmp(FixtureGatt::NotificationCharacteristicValue, "Notify Characteristic") == 0 &&
			std::strcmp(FixtureGatt::IndicationCharacteristicValue, "Indicate Characteristic") == 0 &&
			std::strcmp(FixtureGatt::CustomDescriptorValue, "Custom Descriptor") == 0,
			"approved original characteristic and descriptor bytes");
		ok &= check(std::strcmp(FixtureGatt::NotificationPayload, "notify-event") == 0 &&
			std::strcmp(FixtureGatt::IndicationPayload, "indicate-event") == 0,
			"approved original callback payload bytes");
		return ok;
	}

	bool fixtureContentsAreExact(NimBLEServer& server)
	{
		constexpr std::array<const char*, 7> names = {{"Exponent Zero", "Exponent Positive", "Exponent Negative Minimum",
			"Exponent Negative Two", "Temperature Scalar", "Aggregate Value", "Temperature Update Trigger"}};
		constexpr std::array<int8_t, 5> exponents = {{0, 127, -128, -2, -1}};
		constexpr uint8_t scalar[] = {0xEB, 0x00};
		constexpr uint8_t aggregateValue[] = {0xEB, 0x00, 0x2A};
		constexpr uint8_t rangeValue[] = {0x70, 0xFE, 0xE8, 0x03};
		bool ok = true;
		auto* original = server.getServiceByUUID(FixtureGatt::ServiceUuid);
		auto* fixtures = fixtureService(server);
		ok &= check(server.servicesForTest().size() == 2, "exactly two original and fixture services");
		ok &= check(original && original->characteristicsForTest().size() == 3, "original service keeps three characteristics");
		ok &= check(fixtures && fixtures->characteristicsForTest().size() == 7, "fixture service has seven characteristics");
		if (!original || !fixtures) return false;
		ok &= check(original->managedCccdCountForTest() == 2 && fixtures->managedCccdCountForTest() == 1,
			"stack-managed CCCD count follows notify/indicate characteristics");

		auto* existing = original->getCharacteristic(FixtureGatt::ExistingCharacteristicUuid);
		auto* notification = original->getCharacteristic(FixtureGatt::NotificationCharacteristicUuid);
		auto* indication = original->getCharacteristic(FixtureGatt::IndicationCharacteristicUuid);
		ok &= check(existing && existing->getProperties() == (NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE) &&
			bytes(existing) == std::vector<uint8_t>(FixtureGatt::ExistingCharacteristicValue,
				FixtureGatt::ExistingCharacteristicValue + sizeof(FixtureGatt::ExistingCharacteristicValue) - 1),
			"original readable/writable characteristic and bytes are preserved");
		ok &= check(notification && notification->getProperties() ==
			(NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY) &&
			bytes(notification) == std::vector<uint8_t>(FixtureGatt::NotificationCharacteristicValue,
				FixtureGatt::NotificationCharacteristicValue + sizeof(FixtureGatt::NotificationCharacteristicValue) - 1),
			"original notification characteristic and bytes are preserved");
		ok &= check(indication && indication->getProperties() ==
			(NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE) &&
			bytes(indication) == std::vector<uint8_t>(FixtureGatt::IndicationCharacteristicValue,
				FixtureGatt::IndicationCharacteristicValue + sizeof(FixtureGatt::IndicationCharacteristicValue) - 1),
			"original indication characteristic and bytes are preserved");
		auto* custom = notification ? notification->getDescriptorByUUID(NimBLEUUID(FixtureGatt::CustomDescriptorUuid)) : nullptr;
		ok &= check(notification && notification->descriptorsForTest().size() == 1 && custom &&
			custom->getProperties() == (BLE_ATT_F_READ | BLE_ATT_F_WRITE) &&
			bytes(custom) == std::vector<uint8_t>(FixtureGatt::CustomDescriptorValue,
				FixtureGatt::CustomDescriptorValue + sizeof(FixtureGatt::CustomDescriptorValue) - 1),
			"original custom descriptor, permissions, and bytes are preserved");
		ok &= check(notification && notification->getDescriptorByUUID(NimBLEUUID(uint16_t{0x2901})) == nullptr,
			"original custom descriptor UUID is not replaced by a fixture user description");

		for (size_t i = 0; i < names.size(); ++i)
		{
			auto* characteristic = fixtureCharacteristic(server, i);
			const uint16_t properties = i == 6 ? NIMBLE_PROPERTY::WRITE : i == 4 ?
				NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY : NIMBLE_PROPERTY::READ;
			const size_t maxLength = i == 6 ? 1 : i == 5 ? 3 : i == 4 ? BLE_ATT_ATTR_MAX_LEN : 2;
			const std::vector<uint8_t> expectedValue = i < 5 ? std::vector<uint8_t>(scalar, scalar + sizeof(scalar)) :
				i == 5 ? std::vector<uint8_t>(aggregateValue, aggregateValue + sizeof(aggregateValue)) : std::vector<uint8_t>{};
			ok &= check(characteristic && characteristic->getProperties() == properties &&
				characteristic->getValue().max_size() == maxLength && bytes(characteristic) == expectedValue,
				"fixture characteristic " + std::to_string(i + 1) + " contract/value");
			if (!characteristic) continue;
			auto* userDescription = descriptor(characteristic, 0x2901);
			ok &= check(userDescription && userDescription->getProperties() == BLE_ATT_F_READ &&
				bytes(userDescription) == std::vector<uint8_t>(names[i], names[i] + std::strlen(names[i])),
				"exact user description " + std::to_string(i + 1));
			if (i < 6)
			{
				auto* format = descriptor(characteristic, 0x2904);
				const std::vector<uint8_t> expectedFormat = {
					static_cast<uint8_t>(i < 5 ? 0x0E : 0x04),
					static_cast<uint8_t>(i < 5 ? exponents[i] : 0),
					static_cast<uint8_t>(i < 5 ? 0x2F : 0x00), 0x27, 1, 0, 0};
				ok &= check(format && format->getProperties() == BLE_ATT_F_READ && bytes(format) == expectedFormat,
					"exact 0x2904 bytes and read permission " + std::to_string(i + 1));
			}
			if (i == 3)
			{
				auto* range = descriptor(characteristic, 0x2906);
				ok &= check(range && range->getProperties() == BLE_ATT_F_READ &&
					bytes(range) == std::vector<uint8_t>(rangeValue, rangeValue + sizeof(rangeValue)),
					"exact temperature range descriptor bytes");
			}
			if (i == 5)
			{
				auto* aggregate = descriptor(characteristic, 0x2905);
				ok &= check(aggregate && aggregate->getProperties() == BLE_ATT_F_READ && aggregate->getValue().max_size() == 4,
					"aggregate descriptor retains four-byte capacity and read permission");
			}
		}
		ok &= check(fixtures->characteristicsForTest()[0]->descriptorsForTest().size() == 2 &&
			fixtures->characteristicsForTest()[1]->descriptorsForTest().size() == 2 &&
			fixtures->characteristicsForTest()[2]->descriptorsForTest().size() == 2 &&
			fixtures->characteristicsForTest()[3]->descriptorsForTest().size() == 3 &&
			fixtures->characteristicsForTest()[4]->descriptorsForTest().size() == 2 &&
			fixtures->characteristicsForTest()[5]->descriptorsForTest().size() == 3 &&
			fixtures->characteristicsForTest()[6]->descriptorsForTest().size() == 1,
			"exact application descriptor counts; CCCDs remain stack-managed");
		ok &= check(fixtures->characteristicsForTest()[5]->getDescriptorByUUID(NimBLEUUID(uint16_t{0x2905}), 1) == nullptr,
			"aggregate descriptor is unique");
		return ok;
	}

	bool successCase()
	{
		NimBLETest::reset();
		NimBLEServer server;
		bool ok = approvedConstantsAreExact();
		ok &= check(!FixtureGatt::Ready(), "not ready before registration");
		auto* created = FixtureGatt::Create(server);
		ok &= check(created != nullptr && !FixtureGatt::Ready(), "Create succeeds without publishing readiness");
		ok &= check(NimBLETest::controls().counters.serviceCreates == 2 &&
			NimBLETest::controls().counters.characteristicCreates == 10 &&
			NimBLETest::controls().counters.descriptorCreates == 10 &&
			NimBLETest::controls().counters.presentationFormatCreates == 6 &&
			NimBLETest::controls().factoryCalls == 28, "exact fixture factory counts");
		ok &= check(!FixtureGatt::Finalize() && !FixtureGatt::Ready(), "Finalize fails closed before stack registration");
		ok &= check(server.start() && !FixtureGatt::Ready(), "registration assigns handles before readiness");
		ok &= check(FixtureGatt::Finalize(), "expected finalization success but got false");
		ok &= check(FixtureGatt::Ready(), "ready only after finalization");
		ok &= fixtureContentsAreExact(server);
		const auto registrations = NimBLETest::controls().counters.actualRegistrations;
		const auto cpfHandle = descriptor(fixtureCharacteristic(server, 4), 0x2904)->getHandle();
		ok &= check(server.start(), "repeated NimBLE server start succeeds");
		ok &= check(NimBLETest::controls().counters.serverStarts == 2 && registrations == 1 &&
			NimBLETest::controls().counters.actualRegistrations == registrations &&
			descriptor(fixtureCharacteristic(server, 4), 0x2904)->getHandle() == cpfHandle,
			"repeated server start is an attempt but does not reregister or reassign handles");

		const auto serviceCount = server.servicesForTest().size();
		std::array<size_t, 2> characteristicCounts = {{server.servicesForTest()[0]->characteristicsForTest().size(),
			server.servicesForTest()[1]->characteristicsForTest().size()}};
		std::array<size_t, 2> descriptorCounts = {{0, 0}};
		for (size_t serviceIndex = 0; serviceIndex < 2; ++serviceIndex)
			for (const auto& characteristic : server.servicesForTest()[serviceIndex]->characteristicsForTest())
				descriptorCounts[serviceIndex] += characteristic->descriptorsForTest().size();
		const auto factoryCalls = NimBLETest::controls().factoryCalls;
		ok &= check(FixtureGatt::Finalize(), "Finalize is idempotent after readiness");
		ok &= check(FixtureGatt::Create(server) == nullptr, "Create rejects post-finalization schema mutation");
		ok &= check(server.servicesForTest().size() == serviceCount &&
			server.servicesForTest()[0]->characteristicsForTest().size() == characteristicCounts[0] &&
			server.servicesForTest()[1]->characteristicsForTest().size() == characteristicCounts[1] &&
			NimBLETest::controls().factoryCalls == factoryCalls,
			"repeated Finalize/Create calls do not mutate schema");
		std::array<size_t, 2> afterDescriptorCounts = {{0, 0}};
		for (size_t serviceIndex = 0; serviceIndex < 2; ++serviceIndex)
			for (const auto& characteristic : server.servicesForTest()[serviceIndex]->characteristicsForTest())
				afterDescriptorCounts[serviceIndex] += characteristic->descriptorsForTest().size();
		ok &= check(afterDescriptorCounts == descriptorCounts, "repeated Finalize/Create calls do not add descriptors");

		auto* original = server.getServiceByUUID(FixtureGatt::ServiceUuid);
		auto* notification = original ? original->getCharacteristic(FixtureGatt::NotificationCharacteristicUuid) : nullptr;
		auto* indication = original ? original->getCharacteristic(FixtureGatt::IndicationCharacteristicUuid) : nullptr;
		auto* temperatureTrigger = fixtureCharacteristic(server, 6);
		ok &= check(notification && notification->callbacksForTest(), "original notification callback remains registered");
		ok &= check(indication && indication->callbacksForTest(), "original indication callback remains registered");
		ok &= check(temperatureTrigger && temperatureTrigger->callbacksForTest(), "fixture temperature-trigger callback remains registered");
		if (notification) notification->writeForTest({FixtureGatt::UpdateTrigger});
		if (indication) indication->writeForTest({FixtureGatt::UpdateTrigger});
		ok &= check(NimBLETest::controls().lastNotification == std::vector<uint8_t>(FixtureGatt::NotificationPayload,
			FixtureGatt::NotificationPayload + sizeof(FixtureGatt::NotificationPayload) - 1),
			"original notify callback still publishes its canonical payload");
		ok &= check(NimBLETest::controls().lastIndication == std::vector<uint8_t>(FixtureGatt::IndicationPayload,
			FixtureGatt::IndicationPayload + sizeof(FixtureGatt::IndicationPayload) - 1),
			"original indicate callback still publishes its canonical payload");
		if (temperatureTrigger) temperatureTrigger->writeForTest({FixtureGatt::UpdateTrigger});
		ok &= check(NimBLETest::controls().lastNotification == std::vector<uint8_t>({0xEB, 0x00}),
			"temperature update trigger still notifies the current scalar value");
		ok &= check(NimBLETest::controls().counters.notifyCalls == 2 && NimBLETest::controls().counters.indicateCalls == 1,
			"original callback and temperature trigger dispatch counts");
		return ok;
	}

	bool failureCase(const std::string& mode)
	{
		NimBLETest::reset();
		auto& controls = NimBLETest::controls();
		if (mode == "permission") controls.postRegistrationFault = NimBLETest::Fault::DescriptionWritePermission;
		else if (mode == "zero-handle") controls.postRegistrationFault = NimBLETest::Fault::ZeroTemperatureCpfHandle;
		else if (mode == "duplicate-handle") controls.postRegistrationFault = NimBLETest::Fault::DuplicateCpfHandles;
		else if (mode == "aggregate-readback") controls.postRegistrationFault = NimBLETest::Fault::CorruptAggregateReadback;
		else if (mode == "registration-failure") controls.registrationSucceeds = false;
		else if (mode == "create-failure") controls.failFactoryOrdinal = 2;
		else return check(false, "unknown failure case: " + mode);

		NimBLEServer server;
		bool ok = check(!FixtureGatt::Ready(), "not ready before fault injection");
		auto* created = FixtureGatt::Create(server);
		if (mode == "create-failure")
		{
			ok &= check(created == nullptr && !FixtureGatt::Ready(), "injected factory failure aborts Create and remains not ready");
			ok &= check(controls.counters.serverStarts == 0, "failed Create never starts registration");
			const auto serviceCount = server.servicesForTest().size();
			const auto calls = controls.factoryCalls;
			ok &= check(FixtureGatt::Create(server) == nullptr && server.servicesForTest().size() == serviceCount &&
				controls.factoryCalls == calls, "partial Create cannot retry or extend schema");
			ok &= check(!FixtureGatt::Finalize() && !FixtureGatt::Ready(), "partial schema cannot finalize");
			return ok;
		}
		ok &= check(created != nullptr && !FixtureGatt::Ready(), "valid creation remains not ready until stack work completes");
		if (!created) return false;
		const bool registered = server.start();
		if (mode == "registration-failure")
		{
			ok &= check(!registered && !FixtureGatt::Ready(), "failed registration remains not ready");
			ok &= check(!FixtureGatt::Finalize() && !FixtureGatt::Ready(), "failed registration cannot finalize");
			return ok;
		}
		ok &= check(registered && !FixtureGatt::Ready(), "fault is injected after successful handle assignment");
		ok &= check(!FixtureGatt::Finalize() && !FixtureGatt::Ready(), "invalid registered metadata fails closed");
		if (mode == "permission")
		{
			auto* description = descriptor(fixtureCharacteristic(server, 0), 0x2901);
			ok &= check(description && description->getProperties() == BLE_ATT_F_WRITE,
				"injected descriptor permission uses ATT write flag");
		}
		if (mode == "zero-handle")
		{
			auto* format = descriptor(fixtureCharacteristic(server, 4), 0x2904);
			ok &= check(format && format->getHandle() == 0, "injected zero assigned handle");
		}
		if (mode == "duplicate-handle")
		{
			auto* first = descriptor(fixtureCharacteristic(server, 4), 0x2904);
			auto* second = descriptor(fixtureCharacteristic(server, 5), 0x2904);
			ok &= check(first && second && first->getHandle() == second->getHandle(), "injected duplicate CPF handles");
		}
		return ok;
	}
}

int main(int argc, char** argv)
{
	static_assert(BLE_GATT_CHR_F_READ == 0x0002 && BLE_GATT_CHR_F_WRITE == 0x0008 &&
		BLE_GATT_CHR_F_NOTIFY == 0x0010 && BLE_GATT_CHR_F_INDICATE == 0x0020,
		"characteristic properties must use the NimBLE host flag domain");
	static_assert(BLE_ATT_F_READ == 0x01 && BLE_ATT_F_WRITE == 0x02,
		"descriptor properties must use the ATT permission flag domain");
	if (argc != 2) return 2;
	const std::string mode = argv[1];
	const bool ok = mode == "success" ? successCase() : failureCase(mode);
	if (!ok) return 1;
	std::cout << "PASS: " << mode << '\n';
	return 0;
}
