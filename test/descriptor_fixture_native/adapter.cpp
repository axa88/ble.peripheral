#include "NimBLEDevice.h"

namespace
{
	uint16_t descriptorFlags(uint32_t properties)
	{
		uint16_t result = 0;
		if (properties & NIMBLE_PROPERTY::READ) result |= BLE_ATT_F_READ;
		if (properties & (NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE)) result |= BLE_ATT_F_WRITE;
		return result;
	}

	std::vector<uint8_t> capture(const NimBLEAttValue& value)
	{
		return {value.begin(), value.end()};
	}

	std::vector<uint8_t> capture(const uint8_t* bytes, size_t length)
	{
		return length ? std::vector<uint8_t>(bytes, bytes + length) : std::vector<uint8_t>{};
	}
}

namespace NimBLETest
{
	Controls& controls()
	{
		static Controls state;
		return state;
	}

	void reset() { controls() = Controls{}; }

	bool allowFactory(FactoryKind kind)
	{
		auto& state = controls();
		++state.factoryCalls;
		switch (kind)
		{
			case FactoryKind::Service: ++state.counters.serviceCreates; break;
			case FactoryKind::Characteristic: ++state.counters.characteristicCreates; break;
			case FactoryKind::Descriptor: ++state.counters.descriptorCreates; break;
			case FactoryKind::PresentationFormat: ++state.counters.presentationFormatCreates; break;
		}
		return !state.failFactoryOrdinal || state.factoryCalls != state.failFactoryOrdinal;
	}

	bool corruptAggregateReadback()
	{
		const auto& state = controls();
		return state.registrationComplete && state.postRegistrationFault == Fault::CorruptAggregateReadback;
	}

	void afterRegistration(NimBLEServer& server)
	{
		auto& state = controls();
		auto* service = server.getServiceByUUID("12345678-1234-5678-1234-56789abcdf00");
		if (!service) return;
		auto* temperature = service->getCharacteristic("12345678-1234-5678-1234-56789abcdf05");
		auto* aggregateValue = service->getCharacteristic("12345678-1234-5678-1234-56789abcdf06");
		switch (state.postRegistrationFault)
		{
			case Fault::DescriptionWritePermission:
				if (auto* description = service->getCharacteristic("12345678-1234-5678-1234-56789abcdf01")
						? service->getCharacteristic("12345678-1234-5678-1234-56789abcdf01")->getDescriptorByUUID(NimBLEUUID(uint16_t{0x2901}))
						: nullptr)
					description->setPropertiesForTest(BLE_ATT_F_WRITE);
				break;
			case Fault::ZeroTemperatureCpfHandle:
				if (temperature)
					if (auto* format = temperature->getDescriptorByUUID(NimBLEUUID(uint16_t{0x2904}))) format->setHandleForTest(0);
				break;
			case Fault::DuplicateCpfHandles:
				if (temperature && aggregateValue)
				{
					auto* first = temperature->getDescriptorByUUID(NimBLEUUID(uint16_t{0x2904}));
					auto* second = aggregateValue->getDescriptorByUUID(NimBLEUUID(uint16_t{0x2904}));
					if (first && second) second->setHandleForTest(first->getHandle());
				}
				break;
			case Fault::None:
			case Fault::CorruptAggregateReadback:
				break;
		}
	}
}

NimBLEDescriptor::NimBLEDescriptor(const NimBLEUUID& uuid, uint32_t properties, uint16_t maxLength, NimBLECharacteristic* characteristic)
	: NimBLELocalValueAttribute(uuid, descriptorFlags(properties), maxLength, true), characteristic_(characteristic)
{}

NimBLE2904::NimBLE2904(NimBLECharacteristic* characteristic)
	: NimBLEDescriptor(NimBLEUUID(uint16_t{0x2904}), BLE_GATT_CHR_F_READ, sizeof(NimBLE2904Data), characteristic)
{
	setValue(data_);
}

NimBLEDescriptor* NimBLECharacteristic::createDescriptor(const NimBLEUUID& uuid, uint32_t properties, uint16_t maxLength)
{
	if (uuid == NimBLEUUID(uint16_t{0x2904})) return create2904();
	if (!NimBLETest::allowFactory(NimBLETest::FactoryKind::Descriptor)) return nullptr;
	std::unique_ptr<NimBLEDescriptor> descriptor(new NimBLEDescriptor(uuid, properties, maxLength, this));
	auto* result = descriptor.get();
	addDescriptor(std::move(descriptor));
	return result;
}

NimBLE2904* NimBLECharacteristic::create2904()
{
	if (!NimBLETest::allowFactory(NimBLETest::FactoryKind::PresentationFormat)) return nullptr;
	std::unique_ptr<NimBLEDescriptor> descriptor(new NimBLE2904(this));
	auto* result = static_cast<NimBLE2904*>(descriptor.get());
	addDescriptor(std::move(descriptor));
	return result;
}

NimBLEDescriptor* NimBLECharacteristic::getDescriptorByUUID(const NimBLEUUID& uuid, uint16_t index) const
{
	for (const auto& descriptor : descriptors_)
		if (descriptor->getUUID() == uuid && index-- == 0) return descriptor.get();
	return nullptr;
}

bool NimBLECharacteristic::notify() const
{
	++NimBLETest::controls().counters.notifyCalls;
	NimBLETest::controls().lastNotification = capture(getValue());
	return true;
}

bool NimBLECharacteristic::notify(const uint8_t* bytes, size_t length) const
{
	++NimBLETest::controls().counters.notifyCalls;
	NimBLETest::controls().lastNotification = capture(bytes, length);
	return true;
}

bool NimBLECharacteristic::indicate() const
{
	++NimBLETest::controls().counters.indicateCalls;
	NimBLETest::controls().lastIndication = capture(getValue());
	return true;
}

bool NimBLECharacteristic::indicate(const uint8_t* bytes, size_t length) const
{
	++NimBLETest::controls().counters.indicateCalls;
	NimBLETest::controls().lastIndication = capture(bytes, length);
	return true;
}

NimBLECharacteristic* NimBLEService::createCharacteristic(const NimBLEUUID& uuid, uint32_t properties, uint16_t maxLength)
{
	if (!NimBLETest::allowFactory(NimBLETest::FactoryKind::Characteristic)) return nullptr;
	std::unique_ptr<NimBLECharacteristic> characteristic(new NimBLECharacteristic(uuid, properties, maxLength, this));
	auto* result = characteristic.get();
	characteristics_.push_back(std::move(characteristic));
	return result;
}

NimBLECharacteristic* NimBLEService::getCharacteristic(const NimBLEUUID& uuid) const
{
	for (const auto& characteristic : characteristics_)
		if (characteristic->getUUID() == uuid) return characteristic.get();
	return nullptr;
}

NimBLEService* NimBLEServer::createService(const char* uuid)
{
	if (!NimBLETest::allowFactory(NimBLETest::FactoryKind::Service)) return nullptr;
	std::unique_ptr<NimBLEService> service(new NimBLEService(NimBLEUUID(uuid)));
	auto* result = service.get();
	services_.push_back(std::move(service));
	return result;
}

NimBLEService* NimBLEServer::getServiceByUUID(const NimBLEUUID& uuid) const
{
	for (const auto& service : services_)
		if (service->getUUID() == uuid) return service.get();
	return nullptr;
}

bool NimBLEServer::start()
{
	auto& state = NimBLETest::controls();
	++state.counters.serverStarts;
	if (state.registrationComplete) return true; // Pinned NimBLE returns immediately once GATT is already started.
	if (!state.registrationSucceeds) return false;
	uint16_t nextHandle = 1;
	for (const auto& service : services_)
	{
		service->handle_ = nextHandle++;
		service->managedCccdCount_ = 0;
		for (const auto& characteristic : service->characteristics_)
		{
			++nextHandle; // Characteristic declaration handle.
			characteristic->setHandleForTest(nextHandle++); // Assigned value handle.
			for (const auto& descriptor : characteristic->descriptors_)
				descriptor->setHandleForTest(nextHandle++);
			if (characteristic->getProperties() & (NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE))
			{
				++service->managedCccdCount_; // Stack-managed 0x2902; not an application descriptor object.
				++nextHandle;
			}
		}
	}
	state.registrationComplete = true;
	++state.counters.actualRegistrations;
	NimBLETest::afterRegistration(*this);
	return true;
}
