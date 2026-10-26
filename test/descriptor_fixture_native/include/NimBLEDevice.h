#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#define BLE_ATT_ATTR_MAX_LEN 512
#define BLE_GATT_CHR_F_BROADCAST 0x0001
#define BLE_GATT_CHR_F_READ 0x0002
#define BLE_GATT_CHR_F_WRITE_NO_RSP 0x0004
#define BLE_GATT_CHR_F_WRITE 0x0008
#define BLE_GATT_CHR_F_NOTIFY 0x0010
#define BLE_GATT_CHR_F_INDICATE 0x0020
#define BLE_ATT_F_READ 0x01
#define BLE_ATT_F_WRITE 0x02

namespace NIMBLE_PROPERTY
{
inline constexpr uint16_t BROADCAST = BLE_GATT_CHR_F_BROADCAST;
inline constexpr uint16_t READ = BLE_GATT_CHR_F_READ;
inline constexpr uint16_t WRITE_NR = BLE_GATT_CHR_F_WRITE_NO_RSP;
inline constexpr uint16_t WRITE = BLE_GATT_CHR_F_WRITE;
inline constexpr uint16_t NOTIFY = BLE_GATT_CHR_F_NOTIFY;
inline constexpr uint16_t INDICATE = BLE_GATT_CHR_F_INDICATE;
}

class NimBLEServer;

namespace NimBLETest
{
enum class FactoryKind { Service, Characteristic, Descriptor, PresentationFormat };
enum class Fault { None, DescriptionWritePermission, ZeroTemperatureCpfHandle, DuplicateCpfHandles, CorruptAggregateReadback };

struct Counters
{
	size_t serviceCreates = 0;
	size_t characteristicCreates = 0;
	size_t descriptorCreates = 0;
	size_t presentationFormatCreates = 0;
	size_t serverStarts = 0;
	size_t actualRegistrations = 0;
	size_t notifyCalls = 0;
	size_t indicateCalls = 0;
};

struct Controls
{
	Counters counters{};
	size_t factoryCalls = 0;
	size_t failFactoryOrdinal = 0;
	bool registrationSucceeds = true;
	Fault postRegistrationFault = Fault::None;
	bool registrationComplete = false;
	std::vector<uint8_t> lastNotification;
	std::vector<uint8_t> lastIndication;
};

Controls& controls();
void reset();
bool allowFactory(FactoryKind kind);
bool corruptAggregateReadback();
void afterRegistration(::NimBLEServer& server);
}

class NimBLEUUID
{
public:
	explicit NimBLEUUID(const char* value) : stringValue_(value), short_(false), shortValue_(0) {}
	explicit NimBLEUUID(std::string value) : stringValue_(std::move(value)), short_(false), shortValue_(0) {}
	explicit NimBLEUUID(uint16_t value) : short_(true), shortValue_(value) {}

	bool operator==(const NimBLEUUID& other) const
	{
		return short_ == other.short_ && (short_ ? shortValue_ == other.shortValue_ : stringValue_ == other.stringValue_);
	}
	bool operator!=(const NimBLEUUID& other) const { return !(*this == other); }
	std::string toString() const { return short_ ? std::to_string(shortValue_) : stringValue_; }

private:
	std::string stringValue_;
	bool short_;
	uint16_t shortValue_;
};

class NimBLEAttValue
{
public:
	explicit NimBLEAttValue(uint16_t maxLength = BLE_ATT_ATTR_MAX_LEN) : maxLength_(maxLength) {}
	bool setValue(const uint8_t* bytes, size_t length)
	{
		value_.clear();
		if (length > maxLength_ || (length && !bytes)) return false;
		if (length) value_.assign(bytes, bytes + length);
		return true;
	}
	bool setValue(const char* text)
	{
		return setValue(reinterpret_cast<const uint8_t*>(text), text ? std::strlen(text) : 0);
	}
	template <typename T> bool setValue(const T& value)
	{
		return setValue(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
	}
	const uint8_t* data() const { return value_.empty() ? nullptr : value_.data(); }
	size_t size() const { return value_.size(); }
	size_t max_size() const { return maxLength_; }
	auto begin() const { return value_.begin(); }
	auto end() const { return value_.end(); }

private:
	uint16_t maxLength_;
	std::vector<uint8_t> value_;
};

class NimBLEConnInfo {};
class NimBLECharacteristic;

class NimBLELocalValueAttribute
{
public:
	virtual ~NimBLELocalValueAttribute() = default;
	const NimBLEUUID& getUUID() const { return uuid_; }
	uint16_t getHandle() const { return handle_; }
	uint16_t getProperties() const { return properties_; }
	const NimBLEAttValue& getValue() const { return value_; }
	uint16_t getLength() const { return static_cast<uint16_t>(value_.size()); }
	void setValue(const uint8_t* bytes, size_t length)
	{
		if (isDescriptor_ && uuid_ == NimBLEUUID(uint16_t{0x2905}) && NimBLETest::corruptAggregateReadback()) return;
		value_.setValue(bytes, length);
	}
	void setValue(const char* text) { value_.setValue(text); }
	template <typename T> void setValue(const T& value) { value_.setValue(value); }
	void setHandleForTest(uint16_t handle) { handle_ = handle; }
	void setPropertiesForTest(uint16_t properties) { properties_ = properties; }

protected:
	NimBLELocalValueAttribute(const NimBLEUUID& uuid, uint16_t properties, uint16_t maxLength, bool isDescriptor)
		: uuid_(uuid), properties_(properties), value_(maxLength), isDescriptor_(isDescriptor) {}
	void setHandle(uint16_t handle) { handle_ = handle; }

private:
	NimBLEUUID uuid_;
	uint16_t properties_ = 0;
	uint16_t handle_ = 0;
	NimBLEAttValue value_;
	bool isDescriptor_ = false;
	friend class NimBLEServer;
};

class NimBLECharacteristicCallbacks
{
public:
	virtual ~NimBLECharacteristicCallbacks() = default;
	virtual void onRead(NimBLECharacteristic*, NimBLEConnInfo&) {}
	virtual void onWrite(NimBLECharacteristic*, NimBLEConnInfo&) {}
};

class NimBLEDescriptor : public NimBLELocalValueAttribute
{
public:
	NimBLEDescriptor(const NimBLEUUID& uuid, uint32_t properties, uint16_t maxLength, NimBLECharacteristic* characteristic);
	NimBLECharacteristic* getCharacteristic() const { return characteristic_; }

private:
	NimBLECharacteristic* characteristic_;
	friend class NimBLECharacteristic;
};

#pragma pack(push, 1)
struct NimBLE2904Data
{
	uint8_t format = 0;
	int8_t exponent = 0;
	uint16_t unit = 0x2700;
	uint8_t nameSpace = 1;
	uint16_t description = 0;
};
#pragma pack(pop)

class NimBLE2904 final : public NimBLEDescriptor
{
public:
	explicit NimBLE2904(NimBLECharacteristic* characteristic);
	void setDescription(uint16_t value) { data_.description = value; setValue(data_); }
	void setExponent(int8_t value) { data_.exponent = value; setValue(data_); }
	void setFormat(uint8_t value) { data_.format = value; setValue(data_); }
	void setNamespace(uint8_t value) { data_.nameSpace = value; setValue(data_); }
	void setUnit(uint16_t value) { data_.unit = value; setValue(data_); }

private:
	NimBLE2904Data data_{};
};

class NimBLEService;
class NimBLEServerCallbacks
{
public:
	virtual ~NimBLEServerCallbacks() = default;
	virtual void onConnect(NimBLEServer*, NimBLEConnInfo&) {}
	virtual void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) {}
	virtual void onMTUChange(uint16_t, NimBLEConnInfo&) {}
	virtual uint32_t onPassKeyDisplay() { return 0; }
};

class NimBLECharacteristic final : public NimBLELocalValueAttribute
{
public:
	NimBLECharacteristic(const NimBLEUUID& uuid, uint32_t properties, uint16_t maxLength, NimBLEService* service)
		: NimBLELocalValueAttribute(uuid, static_cast<uint16_t>(properties), maxLength, false), service_(service) {}
	NimBLEDescriptor* createDescriptor(const NimBLEUUID& uuid, uint32_t properties, uint16_t maxLength = BLE_ATT_ATTR_MAX_LEN);
	NimBLEDescriptor* createDescriptor(const char* uuid, uint32_t properties, uint16_t maxLength = BLE_ATT_ATTR_MAX_LEN)
	{
		return createDescriptor(NimBLEUUID(uuid), properties, maxLength);
	}
	NimBLE2904* create2904();
	NimBLEDescriptor* getDescriptorByUUID(const NimBLEUUID& uuid, uint16_t index = 0) const;
	NimBLEDescriptor* getDescriptorByUUID(const char* uuid, uint16_t index = 0) const
	{
		return getDescriptorByUUID(NimBLEUUID(uuid), index);
	}
	void setCallbacks(NimBLECharacteristicCallbacks* callbacks) { callbacks_ = callbacks; }
	NimBLECharacteristicCallbacks* callbacksForTest() const { return callbacks_; }
	void writeForTest(const std::vector<uint8_t>& bytes)
	{
		setValue(bytes.data(), bytes.size());
		if (callbacks_) { NimBLEConnInfo info; callbacks_->onWrite(this, info); }
	}
	bool notify() const;
	bool notify(const uint8_t* bytes, size_t length) const;
	bool indicate() const;
	bool indicate(const uint8_t* bytes, size_t length) const;
	const std::vector<std::unique_ptr<NimBLEDescriptor>>& descriptorsForTest() const { return descriptors_; }
	NimBLEService* serviceForTest() const { return service_; }

private:
	void addDescriptor(std::unique_ptr<NimBLEDescriptor> descriptor) { descriptors_.push_back(std::move(descriptor)); }
	NimBLEService* service_;
	NimBLECharacteristicCallbacks* callbacks_ = nullptr;
	std::vector<std::unique_ptr<NimBLEDescriptor>> descriptors_;
	friend class NimBLEServer;
};

class NimBLEService
{
public:
	explicit NimBLEService(const NimBLEUUID& uuid) : uuid_(uuid) {}
	NimBLECharacteristic* createCharacteristic(const char* uuid, uint32_t properties, uint16_t maxLength = BLE_ATT_ATTR_MAX_LEN)
	{
		return createCharacteristic(NimBLEUUID(uuid), properties, maxLength);
	}
	NimBLECharacteristic* createCharacteristic(const NimBLEUUID& uuid, uint32_t properties, uint16_t maxLength = BLE_ATT_ATTR_MAX_LEN);
	NimBLECharacteristic* getCharacteristic(const NimBLEUUID& uuid) const;
	NimBLECharacteristic* getCharacteristic(const char* uuid) const { return getCharacteristic(NimBLEUUID(uuid)); }
	const NimBLEUUID& getUUID() const { return uuid_; }
	uint16_t getHandle() const { return handle_; }
	const std::vector<std::unique_ptr<NimBLECharacteristic>>& characteristicsForTest() const { return characteristics_; }
	size_t managedCccdCountForTest() const { return managedCccdCount_; }

private:
	NimBLEUUID uuid_;
	uint16_t handle_ = 0;
	std::vector<std::unique_ptr<NimBLECharacteristic>> characteristics_;
	size_t managedCccdCount_ = 0;
	friend class NimBLEServer;
};

class NimBLEServer
{
public:
	NimBLEService* createService(const char* uuid);
	NimBLEService* getServiceByUUID(const char* uuid) const { return getServiceByUUID(NimBLEUUID(uuid)); }
	NimBLEService* getServiceByUUID(const NimBLEUUID& uuid) const;
	void setCallbacks(NimBLEServerCallbacks* callbacks) { callbacks_ = callbacks; }
	NimBLEServerCallbacks* callbacksForTest() const { return callbacks_; }
	void advertiseOnDisconnect(bool enabled) { advertiseOnDisconnect_ = enabled; }
	bool advertiseOnDisconnectForTest() const { return advertiseOnDisconnect_; }
	bool start();
	const std::vector<std::unique_ptr<NimBLEService>>& servicesForTest() const { return services_; }

private:
	std::vector<std::unique_ptr<NimBLEService>> services_;
	NimBLEServerCallbacks* callbacks_ = nullptr;
	bool advertiseOnDisconnect_ = false;
};
