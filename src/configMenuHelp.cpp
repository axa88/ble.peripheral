// configMenuHelp.h
#include "configMenuHelp.h"
#include <NimBLEDevice.h>

namespace ConfigMenuHelp
{
	void printConnInfo(const NimBLEConnInfo &connInfo)
	{
		Serial.printf("=== NimBLEConnInfo ===\n");
		Serial.printf("OTA Address: %s\n", formatAddress(connInfo.getAddress()));
		Serial.printf("ID Address: %s\n", formatAddress(connInfo.getIdAddress()));
		Serial.printf("Conn Handle: %u\n", connInfo.getConnHandle());
		Serial.printf("Conn Interval: %.2f ms\n", connInfo.getConnInterval() * 1.25f);
		Serial.printf("Supervision Timeout: %u ms\n", connInfo.getConnTimeout() * 10u);
		Serial.printf("Conn Latency: %u\n", connInfo.getConnLatency());
		Serial.printf("MTU: %u\n", connInfo.getMTU());
		Serial.printf("Role: %s\n", (connInfo.isMaster() ? "Master" : (connInfo.isSlave() ? "Slave" : "Unknown")));
		Serial.printf("Bonded: %s\n", connInfo.isBonded() ? "Yes" : "No");
		Serial.printf("Encrypted: %s\n", connInfo.isEncrypted() ? "Yes" : "No");
		Serial.printf("Authenticated: %s\n", connInfo.isAuthenticated() ? "Yes" : "No");
		Serial.printf("Security Key Size: %u\n", connInfo.getSecKeySize());
		Serial.printf("======================\n");
	}

	std::array<char, 18> formatAddress(const NimBLEAddress &addr)
	{
		std::array<char, 18> buf{};
		const uint8_t *v = addr.getVal();
		if (v == nullptr)
		{
			std::snprintf(buf.data(), buf.size(), "<no address>");
			return buf;
		}

		std::snprintf(buf.data(), buf.size(), "%02X:%02X:%02X:%02X:%02X:%02X", v[5], v[4], v[3], v[2], v[1], v[0]);

		return buf;
	}
} // namespace ConfigMenuHelp
