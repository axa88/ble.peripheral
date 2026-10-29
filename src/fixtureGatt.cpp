#include "fixtureGatt.h"
#include "dynamicGatt.h"
#include <Arduino.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#ifdef USING_NIMBLE_ARDUINO_HEADERS
#include "nimble/nimble/host/include/host/ble_gatt.h"
#include "nimble/nimble/host/include/host/ble_att.h"
#include "nimble/porting/nimble/include/os/os_mbuf.h"
#else
#include "host/ble_gatt.h"
#include "host/ble_att.h"
#include "os/os_mbuf.h"
#endif
#if BLE_ENABLE_WEB_INTERFACE
#include "bluetoothManager.h"
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <memory>
#include <mutex>
#ifdef USING_NIMBLE_ARDUINO_HEADERS
#include "nimble/porting/nimble/include/nimble/nimble_port.h"
#else
#include "nimble/nimble_port.h"
#endif
#endif

namespace FixtureGatt
{
	namespace
	{
		std::atomic<bool> ready{false};
		constexpr uint8_t scalarValue[] = {0xEB, 0x00};
		constexpr uint8_t aggregateValue[] = {0xEB, 0x00, 0x2A};
		constexpr uint8_t rangeValue[] = {0x70, 0xFE, 0xE8, 0x03};
		constexpr uint8_t unsignedRangeValue[] = {0x02, 0x2A};
		constexpr uint8_t unresolvedRangeValue[] = {0x02, 0x00, 0x2A, 0x00};
		constexpr uint8_t writableDescriptionProperties[] = {0x02, 0x00};
		constexpr uint8_t unsignedValue[] = {0x0A};
		constexpr uint8_t unresolvedValue[] = {0x0A, 0x00};
		constexpr char writableDescriptionText[] = "Writable User Description";
		constexpr std::array<const char*, 8> names = {{"Exponent Zero", "Exponent Positive", "Exponent Negative Minimum",
			"Exponent Negative Two", "Temperature Scalar", "Aggregate Value", "Temperature Update Trigger",
			writableDescriptionText}};
		constexpr std::array<int8_t, 5> exponents = {{0, 127, -128, -2, -1}};
		std::array<NimBLECharacteristic*, 10> fixtures{};
		std::array<NimBLEDescriptor*, 8> descriptions{};
		std::array<NimBLE2904*, 7> formats{};
		NimBLECharacteristic* notificationFixture = nullptr;
		NimBLECharacteristic* indicationFixture = nullptr;
		NimBLECharacteristic* dualUpdateFixture = nullptr;
		NimBLEDescriptor* extendedProperties = nullptr;
		NimBLEDescriptor* aggregate = nullptr;
		NimBLEDescriptor* range = nullptr;
		NimBLEDescriptor* unsignedRange = nullptr;
		NimBLEDescriptor* unresolvedRange = nullptr;
		NimBLEServer* fixtureServer = nullptr;

		bool equals(const NimBLELocalValueAttribute* attribute, const uint8_t* bytes, size_t length)
		{
			if (!attribute) return false;
			const auto value = attribute->getValue();
			return value.size() == length && (length == 0 || std::memcmp(value.data(), bytes, length) == 0);
		}

		bool nativeAttributeEquals(uint16_t handle, const uint8_t* bytes, size_t length)
		{
			if (!handle || length > BLE_ATT_ATTR_MAX_LEN) return false;
			os_mbuf* value = nullptr;
			if (ble_att_svr_read_local(handle, &value) != 0 || !value)
			{
				if (value) os_mbuf_free_chain(value);
				return false;
			}
			const size_t actualLength = OS_MBUF_PKTLEN(value);
			std::array<uint8_t, BLE_ATT_ATTR_MAX_LEN> observed{};
			const bool read = actualLength <= observed.size() &&
				os_mbuf_copydata(value, 0, static_cast<int>(actualLength), observed.data()) == 0;
			os_mbuf_free_chain(value);
			return read && actualLength == length && (length == 0 || std::memcmp(observed.data(), bytes, length) == 0);
		}

		bool registeredDescriptorHandle(NimBLECharacteristic* characteristic, const NimBLEUUID& uuid, uint16_t& handle)
		{
			if (!characteristic || !characteristic->getService()) return false;
			return ble_gatts_find_dsc(characteristic->getService()->getUUID().getBase(), characteristic->getUUID().getBase(),
				uuid.getBase(), &handle) == 0 && handle != 0;
		}

		bool registeredDescriptorMatches(NimBLECharacteristic* characteristic, NimBLEDescriptor* descriptor)
		{
			if (!characteristic || !descriptor || !descriptor->getHandle()) return false;
			uint16_t registeredHandle = 0;
			return registeredDescriptorHandle(characteristic, descriptor->getUUID(), registeredHandle) && registeredHandle == descriptor->getHandle();
		}

		uint8_t declarationProperties(uint16_t properties)
		{
			uint8_t result = 0;
			if (properties & NIMBLE_PROPERTY::BROADCAST) result |= BLE_GATT_CHR_PROP_BROADCAST;
			if (properties & NIMBLE_PROPERTY::READ) result |= BLE_GATT_CHR_PROP_READ;
			if (properties & NIMBLE_PROPERTY::WRITE_NR) result |= BLE_GATT_CHR_PROP_WRITE_NO_RSP;
			if (properties & NIMBLE_PROPERTY::WRITE) result |= BLE_GATT_CHR_PROP_WRITE;
			if (properties & NIMBLE_PROPERTY::NOTIFY) result |= BLE_GATT_CHR_PROP_NOTIFY;
			if (properties & NIMBLE_PROPERTY::INDICATE) result |= BLE_GATT_CHR_PROP_INDICATE;
			if (properties & BLE_GATT_CHR_F_AUTH_SIGN_WRITE) result |= BLE_GATT_CHR_PROP_AUTH_SIGN_WRITE;
			if (properties & (BLE_GATT_CHR_F_RELIABLE_WRITE | BLE_GATT_CHR_F_AUX_WRITE)) result |= BLE_GATT_CHR_PROP_EXTENDED;
			return result;
		}

		bool nativeDeclarationMatches(NimBLECharacteristic* characteristic, uint8_t properties)
		{
			if (!characteristic || !characteristic->getService()) return false;
			uint16_t declarationHandle = 0, valueHandle = 0;
			if (ble_gatts_find_chr(characteristic->getService()->getUUID().getBase(), characteristic->getUUID().getBase(),
				&declarationHandle, &valueHandle) != 0 || valueHandle != characteristic->getHandle()) return false;
			os_mbuf* declaration = nullptr;
			if (ble_att_svr_read_local(declarationHandle, &declaration) != 0 || !declaration) return false;
			const size_t length = OS_MBUF_PKTLEN(declaration);
			uint8_t actualProperties = 0;
			const bool read = length >= 1 && os_mbuf_copydata(declaration, 0, 1, &actualProperties) == 0;
			os_mbuf_free_chain(declaration);
			return read && actualProperties == properties;
		}

		class TemperatureTrigger final : public NimBLECharacteristicCallbacks
		{
			void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo&) override
			{
				const auto value = characteristic->getValue();
				if (Ready() && value.size() == 1 && value.data()[0] == UpdateTrigger && !fixtures[4]->notify())
					Serial.println("[ERR] Temperature update request failed");
			}
		};

		class PublicationCallback : public NimBLECharacteristicCallbacks
		{
		public:
			explicit PublicationCallback(const char* uuid) : uuid_(uuid) {}

#if BLE_ENABLE_WEB_INTERFACE
			void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo& connInfo, uint16_t subValue) override
			{
				BluetoothManager::Instance().RecordGattSubscription(connInfo, uuid_, subValue);
			}

			void onStatus(NimBLECharacteristic*, NimBLEConnInfo& connInfo, int status) override
			{
				BluetoothManager::Instance().RecordGattPublicationStatus(connInfo, uuid_, status);
			}
#endif

		private:
			const char* uuid_;
		};

#if BLE_ENABLE_WEB_INTERFACE
		struct Entry
		{
			NimBLECharacteristic* characteristic = nullptr;
			const char* name = nullptr;
			const char* serviceUuid = nullptr;
			const char* uuid = nullptr;
		};
		std::array<Entry, 4> entries{};
		uint64_t inventoryBoot = 0;
		std::atomic<bool> hostWorkPending{false};
		enum class Work { Snapshot, Publish };
		struct HostJob
		{
			ble_npl_event event{};
			SemaphoreHandle_t done = xSemaphoreCreateBinary();
			std::mutex completionMutex;
			bool canceled = false;
			bool complete = false;
			Work work = Work::Snapshot;
			ConnectionId id{};
			GattValue::Buffer bytes{};
			size_t length = 0;
			WebResult result;
			~HostJob() { if (done) vSemaphoreDelete(done); }
		};

		struct PublicationLimits
		{
			size_t maximum = GattValue::MaxWebLength;
			size_t smallestMtu = 0;
			size_t peerCount = 0;
		};

		PublicationLimits publicationLimits()
		{
			PublicationLimits limits;
			for (auto handle : fixtureServer->getPeerDevices())
			{
				const size_t mtu = fixtureServer->getPeerInfoByHandle(handle).getMTU();
				if (!limits.peerCount || mtu < limits.smallestMtu) limits.smallestMtu = mtu;
				++limits.peerCount;
				limits.maximum = std::min(limits.maximum, mtu >= 3 ? mtu - 3 : size_t{0});
			}
			return limits;
		}

		WebResult executeHost(const HostJob& job)
		{
			if (!Ready()) return {Outcome::NotReady, "GATT fixture is not ready."};
			WebResult result;
			if (job.work == Work::Snapshot)
			{
				const auto limits = publicationLimits();
				for (size_t i = 0; i < entries.size(); ++i)
				{
					const auto& entry = entries[i];
					const auto current = entry.characteristic->getValue();
					ValueSnapshot snapshot;
					snapshot.id = {inventoryBoot, static_cast<uint32_t>(i + 1)};
					snapshot.name = entry.name;
					snapshot.serviceUuid = entry.serviceUuid;
					snapshot.characteristicUuid = entry.uuid;
					snapshot.properties = entry.characteristic->getProperties();
					snapshot.maxLength = current.max_size();
					snapshot.publicationLimit = std::min(limits.maximum, snapshot.maxLength);
					snapshot.minimumPeerMtu = limits.smallestMtu;
					snapshot.connectedPeerCount = limits.peerCount;
					snapshot.current.assign(current.begin(), current.end());
					result.values.push_back(std::move(snapshot));
				}
				result.outcome = Outcome::Ready;
				result.detail = "Current stored values observed.";
				return result;
			}
			if (job.id.boot != inventoryBoot || job.id.sequence < 1 || job.id.sequence > entries.size())
				return {Outcome::NotFound, "GATT target is unknown or belongs to another boot."};
			const auto& entry = entries[job.id.sequence - 1];
			auto* characteristic = entry.characteristic;
			const auto properties = characteristic->getProperties();
			if (!(properties & (NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE)))
				return {Outcome::InvalidArgument, "GATT target does not support publication."};
			const uint8_t* bytes = job.bytes.data();
			const size_t length = job.length;
			const auto previous = characteristic->getValue();
			const size_t localMaximum = std::min(size_t{previous.max_size()}, GattValue::MaxWebLength);
			if (length > localMaximum)
			{
				const char* reason = previous.max_size() < GattValue::MaxWebLength ? "characteristic storage" : "Web payload";
				return {Outcome::InvalidArgument, "Cannot send " + std::to_string(length) + " bytes: the " + reason +
					" maximum is " + std::to_string(localMaximum) + " bytes. Nothing was changed or sent."};
			}
			const auto limits = publicationLimits();
			if (length > limits.maximum)
			{
				std::string detail = "Cannot send " + std::to_string(length) + " bytes: the connected-peer update limit is " +
					std::to_string(limits.maximum) + " bytes. ";
				if (limits.smallestMtu >= 3)
					detail += "Smallest negotiated MTU " + std::to_string(limits.smallestMtu) +
						" minus 3 protocol bytes. The limit includes every connected peer. ";
				else detail += "Negotiated MTU is not available. ";
				detail += "Nothing was changed or sent.";
				return {Outcome::InvalidArgument, std::move(detail)};
			}
			characteristic->setValue(bytes, length);
			if (!equals(characteristic, bytes, length))
				return {Outcome::Failed, "Stored-value verification failed; publication was not attempted."};
			result.storageVerified = true;
			result.publicationAttempted = true;
			result.publicationDispatched = properties & NIMBLE_PROPERTY::NOTIFY ? characteristic->notify() : characteristic->indicate();
			result.outcome = result.publicationDispatched ? Outcome::Dispatched : Outcome::Failed;
			result.detail = result.publicationDispatched ?
				"Value stored; NimBLE accepted the characteristic update request. Peer dispatch and client-application receipt are not proven." :
				"Value stored; NimBLE rejected the characteristic update request. Stored bytes were retained.";
			return result;
		}

		void hostCallback(ble_npl_event* event)
		{
			// The queued owner keeps both the event and semaphore alive after a caller timeout.
			std::unique_ptr<std::shared_ptr<HostJob>> owner(static_cast<std::shared_ptr<HostJob>*>(ble_npl_event_get_arg(event)));
			const auto job = *owner;
			{
				// Private completion/lifetime lock: no BLE callback or other application state uses it.
				std::lock_guard<std::mutex> lock(job->completionMutex);
				if (!job->canceled) job->result = executeHost(*job);
				job->complete = true;
			}
			hostWorkPending.store(false);
			xSemaphoreGive(job->done);
		}

		WebResult runHost(Work work, ConnectionId id, const GattValue::Buffer& bytes, size_t length)
		{
			{
			DynamicGatt::Entry entry;
			if (!entry || !Ready()) return {Outcome::NotReady, "GATT fixture is not ready."};
			if (length > bytes.size()) return {Outcome::InvalidArgument, "Value exceeds the Web payload limit."};
			bool expected = false;
			if (!hostWorkPending.compare_exchange_strong(expected, true))
				return {Outcome::Busy, "GATT host work is pending; retry after refreshing."};
			}
			auto job = std::make_shared<HostJob>();
			if (!job->done)
			{
				hostWorkPending.store(false);
				return {Outcome::Failed, "GATT completion allocation failed."};
			}
			job->work = work;
			job->id = id;
			job->bytes = bytes;
			job->length = length;
			ble_npl_event_init(&job->event, hostCallback, new std::shared_ptr<HostJob>(job));
			ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &job->event);
			xSemaphoreTake(job->done, pdMS_TO_TICKS(1000));
			std::lock_guard<std::mutex> lock(job->completionMutex);
			if (!job->complete)
			{
				job->canceled = true;
				return {Outcome::Busy, "GATT host timeout; queued request was canceled before execution."};
			}
			return std::move(job->result);
		}
#endif

		enum class UpdateKind
		{
			Notify,
			Indicate
		};

		class FixtureUpdateCallback final : public PublicationCallback
		{
		public:
			FixtureUpdateCallback(UpdateKind kind, const char* uuid) : PublicationCallback(uuid), kind_(kind) {}

			void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo&) override
			{
				const auto value = characteristic->getValue();
				if (value.size() != 1 || value.data()[0] != UpdateTrigger)
					return;

				const auto* payload = kind_ == UpdateKind::Notify ? reinterpret_cast<const uint8_t*>(NotificationPayload) : reinterpret_cast<const uint8_t*>(IndicationPayload);
				const auto payloadLength = kind_ == UpdateKind::Notify ? sizeof(NotificationPayload) - 1 : sizeof(IndicationPayload) - 1;

				characteristic->setValue(payload, payloadLength);
				if (!equals(characteristic, payload, payloadLength))
				{
					Serial.println(kind_ == UpdateKind::Notify ?
						"[ERR] Notification fixture stored-value verification failed" :
						"[ERR] Indication fixture stored-value verification failed");
					return;
				}

				const bool accepted = kind_ == UpdateKind::Notify ? characteristic->notify() : characteristic->indicate();
				if (!accepted)
					Serial.println(kind_ == UpdateKind::Notify ?
						"[ERR] Notification update request failed" :
						"[ERR] Indication update request failed");
			}

		private:
			UpdateKind kind_;
		};
	}

	NimBLECharacteristic* Create(NimBLEServer& server)
	{
		if (fixtureServer) return nullptr; // No partial retry or schema re-registration.
		fixtureServer = &server;
		static FixtureUpdateCallback notifyCallback(UpdateKind::Notify, NotificationCharacteristicUuid);
		static FixtureUpdateCallback indicateCallback(UpdateKind::Indicate, IndicationCharacteristicUuid);
		static PublicationCallback dualUpdateCallback(DualUpdateCharacteristicUuid);

		auto* service = server.createService(ServiceUuid);
		if (!service) return nullptr;
		auto* existingCharacteristic = service->createCharacteristic(ExistingCharacteristicUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
		if (!existingCharacteristic) return nullptr;
		existingCharacteristic->setValue(ExistingCharacteristicValue);

		notificationFixture = service->createCharacteristic(NotificationCharacteristicUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
		if (!notificationFixture) return nullptr;
		notificationFixture->setValue(NotificationCharacteristicValue);
		notificationFixture->setCallbacks(&notifyCallback);

		auto* customDescriptor = notificationFixture->createDescriptor(CustomDescriptorUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
		if (!customDescriptor) return nullptr;
		customDescriptor->setValue(CustomDescriptorValue);

		indicationFixture = service->createCharacteristic(IndicationCharacteristicUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE);
		if (!indicationFixture) return nullptr;
		indicationFixture->setValue(IndicationCharacteristicValue);
		indicationFixture->setCallbacks(&indicateCallback);

		dualUpdateFixture = service->createCharacteristic(DualUpdateCharacteristicUuid,
			NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);
		if (!dualUpdateFixture) return nullptr;
		dualUpdateFixture->setValue("Dual update characteristic");
		dualUpdateFixture->setCallbacks(&dualUpdateCallback);

		auto* descriptorService = server.createService(DescriptorServiceUuid);
		if (!descriptorService) return nullptr;
		for (size_t i = 0; i < fixtures.size(); ++i)
		{
			char uuid[37];
			std::snprintf(uuid, sizeof(uuid), "12345678-1234-5678-1234-56789abcdf%02u", static_cast<unsigned>(i + 1));
			const uint16_t properties = i == 6 ? NIMBLE_PROPERTY::WRITE : i == 4 ?
				NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY : i == 7 ?
				NIMBLE_PROPERTY::READ | BLE_GATT_CHR_F_AUX_WRITE : NIMBLE_PROPERTY::READ;
			const uint16_t maxLength = i == 6 ? 1 : i == 5 ? 3 : i == 4 || i == 7 ? BLE_ATT_ATTR_MAX_LEN : i == 8 ? 1 : 2;
			fixtures[i] = descriptorService->createCharacteristic(uuid, properties, maxLength);
			if (!fixtures[i]) return nullptr;
			auto* characteristic = fixtures[i];
			if (i < 5) characteristic->setValue(scalarValue, sizeof(scalarValue));
			else if (i == 5) characteristic->setValue(aggregateValue, sizeof(aggregateValue));
			else if (i == 8) characteristic->setValue(unsignedValue, sizeof(unsignedValue));
			else if (i == 9) characteristic->setValue(unresolvedValue, sizeof(unresolvedValue));

			if (i < descriptions.size())
			{
				const uint16_t descriptionPermissions = i == 7 ? NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE : NIMBLE_PROPERTY::READ;
				const uint16_t descriptionMaxLength = i == 7 ? 128 : std::strlen(names[i]);
				descriptions[i] = characteristic->createDescriptor(NimBLEUUID(uint16_t{0x2901}), descriptionPermissions, descriptionMaxLength);
				if (!descriptions[i]) return nullptr;
				if (i == 7)
				{
					descriptions[i]->setValue(reinterpret_cast<const uint8_t*>(writableDescriptionText), sizeof(writableDescriptionText) - 1);
					extendedProperties = characteristic->createDescriptor(NimBLEUUID(uint16_t{0x2900}), NIMBLE_PROPERTY::READ,
						sizeof(writableDescriptionProperties));
					if (!extendedProperties) return nullptr;
					extendedProperties->setValue(writableDescriptionProperties, sizeof(writableDescriptionProperties));
				}
				else
					descriptions[i]->setValue(reinterpret_cast<const uint8_t*>(names[i]), std::strlen(names[i]));
			}
			else if (i == 8)
			{
				formats[6] = characteristic->create2904();
				if (!formats[6]) return nullptr;
				formats[6]->setFormat(0x04);
				formats[6]->setExponent(0);
				formats[6]->setUnit(0x2700);
				formats[6]->setNamespace(1);
				formats[6]->setDescription(0);
			}
		}
		for (size_t i = 0; i < 6; ++i)
		{
			formats[i] = fixtures[i]->create2904();
			if (!formats[i]) return nullptr;
			formats[i]->setFormat(i < 5 ? 0x0E : 0x04);
			formats[i]->setExponent(i < 5 ? exponents[i] : 0);
			formats[i]->setUnit(i < 5 ? 0x272F : 0x2700);
			formats[i]->setNamespace(1);
			formats[i]->setDescription(0);
		}
		range = fixtures[3]->createDescriptor(NimBLEUUID(uint16_t{0x2906}), NIMBLE_PROPERTY::READ, sizeof(rangeValue));
		aggregate = fixtures[5]->createDescriptor(NimBLEUUID(uint16_t{0x2905}), NIMBLE_PROPERTY::READ, 4);
		unsignedRange = fixtures[8]->createDescriptor(NimBLEUUID(uint16_t{0x2906}), NIMBLE_PROPERTY::READ, sizeof(unsignedRangeValue));
		unresolvedRange = fixtures[9]->createDescriptor(NimBLEUUID(uint16_t{0x2906}), NIMBLE_PROPERTY::READ, sizeof(unresolvedRangeValue));
		if (!range || !aggregate || !unsignedRange || !unresolvedRange) return nullptr;
		range->setValue(rangeValue, sizeof(rangeValue));
		unsignedRange->setValue(unsignedRangeValue, sizeof(unsignedRangeValue));
		unresolvedRange->setValue(unresolvedRangeValue, sizeof(unresolvedRangeValue));
		constexpr uint8_t placeholder[4]{};
		aggregate->setValue(placeholder, sizeof(placeholder));
		static TemperatureTrigger temperatureTrigger;
		fixtures[6]->setCallbacks(&temperatureTrigger);

#if BLE_ENABLE_WEB_INTERFACE
		inventoryBoot = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
		if (!inventoryBoot) inventoryBoot = 1;
		entries = {{
			{notificationFixture, "Original notification", ServiceUuid, NotificationCharacteristicUuid},
			{indicationFixture, "Original indication", ServiceUuid, IndicationCharacteristicUuid},
			{fixtures[4], names[4], DescriptorServiceUuid, TemperatureUuid},
			{dualUpdateFixture, "Dual notification and indication", ServiceUuid, DualUpdateCharacteristicUuid}
		}};
#endif

		return existingCharacteristic;
	}

	bool Ready() { return ready.load(); }

	bool finalize(bool initialSeeds)
	{
		if (Ready()) return true;
		if (!fixtureServer || !extendedProperties || !aggregate || !range || !unsignedRange || !unresolvedRange ||
			!notificationFixture || !indicationFixture || !dualUpdateFixture) return false;
		// Descriptor getters expose BLE_ATT_F_* permissions after NimBLE's
		// constructor conversion; characteristic getters retain NIMBLE_PROPERTY.
		for (size_t i = 0; i < fixtures.size(); ++i)
		{
			char uuid[37];
			std::snprintf(uuid, sizeof(uuid), "12345678-1234-5678-1234-56789abcdf%02u", static_cast<unsigned>(i + 1));
			const NimBLEUUID expectedUuid(uuid);
			const uint16_t properties = i == 6 ? NIMBLE_PROPERTY::WRITE : i == 4 ?
				NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY : i == 7 ?
				NIMBLE_PROPERTY::READ | BLE_GATT_CHR_F_AUX_WRITE : NIMBLE_PROPERTY::READ;
			if (!fixtures[i] || fixtures[i]->getUUID() != expectedUuid || !fixtures[i]->getHandle() ||
				fixtures[i]->getProperties() != properties || !nativeDeclarationMatches(fixtures[i], declarationProperties(properties))) return false;
			if (i != 6)
			{
				const auto value = fixtures[i]->getValue();
				if (!nativeAttributeEquals(fixtures[i]->getHandle(), value.data(), value.size())) return false;
			}
			if (initialSeeds && i < 5 && !equals(fixtures[i], scalarValue, sizeof(scalarValue))) return false;
			if (initialSeeds && i == 5 && !equals(fixtures[i], aggregateValue, sizeof(aggregateValue))) return false;
			if (initialSeeds && i == 6 && fixtures[i]->getLength() != 0) return false;
			if (initialSeeds && i == 8 && !equals(fixtures[i], unsignedValue, sizeof(unsignedValue))) return false;
			if (initialSeeds && i == 9 && !equals(fixtures[i], unresolvedValue, sizeof(unresolvedValue))) return false;

			const NimBLEUUID userDescriptionUuid(uint16_t{0x2901});
			const NimBLEUUID extendedPropertiesUuid(uint16_t{0x2900});
			if (i < descriptions.size())
			{
				const uint16_t permissions = i == 7 ? BLE_ATT_F_READ | BLE_ATT_F_WRITE : BLE_ATT_F_READ;
				const size_t maxLength = i == 7 ? 128 : std::strlen(names[i]);
				if (!descriptions[i] || !descriptions[i]->getHandle() || descriptions[i]->getUUID() != userDescriptionUuid ||
					fixtures[i]->getDescriptorByUUID(userDescriptionUuid) != descriptions[i] ||
					fixtures[i]->getDescriptorByUUID(userDescriptionUuid, 1) != nullptr ||
					descriptions[i]->getCharacteristic() != fixtures[i] || descriptions[i]->getProperties() != permissions ||
					descriptions[i]->getValue().max_size() != maxLength || !registeredDescriptorMatches(fixtures[i], descriptions[i])) return false;
				if ((i < 7 || initialSeeds) && !equals(descriptions[i], reinterpret_cast<const uint8_t*>(names[i]), std::strlen(names[i]))) return false;
				const auto descriptionValue = descriptions[i]->getValue();
				if (!nativeAttributeEquals(descriptions[i]->getHandle(), descriptionValue.data(), descriptionValue.size())) return false;
			}
			else if (fixtures[i]->getDescriptorByUUID(userDescriptionUuid)) return false;

			if (i == 7)
			{
				if (fixtures[i]->getDescriptorByUUID(extendedPropertiesUuid) != extendedProperties ||
					fixtures[i]->getDescriptorByUUID(extendedPropertiesUuid, 1) != nullptr ||
					extendedProperties->getUUID() != extendedPropertiesUuid || !extendedProperties->getHandle() ||
					extendedProperties->getCharacteristic() != fixtures[i] || extendedProperties->getProperties() != BLE_ATT_F_READ ||
					extendedProperties->getValue().max_size() != sizeof(writableDescriptionProperties) ||
					!equals(extendedProperties, writableDescriptionProperties, sizeof(writableDescriptionProperties)) ||
					!registeredDescriptorMatches(fixtures[i], extendedProperties) ||
					!nativeAttributeEquals(extendedProperties->getHandle(), writableDescriptionProperties, sizeof(writableDescriptionProperties))) return false;
				const auto extendedValue = extendedProperties->getValue();
				const bool descriptorAllowsAuxiliaryWrite = extendedValue.size() == sizeof(writableDescriptionProperties) &&
					(extendedValue.data()[0] & 0x02u) != 0;
				const bool declarationAllowsAuxiliaryWrite =
					(fixtures[i]->getProperties() & BLE_GATT_CHR_F_AUX_WRITE) != 0;
				if (descriptorAllowsAuxiliaryWrite != declarationAllowsAuxiliaryWrite) return false;
			}
			else if (fixtures[i]->getDescriptorByUUID(extendedPropertiesUuid)) return false;

			if (i < 6)
			{
				const uint8_t expected[] = {static_cast<uint8_t>(i < 5 ? 0x0E : 0x04), static_cast<uint8_t>(i < 5 ? exponents[i] : 0),
					static_cast<uint8_t>(i < 5 ? 0x2F : 0), 0x27, 1, 0, 0};
				if (!formats[i] || !formats[i]->getHandle() || formats[i]->getUUID() != NimBLEUUID(uint16_t{0x2904}) ||
					formats[i]->getCharacteristic() != fixtures[i] || formats[i]->getProperties() != BLE_ATT_F_READ ||
					!registeredDescriptorMatches(fixtures[i], formats[i]) || !equals(formats[i], expected, sizeof(expected)) ||
					!nativeAttributeEquals(formats[i]->getHandle(), expected, sizeof(expected))) return false;
			}
		}
		const uint8_t unsignedFormatValue[] = {0x04, 0x00, 0x00, 0x27, 0x01, 0x00, 0x00};
		if (!formats[6] || !formats[6]->getHandle() || formats[6]->getUUID() != NimBLEUUID(uint16_t{0x2904}) ||
			formats[6]->getCharacteristic() != fixtures[8] || formats[6]->getProperties() != BLE_ATT_F_READ ||
			formats[6]->getValue().max_size() != sizeof(unsignedFormatValue) ||
			fixtures[8]->getDescriptorByUUID(NimBLEUUID(uint16_t{0x2904})) != formats[6] ||
			fixtures[8]->getDescriptorByUUID(NimBLEUUID(uint16_t{0x2904}), 1) != nullptr ||
			!registeredDescriptorMatches(fixtures[8], formats[6]) || !equals(formats[6], unsignedFormatValue, sizeof(unsignedFormatValue)) ||
			!nativeAttributeEquals(formats[6]->getHandle(), unsignedFormatValue, sizeof(unsignedFormatValue)) ||
			fixtures[8]->getDescriptorByUUID(NimBLEUUID(uint16_t{0x2905})) ||
			fixtures[9]->getDescriptorByUUID(NimBLEUUID(uint16_t{0x2904})) || fixtures[9]->getDescriptorByUUID(NimBLEUUID(uint16_t{0x2905}))) return false;

		const auto checkRange = [](NimBLEDescriptor* descriptor, NimBLECharacteristic* characteristic, const uint8_t* value, size_t length)
		{
			return descriptor && descriptor->getHandle() && descriptor->getUUID() == NimBLEUUID(uint16_t{0x2906}) &&
				descriptor->getCharacteristic() == characteristic && descriptor->getProperties() == BLE_ATT_F_READ &&
				descriptor->getValue().max_size() == length && characteristic->getDescriptorByUUID(NimBLEUUID(uint16_t{0x2906})) == descriptor &&
				characteristic->getDescriptorByUUID(NimBLEUUID(uint16_t{0x2906}), 1) == nullptr &&
				registeredDescriptorMatches(characteristic, descriptor) && equals(descriptor, value, length) &&
				nativeAttributeEquals(descriptor->getHandle(), value, length);
		};
		const uint16_t first = formats[4]->getHandle(), second = formats[5]->getHandle();
		if (first == second || !aggregate->getHandle() || aggregate->getUUID() != NimBLEUUID(uint16_t{0x2905}) ||
			aggregate->getValue().max_size() != 4 || aggregate->getCharacteristic() != fixtures[5] ||
			aggregate->getProperties() != BLE_ATT_F_READ || fixtures[5]->getDescriptorByUUID(NimBLEUUID(uint16_t{0x2905})) != aggregate ||
			fixtures[5]->getDescriptorByUUID(NimBLEUUID(uint16_t{0x2905}), 1) != nullptr || !registeredDescriptorMatches(fixtures[5], aggregate) ||
			!range || range->getValue().max_size() != sizeof(rangeValue) || !checkRange(range, fixtures[3], rangeValue, sizeof(rangeValue)) ||
			!checkRange(unsignedRange, fixtures[8], unsignedRangeValue, sizeof(unsignedRangeValue)) ||
			!checkRange(unresolvedRange, fixtures[9], unresolvedRangeValue, sizeof(unresolvedRangeValue))) return false;

		const NimBLEUUID cccdUuid(uint16_t{0x2902});
		for (auto* characteristic : {notificationFixture, indicationFixture, dualUpdateFixture})
		{
			uint16_t cccdHandle = 0;
			if (!characteristic->getHandle() || characteristic->getDescriptorByUUID(cccdUuid) ||
				!registeredDescriptorHandle(characteristic, cccdUuid, cccdHandle)) return false;
		}
		const uint16_t dualProperties = NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE;
		if (notificationFixture->getUUID() != NimBLEUUID(NotificationCharacteristicUuid) ||
			notificationFixture->getProperties() != (NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY) ||
			!nativeDeclarationMatches(notificationFixture, declarationProperties(NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY)) ||
			indicationFixture->getUUID() != NimBLEUUID(IndicationCharacteristicUuid) ||
			indicationFixture->getProperties() != (NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE) ||
			!nativeDeclarationMatches(indicationFixture, declarationProperties(NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE)) ||
			dualUpdateFixture->getUUID() != NimBLEUUID(DualUpdateCharacteristicUuid) ||
			dualUpdateFixture->getProperties() != dualProperties || !nativeDeclarationMatches(dualUpdateFixture, declarationProperties(dualProperties))) return false;

		const uint8_t handles[] = {static_cast<uint8_t>(first), static_cast<uint8_t>(first >> 8),
			static_cast<uint8_t>(second), static_cast<uint8_t>(second >> 8)};
		aggregate->setValue(handles, sizeof(handles));
		if (!equals(aggregate, handles, sizeof(handles)) || !nativeAttributeEquals(aggregate->getHandle(), handles, sizeof(handles))) return false;
		Serial.printf("[BT] Descriptor fixture finalized | Temperature CPF:%u | Aggregate CPF:%u | 0x2905:%u\n", first, second, aggregate->getHandle());
		ready.store(true);
		return true;
	}

    bool Finalize() { return finalize(true); }
    bool BeginRebuild()
    {
#if BLE_ENABLE_WEB_INTERFACE
        if (hostWorkPending.load()) return false;
#endif
        if (!Ready()) return false;
        ready.store(false);
        return true;
    }
    bool FinalizeAfterRebuild() { return finalize(false); }

#if BLE_ENABLE_WEB_INTERFACE
	WebResult GetValues() { return runHost(Work::Snapshot, {}, {}, 0); }
	WebResult ChangeValue(ConnectionId id, const GattValue::Buffer& bytes, size_t length)
	{
		return runHost(Work::Publish, id, bytes, length);
	}
#endif
}
