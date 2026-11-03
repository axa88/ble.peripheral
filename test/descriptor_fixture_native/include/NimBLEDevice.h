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
#define BLE_GATT_CHR_F_AUTH_SIGN_WRITE 0x0040
#define BLE_GATT_CHR_F_RELIABLE_WRITE 0x0080
#define BLE_GATT_CHR_F_AUX_WRITE 0x0100
#define BLE_GATT_CHR_PROP_BROADCAST 0x01
#define BLE_GATT_CHR_PROP_READ 0x02
#define BLE_GATT_CHR_PROP_WRITE_NO_RSP 0x04
#define BLE_GATT_CHR_PROP_WRITE 0x08
#define BLE_GATT_CHR_PROP_NOTIFY 0x10
#define BLE_GATT_CHR_PROP_INDICATE 0x20
#define BLE_GATT_CHR_PROP_AUTH_SIGN_WRITE 0x40
#define BLE_GATT_CHR_PROP_EXTENDED 0x80
#define BLE_ATT_F_READ 0x01
#define BLE_ATT_F_WRITE 0x02
#define BLE_HS_ENOENT 2

namespace NIMBLE_PROPERTY
{
inline constexpr uint16_t BROADCAST = BLE_GATT_CHR_F_BROADCAST;
inline constexpr uint16_t READ = BLE_GATT_CHR_F_READ;
inline constexpr uint16_t WRITE_NR = BLE_GATT_CHR_F_WRITE_NO_RSP;
inline constexpr uint16_t WRITE = BLE_GATT_CHR_F_WRITE;
inline constexpr uint16_t NOTIFY = BLE_GATT_CHR_F_NOTIFY;
inline constexpr uint16_t INDICATE = BLE_GATT_CHR_F_INDICATE;
inline constexpr uint16_t AUTH_SIGN_WRITE = BLE_GATT_CHR_F_AUTH_SIGN_WRITE;
}

class NimBLEServer;
class NimBLEService;
class NimBLELocalValueAttribute;
class NimBLECharacteristic;

namespace NimBLETest
{
enum class FactoryKind { Service, Characteristic, Descriptor, PresentationFormat };
enum class Fault
{
	None,
	DescriptionWritePermission,
	ZeroTemperatureCpfHandle,
	DuplicateCpfHandles,
	CorruptAggregateReadback,
	CorruptExtendedDeclaration,
	CorruptExtendedProperties
};
enum class AttResult { Success, NotFound, ReadNotPermitted, WriteNotPermitted, InvalidLength };

struct NativeAttribute
{
	NimBLELocalValueAttribute* attribute = nullptr;
	uint16_t handle = 0;
	uint16_t permissions = 0;
	bool descriptor = false;
};

struct NativeDeclaration
{
	NimBLECharacteristic* characteristic = nullptr;
	uint16_t declarationHandle = 0;
	uint16_t valueHandle = 0;
	uint8_t properties = 0;
};

struct NativeService
{
	NimBLEService* service = nullptr;
	uint16_t handle = 0;
};

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
	size_t notifyPayloadOverloadCalls = 0;
	size_t indicatePayloadOverloadCalls = 0;
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
	std::vector<NativeAttribute> nativeAttributes;
	std::vector<NativeDeclaration> nativeDeclarations;
	std::vector<NativeService> nativeServices;
	std::vector<std::pair<NimBLECharacteristic*, uint16_t>> managedCccds;
};

Controls& controls();
void reset();
bool allowFactory(FactoryKind kind);
bool corruptAggregateReadback();
void afterRegistration(::NimBLEServer& server);
AttResult attRead(uint16_t handle, std::vector<uint8_t>& bytes);
AttResult attWrite(uint16_t handle, const std::vector<uint8_t>& bytes, uint16_t mtu = 23);
bool readCharacteristicDeclaration(const NimBLECharacteristic* characteristic, std::vector<uint8_t>& bytes);
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
	const NimBLEUUID* getBase() const { return this; }

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
	NimBLEService* getService() const { return service_; }

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
	const std::vector<NimBLECharacteristic*>& getCharacteristics() const { return characteristicView_; }
	NimBLEServer* getServer() const { return server_; }
	bool getRemoved() const { return removed_; }
	void setServerForTest(NimBLEServer* server) { server_ = server; }
	void setRemovedForTest(bool removed) { removed_ = removed; }
	void addCharacteristicViewForTest(NimBLECharacteristic* characteristic) { characteristicView_.push_back(characteristic); }

private:
	NimBLEUUID uuid_;
	uint16_t handle_ = 0;
	NimBLEServer* server_ = nullptr;
	bool removed_ = false;
	std::vector<std::unique_ptr<NimBLECharacteristic>> characteristics_;
	std::vector<NimBLECharacteristic*> characteristicView_;
	size_t managedCccdCount_ = 0;
	friend class NimBLEServer;
};

struct os_mbuf
{
	std::vector<uint8_t> bytes;
};

#define OS_MBUF_PKTLEN(value) ((value)->bytes.size())
inline int os_mbuf_copydata(const os_mbuf* source, size_t offset, size_t length, void* destination)
{
	if (!source || offset > source->bytes.size() || length > source->bytes.size() - offset) return -1;
	if (length) std::memcpy(destination, source->bytes.data() + offset, length);
	return 0;
}
inline void os_mbuf_free_chain(os_mbuf* value) { delete value; }

int ble_gatts_find_svc(const NimBLEUUID* service, uint16_t* handle);
int ble_gatts_find_chr(const NimBLEUUID* service, const NimBLEUUID* characteristic,
	uint16_t* declarationHandle, uint16_t* valueHandle);
int ble_gatts_find_dsc(const NimBLEUUID* service, const NimBLEUUID* characteristic,
	const NimBLEUUID* descriptor, uint16_t* handle);
int ble_att_svr_read_local(uint16_t handle, os_mbuf** data);
int ble_gatts_reset();
void ble_gatts_stop();
int ble_gap_adv_active();
int ble_gap_disc_active();
int ble_gap_conn_active();
uint32_t esp_random();

class NimBLEServer
{
public:
	NimBLEService* createService(const char* uuid);
	NimBLEService* getServiceByUUID(const char* uuid) const { return getServiceByUUID(NimBLEUUID(uuid)); }
	NimBLEService* getServiceByUUID(const NimBLEUUID& uuid) const;
	NimBLEService* getServiceByHandle(uint16_t handle) const;
	void removeService(NimBLEService* service, bool deleteService);
	uint8_t getConnectedCount() const { return 0; }
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
