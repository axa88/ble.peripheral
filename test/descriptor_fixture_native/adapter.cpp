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

	uint8_t declarationProperties(uint16_t flags)
	{
		uint8_t result = 0;
		if (flags & BLE_GATT_CHR_F_BROADCAST) result |= 0x01;
		if (flags & BLE_GATT_CHR_F_READ) result |= 0x02;
		if (flags & BLE_GATT_CHR_F_WRITE_NO_RSP) result |= 0x04;
		if (flags & BLE_GATT_CHR_F_WRITE) result |= 0x08;
		if (flags & BLE_GATT_CHR_F_NOTIFY) result |= 0x10;
		if (flags & BLE_GATT_CHR_F_INDICATE) result |= 0x20;
		if (flags & (BLE_GATT_CHR_F_RELIABLE_WRITE | BLE_GATT_CHR_F_AUX_WRITE)) result |= BLE_GATT_CHR_PROP_EXTENDED;
		return result;
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

	AttResult attRead(uint16_t handle, std::vector<uint8_t>& bytes)
	{
		const auto& attributes = controls().nativeAttributes;
		const auto it = std::find_if(attributes.begin(), attributes.end(), [handle](const NativeAttribute& item) {
			return item.handle == handle;
		});
		if (it == attributes.end() || !it->attribute) return AttResult::NotFound;
		const uint16_t required = it->descriptor ? BLE_ATT_F_READ : BLE_GATT_CHR_F_READ;
		if (!(it->permissions & required)) return AttResult::ReadNotPermitted;
		bytes = capture(it->attribute->getValue());
		return AttResult::Success;
	}

	AttResult attWrite(uint16_t handle, const std::vector<uint8_t>& bytes, uint16_t mtu)
	{
		const auto& attributes = controls().nativeAttributes;
		const auto it = std::find_if(attributes.begin(), attributes.end(), [handle](const NativeAttribute& item) {
			return item.handle == handle;
		});
		if (it == attributes.end() || !it->attribute) return AttResult::NotFound;
		const uint16_t required = it->descriptor ? BLE_ATT_F_WRITE : BLE_GATT_CHR_F_WRITE;
		if (!(it->permissions & required)) return AttResult::WriteNotPermitted;
		const size_t singleWriteMaximum = mtu >= 3 ? mtu - 3 : 0;
		if (bytes.size() > it->attribute->getValue().max_size() || bytes.size() > singleWriteMaximum)
			return AttResult::InvalidLength;
		it->attribute->setValue(bytes.data(), bytes.size());
		return capture(it->attribute->getValue()) == bytes ? AttResult::Success : AttResult::InvalidLength;
	}

	bool readCharacteristicDeclaration(const NimBLECharacteristic* characteristic, std::vector<uint8_t>& bytes)
	{
		const auto& declarations = controls().nativeDeclarations;
		const auto it = std::find_if(declarations.begin(), declarations.end(), [characteristic](const NativeDeclaration& item) {
			return item.characteristic == characteristic;
		});
		if (it == declarations.end()) return false;
		bytes = {it->properties, static_cast<uint8_t>(it->valueHandle), static_cast<uint8_t>(it->valueHandle >> 8)};
		const auto uuidBytes = characteristic->getUUID().toString().size() == 36 ? size_t{16} : size_t{2};
		bytes.resize(bytes.size() + uuidBytes, 0);
		return true;
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
			case Fault::CorruptExtendedDeclaration:
				for (auto& declaration : state.nativeDeclarations)
					if (declaration.characteristic && declaration.characteristic->getUUID() == NimBLEUUID("12345678-1234-5678-1234-56789abcdf08"))
						declaration.properties &= static_cast<uint8_t>(~BLE_GATT_CHR_PROP_EXTENDED);
				break;
			case Fault::CorruptExtendedProperties:
				if (auto* writable = service->getCharacteristic("12345678-1234-5678-1234-56789abcdf08"))
					if (auto* extended = writable->getDescriptorByUUID(NimBLEUUID(uint16_t{0x2900})))
						extended->setValue(std::vector<uint8_t>{0x00, 0x00}.data(), 2);
				break;
			case Fault::None:
			case Fault::CorruptAggregateReadback:
				break;
		}
	}
}

int ble_gatts_find_chr(const NimBLEUUID* service, const NimBLEUUID* characteristic,
	uint16_t* declarationHandle, uint16_t* valueHandle)
{
	if (!service || !characteristic) return -1;
	for (const auto& item : NimBLETest::controls().nativeDeclarations)
	{
		if (!item.characteristic || !item.characteristic->getService() ||
			item.characteristic->getService()->getUUID() != *service || item.characteristic->getUUID() != *characteristic) continue;
		if (declarationHandle) *declarationHandle = item.declarationHandle;
		if (valueHandle) *valueHandle = item.valueHandle;
		return 0;
	}
	return 2;
}

int ble_gatts_find_dsc(const NimBLEUUID* service, const NimBLEUUID* characteristic,
	const NimBLEUUID* descriptor, uint16_t* handle)
{
	if (!service || !characteristic || !descriptor) return -1;
	for (const auto& item : NimBLETest::controls().nativeAttributes)
	{
		auto* value = item.attribute;
		auto* dsc = dynamic_cast<NimBLEDescriptor*>(value);
		if (!dsc || !dsc->getCharacteristic() || !dsc->getCharacteristic()->getService() ||
			dsc->getCharacteristic()->getService()->getUUID() != *service ||
			dsc->getCharacteristic()->getUUID() != *characteristic || dsc->getUUID() != *descriptor) continue;
		if (handle) *handle = item.handle;
		return 0;
	}
	for (const auto& item : NimBLETest::controls().managedCccds)
	{
		if (!item.first || !item.first->getService() ||
			item.first->getService()->getUUID() != *service || item.first->getUUID() != *characteristic ||
			*descriptor != NimBLEUUID(uint16_t{0x2902})) continue;
		if (handle) *handle = item.second;
		return 0;
	}
	return 2;
}

int ble_att_svr_read_local(uint16_t handle, os_mbuf** data)
{
	if (!data) return -1;
	*data = nullptr;
	for (const auto& declaration : NimBLETest::controls().nativeDeclarations)
	{
		if (declaration.declarationHandle != handle) continue;
		std::vector<uint8_t> bytes;
		if (!NimBLETest::readCharacteristicDeclaration(declaration.characteristic, bytes)) return 2;
		*data = new os_mbuf{std::move(bytes)};
		return 0;
	}
	std::vector<uint8_t> bytes;
	if (NimBLETest::attRead(handle, bytes) != NimBLETest::AttResult::Success) return 2;
	*data = new os_mbuf{std::move(bytes)};
	return 0;
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
	++NimBLETest::controls().counters.notifyPayloadOverloadCalls;
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
	++NimBLETest::controls().counters.indicatePayloadOverloadCalls;
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
	state.nativeAttributes.clear();
	state.nativeDeclarations.clear();
	state.managedCccds.clear();
	uint16_t nextHandle = 1;
	for (const auto& service : services_)
	{
		service->handle_ = nextHandle++;
		service->managedCccdCount_ = 0;
		for (const auto& characteristic : service->characteristics_)
		{
			const uint16_t declarationHandle = nextHandle++;
			const uint16_t valueHandle = nextHandle++;
			characteristic->setHandleForTest(valueHandle);
			state.nativeDeclarations.push_back({characteristic.get(), declarationHandle, valueHandle,
				declarationProperties(characteristic->getProperties())});
			state.nativeAttributes.push_back({characteristic.get(), valueHandle, characteristic->getProperties(), false});
			for (const auto& descriptor : characteristic->descriptors_)
			{
				descriptor->setHandleForTest(nextHandle++);
				state.nativeAttributes.push_back({descriptor.get(), descriptor->getHandle(), descriptor->getProperties(), true});
			}
			if (characteristic->getProperties() & (NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE))
			{
				++service->managedCccdCount_; // Stack-managed 0x2902; not an application descriptor object.
				state.managedCccds.emplace_back(characteristic.get(), nextHandle++);
			}
		}
	}
	state.registrationComplete = true;
	++state.counters.actualRegistrations;
	NimBLETest::afterRegistration(*this);
	return true;
}
