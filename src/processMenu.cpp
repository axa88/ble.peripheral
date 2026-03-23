// processMenu.cpp
// #include <Arduino.h>
#include "processMenu.h"
#include "bluetoothManager.h"
#include "configMenuHelp.h"
#include <Arduino.h>    // Serial, millis(), etc.
#include <optional>     // std::optional
#include <algorithm>    // std::find
#include <atomic>       // std::atomic
#include <cctype>       // toupper
#include <cstdint>      // uint16_t, uint32_t
#include <random>

namespace
{
	static std::optional<uint16_t> selectedHandle;

	void listConnection()
	{
		auto server = BluetoothManager::Instance().Server();
		auto connections = server->getPeerDevices();
		if (connections.empty())
		{
			Serial.println("No Connections");
			return;
		}

		Serial.println("Connected Peers:");
		for (size_t i = 0; i < connections.size(); ++i)
		{
			uint16_t handle = connections[i];
			NimBLEConnInfo connInfo = server->getPeerInfoByHandle(handle);
			const char* marker = (selectedHandle.has_value() && selectedHandle.value() == handle) ? "-->" : "   ";
			Serial.printf("%s[%zu] handle:%u address:%s\n", marker, i, static_cast<unsigned>(handle), connInfo.getAddress().toString().c_str());
		}
	}

	void reportPeerState(const NimBLEConnInfo& connInfo)
	{
		Serial.println(connInfo.toString().c_str());
		listConnection();
	}

	void printConfig()
	{
		Serial.println();
		Serial.println("=== Current Config >>>");
		// Serial.printf("Device Address: %s\n", NimBLEDevice::getAddress().toString().c_str());
		Serial.printf("Device Address: %s\n", NimBLEDevice::toString().c_str());

		auto& btMgr = BluetoothManager::Instance();

		Serial.println("<--- Security ---");
		std::string_view caps = ConfigMenuHelp::capIoToString(btMgr.Capabilities());
		Serial.printf("capabilities: %.*s\n", static_cast<int>(caps.size()), caps.data());
		std::string_view auth = ConfigMenuHelp::authToString(btMgr.Authentication());
		Serial.printf("authentication: %.*s\n", static_cast<int>(auth.size()), auth.data());
		std::string_view enc = ConfigMenuHelp::encToString(btMgr.Encryption());
		Serial.printf("encryption: %.*s\n", static_cast<int>(enc.size()), enc.data());
		Serial.println("--- -------- --->");
		Serial.println("<--- Connections ---");
		if (!btMgr.Server())
			Serial.println("Server Uninitialized");
		else
			listConnection();
		Serial.println("--- ----------- --->");
		Serial.println("<<< Current Config ===");
	}

	std::optional<char> readCommandChar() // Skip leading whitespace, CR/LF, read a single char
	{
		while (Serial.available() > 0)
		{
			int p = Serial.peek();
			if (p < 0)
				return std::nullopt;

			char pc = static_cast<char>(p);
			if (pc == ' ' || pc == '\r' || pc == '\n')
			{
				Serial.read();
				if (pc == '\r' && Serial.available() > 0 && Serial.peek() == '\n')
					Serial.read();
				continue;
			}
			break;
		}

		if (Serial.available() == 0)
			return std::nullopt;

		int r = Serial.read();
		if (r < 0)
			return std::nullopt;

		char cmd = static_cast<char>(toupper(static_cast<unsigned char>(r)));
		return cmd;
	}
} // namespace

namespace ProcessMenu
{
	std::atomic<ConsoleMode> consoleMode{ ConsoleMode::Config };

	void printConfigMenu()
	{
		Serial.println();
		Serial.println("Set Pairing:");
		Serial.println("- A --> Cycle I/O Capabilities:");
		Serial.println();
		Serial.println("Set Authorization:");
		Serial.println("- B --> Toggle Bond");
		Serial.println("- C --> Toggle MitM");
		Serial.println("- D --> Toggle Secure Connections");
		Serial.println("- E --> Toggle Key Press");
		Serial.println();
		Serial.println("Set Encryption:");
		Serial.println("- F --> Toggle Long Term Key, used to encrypt the BLE link after pairing");
		Serial.println("- G --> Toggle Identity Resolving Key, used for privacy and address resolution");
		Serial.println("- H --> Toggle Connection Signature Resolving Key, used for data signing (authenticated but unencrypted operations)");
		Serial.println("- I --> Toggle BR/EDR Link Key, used for authentication and encryption in BR/EDR connections and for BLE/BR-EDR coexistence");
		Serial.println();
		Serial.println("Set State:");
		Serial.println("- J --> Disconnect Selected");
		Serial.println();
		Serial.println("Set Advertising:");
		Serial.println("- K --> Toggle Advert Restart on Disconnect");
		Serial.println("- L --> Toggle Advertising");
		Serial.println();
		Serial.println("Peer:");
		Serial.println("- M --> Get Mtu");
		Serial.println("- N --> Set Phy");
		Serial.println("- O --> Set Connection Parameters");
		Serial.println();
		Serial.println("- Y --> Print Config");
		Serial.println("- Z --> Print Menu");
		Serial.println();
		Serial.println("- 0-9 --> Select Connection");
		Serial.println();
	}

	void handleConfigInput()
	{
		ConsoleMode mode = consoleMode;
		if (mode != ConsoleMode::Config)
			return;

		auto c = readCommandChar();
		if (!c)
			return;

		auto& btMgr = BluetoothManager::Instance();
		switch (*c)
		{
			case 'A':
			{
				std::string_view sv = ConfigMenuHelp::capIoToString(btMgr.Capabilities(ConfigMenuHelp::capIoNext(btMgr.Capabilities())));
				Serial.printf("%.*s\n", static_cast<int>(sv.size()), sv.data());
				printConfig();
				break;
			}

			case 'B':
				btMgr.Authentication(ConfigMenuHelp::authToggleBond(btMgr.Authentication()));
				Serial.println("Auth Bond Toggled");
				printConfig();
				break;
			case 'C':
				btMgr.Authentication(ConfigMenuHelp::authToggleMitm(btMgr.Authentication()));
				Serial.println("Auth MitM toggled");
				printConfig();
				break;
			case 'D':
				btMgr.Authentication(ConfigMenuHelp::authToggleSC(btMgr.Authentication()));
				Serial.println("Auth Secure Connections Toggled");
				printConfig();
				break;
			case 'E':
				btMgr.Authentication(ConfigMenuHelp::authToggleKP(btMgr.Authentication()));
				Serial.println("Auth Key Press Toggled");
				printConfig();
				break;

			case 'F':
				btMgr.Encryption(ConfigMenuHelp::encToggleLTK(btMgr.Encryption()));
				Serial.println("Long Term Key Toggled");
				printConfig();
				break;
			case 'G':
				btMgr.Encryption(ConfigMenuHelp::encToggleIRK(btMgr.Encryption()));
				Serial.println("Identity Resolving Key Toggled");
				printConfig();
				break;
			case 'H':
				btMgr.Encryption(ConfigMenuHelp::encToggleCSRK(btMgr.Encryption()));
				Serial.println("Connection Signature Resolving Key Toggled");
				printConfig();
				break;
			case 'I':
				btMgr.Encryption(ConfigMenuHelp::encToggleLK(btMgr.Encryption()));
				Serial.println("BR/EDR Link Key Toggled");
				printConfig();
				break;

			case 'J':
			{
				if (!btMgr.Server()) { Serial.println("Server Uninitialized"); break; }
				if (!selectedHandle.has_value()) { Serial.println("No selected Connection"); break; }

				btMgr.Server()->disconnect(selectedHandle.value());
				break;
			}

			case 'K':
			{
				Serial.printf("restart advert on disconnect:%s\n", btMgr.AdvertisingRestartOnDisconnect(!btMgr.AdvertisingRestartOnDisconnect()) ? "yes" : "no");
				break;
			}
			case 'L':
			{
				Serial.printf("Advert is:%s\n", btMgr.AdvertisingState((0), !btMgr.AdvertisingState(0)) ? "on" : "off");
				break;
			}

			case 'M':
			{
				if (!btMgr.Server()) { Serial.println("Server Uninitialized"); break; }
				if (!selectedHandle.has_value()) { Serial.println("No selected Connection"); break; }

				Serial.printf("Peer Mtu is:%u\n", btMgr.GetPeerMtu(selectedHandle.value()));
				break;
			}

			case 'N':
			{
				if (!btMgr.Server()) { Serial.println("Server Uninitialized"); break; }
				if (!selectedHandle.has_value()) { Serial.println("No selected Connection"); break; }

				if (auto opt = btMgr.Phy(selectedHandle.value(), BluetoothManager::PhyUpdate{ .txPhysMask = BLE_GAP_LE_PHY_1M_MASK, .rxPhysMask = BLE_GAP_LE_PHY_2M_MASK, .phyOptions = BLE_GAP_LE_PHY_CODED_ANY }))
				{
					const auto& [tx, rx] = *opt;
					Serial.printf("After request tx=%u rx=%u\n", static_cast<unsigned>(tx), static_cast<unsigned>(rx));
				}
				else
					Serial.println("Phy update/read failed");

				break;
			}

			case 'O':
			{
				if (!btMgr.Server()) { Serial.println("Server Uninitialized"); break; }
				if (!selectedHandle.has_value()) { Serial.println("No selected Connection"); break; }

				using namespace std::chrono_literals;
				btMgr.UpdateConnectionParams(selectedHandle.value(), 30ms, 50ms, 0, 4s);
				printConfig();
				break;
			}

			case 'P':
			{
				if (!btMgr.Server()) { Serial.println("Server Uninitialized"); break; }
				if (!selectedHandle.has_value()) { Serial.println("No selected Connection"); break; }

				uint16_t rando = 0x001B + (esp_random() % (0x00FB - 0x001B + 1)); // 0x001B (27) to 0x00FB (251)
				btMgr.RequestDataLength(selectedHandle.value(), rando);
				printConfig();
				break;
			}

			case 'Y': printConfig(); break;

			case 'Z': printConfigMenu(); break;

			case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
			{
				if (!btMgr.Server()) { Serial.println("Server Uninitialized"); break; }

				auto connHandles = btMgr.Server()->getPeerDevices();
				if (connHandles.empty()) { Serial.println("No Connections"); break; }

				size_t selection = *c - '0';
				if (selection >= connHandles.size())
				{
					Serial.println("Invalid selection\n");

					if (selectedHandle.has_value() && std::find(connHandles.begin(), connHandles.end(), selectedHandle.value()) == connHandles.end())
					{
						Serial.println("Previously selected connection no longer valid; clearing selection");
						selectedHandle.reset();
					}

					break;
				}

				selectedHandle = connHandles[selection];
				uint16_t handle = selectedHandle.value();
				Serial.printf("Selected item %zu (handle %u)\n", selection, static_cast<unsigned>(handle));
				Serial.printf("%s\n", btMgr.Server()->getPeerInfoByHandle(handle).toString().c_str());

				break;
			}

			default: break;
		}

		if (Serial.available() > 0 && Serial.peek() == '\r')
			Serial.read();
		if (Serial.available() > 0 && Serial.peek() == '\n')
			Serial.read();

		Serial.println();
	}

	void handleConfigInputSub()
	{
		ConsoleMode mode = consoleMode;
		if (mode != ConsoleMode::ConfigSub)
			return;

		auto c = readCommandChar();
		if (!c)
			return;

		switch (*c)
		{
			case 'D': Serial.println("- D --> Disconnect");

			case 'X':
				consoleMode = ConsoleMode::Config;
				printConfig();
				break;
			default: break;
		}
	}

	void setupProcessMenu()
	{
		auto& btMgr = BluetoothManager::Instance();
		btMgr.SubscribeToEvent(BluetoothManager::Event::Connect, reportPeerState);
		btMgr.SubscribeToEvent
		(
			BluetoothManager::Event::Disconnect, [](const NimBLEConnInfo& connInfo)
			{
				reportPeerState(connInfo);
				if (selectedHandle == connInfo.getConnHandle()) // reset the selected connection on disconnect
					selectedHandle.reset();
			}
		);
		btMgr.SubscribeToEvent
		(
			BluetoothManager::Event::AuthComplete, [](const NimBLEConnInfo& connInfo)
			{
				ProcessMenu::consoleMode = ConsoleMode::Config;
				reportPeerState(connInfo);
			}
		);
	}

} // ProcessMenu
