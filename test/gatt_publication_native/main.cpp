#include "fixtureGatt.h"
#include <array>
#include <cstdio>
#include <string>

namespace FixtureGatt
{
extern bool nativeReady;
extern NimBLEServer* fixtureServer;
WebResult nativePublish(const std::vector<uint8_t>& bytes, ConnectionId id = {99, 2});
WebResult nativeSnapshot();
void nativeEntries(NimBLECharacteristic*, NimBLECharacteristic*, NimBLECharacteristic*);
}

int main()
{
	using namespace FixtureGatt;
	unsigned passed = 0;
	auto check = [&](bool ok, const char* text) {
		if (!ok) { std::fprintf(stderr, "FAIL %s\n", text); return false; }
		++passed; return true;
	};
	NimBLEServer server;
	NimBLECharacteristic notification, indication, temperature;
	indication.properties = NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::INDICATE;
	nativeEntries(&notification, &indication, &temperature);
	fixtureServer = &server;
	const std::string original = "Indicate Characteristic";
	const std::vector<uint8_t> originalBytes(original.begin(), original.end());
	server.mtus = {23};
	auto result = nativePublish({0xAA});
	if (!check(result.outcome == Outcome::Dispatched && indication.indications == 1 &&
		indication.value.bytes == std::vector<uint8_t>{0xAA}, "one byte sends at MTU 23")) return 1;
	result = nativePublish(originalBytes);
	if (!check(result.outcome == Outcome::InvalidArgument && indication.indications == 1 &&
		indication.value.bytes == std::vector<uint8_t>{0xAA}, "23 bytes rejected before storage/publication at MTU 23")) return 1;
	if (!check(result.detail.find("23 bytes") != std::string::npos &&
		result.detail.find("20 bytes") != std::string::npos && result.detail.find("MTU 23") != std::string::npos &&
		result.detail.find("3 protocol bytes") != std::string::npos &&
		result.detail.find("Shorten") == std::string::npos && result.detail.find("MTU 26") == std::string::npos, "rejection owns numeric MTU explanation without remedy advice")) return 1;
	server.mtus = {517};
	result = nativePublish(originalBytes);
	if (!check(result.outcome == Outcome::Dispatched && indication.value.bytes == originalBytes &&
		indication.indications == 2, "23 bytes send at MTU 517")) return 1;
	server.mtus = {517, 23};
	result = nativePublish(originalBytes);
	if (!check(result.outcome == Outcome::InvalidArgument && indication.indications == 2 &&
		result.detail.find("20 bytes") != std::string::npos, "smallest connected peer constrains publication")) return 1;
	result = nativeSnapshot();
	if (!check(result.values.size() == 3 && result.values[1].minimumPeerMtu == 23 &&
		result.values[1].connectedPeerCount == 2 && result.values[1].publicationLimit == 20,
		"snapshot reports the same authoritative peer count/MTU and bound")) return 1;
	server.mtus.clear();
	result = nativeSnapshot();
	if (!check(result.values[1].connectedPeerCount == 0 && result.values[1].minimumPeerMtu == 0 &&
		result.values[1].publicationLimit == 256, "no-peer snapshot is explicit, not proof of delivery")) return 1;
	server.mtus = {0, 517};
	result = nativePublish({0xBB});
	if (!check(result.outcome == Outcome::InvalidArgument && result.detail.find("MTU is not available") != std::string::npos &&
		indication.value.bytes == originalBytes, "unknown MTU stays conservative without a false minimum")) return 1;
	server.mtus = {517};
	indication.value.maximum = 16;
	result = nativePublish(originalBytes);
	if (!check(result.outcome == Outcome::InvalidArgument && result.detail.find("storage maximum is 16") != std::string::npos,
		"storage capacity has its own explanation")) return 1;
	indication.value.maximum = 512;
	result = nativePublish(std::vector<uint8_t>(257, 0xAB));
	if (!check(result.outcome == Outcome::InvalidArgument && result.detail.find("Web payload maximum is 256") != std::string::npos,
		"Web cap has its own explanation")) return 1;
	result = nativePublish({0xBB}, {98, 2});
	if (!check(result.outcome == Outcome::NotFound && indication.value.bytes == originalBytes, "old boot identity cannot mutate")) return 1;
	indication.dispatchSucceeds = false;
	result = nativePublish({0xCC});
	if (!check(result.outcome == Outcome::Failed && result.storageVerified && result.publicationAttempted &&
		!result.publicationDispatched && indication.value.bytes == std::vector<uint8_t>{0xCC},
		"dispatch failure retains verified storage and truthful flags")) return 1;
	indication.dispatchSucceeds = true;
	result = nativePublish({});
	if (!check(result.outcome == Outcome::Dispatched && indication.value.bytes.empty(), "empty publication remains supported")) return 1;
	nativeReady = false;
	result = nativePublish({0xDD});
	if (!check(result.outcome == Outcome::NotReady && indication.value.bytes.empty(), "readiness blocks mutation")) return 1;
	std::printf("PASS %u production host-publication checks; adapter does not establish radio delivery.\n", passed);
	return 0;
}
