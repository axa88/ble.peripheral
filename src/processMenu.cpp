// processMenu.cpp
// #include <Arduino.h>
#include "processMenu.h"
#include "bluetoothManager.h"
#include "advertising.h"
#include "configMenuHelp.h"
#include <Arduino.h>    // Serial, millis(), etc.
#include <optional>     // std::optional
#include <atomic>       // std::atomic
#include <cctype>       // toupper
#include <cstdint>      // uint16_t, uint32_t
#include <string>       // std::string
#include <vector>       // std::vector
#include <random>

namespace
{
	static std::optional<uint16_t> selectedHandle;
	static std::optional<NimBLEAddress> selectedBondTarget;
	static std::atomic<uint8_t> selectedAdvInstance{0};

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
		if (!server)
		{
			Serial.println("[BT] Server uninitialized");
			return;
		}
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

	bool isStoredBond(const NimBLEAddress& address)
	{
		int count = NimBLEDevice::getNumBonds();
		for (int i = 0; i < count; ++i)
		{
			if (NimBLEDevice::getBondedAddress(i) == address)
				return true;
		}
		return false;
	}

	void listBonds()
	{
		int count = NimBLEDevice::getNumBonds();
		if (count == 0)
		{
			Serial.println("[BOND] No bonds stored");
			return;
		}

		Serial.printf("[BOND] %d bond(s) stored:\n", count);
		for (int i = 0; i < count; ++i)
		{
			NimBLEAddress address = NimBLEDevice::getBondedAddress(i);
			const char* marker = selectedBondTarget.has_value() && *selectedBondTarget == address ? "-->" : "   ";
			Serial.printf("[BOND] %s [%d] %s\n", marker, i, address.toString().c_str());
		}
	}

	std::string describeBondTarget()
	{
		return selectedBondTarget.has_value() ? selectedBondTarget->toString() : "none";
	}

	std::string targetForConfig(const Advertising::InstanceConfig& cfg)
	{
		if (!Advertising::modeIsDirected(cfg.mode))
			return "-";
		return cfg.directedTarget.has_value() ? cfg.directedTarget->toString() : "none";
	}

	void printAdvertisingInstanceLine(uint8_t id, bool selected)
	{
		auto cfg = Advertising::GetConfig(id);
		if (!cfg.has_value())
			return;
		const auto props = Advertising::modeProperties(cfg->mode);
		auto discStr = Advertising::discModeToString(cfg->discoverable);
		const char* marker = selected ? "-->" : "   ";
		Serial.printf("[CFG] %s [%u] \"%s\" | On [%s] | Restarts [%s] | %.*s | Discoverability=%.*s | Directed=%s | %ums\n",
			marker,
			id,
			cfg->name.c_str(),
			Advertising::IsActive(id) ? "X" : " ",
			cfg->restartAfterConnection ? "X" : " ",
			static_cast<int>(props.name.size()), props.name.data(),
			static_cast<int>(discStr.size()), discStr.data(),
			targetForConfig(*cfg).c_str(),
			cfg->intervalMs);
	}

	void listAdvertisingInstances()
	{
#if CONFIG_BT_NIMBLE_EXT_ADV
		auto ids = Advertising::ListInstances();
		for (uint8_t id : ids)
			printAdvertisingInstanceLine(id, id == selectedAdvInstance.load());
#else
		printAdvertisingInstanceLine(0, true);
#endif
	}

	void reportPeerState(const NimBLEConnInfo& connInfo)
	{
		Serial.printf("[BT] %s\n", connInfo.toString().c_str());
		listConnection();
	}

	void printStatus()
	{
		listAdvertisingInstances();
	}

	void printConfig()
	{
		auto& btMgr = BluetoothManager::Instance();

		Serial.println();
		Serial.println("[CFG] ============================================================");
		Serial.println("[CFG] === Peripheral");
		Serial.printf( "[CFG]     Address:        %s\n", NimBLEDevice::toString().c_str());
		Serial.printf( "[CFG]     Adv name:       %s\n", btMgr.AdvertisingName().c_str());
		Serial.println("[CFG]     --- Advertising Instances ---");
		listAdvertisingInstances();
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
				Serial.println("[CFG] === Connected Peers: none");
			else
			{
				for (size_t i = 0; i < connections.size(); ++i)
				{
					uint16_t handle = connections[i];
					NimBLEConnInfo info = btMgr.Server()->getPeerInfoByHandle(handle);
					bool selected = selectedHandle.has_value() && selectedHandle.value() == handle;
					Serial.printf("[CFG] === Connected Peer [%zu]%s\n", i, selected ? " <-- selected" : "");
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
					Serial.printf("[CFG]     RSSI:            %d dBm\n", btMgr.GetPeerRssi(handle));

					if (i < connections.size() - 1)
						Serial.println("[CFG]     --------------------------------------------------------");
				}
			}
		}
		Serial.println("[CFG] ============================================================");
	}

	std::optional<char> readCommandChar()
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

		if (r == 0x1b)
		{
			if (Serial.available() < 2)
				return std::nullopt;
			int a = Serial.read();
			int b = Serial.read();
			if (a == 'O' && b == 'P')
				return static_cast<char>(0x01);
			if (a == 'O' && b == 'Q')
				return static_cast<char>(0x02);
			if (a == 'O' && b == 'R')
				return static_cast<char>(0x03);
			if (a == '[' && b == '1' && Serial.available() >= 2)
			{
				int c = Serial.read();
				int d = Serial.read();
				if (c == '1' && d == '~')
					return static_cast<char>(0x01);
				if (c == '2' && d == '~')
					return static_cast<char>(0x02);
				if (c == '3' && d == '~')
					return static_cast<char>(0x03);
			}
			return std::nullopt;
		}

		return static_cast<char>(toupper(static_cast<unsigned char>(r)));
	}
} // namespace

namespace ProcessMenu
{
	std::atomic<ConsoleMode> consoleMode{ ConsoleMode::Config };

	void printConfigMenu()
	{
		Serial.println();
		Serial.println("[MENU] ==================== Config Menu ====================");
		Serial.println("[MENU] Advertising");
		Serial.println("[MENU]   Q  Show all advertising instances");
		Serial.println("[MENU]   W  Select advertising instance by id");
		Serial.println("[MENU]   E  Start or stop the selected advertising instance");
		Serial.println("[MENU]   R  Stop all advertising instances");
		Serial.println("[MENU]   T  Toggle restart of selected advertising instance after connection");
		Serial.println("[MENU]   Y  Cycle supported Connectable Directed Scannable mode");
		Serial.println("[MENU]   U  Set discoverability to None Limited or General");
		Serial.println("[MENU]   I  List bonds and set the directed target");
		Serial.println("[MENU]   O  Enter the selected advertising interval");
#if CONFIG_BT_NIMBLE_EXT_ADV
		Serial.println("[MENU]   P  Add an advertising instance");
		Serial.println("[MENU]   [  Remove the selected advertising instance");
#endif
		Serial.println("[MENU]");
		Serial.println("[MENU] Bonding and Security");
		Serial.println("[MENU]   A  Cycle LE Pairing I/O capability");
		Serial.println("[MENU]   S  Delete Bond for Selected Active Connection");
		Serial.println("[MENU]   D  Delete All Stored Bonds");
		Serial.println("[MENU]   F  Toggle MITM protection is requests");
		Serial.println("[MENU]   G  Toggle Secure Connections requests");
		Serial.println("[MENU]   H  Toggle LTK distribution (Long Term Keys)");
		Serial.println("[MENU]   J  Toggle IRK distribution (Identity Resolving Keys)");
		Serial.println("[MENU]   K  Toggle CSRK distribution (Connection Signature Resolving Keys)");
		Serial.println("[MENU]   L  Toggle Bond Stored on pairing");
		Serial.println("[MENU]");
		Serial.println("[MENU] Connection");
		Serial.println("[MENU]   0-9  Select Active Connected peer");
		Serial.println("[MENU]   Z    Disconnect Selected Connected peer");
		Serial.println("[MENU]   X    Disconnect All Connected peer");
		Serial.println("[MENU]   C    Set local MTU");
		Serial.println("[MENU]   V    Read MTU Selected peer");
		Serial.println("[MENU]   B    Request PHY update Selected peer");
		Serial.println("[MENU]   N    Set Connection Parameters Selected peer");
		Serial.println("[MENU]   M    Request Data Lengh Selected peer");
		Serial.println("[MENU]   ,    Read RSSI Selected peer");
		Serial.println("[MENU]");
		Serial.println("[MENU] Other");
		Serial.println("[MENU]   -   Print this command menu");
		Serial.println("[MENU]   =   Print complete device and advertising configuration");
		Serial.println("[MENU] =======================================================");
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
				Serial.printf("[CFG] capabilities=%.*s\n", static_cast<int>(sv.size()), sv.data());
				break;
			}
			case 'S':
			{
				if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); break; }
				if (!selectedHandle.has_value()) { Serial.println("[ERR] No connection selected"); break; }
				btMgr.DeleteBond(selectedHandle.value());
				Advertising::ClearDirectedTargets();
				selectedBondTarget.reset();
				break;
			}
			case 'D':
			{
				btMgr.DeleteAllBonds();
				Advertising::ClearDirectedTargets();
				selectedBondTarget.reset();
				break;
			}
			case 'F':
			{
				btMgr.Authentication(ConfigMenuHelp::authToggleMitm(btMgr.Authentication()));
				std::string_view sv = ConfigMenuHelp::authToString(btMgr.Authentication());
				Serial.printf("[CFG] authentication=%.*s\n", static_cast<int>(sv.size()), sv.data());
				break;
			}
			case 'G':
			{
				btMgr.Authentication(ConfigMenuHelp::authToggleSC(btMgr.Authentication()));
				std::string_view sv = ConfigMenuHelp::authToString(btMgr.Authentication());
				Serial.printf("[CFG] authentication=%.*s\n", static_cast<int>(sv.size()), sv.data());
				break;
			}
			case 'H':
			{
				btMgr.Encryption(ConfigMenuHelp::encToggleLTK(btMgr.Encryption()));
				std::string_view sv = ConfigMenuHelp::encToString(btMgr.Encryption());
				Serial.printf("[CFG] encryption=%.*s\n", static_cast<int>(sv.size()), sv.data());
				break;
			}
			case 'J':
			{
				btMgr.Encryption(ConfigMenuHelp::encToggleIRK(btMgr.Encryption()));
				std::string_view sv = ConfigMenuHelp::encToString(btMgr.Encryption());
				Serial.printf("[CFG] encryption=%.*s\n", static_cast<int>(sv.size()), sv.data());
				break;
			}
			case 'K':
			{
				btMgr.Encryption(ConfigMenuHelp::encToggleCSRK(btMgr.Encryption()));
				std::string_view sv = ConfigMenuHelp::encToString(btMgr.Encryption());
				Serial.printf("[CFG] encryption=%.*s\n", static_cast<int>(sv.size()), sv.data());
				break;
			}
			case 'L':
			{
				btMgr.Authentication(ConfigMenuHelp::authToggleBond(btMgr.Authentication()));
				std::string_view sv = ConfigMenuHelp::authToString(btMgr.Authentication());
				Serial.printf("[CFG] authentication=%.*s\n", static_cast<int>(sv.size()), sv.data());
				break;
			}
			case 'Z':
			{
				if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); break; }
				if (!selectedHandle.has_value()) { Serial.println("[ERR] No connection selected"); break; }
				btMgr.Server()->disconnect(selectedHandle.value());
				break;
			}
			case 'W':
			{
				listAdvertisingInstances();
				Serial.println("[CFG] Select advertising instance id:");
				consoleMode = ConsoleMode::SelectAdvInstance;
				return;
			}
			case 'E':
			{
				uint8_t inst = selectedAdvInstance.load();
				if (Advertising::IsActive(inst))
					btMgr.AdvertisingState(inst, false);
				else
					btMgr.AdvertisingState(inst, true);
				break;
			}
			case 'R':
			{
				unsigned stopped = 0;
				unsigned failed = 0;
				std::vector<uint8_t> ids;
#if CONFIG_BT_NIMBLE_EXT_ADV
				ids = Advertising::ListInstances();
#else
				ids.push_back(0);
#endif
				for (uint8_t id : ids)
				{
					if (!Advertising::IsActive(id))
						continue;
					if (Advertising::Stop(id))
						++stopped;
					else
						++failed;
				}
				Serial.printf("[CFG] Advertising stopped=%u failed=%u\n", stopped, failed);
				break;
			}
			case 'T':
			{
				uint8_t inst = selectedAdvInstance.load();
				bool current = Advertising::RestartAfterConnection(inst);
				bool updated = Advertising::RestartAfterConnection(inst, !current);
				Serial.printf("[CFG] Adv[%u] RestartAfterConnection=%s\n", inst, updated ? "yes" : "no");
				break;
			}
			case 'Y':
			{
				uint8_t inst = selectedAdvInstance.load();
				if (Advertising::CycleMode(inst))
					printAdvertisingInstanceLine(inst, true);
				break;
			}
			case 'I':
			{
				listBonds();
				if (NimBLEDevice::getNumBonds() == 0)
					break;
				Serial.println("[BOND] Enter bond index to use as directed target:");
				consoleMode = ConsoleMode::SelectBondTarget;
				return;
			}
			case 'U':
			{
				uint8_t inst = selectedAdvInstance.load();
				if (Advertising::CycleDiscoverability(inst))
				{
					auto discStr = Advertising::discModeToString(btMgr.AdvertisingDiscoverable(inst));
					Serial.printf("[CFG] Adv[%u] Discoverability=%.*s\n", inst, static_cast<int>(discStr.size()), discStr.data());
				}
				break;
			}
			case 'O':
			{
				uint8_t inst = selectedAdvInstance.load();
				Serial.printf("[CFG] Current advertising interval[%u]=%u ms\n", inst, btMgr.AdvertisingInterval(inst));
				Serial.println("[CFG] Enter new interval in milliseconds from 20 to 10240:");
				consoleMode = ConsoleMode::SetAdvInterval;
				return;
			}
#if CONFIG_BT_NIMBLE_EXT_ADV
			case 'P':
			{
				Serial.println("[CFG] Enter new instance as id name:");
				consoleMode = ConsoleMode::AddAdvInstance;
				return;
			}
			case '[':
			{
				uint8_t inst = selectedAdvInstance.load();
				if (inst != 0 && Advertising::RemoveInstance(inst))
					selectedAdvInstance = 0;
				else if (inst == 0)
					Serial.println("[ERR] Instance 0 cannot be removed");
				break;
			}
#endif
			case 'X':
			{
				if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); break; }
				auto handles = btMgr.Server()->getPeerDevices();
				if (handles.empty())
				{
					Serial.println("[BT] No active connections");
					break;
				}
				for (uint16_t handle : handles)
					btMgr.Server()->disconnect(handle);
				selectedHandle.reset();
				Serial.printf("[BT] Disconnect requested for %u connection(s)\n", static_cast<unsigned>(handles.size()));
				break;
			}
			case 'C':
				Serial.printf("[CFG] Current local MTU=%u bytes\n", NimBLEDevice::getMTU());
				Serial.println("[CFG] Enter new MTU from 23 to 517:");
				consoleMode = ConsoleMode::SetMtu;
				return;
			case 'V':
			{
				if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); break; }
				if (!selectedHandle.has_value()) { Serial.println("[ERR] No connection selected"); break; }
				Serial.printf("[BT] Peer MTU=%u\n", btMgr.GetPeerMtu(selectedHandle.value()));
				break;
			}
			case 'B':
			{
				if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); break; }
				if (!selectedHandle.has_value()) { Serial.println("[ERR] No connection selected"); break; }
				btMgr.Phy(selectedHandle.value(), BluetoothManager::PhyUpdate{ .txPhysMask = BLE_GAP_LE_PHY_1M_MASK, .rxPhysMask = BLE_GAP_LE_PHY_2M_MASK, .phyOptions = BLE_GAP_LE_PHY_CODED_ANY });
				break;
			}
			case 'N':
			{
				if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); break; }
				if (!selectedHandle.has_value()) { Serial.println("[ERR] No connection selected"); break; }
				Serial.println("[CFG] Enter conn params as minMs maxMs latency timeoutMs:");
				consoleMode = ConsoleMode::SetConnParams;
				return;
			}
			case 'M':
			{
				if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); break; }
				if (!selectedHandle.has_value()) { Serial.println("[ERR] No connection selected"); break; }
				Serial.println("[CFG] Enter data length in octets from 27 to 251:");
				consoleMode = ConsoleMode::SetDataLen;
				return;
			}
			case ',':
			{
				if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); break; }
				if (!selectedHandle.has_value()) { Serial.println("[ERR] No connection selected"); break; }
				int8_t rssi = btMgr.GetPeerRssi(selectedHandle.value());
				Serial.printf(rssi == INT8_MIN ? "[BT] RSSI unavailable\n" : "[BT] RSSI=%d dBm\n", static_cast<int>(rssi));
				break;
			}
			case '-':
				printConfigMenu();
				return;
			case '=':
				printConfig();
				return;
			case 'Q':
			{
				listAdvertisingInstances();
				break;
			}
			case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
			{
				if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); break; }
				auto connHandles = btMgr.Server()->getPeerDevices();
				if (connHandles.empty()) { Serial.println("[BT] No connections"); break; }
				size_t selection = *c - '0';
				if (selection >= connHandles.size())
				{
					Serial.printf("[ERR] Invalid connection selection %zu\n", selection);
					break;
				}
				selectedHandle = connHandles[selection];
				Serial.printf("[BT] Selected peer %s\n", describeHandle(selectedHandle).c_str());
				break;
			}
			default:
				break;
		}

		if (Serial.available() > 0 && Serial.peek() == '\r')
			Serial.read();
		if (Serial.available() > 0 && Serial.peek() == '\n')
			Serial.read();

	}

	void setupProcessMenu()
	{
		auto& btMgr = BluetoothManager::Instance();
		btMgr.SubscribeToEvent
		(
			BluetoothManager::Event::Connect, [](const NimBLEConnInfo& connInfo)
			{
				reportPeerState(connInfo);
				Serial.println("[HINT] Select an index to select a device.");
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

	std::optional<uint16_t> GetSelectedHandle()
	{
		return selectedHandle;
	}

	bool SelectBondTarget(size_t index)
	{
		int count = NimBLEDevice::getNumBonds();
		if (count == 0)
		{
			Serial.println("[ERR] No bonds stored");
			return false;
		}
		if (index >= static_cast<size_t>(count))
		{
			Serial.printf("[ERR] Invalid bond selection: %u (valid range 0-%d)\n", static_cast<unsigned>(index), count - 1);
			return false;
		}

		selectedBondTarget = NimBLEDevice::getBondedAddress(static_cast<int>(index));
		uint8_t instance = selectedAdvInstance.load();
		if (!Advertising::SetDirectedTarget(instance, *selectedBondTarget))
		{
			Serial.printf("[ERR] Directed target could not be assigned to Adv[%u]\n", instance);
			return false;
		}
		Serial.printf("[BOND] Adv[%u] directed target=%s\n", instance, selectedBondTarget->toString().c_str());
		return true;
	}

	bool SelectAdvInstance(size_t index)
	{
#if CONFIG_BT_NIMBLE_EXT_ADV
		auto ids = Advertising::ListInstances();
		for (uint8_t id : ids)
		{
			if (id != index)
				continue;
			selectedAdvInstance = id;
			printAdvertisingInstanceLine(id, true);
			return true;
		}
		Serial.printf("[ERR] Advertising instance %u does not exist\n", static_cast<unsigned>(index));
		return false;
#else
		if (index != 0)
		{
			Serial.println("[ERR] Legacy advertising only supports instance 0");
			return false;
		}
		selectedAdvInstance = 0;
		printAdvertisingInstanceLine(0, true);
		return true;
#endif
	}

	uint8_t SelectedAdvInstance()
	{
#if CONFIG_BT_NIMBLE_EXT_ADV
		return selectedAdvInstance.load();
#else
		return 0;
#endif
	}

} // ProcessMenu
