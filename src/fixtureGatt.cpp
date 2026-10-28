#include "fixtureGatt.h"
#include <Arduino.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#if BLE_ENABLE_WEB_INTERFACE
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
		constexpr std::array<const char*, 7> names = {{"Exponent Zero", "Exponent Positive", "Exponent Negative Minimum",
			"Exponent Negative Two", "Temperature Scalar", "Aggregate Value", "Temperature Update Trigger"}};
		constexpr std::array<int8_t, 5> exponents = {{0, 127, -128, -2, -1}};
		std::array<NimBLECharacteristic*, 7> fixtures{};
		std::array<NimBLEDescriptor*, 7> descriptions{};
		std::array<NimBLE2904*, 6> formats{};
		NimBLEDescriptor* aggregate = nullptr;
		NimBLEDescriptor* range = nullptr;
		NimBLEServer* fixtureServer = nullptr;

		bool equals(const NimBLELocalValueAttribute* attribute, const uint8_t* bytes, size_t length)
		{
			if (!attribute) return false;
			const auto value = attribute->getValue();
			return value.size() == length && (length == 0 || std::memcmp(value.data(), bytes, length) == 0);
		}

		class TemperatureTrigger final : public NimBLECharacteristicCallbacks
		{
			void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo&) override
			{
				const auto value = characteristic->getValue();
				if (Ready() && value.size() == 1 && value.data()[0] == UpdateTrigger && !fixtures[4]->notify())
					Serial.println("[ERR] Temperature publication dispatch failed");
			}
		};

#if BLE_ENABLE_WEB_INTERFACE
		struct Entry
		{
			NimBLECharacteristic* characteristic = nullptr;
			const char* name = nullptr;
			const char* serviceUuid = nullptr;
			const char* uuid = nullptr;
		};
		std::array<Entry, 3> entries{};
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
			result.detail = result.publicationDispatched ? "Value stored; publication dispatched. Verify receipt on the BLE client." :
				"Value stored; publication dispatch failed. Stored bytes were retained.";
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
			if (!Ready()) return {Outcome::NotReady, "GATT fixture is not ready."};
			if (length > bytes.size()) return {Outcome::InvalidArgument, "Value exceeds the Web payload limit."};
			bool expected = false;
			if (!hostWorkPending.compare_exchange_strong(expected, true))
				return {Outcome::Busy, "GATT host work is pending; retry after refreshing."};
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

		class FixtureUpdateCallback final : public NimBLECharacteristicCallbacks
		{
		public:
			explicit FixtureUpdateCallback(UpdateKind kind) : kind_(kind) {}

			void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo&) override
			{
				const auto value = characteristic->getValue();
				if (value.size() != 1 || value.data()[0] != UpdateTrigger)
					return;

				const auto* payload = kind_ == UpdateKind::Notify ? reinterpret_cast<const uint8_t*>(NotificationPayload) : reinterpret_cast<const uint8_t*>(IndicationPayload);
				const auto payloadLength = kind_ == UpdateKind::Notify ? sizeof(NotificationPayload) - 1 : sizeof(IndicationPayload) - 1;

				if (kind_ == UpdateKind::Notify)
					characteristic->notify(payload, payloadLength);
				else
					characteristic->indicate(payload, payloadLength);
			}

		private:
			UpdateKind kind_;
		};
	}

	NimBLECharacteristic* Create(NimBLEServer& server)
	{
		if (fixtureServer) return nullptr; // No partial retry or schema re-registration.
		fixtureServer = &server;
		static FixtureUpdateCallback notifyCallback(UpdateKind::Notify);
		static FixtureUpdateCallback indicateCallback(UpdateKind::Indicate);

		auto* service = server.createService(ServiceUuid);
		if (!service) return nullptr;
		auto* existingCharacteristic = service->createCharacteristic(ExistingCharacteristicUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
		if (!existingCharacteristic) return nullptr;
		existingCharacteristic->setValue(ExistingCharacteristicValue);

		auto* notificationCharacteristic = service->createCharacteristic(NotificationCharacteristicUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
		if (!notificationCharacteristic) return nullptr;
		notificationCharacteristic->setValue(NotificationCharacteristicValue);
		notificationCharacteristic->setCallbacks(&notifyCallback);

		auto* customDescriptor = notificationCharacteristic->createDescriptor(CustomDescriptorUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
		if (!customDescriptor) return nullptr;
		customDescriptor->setValue(CustomDescriptorValue);

		auto* indicationCharacteristic = service->createCharacteristic(IndicationCharacteristicUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE);
		if (!indicationCharacteristic) return nullptr;
		indicationCharacteristic->setValue(IndicationCharacteristicValue);
		indicationCharacteristic->setCallbacks(&indicateCallback);

		auto* descriptorService = server.createService(DescriptorServiceUuid);
		if (!descriptorService) return nullptr;
		for (size_t i = 0; i < fixtures.size(); ++i)
		{
			char uuid[37];
			std::snprintf(uuid, sizeof(uuid), "12345678-1234-5678-1234-56789abcdf%02u", static_cast<unsigned>(i + 1));
			const uint16_t properties = i == 6 ? NIMBLE_PROPERTY::WRITE : i == 4 ?
				NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY : NIMBLE_PROPERTY::READ;
			const uint16_t maxLength = i == 6 ? 1 : i == 5 ? 3 : i == 4 ? BLE_ATT_ATTR_MAX_LEN : 2;
			fixtures[i] = descriptorService->createCharacteristic(uuid, properties, maxLength);
			if (!fixtures[i]) return nullptr;
			auto* characteristic = fixtures[i];
			if (i < 5) characteristic->setValue(scalarValue, sizeof(scalarValue));
			else if (i == 5) characteristic->setValue(aggregateValue, sizeof(aggregateValue));
			descriptions[i] = characteristic->createDescriptor(NimBLEUUID(uint16_t{0x2901}), NIMBLE_PROPERTY::READ, std::strlen(names[i]));
			if (!descriptions[i]) return nullptr;
			descriptions[i]->setValue(reinterpret_cast<const uint8_t*>(names[i]), std::strlen(names[i]));
			if (i < formats.size())
			{
				formats[i] = characteristic->create2904();
				if (!formats[i]) return nullptr;
				formats[i]->setFormat(i < 5 ? 0x0E : 0x04);
				formats[i]->setExponent(i < 5 ? exponents[i] : 0);
				formats[i]->setUnit(i < 5 ? 0x272F : 0x2700);
				formats[i]->setNamespace(1);
				formats[i]->setDescription(0);
			}
		}
		range = fixtures[3]->createDescriptor(NimBLEUUID(uint16_t{0x2906}), NIMBLE_PROPERTY::READ, sizeof(rangeValue));
		aggregate = fixtures[5]->createDescriptor(NimBLEUUID(uint16_t{0x2905}), NIMBLE_PROPERTY::READ, 4);
		if (!range || !aggregate) return nullptr;
		range->setValue(rangeValue, sizeof(rangeValue));
		constexpr uint8_t placeholder[4]{};
		aggregate->setValue(placeholder, sizeof(placeholder));
		static TemperatureTrigger temperatureTrigger;
		fixtures[6]->setCallbacks(&temperatureTrigger);

#if BLE_ENABLE_WEB_INTERFACE
		inventoryBoot = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
		if (!inventoryBoot) inventoryBoot = 1;
		entries = {{
			{notificationCharacteristic, "Original notification", ServiceUuid, NotificationCharacteristicUuid},
			{indicationCharacteristic, "Original indication", ServiceUuid, IndicationCharacteristicUuid},
			{fixtures[4], names[4], DescriptorServiceUuid, TemperatureUuid}
		}};
#endif

		return existingCharacteristic;
	}

	bool Ready() { return ready.load(); }

	bool Finalize()
	{
		if (Ready()) return true;
		if (!fixtureServer || !aggregate || !range) return false;
		// Descriptor getters expose BLE_ATT_F_* permissions after NimBLE's
		// constructor conversion; characteristic getters retain NIMBLE_PROPERTY.
		for (size_t i = 0; i < fixtures.size(); ++i)
		{
			const auto userDescriptionUuid = NimBLEUUID(uint16_t{0x2901});
			const uint16_t properties = i == 6 ? NIMBLE_PROPERTY::WRITE : i == 4 ?
				NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY : NIMBLE_PROPERTY::READ;
			if (!fixtures[i] || !fixtures[i]->getHandle() || !descriptions[i] || !descriptions[i]->getHandle() ||
				fixtures[i]->getProperties() != properties || descriptions[i]->getUUID() != userDescriptionUuid ||
				fixtures[i]->getDescriptorByUUID(userDescriptionUuid) != descriptions[i] ||
				fixtures[i]->getDescriptorByUUID(userDescriptionUuid, 1) != nullptr ||
				descriptions[i]->getCharacteristic() != fixtures[i] || descriptions[i]->getProperties() != BLE_ATT_F_READ ||
				descriptions[i]->getValue().max_size() != std::strlen(names[i]) ||
				!equals(descriptions[i], reinterpret_cast<const uint8_t*>(names[i]), std::strlen(names[i]))) return false;
			if (i < 5 && !equals(fixtures[i], scalarValue, sizeof(scalarValue))) return false;
			if (i == 5 && !equals(fixtures[i], aggregateValue, sizeof(aggregateValue))) return false;
			if (i == 6 && fixtures[i]->getLength() != 0) return false;
			if (i < formats.size())
			{
				const uint8_t expected[] = {static_cast<uint8_t>(i < 5 ? 0x0E : 0x04), static_cast<uint8_t>(i < 5 ? exponents[i] : 0),
					static_cast<uint8_t>(i < 5 ? 0x2F : 0), 0x27, 1, 0, 0};
				if (!formats[i] || !formats[i]->getHandle() || formats[i]->getUUID() != NimBLEUUID(uint16_t{0x2904}) ||
					formats[i]->getCharacteristic() != fixtures[i] || formats[i]->getProperties() != BLE_ATT_F_READ ||
					!equals(formats[i], expected, sizeof(expected))) return false;
			}
		}
		const uint16_t first = formats[4]->getHandle(), second = formats[5]->getHandle();
		if (first == second || !aggregate->getHandle() || !range->getHandle() ||
			aggregate->getValue().max_size() != 4 || range->getValue().max_size() != sizeof(rangeValue) ||
			aggregate->getCharacteristic() != fixtures[5] || range->getCharacteristic() != fixtures[3] ||
			aggregate->getProperties() != BLE_ATT_F_READ || range->getProperties() != BLE_ATT_F_READ ||
			!equals(range, rangeValue, sizeof(rangeValue))) return false;
		const uint8_t handles[] = {static_cast<uint8_t>(first), static_cast<uint8_t>(first >> 8),
			static_cast<uint8_t>(second), static_cast<uint8_t>(second >> 8)};
		aggregate->setValue(handles, sizeof(handles));
		if (!equals(aggregate, handles, sizeof(handles))) return false;
		Serial.printf("[BT] Descriptor fixture finalized | Temperature CPF:%u | Aggregate CPF:%u | 0x2905:%u\n", first, second, aggregate->getHandle());
		ready.store(true);
		return true;
	}

#if BLE_ENABLE_WEB_INTERFACE
	WebResult GetValues() { return runHost(Work::Snapshot, {}, {}, 0); }
	WebResult ChangeValue(ConnectionId id, const GattValue::Buffer& bytes, size_t length)
	{
		return runHost(Work::Publish, id, bytes, length);
	}
#endif
}
