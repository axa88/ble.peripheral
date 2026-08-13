#include "fixtureGatt.h"

namespace FixtureGatt
{
	namespace
	{
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
		static FixtureUpdateCallback notifyCallback(UpdateKind::Notify);
		static FixtureUpdateCallback indicateCallback(UpdateKind::Indicate);

		auto* service = server.createService(ServiceUuid);
		auto* existingCharacteristic = service->createCharacteristic(ExistingCharacteristicUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
		existingCharacteristic->setValue(ExistingCharacteristicValue);

		auto* notificationCharacteristic = service->createCharacteristic(NotificationCharacteristicUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
		notificationCharacteristic->setValue(NotificationCharacteristicValue);
		notificationCharacteristic->setCallbacks(&notifyCallback);

		auto* customDescriptor = notificationCharacteristic->createDescriptor(CustomDescriptorUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
		customDescriptor->setValue(CustomDescriptorValue);

		auto* indicationCharacteristic = service->createCharacteristic(IndicationCharacteristicUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE);
		indicationCharacteristic->setValue(IndicationCharacteristicValue);
		indicationCharacteristic->setCallbacks(&indicateCallback);

		return existingCharacteristic;
	}
}
