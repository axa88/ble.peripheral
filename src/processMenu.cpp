#include "bleConfig.h"
#if BLE_ENABLE_SERIAL_CONSOLE
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
#include <algorithm>    // std::none_of
#include <random>

namespace
{
	static std::optional<ConnectionId> selectedConnection;
	static std::optional<NimBLEAddress> selectedBondTarget;
	static std::atomic<uint8_t> selectedAdvInstance{0};

	void printCommandEcho(const ConsoleCommand::Descriptor& command, char key)
	{
		Serial.printf("[CMD] %c  %.*s\n", key, static_cast<int>(command.description.size()), command.description.data());
	}

	// Returns a short "address (handle N)" string for a live peer snapshot.
	std::string describePeer(const BluetoothManager::ConnectedPeerSnapshot& peer)
	{
		char out[64];
		snprintf(out, sizeof(out), "%s (handle %u)", peer.address.c_str(), static_cast<unsigned>(peer.handle));
		return std::string(out);
	}

	std::optional<ConnectionId> currentSelectedConnection()
	{
		if (!selectedConnection)
			return std::nullopt;
		if (!BluetoothManager::Instance().ConnectionExists(*selectedConnection))
		{
			selectedConnection.reset();
			return std::nullopt;
		}
		return selectedConnection;
	}

	std::optional<BluetoothManager::ConnectedPeerSnapshot> selectedPeerSnapshot()
	{
		auto connection = currentSelectedConnection();
		if (!connection)
			return std::nullopt;
		for (const auto& peer : BluetoothManager::Instance().GetConnectedPeerSnapshots())
			if (peer.connectionId == *connection)
				return peer;
		selectedConnection.reset();
		return std::nullopt;
	}

	void listConnection()
	{
		auto connections = BluetoothManager::Instance().GetConnectedPeerSnapshots();
		if (connections.empty())
		{
			selectedConnection.reset();
			Serial.println("[BT] No connections");
			return;
		}
		if (selectedConnection && std::none_of(connections.begin(), connections.end(), [](const auto& peer) { return peer.connectionId == *selectedConnection; }))
			selectedConnection.reset();

		Serial.println("[BT] Connected peers:");
		for (size_t i = 0; i < connections.size(); ++i)
		{
			const auto& peer = connections[i];
			const char* marker = (selectedConnection && *selectedConnection == peer.connectionId) ? "-->" : "   ";
			Serial.printf("%s [%zu] handle:%u address:%s\n", marker, i, static_cast<unsigned>(peer.handle), peer.address.c_str());
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
			auto connections = btMgr.GetConnectedPeerSnapshots();
			if (selectedConnection && std::none_of(connections.begin(), connections.end(), [](const auto& peer) { return peer.connectionId == *selectedConnection; }))
				selectedConnection.reset();
			if (connections.empty())
				Serial.println("[CFG] === Connected Peers: none");
			else
			{
				for (size_t i = 0; i < connections.size(); ++i)
				{
					const auto& peer = connections[i];
					bool selected = selectedConnection && *selectedConnection == peer.connectionId;
					Serial.printf("[CFG] === Connected Peer [%zu]%s\n", i, selected ? " <-- selected" : "");
					Serial.printf("[CFG]     Address:         %s\n", peer.address.c_str());
					Serial.printf("[CFG]     Handle:          %u\n", static_cast<unsigned>(peer.handle));
					Serial.printf("[CFG]     Interval:        %.1f ms\n", peer.intervalMs);
					Serial.printf("[CFG]     Timeout:         %u ms\n", static_cast<unsigned>(peer.timeoutMs));
					Serial.printf("[CFG]     Latency:         %u\n", peer.latency);
					Serial.printf("[CFG]     MTU:             %u bytes\n", peer.mtu);
					Serial.printf("[CFG]     Bonded:          %s\n", peer.bonded ? "yes" : "no");
					Serial.printf("[CFG]     Encrypted:       %s\n", peer.encrypted ? "yes" : "no");
					Serial.printf("[CFG]     Authenticated:   %s\n", peer.authenticated ? "yes" : "no");
					Serial.printf("[CFG]     Key Size:        %u\n", peer.keySize);
					auto rssi = btMgr.GetPeerRssi(peer.connectionId);
					Serial.printf(rssi.has_value() ? "[CFG]     RSSI:            %d dBm\n" : "[CFG]     RSSI:            unavailable\n", rssi.value_or(0));

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
		std::string_view sv = ConfigMenuHelp::capIoToString(btMgr.CycleCapabilities());
		Serial.printf("[CFG] capabilities=%.*s\n", static_cast<int>(sv.size()), sv.data());
	}

	void handleDeleteSelectedBond(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		auto connection = ProcessMenu::GetSelectedConnectionId();
		if (!connection) { Serial.println("[ERR] No live connection selected"); return; }
		auto result = btMgr.DeleteConnectedPeerBond(*connection);
		Serial.printf(result.outcome == BluetoothManager::Outcome::Applied
			? "[BOND] Deleted %d bond(s); %d remain\n"
			: "[ERR] Bond deletion failed after deleting %d bond(s); %d remain\n",
			result.deleted, result.remaining);
		if (selectedBondTarget && !isStoredBond(*selectedBondTarget))
			selectedBondTarget.reset();
	}

	void handleDeleteAllBonds(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		auto result = btMgr.DeleteAllBonds();
		Serial.printf(result.outcome == BluetoothManager::Outcome::Applied
			? "[BOND] Deleted %d bond(s); %d remain\n"
			: "[ERR] Bulk bond deletion failed after deleting %d bond(s); %d remain\n",
			result.deleted, result.remaining);
		if (selectedBondTarget && !isStoredBond(*selectedBondTarget))
			selectedBondTarget.reset();
	}

	void handleToggleMitm(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		btMgr.ToggleAuthenticationFlag(BLE_SM_PAIR_AUTHREQ_MITM);
		std::string_view sv = ConfigMenuHelp::authToString(btMgr.Authentication());
		Serial.printf("[CFG] authentication=%.*s\n", static_cast<int>(sv.size()), sv.data());
	}

	void handleToggleSecureConnections(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		btMgr.ToggleAuthenticationFlag(BLE_SM_PAIR_AUTHREQ_SC);
		std::string_view sv = ConfigMenuHelp::authToString(btMgr.Authentication());
		Serial.printf("[CFG] authentication=%.*s\n", static_cast<int>(sv.size()), sv.data());
	}

	void handleToggleLtk(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		btMgr.ToggleKeyDistributionFlag(BLE_SM_PAIR_KEY_DIST_ENC);
		std::string_view sv = ConfigMenuHelp::encToString(btMgr.Encryption());
		Serial.printf("[CFG] encryption=%.*s\n", static_cast<int>(sv.size()), sv.data());
	}

	void handleToggleIrk(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		btMgr.ToggleKeyDistributionFlag(BLE_SM_PAIR_KEY_DIST_ID);
		std::string_view sv = ConfigMenuHelp::encToString(btMgr.Encryption());
		Serial.printf("[CFG] encryption=%.*s\n", static_cast<int>(sv.size()), sv.data());
	}

	void handleToggleCsrk(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		btMgr.ToggleKeyDistributionFlag(BLE_SM_PAIR_KEY_DIST_SIGN);
		std::string_view sv = ConfigMenuHelp::encToString(btMgr.Encryption());
		Serial.printf("[CFG] encryption=%.*s\n", static_cast<int>(sv.size()), sv.data());
	}

	void handleToggleBond(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		btMgr.ToggleAuthenticationFlag(BLE_SM_PAIR_AUTHREQ_BOND);
		std::string_view sv = ConfigMenuHelp::authToString(btMgr.Authentication());
		Serial.printf("[CFG] authentication=%.*s\n", static_cast<int>(sv.size()), sv.data());
	}

	void handleDisconnectSelected(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		auto connection = ProcessMenu::GetSelectedConnectionId();
		if (!connection) { Serial.println("[ERR] No live connection selected"); return; }
		Serial.println(btMgr.Disconnect(*connection) ? "[BT] Disconnect accepted" : "[ERR] Disconnect failed");
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
		auto result = Advertising::StopAll();
		Serial.printf("[CFG] Advertising stopped=%u failed=%u\n", result.stopped, result.failed);
	}

	void handleToggleRestart(char)
	{
		uint8_t instance = selectedAdvInstance.load();
		bool current = Advertising::RestartAfterConnection(instance);
		if (Advertising::SetRestartAfterConnection(instance, !current))
			Serial.printf("[CFG] Adv[%u] RestartAfterConnection=%s\n", instance, !current ? "yes" : "no");
		else
			Serial.printf("[ERR] Adv[%u] restart setting could not be updated\n", instance);
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
		auto result = btMgr.DisconnectAll();
		if (result == BluetoothManager::Outcome::Applied)
			Serial.println("[BT] No active connections");
		else if (result == BluetoothManager::Outcome::Accepted)
		{
			selectedConnection.reset();
			Serial.println("[BT] Disconnect accepted for active connection(s)");
		}
		else
			Serial.println("[ERR] One or more disconnect requests failed");
	}

	void handleSetLocalMtu(char)
	{
		Serial.printf("[CFG] Current local MTU=%u bytes\n", NimBLEDevice::getMTU());
		Serial.println("[CFG] Enter new MTU from 23 to 517:");
		ProcessMenu::consoleMode = ConsoleMode::SetMtu;
	}

	void handleReadPeerMtu(char)
	{
		auto peer = selectedPeerSnapshot();
		if (!peer) { Serial.println("[ERR] No live connection selected"); return; }
		Serial.printf("[BT] Peer MTU=%u\n", peer->mtu);
	}

	void handleRequestPeerPhy(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		auto connection = ProcessMenu::GetSelectedConnectionId();
		if (!connection) { Serial.println("[ERR] No live connection selected"); return; }
		auto outcome = btMgr.RequestPhy(*connection, BluetoothManager::PhyUpdate{ .txPhysMask = BLE_GAP_LE_PHY_1M_MASK, .rxPhysMask = BLE_GAP_LE_PHY_2M_MASK, .phyOptions = BLE_GAP_LE_PHY_CODED_ANY });
		Serial.println(outcome == BluetoothManager::Outcome::Accepted ? "[BT] PHY update accepted" : "[ERR] PHY update failed");
	}

	void handleSetConnectionParameters(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); return; }
		if (!ProcessMenu::GetSelectedConnectionId()) { Serial.println("[ERR] No live connection selected"); return; }
		Serial.println("[CFG] Enter conn params as minMs maxMs latency timeoutMs:");
		ProcessMenu::consoleMode = ConsoleMode::SetConnParams;
	}

	void handleRequestDataLength(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		if (!btMgr.Server()) { Serial.println("[ERR] Server uninitialized"); return; }
		if (!ProcessMenu::GetSelectedConnectionId()) { Serial.println("[ERR] No live connection selected"); return; }
		Serial.println("[CFG] Enter data length in octets from 27 to 251:");
		ProcessMenu::consoleMode = ConsoleMode::SetDataLen;
	}

	void handleReadPeerRssi(char)
	{
		auto& btMgr = BluetoothManager::Instance();
		auto connection = ProcessMenu::GetSelectedConnectionId();
		if (!connection) { Serial.println("[ERR] No live connection selected"); return; }
		auto rssi = btMgr.GetPeerRssi(*connection);
		Serial.printf(rssi.has_value() ? "[BT] RSSI=%d dBm\n" : "[BT] RSSI unavailable\n", static_cast<int>(rssi.value_or(0)));
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
		auto peers = btMgr.GetConnectedPeerSnapshots();
		if (peers.empty()) { selectedConnection.reset(); Serial.println("[BT] No connections"); return; }
		size_t selection = static_cast<size_t>(command - '0');
		if (selection >= peers.size())
		{
			Serial.printf("[ERR] Invalid connection selection %zu\n", selection);
			return;
		}
		selectedConnection = peers[selection].connectionId;
		Serial.printf("[BT] Selected peer %s\n", describePeer(peers[selection]).c_str());
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
			}
		);
		btMgr.SubscribeToEvent
		(
			BluetoothManager::Event::AuthComplete, [](const NimBLEConnInfo& connInfo)
			{
				Serial.printf("[PAIR] Authentication result: encrypted=%s authenticated=%s bonded=%s key-size=%u\n",
					connInfo.isEncrypted() ? "yes" : "no",
					connInfo.isAuthenticated() ? "yes" : "no",
					connInfo.isBonded() ? "yes" : "no",
					static_cast<unsigned>(connInfo.getSecKeySize()));
				reportPeerState(connInfo);
				printStatus();
			}
		);
	}

	std::optional<ConnectionId> GetSelectedConnectionId()
	{
		return currentSelectedConnection();
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

		NimBLEAddress target = NimBLEDevice::getBondedAddress(static_cast<int>(index));
		uint8_t instance = selectedAdvInstance.load();
		if (!Advertising::SetDirectedTarget(instance, target))
		{
			Serial.printf("[ERR] Directed target could not be assigned to Adv[%u]\n", instance);
			return false;
		}
		selectedBondTarget = target;
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

#endif
