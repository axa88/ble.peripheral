// processMenu.cpp
// #include <Arduino.h>
#include "processMenu.h"
#include "bluetoothManager.h"
#include "advertising.h"
#include "configMenuHelp.h"
#include "consoleCommand.h"
#include <Arduino.h>    // Serial, millis(), etc.
#include <optional>     // std::optional
#include <atomic>       // std::atomic
#include <cctype>       // toupper
#include <cstdint>      // uint16_t, uint32_t
#include <string>       // std::string
#include <string_view>  // std::string_view
#include <vector>       // std::vector
#include <random>

namespace
{
	static std::optional<uint16_t> selectedHandle;
	static std::optional<NimBLEAddress> selectedBondTarget;
	static std::atomic<uint8_t> selectedAdvInstance{0};

	void printCommandEcho(const ConsoleCommand::Descriptor& command, char key)
	{
		Serial.printf("[CMD] %c  %.*s\n", key, static_cast<int>(command.description.size()), command.description.data());
	}

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

	void handleCapabilities(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		std::string_view sv = ConfigMenuHelp::capIoToString(btMgr.Capabilities(ConfigMenuHelp::capIoNext(btMgr.Capabilities())));
		Serial.printf("[CFG] capabilities=%.*s\n", static_cast<int>(sv.size()), sv.data());
	}

	void handleDeleteSelectedBond(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); return; }
		if (!selectedHandle.has_value()) { Serial.println("[ERR] No connection selected"); return; }
		btMgr.DeleteBond(selectedHandle.value());
		Advertising::ClearDirectedTargets();
		selectedBondTarget.reset();
	}

	void handleDeleteAllBonds(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		btMgr.DeleteAllBonds();
		Advertising::ClearDirectedTargets();
		selectedBondTarget.reset();
	}

	void handleToggleMitm(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		btMgr.Authentication(ConfigMenuHelp::authToggleMitm(btMgr.Authentication()));
		std::string_view sv = ConfigMenuHelp::authToString(btMgr.Authentication());
		Serial.printf("[CFG] authentication=%.*s\n", static_cast<int>(sv.size()), sv.data());
	}

	void handleToggleSecureConnections(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		btMgr.Authentication(ConfigMenuHelp::authToggleSC(btMgr.Authentication()));
		std::string_view sv = ConfigMenuHelp::authToString(btMgr.Authentication());
		Serial.printf("[CFG] authentication=%.*s\n", static_cast<int>(sv.size()), sv.data());
	}

	void handleToggleLtk(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		btMgr.Encryption(ConfigMenuHelp::encToggleLTK(btMgr.Encryption()));
		std::string_view sv = ConfigMenuHelp::encToString(btMgr.Encryption());
		Serial.printf("[CFG] encryption=%.*s\n", static_cast<int>(sv.size()), sv.data());
	}

	void handleToggleIrk(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		btMgr.Encryption(ConfigMenuHelp::encToggleIRK(btMgr.Encryption()));
		std::string_view sv = ConfigMenuHelp::encToString(btMgr.Encryption());
		Serial.printf("[CFG] encryption=%.*s\n", static_cast<int>(sv.size()), sv.data());
	}

	void handleToggleCsrk(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		btMgr.Encryption(ConfigMenuHelp::encToggleCSRK(btMgr.Encryption()));
		std::string_view sv = ConfigMenuHelp::encToString(btMgr.Encryption());
		Serial.printf("[CFG] encryption=%.*s\n", static_cast<int>(sv.size()), sv.data());
	}

	void handleToggleBond(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		btMgr.Authentication(ConfigMenuHelp::authToggleBond(btMgr.Authentication()));
		std::string_view sv = ConfigMenuHelp::authToString(btMgr.Authentication());
		Serial.printf("[CFG] authentication=%.*s\n", static_cast<int>(sv.size()), sv.data());
	}

	void handleDisconnectSelected(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); return; }
		if (!selectedHandle.has_value()) { Serial.println("[ERR] No connection selected"); return; }
		btMgr.Server()->disconnect(selectedHandle.value());
	}

	void handleSelectAdvertisingInstance(char)
	{
		listAdvertisingInstances();
		Serial.println("[CFG] Select advertising instance id:");
		ProcessMenu::consoleMode = ConsoleMode::SelectAdvInstance;
	}

	void handleToggleAdvertising(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		uint8_t instance = selectedAdvInstance.load();
		btMgr.AdvertisingState(instance, !Advertising::IsActive(instance));
	}

	void handleStopAllAdvertising(char)
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
	}

	void handleToggleRestart(char)
	{
		uint8_t instance = selectedAdvInstance.load();
		bool current = Advertising::RestartAfterConnection(instance);
		bool updated = Advertising::RestartAfterConnection(instance, !current);
		Serial.printf("[CFG] Adv[%u] RestartAfterConnection=%s\n", instance, updated ? "yes" : "no");
	}

	void handleCycleMode(char)
	{
		uint8_t instance = selectedAdvInstance.load();
		if (Advertising::CycleMode(instance))
			printAdvertisingInstanceLine(instance, true);
	}

	void handleSelectBondTarget(char)
	{
		listBonds();
		if (NimBLEDevice::getNumBonds() == 0)
			return;
		Serial.println("[BOND] Enter bond index to use as directed target:");
		ProcessMenu::consoleMode = ConsoleMode::SelectBondTarget;
	}

	void handleCycleDiscoverability(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		uint8_t instance = selectedAdvInstance.load();
		if (Advertising::CycleDiscoverability(instance))
		{
			auto discStr = Advertising::discModeToString(btMgr.AdvertisingDiscoverable(instance));
			Serial.printf("[CFG] Adv[%u] Discoverability=%.*s\n", instance, static_cast<int>(discStr.size()), discStr.data());
		}
	}

	void handleSetAdvertisingInterval(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		uint8_t instance = selectedAdvInstance.load();
		Serial.printf("[CFG] Current advertising interval[%u]=%u ms\n", instance, btMgr.AdvertisingInterval(instance));
		Serial.println("[CFG] Enter new interval in milliseconds from 20 to 10240:");
		ProcessMenu::consoleMode = ConsoleMode::SetAdvInterval;
	}

#if CONFIG_BT_NIMBLE_EXT_ADV
	void handleAddAdvertisingInstance(char)
	{
		Serial.println("[CFG] Enter new instance as id name:");
		ProcessMenu::consoleMode = ConsoleMode::AddAdvInstance;
	}

	void handleRemoveAdvertisingInstance(char)
	{
		uint8_t instance = selectedAdvInstance.load();
		if (instance != 0 && Advertising::RemoveInstance(instance))
			selectedAdvInstance = 0;
		else if (instance == 0)
			Serial.println("[ERR] Instance 0 cannot be removed");
	}
#endif

	void handleDisconnectAll(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); return; }
		auto handles = btMgr.Server()->getPeerDevices();
		if (handles.empty())
		{
			Serial.println("[BT] No active connections");
			return;
		}
		for (uint16_t handle : handles)
			btMgr.Server()->disconnect(handle);
		selectedHandle.reset();
		Serial.printf("[BT] Disconnect requested for %u connection(s)\n", static_cast<unsigned>(handles.size()));
	}

	void handleSetLocalMtu(char)
	{
		Serial.printf("[CFG] Current local MTU=%u bytes\n", NimBLEDevice::getMTU());
		Serial.println("[CFG] Enter new MTU from 23 to 517:");
		ProcessMenu::consoleMode = ConsoleMode::SetMtu;
	}

	void handleReadPeerMtu(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); return; }
		if (!selectedHandle.has_value()) { Serial.println("[ERR] No connection selected"); return; }
		Serial.printf("[BT] Peer MTU=%u\n", btMgr.GetPeerMtu(selectedHandle.value()));
	}

	void handleRequestPeerPhy(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); return; }
		if (!selectedHandle.has_value()) { Serial.println("[ERR] No connection selected"); return; }
		btMgr.Phy(selectedHandle.value(), BluetoothManager::PhyUpdate{ .txPhysMask = BLE_GAP_LE_PHY_1M_MASK, .rxPhysMask = BLE_GAP_LE_PHY_2M_MASK, .phyOptions = BLE_GAP_LE_PHY_CODED_ANY });
	}

	void handleSetConnectionParameters(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); return; }
		if (!selectedHandle.has_value()) { Serial.println("[ERR] No connection selected"); return; }
		Serial.println("[CFG] Enter conn params as minMs maxMs latency timeoutMs:");
		ProcessMenu::consoleMode = ConsoleMode::SetConnParams;
	}

	void handleRequestDataLength(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); return; }
		if (!selectedHandle.has_value()) { Serial.println("[ERR] No connection selected"); return; }
		Serial.println("[CFG] Enter data length in octets from 27 to 251:");
		ProcessMenu::consoleMode = ConsoleMode::SetDataLen;
	}

	void handleReadPeerRssi(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); return; }
		if (!selectedHandle.has_value()) { Serial.println("[ERR] No connection selected"); return; }
		int8_t rssi = btMgr.GetPeerRssi(selectedHandle.value());
		Serial.printf(rssi == INT8_MIN ? "[BT] RSSI unavailable\n" : "[BT] RSSI=%d dBm\n", static_cast<int>(rssi));
	}

	void handlePrintMenu(char)
	{
		ProcessMenu::printConfigMenu();
	}

	void handlePrintConfig(char)
	{
		printConfig();
	}

	void handleShowAdvertising(char)
	{
		listAdvertisingInstances();
	}

	void handleSelectPeer(char command)
	{
		auto& btMgr = BluetoothManager::Instance();
		if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); return; }
		auto connHandles = btMgr.Server()->getPeerDevices();
		if (connHandles.empty()) { Serial.println("[BT] No connections"); return; }
		size_t selection = static_cast<size_t>(command - '0');
		if (selection >= connHandles.size())
		{
			Serial.printf("[ERR] Invalid connection selection %zu\n", selection);
			return;
		}
		selectedHandle = connHandles[selection];
		Serial.printf("[BT] Selected peer %s\n", describeHandle(selectedHandle).c_str());
	}

	const ConsoleCommand::Descriptor commandRegistry[] =
	{
		{ 'Q', 'Q', "Q", ConsoleCommand::Group::Advertising, "Show all advertising instances", false, handleShowAdvertising },
		{ 'W', 'W', "W", ConsoleCommand::Group::Advertising, "Select advertising instance by id", false, handleSelectAdvertisingInstance },
		{ 'E', 'E', "E", ConsoleCommand::Group::Advertising, "Start or stop the selected advertising instance", false, handleToggleAdvertising },
		{ 'R', 'R', "R", ConsoleCommand::Group::Advertising, "Stop all advertising instances", false, handleStopAllAdvertising },
		{ 'T', 'T', "T", ConsoleCommand::Group::Advertising, "Toggle restart of selected advertising instance after connection", false, handleToggleRestart },
		{ 'Y', 'Y', "Y", ConsoleCommand::Group::Advertising, "Cycle supported Connectable Directed Scannable mode", false, handleCycleMode },
		{ 'U', 'U', "U", ConsoleCommand::Group::Advertising, "Set discoverability to None Limited or General", false, handleCycleDiscoverability },
		{ 'I', 'I', "I", ConsoleCommand::Group::Advertising, "List bonds and set the directed target", false, handleSelectBondTarget },
		{ 'O', 'O', "O", ConsoleCommand::Group::Advertising, "Enter the selected advertising interval", false, handleSetAdvertisingInterval },
#if CONFIG_BT_NIMBLE_EXT_ADV
		{ 'P', 'P', "P", ConsoleCommand::Group::Advertising, "Add an advertising instance", true, handleAddAdvertisingInstance },
		{ '[', '[', "[", ConsoleCommand::Group::Advertising, "Remove the selected advertising instance", true, handleRemoveAdvertisingInstance },
#endif

		{ 'A', 'A', "A", ConsoleCommand::Group::Security, "Cycle LE Pairing I/O capability", false, handleCapabilities },
		{ 'S', 'S', "S", ConsoleCommand::Group::Security, "Delete bond for selected active connection", false, handleDeleteSelectedBond },
		{ 'D', 'D', "D", ConsoleCommand::Group::Security, "Delete all stored bonds", false, handleDeleteAllBonds },
		{ 'F', 'F', "F", ConsoleCommand::Group::Security, "Toggle MITM protection requests", false, handleToggleMitm },
		{ 'G', 'G', "G", ConsoleCommand::Group::Security, "Toggle Secure Connections requests", false, handleToggleSecureConnections },
		{ 'H', 'H', "H", ConsoleCommand::Group::Security, "Toggle LTK distribution (Long Term Keys)", false, handleToggleLtk },
		{ 'J', 'J', "J", ConsoleCommand::Group::Security, "Toggle IRK distribution (Identity Resolving Keys)", false, handleToggleIrk },
		{ 'K', 'K', "K", ConsoleCommand::Group::Security, "Toggle CSRK distribution (Connection Signature Resolving Keys)", false, handleToggleCsrk },
		{ 'L', 'L', "L", ConsoleCommand::Group::Security, "Toggle bond storage on pairing", false, handleToggleBond },

		{ '0', '9', "0-9", ConsoleCommand::Group::Connection, "Select active connected peer", false, handleSelectPeer },
		{ 'Z', 'Z', "Z", ConsoleCommand::Group::Connection, "Disconnect selected connected peer", false, handleDisconnectSelected },
		{ 'X', 'X', "X", ConsoleCommand::Group::Connection, "Disconnect all connected peers", false, handleDisconnectAll },
		{ 'C', 'C', "C", ConsoleCommand::Group::Connection, "Set local MTU", false, handleSetLocalMtu },
		{ 'V', 'V', "V", ConsoleCommand::Group::Connection, "Read MTU selected peer", false, handleReadPeerMtu },
		{ 'B', 'B', "B", ConsoleCommand::Group::Connection, "Request PHY update selected peer", false, handleRequestPeerPhy },
		{ 'N', 'N', "N", ConsoleCommand::Group::Connection, "Set connection parameters selected peer", false, handleSetConnectionParameters },
		{ 'M', 'M', "M", ConsoleCommand::Group::Connection, "Request data length selected peer", false, handleRequestDataLength },
		{ ',', ',', ",", ConsoleCommand::Group::Connection, "Read RSSI selected peer", false, handleReadPeerRssi },

		{ '-', '-', "-", ConsoleCommand::Group::Other, "Print this command menu", false, handlePrintMenu },
		{ '=', '=', "=", ConsoleCommand::Group::Other, "Print complete device and advertising configuration", false, handlePrintConfig }
	};

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

namespace ConsoleCommand
{
	const Descriptor* Registry(std::size_t& count)
	{
		count = sizeof(commandRegistry) / sizeof(commandRegistry[0]);
		return commandRegistry;
	}

	const Descriptor* Find(char command)
	{
		std::size_t count = 0;
		const Descriptor* commands = Registry(count);
		for (std::size_t i = 0; i < count; ++i)
		{
			if (commands[i].matches(command))
				return &commands[i];
		}
		return nullptr;
	}
}

namespace ProcessMenu
{
	std::atomic<ConsoleMode> consoleMode{ ConsoleMode::Config };

	void printConfigMenu()
	{
		auto printGroup = [](ConsoleCommand::Group group, const char* title)
		{
			std::size_t count = 0;
			const ConsoleCommand::Descriptor* commands = ConsoleCommand::Registry(count);
			Serial.printf("[MENU] %s\n", title);
			for (std::size_t i = 0; i < count; ++i)
			{
				const auto& command = commands[i];
				if (command.group != group)
					continue;
				Serial.printf("[MENU]   %s  %.*s\n", command.keyText, static_cast<int>(command.description.size()), command.description.data());
			}
		};

		Serial.println();
		Serial.println("[MENU] ==================== Config Menu ====================");
		printGroup(ConsoleCommand::Group::Advertising, "Advertising");
		Serial.println("[MENU]");
		printGroup(ConsoleCommand::Group::Security, "Security and bonds");
		Serial.println("[MENU]");
		printGroup(ConsoleCommand::Group::Connection, "Connection");
		Serial.println("[MENU]");
		printGroup(ConsoleCommand::Group::Other, "Other");
		Serial.println("[MENU] =======================================================");
	}

	void handleConfigInput()
	{
		if (consoleMode != ConsoleMode::Config)
			return;

		auto c = readCommandChar();
		if (!c)
			return;

		const ConsoleCommand::Descriptor* command = ConsoleCommand::Find(*c);
		if (command == nullptr)
		{
			Serial.printf("[ERR] Unknown command: %c\n", *c);
		}
		else
		{
			printCommandEcho(*command, *c);
			command->handler(*c);
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
