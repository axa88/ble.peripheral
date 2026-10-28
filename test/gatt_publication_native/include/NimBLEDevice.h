#pragma once
#include <cstdint>
#include <vector>

namespace NIMBLE_PROPERTY
{
inline constexpr uint16_t READ = 0x02;
inline constexpr uint16_t WRITE = 0x08;
inline constexpr uint16_t NOTIFY = 0x10;
inline constexpr uint16_t INDICATE = 0x20;
}

struct NativeValue
{
	std::vector<uint8_t> bytes;
	uint16_t maximum = 512;
	size_t size() const { return bytes.size(); }
	const uint8_t* data() const { return bytes.data(); }
	auto begin() const { return bytes.begin(); }
	auto end() const { return bytes.end(); }
	uint16_t max_size() const { return maximum; }
};

class NimBLELocalValueAttribute
{
public:
	NativeValue value;
	NativeValue getValue() const { return value; }
	void setValue(const uint8_t* bytes, size_t length)
	{
		if (length > value.maximum) return;
		if (length) value.bytes.assign(bytes, bytes + length);
		else value.bytes.clear();
	}
};

class NimBLECharacteristic : public NimBLELocalValueAttribute
{
public:
	uint16_t properties = NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY;
	bool dispatchSucceeds = true;
	unsigned notifications = 0;
	unsigned indications = 0;
	uint16_t getProperties() const { return properties; }
	bool notify() { ++notifications; return dispatchSucceeds; }
	bool indicate() { ++indications; return dispatchSucceeds; }
};

class NimBLEServer
{
public:
	struct Peer { size_t mtu; size_t getMTU() const { return mtu; } };
	std::vector<size_t> mtus;
	std::vector<uint16_t> getPeerDevices() const
	{
		std::vector<uint16_t> handles;
		for (uint16_t i = 0; i < mtus.size(); ++i) handles.push_back(i);
		return handles;
	}
	Peer getPeerInfoByHandle(uint16_t handle) const { return {mtus.at(handle)}; }
};
