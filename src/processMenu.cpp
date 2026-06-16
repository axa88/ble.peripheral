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
#include <string>       // std::string
#include <random>

namespace
{
	static std::optional<uint16_t> selectedHandle;

	// Returns a short "address (handle N)" string for the given handle, or "none" if unavailable.
	std::string describeHandle(std::optional<uint16_t> handle)
	{
		if (!handle.has_value())
			return "none";

		auto server = BluetoothManager::Instance().Server();
		if (!server)
			return "none";

		NimBLEConnInfo connInfo = server->getPeerInfoByHandle(handle.value());
		char out[64];
		snprintf(out, sizeof(out), "%s (handle %u)", connInfo.getAddress().toString().c_str(), static_cast<unsigned>(handle.value()));
		return std::string(out);
	}

	void listConnection()
	{
		auto server = BluetoothManager::Instance().Server();
		auto connections = server->getPeerDevices();
		if (connections.empty())
		{
			Serial.println("[BT] No connections");
			return;
		}

		Serial.println("[BT] Connected peers:");
		for (size_t i = 0; i < connections.size(); ++i)
		{
			uint16_t handle = connections[i];
			NimBLEConnInfo connInfo = server->getPeerInfoByHandle(handle);
			const char* marker = (selectedHandle.has_value() && selectedHandle.value() == handle) ? "-->" : "   ";
			Serial.printf("%s [%zu] handle:%u address:%s\n", marker, i, static_cast<unsigned>(handle), connInfo.getAddress().toString().c_str());
		}
	}

	void reportPeerState(const NimBLEConnInfo& connInfo)
	{
		Serial.printf("[BT] %s\n", connInfo.toString().c_str());
		listConnection();
	}

	// Prints a short status line: mode-relevant state at a glance.
	void printStatus()
	{
		auto& btMgr = BluetoothManager::Instance();
		bool advOn = btMgr.AdvertisingState(0);
		Serial.printf("[STATUS] Advertising:%s | Selected:%s\n",
			advOn ? "on" : "off",
			describeHandle(selectedHandle).c_str());
	}

	void printConfig()
	{
		auto& btMgr = BluetoothManager::Instance();

		Serial.println();
		Serial.println("[CFG] ============================================================");
		Serial.println("[CFG] === Peripheral");
		Serial.printf( "[CFG]     Address:        %s\n", NimBLEDevice::toString().c_str());
		Serial.printf( "[CFG]     Advertising:    %s\n", btMgr.AdvertisingState(0) ? "on" : "off");
		Serial.println("[CFG]     --- Security ---");
		std::string_view caps = ConfigMenuHelp::capIoToString(btMgr.Capabilities());
		Serial.printf( "[CFG]     capabilities:   %.*s\n", static_cast<int>(caps.size()), caps.data());
		std::string_view auth = ConfigMenuHelp::authToString(btMgr.Authentication());
		Serial.printf( "[CFG]     authentication: %.*s\n", static_cast<int>(auth.size()), auth.data());
		std::string_view enc = ConfigMenuHelp::encToString(btMgr.Encryption());
		Serial.printf( "[CFG]     encryption:     %.*s\n", static_cast<int>(enc.size()), enc.data());

		Serial.println("[CFG] ============================================================");

		if (!btMgr.Server())
			Serial.println("[CFG] === Server uninitialized");
		else
		{
			auto connections = btMgr.Server()->getPeerDevices();
			if (connections.empty())
				Serial.println("[CFG] === Connected Peers: none connected");
			else
			{
				for (size_t i = 0; i < connections.size(); ++i)
				{
					uint16_t handle = connections[i];
					NimBLEConnInfo info = btMgr.Server()->getPeerInfoByHandle(handle);
					bool selected = selectedHandle.has_value() && selectedHandle.value() == handle;
					Serial.printf("[CFG] === ConnectedPeer [%zu]%s\n", i, selected ? " <-- selected" : "");
					Serial.printf("[CFG]     Address:         %s\n", info.getAddress().toString().c_str());
					Serial.printf("[CFG]     Handle:          %u\n", static_cast<unsigned>(handle));
					Serial.printf("[CFG]     Interval:        %.1f ms\n", info.getConnInterval() * 1.25f);
					Serial.printf("[CFG]     Timeout:         %u ms\n", info.getConnTimeout() * 10);
					Serial.printf("[CFG]     Latency:         %u\n", info.getConnLatency());
					Serial.printf("[CFG]     MTU:             %u bytes\n", info.getMTU());
					Serial.printf("[CFG]     Bonded:          %s\n", info.isBonded()    ? "yes" : "no");
					Serial.printf("[CFG]     Encrypted:       %s\n", info.isEncrypted() ? "yes" : "no");
					Serial.printf("[CFG]     Authenticated:   %s\n", info.isAuthenticated() ? "yes" : "no");
					Serial.printf("[CFG]     Key Size:        %u\n", info.getSecKeySize());
					if (i < connections.size() - 1)
						Serial.println("[CFG]     --------------------------------------------------------");
				}
			}
		}
		Serial.println("[CFG] ============================================================");
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
		Serial.println("[MENU] ==================== Config Menu ====================");
		Serial.println("[MENU] Connection:");
		Serial.println("[MENU]   0-9 --> Select active connection by index");
		Serial.println("[MENU]          (a device must connect first; index shown in peer list)");
		Serial.println("[MENU]");
		Serial.println("[MENU] Pairing:");
		Serial.println("[MENU]   A --> Cycle I/O Capabilities");
		Serial.println("[MENU]");
		Serial.println("[MENU] Authorization:");
		Serial.println("[MENU]   B --> Toggle Bond");
		Serial.println("[MENU]   C --> Toggle MitM");
		Serial.println("[MENU]   D --> Toggle Secure Connections");
		Serial.println("[MENU]   E --> Toggle Key Press");
		Serial.println("[MENU]");
		Serial.println("[MENU] Encryption:");
		Serial.println("[MENU]   F --> Toggle Long Term Key (encrypts BLE link after pairing)");
		Serial.println("[MENU]   G --> Toggle Identity Resolving Key (privacy/address resolution)");
		Serial.println("[MENU]   H --> Toggle Connection Signature Resolving Key (data signing)");
		Serial.println("[MENU]   I --> Toggle BR/EDR Link Key (BR/EDR auth/encryption, BLE coexistence)");
		Serial.println("[MENU]");
		Serial.println("[MENU] State:");
		Serial.println("[MENU]   J --> Disconnect Selected");
		Serial.println("[MENU]");
		Serial.println("[MENU] Advertising:");
		Serial.println("[MENU]   K --> Toggle Advert Restart on Disconnect");
		Serial.println("[MENU]   L --> Toggle Advertising");
		Serial.println("[MENU]");
		Serial.println("[MENU] Peer (requires a selected connection):");
		Serial.println("[MENU]   M --> Read peer MTU");
		Serial.println("[MENU]   N --> Set Phy (1M tx / 2M rx)");
		Serial.println("[MENU]   O --> Set Connection Parameters");
		Serial.println("[MENU]   P --> Request Random Data Length");
		Serial.println("[MENU]   Q --> Set Local MTU (takes effect on next connection)");
		Serial.println("[MENU]");
		Serial.println("[MENU] Info:");
		Serial.println("[MENU]   Y --> Print Config");
		Serial.println("[MENU]   Z --> Print This Menu");
		Serial.println("[MENU] =======================================================");
		printStatus();
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
				Serial.printf("[CFG] capabilities:   %.*s\n", static_cast<int>(sv.size()), sv.data());
				break;
			}

			case 'B':
			{
				btMgr.Authentication(ConfigMenuHelp::authToggleBond(btMgr.Authentication()));
				std::string_view sv = ConfigMenuHelp::authToString(btMgr.Authentication());
				Serial.printf("[CFG] authentication: %.*s\n", static_cast<int>(sv.size()), sv.data());
				break;
			}
			case 'C':
			{
				btMgr.Authentication(ConfigMenuHelp::authToggleMitm(btMgr.Authentication()));
				std::string_view sv = ConfigMenuHelp::authToString(btMgr.Authentication());
				Serial.printf("[CFG] authentication: %.*s\n", static_cast<int>(sv.size()), sv.data());
				break;
			}
			case 'D':
			{
				btMgr.Authentication(ConfigMenuHelp::authToggleSC(btMgr.Authentication()));
				std::string_view sv = ConfigMenuHelp::authToString(btMgr.Authentication());
				Serial.printf("[CFG] authentication: %.*s\n", static_cast<int>(sv.size()), sv.data());
				break;
			}
			case 'E':
			{
				btMgr.Authentication(ConfigMenuHelp::authToggleKP(btMgr.Authentication()));
				std::string_view sv = ConfigMenuHelp::authToString(btMgr.Authentication());
				Serial.printf("[CFG] authentication: %.*s\n", static_cast<int>(sv.size()), sv.data());
				break;
			}

			case 'F':
			{
				btMgr.Encryption(ConfigMenuHelp::encToggleLTK(btMgr.Encryption()));
				std::string_view sv = ConfigMenuHelp::encToString(btMgr.Encryption());
				Serial.printf("[CFG] encryption:     %.*s\n", static_cast<int>(sv.size()), sv.data());
				break;
			}
			case 'G':
			{
				btMgr.Encryption(ConfigMenuHelp::encToggleIRK(btMgr.Encryption()));
				std::string_view sv = ConfigMenuHelp::encToString(btMgr.Encryption());
				Serial.printf("[CFG] encryption:     %.*s\n", static_cast<int>(sv.size()), sv.data());
				break;
			}
			case 'H':
			{
				btMgr.Encryption(ConfigMenuHelp::encToggleCSRK(btMgr.Encryption()));
				std::string_view sv = ConfigMenuHelp::encToString(btMgr.Encryption());
				Serial.printf("[CFG] encryption:     %.*s\n", static_cast<int>(sv.size()), sv.data());
				break;
			}
			case 'I':
			{
				btMgr.Encryption(ConfigMenuHelp::encToggleLK(btMgr.Encryption()));
				std::string_view sv = ConfigMenuHelp::encToString(btMgr.Encryption());
				Serial.printf("[CFG] encryption:     %.*s\n", static_cast<int>(sv.size()), sv.data());
				break;
			}

			case 'J':
			{
				if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); break; }
				if (!selectedHandle.has_value()) { Serial.println("[ERR] No connection selected"); break; }

				Serial.printf("[BT] Disconnecting %s...\n", describeHandle(selectedHandle).c_str());
				btMgr.Server()->disconnect(selectedHandle.value());
				break;
			}

			case 'K':
			{
				Serial.printf("[CFG] restart advert on disconnect: %s\n", btMgr.AdvertisingRestartOnDisconnect(!btMgr.AdvertisingRestartOnDisconnect()) ? "yes" : "no");
				break;
			}
			case 'L':
			{
				Serial.printf("[CFG] advertising: %s\n", btMgr.AdvertisingState((0), !btMgr.AdvertisingState(0)) ? "on" : "off");
				break;
			}

			case 'M':
			{
				if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); break; }
				if (!selectedHandle.has_value()) { Serial.println("[ERR] No connection selected"); break; }

				Serial.printf("[BT] %s MTU: %u\n", describeHandle(selectedHandle).c_str(), btMgr.GetPeerMtu(selectedHandle.value()));
				break;
			}

			case 'N':
			{
				if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); break; }
				if (!selectedHandle.has_value()) { Serial.println("[ERR] No connection selected"); break; }

				Serial.printf("[BT] Requesting PHY (tx:1M rx:2M) for %s...\n", describeHandle(selectedHandle).c_str());
				if (auto opt = btMgr.Phy(selectedHandle.value(), BluetoothManager::PhyUpdate{ .txPhysMask = BLE_GAP_LE_PHY_1M_MASK, .rxPhysMask = BLE_GAP_LE_PHY_2M_MASK, .phyOptions = BLE_GAP_LE_PHY_CODED_ANY }))
				{
					const auto& [tx, rx] = *opt;
					Serial.printf("[BT] PHY now tx=%u rx=%u\n", static_cast<unsigned>(tx), static_cast<unsigned>(rx));
				}
				else
					Serial.println("[ERR] PHY update/read failed");

				break;
			}

			case 'O':
			{
				if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); break; }
				if (!selectedHandle.has_value()) { Serial.println("[ERR] No connection selected"); break; }

				using namespace std::chrono_literals;
				Serial.printf("[BT] Requesting connection params for %s (interval 30-50ms, latency 0, timeout 4s)...\n", describeHandle(selectedHandle).c_str());
				btMgr.UpdateConnectionParams(selectedHandle.value(), 30ms, 50ms, 0, 4s);
				break;
			}

			case 'P':
			{
				if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); break; }
				if (!selectedHandle.has_value()) { Serial.println("[ERR] No connection selected"); break; }

				uint16_t rando = 0x001B + (esp_random() % (0x00FB - 0x001B + 1)); // 0x001B (27) to 0x00FB (251)
				Serial.printf("[BT] Requesting data length %u for %s...\n", static_cast<unsigned>(rando), describeHandle(selectedHandle).c_str());
				btMgr.RequestDataLength(selectedHandle.value(), rando);
				break;
			}

			case 'Q':
			{
				Serial.printf("[CFG] Current local MTU: %u bytes\n", NimBLEDevice::getMTU());
				Serial.println("[CFG] Enter new MTU (23-517): ");
				consoleMode = ConsoleMode::SetMtu;
				return; // skip printStatus until value is entered
			}

			case 'Y': printConfig(); break;

			case 'Z': printConfigMenu(); break;

			case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
			{
				if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); break; }

				auto connHandles = btMgr.Server()->getPeerDevices();
				if (connHandles.empty()) { Serial.println("[BT] No connections - wait for a device to connect first"); break; }

				size_t selection = *c - '0';
				if (selection >= connHandles.size())
				{
					Serial.printf("[ERR] Invalid selection: %zu (valid range 0-%zu)\n", selection, connHandles.size() - 1);

					if (selectedHandle.has_value() && std::find(connHandles.begin(), connHandles.end(), selectedHandle.value()) == connHandles.end())
					{
						Serial.println("[BT] Previously selected connection no longer valid; clearing selection");
						selectedHandle.reset();
					}

					break;
				}

				selectedHandle = connHandles[selection];
				Serial.printf("[BT] Selected [%zu] %s\n", selection, describeHandle(selectedHandle).c_str());
				Serial.println("[HINT] Peer commands now active: M (MTU)  N (PHY)  O (conn params)  P (data len)  J (disconnect)");
				break;
			}

			default: break;
		}

		if (Serial.available() > 0 && Serial.peek() == '\r')
			Serial.read();
		if (Serial.available() > 0 && Serial.peek() == '\n')
			Serial.read();

		printStatus();
		Serial.println();
	}

	// TODO: implement submenu
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
			case 'D':
				Serial.println("[MENU] D --> Disconnect (not yet implemented in submenu, use J from main menu)");
				break;

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
		btMgr.SubscribeToEvent
		(
			BluetoothManager::Event::Connect, [](const NimBLEConnInfo& connInfo)
			{
				reportPeerState(connInfo);
				Serial.println("[HINT] Press 0 (or the connection index above) to select this connection.");
			}
		);
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
				printStatus();
			}
		);
	}

} // ProcessMenu
