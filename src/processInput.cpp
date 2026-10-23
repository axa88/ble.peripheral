#include "bleConfig.h"
#if BLE_ENABLE_SERIAL_CONSOLE
// processInput.cpp
#include <Arduino.h>
#include "processInput.h"
#include "processMenu.h"
#include "pairingCoordinator.h"
#include <cstring>
#include <cstdint>
#include <inttypes.h>
#include <esp_timer.h>
#include <array>
#include "bluetoothManager.h"
#include "advertising.h"
#include <NimBLEDevice.h>
#include <cctype>
#include <cstddef>
#include <string>

namespace ProcessInput
{
	static Pairing::RequestId selectedRequest;
	static bool selectedExplicitly = false;
	static Pairing::Type selectedType = Pairing::Type::Display;
	static uint64_t observedChanges = UINT64_MAX;
	static ConsoleMode prevMode = ConsoleMode::Uninitialized;
	static std::array<Pairing::RequestId, Pairing::State::Capacity> displayedNotifications{};
	static size_t nextDisplayedNotification = 0;

	constexpr size_t SINGLE_LEN   = 1;
	constexpr size_t PASSKEY_LEN  = 6;
	constexpr size_t MAX_BUF_LEN  = 64;
	static char   buf[MAX_BUF_LEN + 1];
	static size_t buf_len = 0;
	static bool discardingSerialLine = false;

	static void clearInputBuffer()
	{
		memset(buf, 0, sizeof(buf));
		buf_len = 0;
	}

	static bool isPairingInputMode(ConsoleMode mode)
	{
		return mode == ConsoleMode::Passkey || mode == ConsoleMode::PinConfirm ||
			mode == ConsoleMode::SelectPairingRequest;
	}

	static bool isLineInputMode(ConsoleMode mode)
	{
		return isPairingInputMode(mode) || mode == ConsoleMode::SetMtu ||
			mode == ConsoleMode::SetAdvInterval || mode == ConsoleMode::SetConnParams ||
			mode == ConsoleMode::SetDataLen || mode == ConsoleMode::AddAdvInstance ||
			mode == ConsoleMode::SelectBondTarget || mode == ConsoleMode::SelectAdvInstance;
	}

	static bool parseUnsignedDecimal(const char* first, const char* last, uint32_t maximum, uint32_t& value)
	{
		if (first >= last)
			return false;
		value = 0;
		for (const char* current = first; current < last; ++current)
		{
			if (*current < '0' || *current > '9')
				return false;
			const uint32_t digit = static_cast<uint32_t>(*current - '0');
			if (value > maximum / 10U || (value == maximum / 10U && digit > maximum % 10U))
				return false;
			value = value * 10U + digit;
		}
		return true;
	}

	static bool parseNextUnsigned(const char*& current, const char* end, uint32_t maximum, uint32_t& value)
	{
		while (current < end && (*current == ' ' || *current == '\t')) ++current;
		const char* first = current;
		while (current < end && *current != ' ' && *current != '\t') ++current;
		return parseUnsignedDecimal(first, current, maximum, value);
	}

	static bool hasOnlyWhitespace(const char* current, const char* end)
	{
		while (current < end && (*current == ' ' || *current == '\t')) ++current;
		return current == end;
	}

	static void submit(uint32_t value)
	{
		auto result = PairingCoordinator::respond(selectedRequest, selectedType, value);
		Serial.println(result == PairingCoordinator::Result::Accepted ?
		    "[PAIR] Native response accepted; authentication pending" : "[PAIR] Response rejected or native action failed");
		selectedRequest = {};
		selectedExplicitly = false;
		ProcessMenu::consoleMode = ConsoleMode::Config;
		prevMode = ConsoleMode::Config;
		observedChanges = UINT64_MAX;
	}

	static inline bool purgeSerialLine(bool discardUntilNewlineIfIncomplete = false)
	{
		bool dropped = false;
		bool foundNewline = false;
		while (Serial.available() > 0)
		{
			int rv = Serial.read();
			if (rv < 0) break;
			dropped = true;
			if (static_cast<char>(rv) == '\n')
			{
				foundNewline = true;
				break;
			}
		}
		discardingSerialLine = discardUntilNewlineIfIncomplete && !foundNewline;
		return dropped;
	}

	void processBufferedLine(const char* s, size_t len)
	{
		// trim leading/trailing whitespace
		size_t start = 0;
		while (start < len && isspace((unsigned char)s[start])) ++start;
		size_t end = len;
		while (end > start && isspace((unsigned char)s[end - 1])) --end;
		size_t trimmedLen = end - start;

		ConsoleMode curMode = ProcessMenu::consoleMode;

		if (curMode == ConsoleMode::SelectPairingRequest)
		{
			// Full opaque ID, rather than a position that can change while the operator types.
			if (trimmedLen != 25 || s[start + 16] != '-')
			{ Serial.println("[ERR] Enter a listed pairing request ID"); return; }
			uint64_t boot = 0;
			uint32_t sequence = 0;
			for (size_t i = 0; i < 25; ++i)
			{
				if (i == 16) continue;
				const char ch = s[start + i];
				uint8_t digit;
				if (ch >= '0' && ch <= '9') digit = static_cast<uint8_t>(ch - '0');
				else if (ch >= 'a' && ch <= 'f') digit = static_cast<uint8_t>(ch - 'a' + 10);
				else if (ch >= 'A' && ch <= 'F') digit = static_cast<uint8_t>(ch - 'A' + 10);
				else { Serial.println("[ERR] Enter a listed pairing request ID"); return; }
				if (i < 16) boot = (boot << 4) | digit;
				else sequence = (sequence << 4) | digit;
			}
			const Pairing::RequestId requested{boot, sequence};
			auto snapshot = PairingCoordinator::snapshot();
			for (const auto& request : snapshot.requests)
				if (request.id == requested && request.resolution == Pairing::Resolution::Pending &&
				    request.type != Pairing::Type::Display)
				{
					selectedRequest = request.id;
					selectedExplicitly = true;
					selectedType = request.type;
					ProcessMenu::consoleMode = selectedType == Pairing::Type::PasskeyEntry ? ConsoleMode::Passkey : ConsoleMode::PinConfirm;
					Serial.println(selectedType == Pairing::Type::PasskeyEntry ? "[PAIR] Enter 6-digit passkey > " : "[PAIR] Accept? (y/n) > ");
					return;
				}
			Serial.println("[ERR] Pairing request is no longer pending");
			return;
		}
		if (curMode == ConsoleMode::Passkey)
		{
			if (trimmedLen != PASSKEY_LEN) { Serial.println("[ERR] Passkey requires six digits"); return; }
			uint32_t value = 0;
			for (size_t i = start; i < end; ++i)
			{
				if (s[i] < '0' || s[i] > '9') { Serial.println("[ERR] Passkey digits only"); return; }
				value = value * 10 + (s[i] - '0');
			}
			submit(value);
			return;
		}
		if (curMode == ConsoleMode::PinConfirm)
		{
			if (trimmedLen != 1 || (s[start] != 'y' && s[start] != 'Y' && s[start] != 'n' && s[start] != 'N'))
			{ Serial.println("[ERR] Enter y or n"); return; }
			submit(s[start] == 'y' || s[start] == 'Y');
			return;
		}

		// --- Set MTU ---
		if (curMode == ConsoleMode::SetMtu)
		{
			ProcessMenu::consoleMode = ConsoleMode::Config;
			if (trimmedLen == 0) { Serial.println("[ERR] No value, MTU unchanged"); return; }
			uint32_t mtu = 0;
			if (!parseUnsignedDecimal(s + start, s + end, UINT16_MAX, mtu))
			{
				Serial.println("[ERR] MTU must be a decimal value from 23 to 517");
				return;
			}
			auto outcome = BluetoothManager::Instance().SetPreferredMtu(static_cast<uint16_t>(mtu));
			if (outcome == BluetoothManager::Outcome::InvalidArgument)
				Serial.println("[ERR] MTU must be 23-517");
			else if (outcome != BluetoothManager::Outcome::Applied)
			{
				Serial.println("[ERR] Failed to set local preferred MTU");
				Serial.printf("[CFG] Current local MTU=%u bytes\n", static_cast<unsigned>(NimBLEDevice::getMTU()));
			}
			else
				Serial.printf("[CFG] Local preferred MTU set to %u\n", static_cast<unsigned>(mtu));
			return;
		}

		// --- Set adv interval ---
		if (curMode == ConsoleMode::SetAdvInterval)
		{
			ProcessMenu::consoleMode = ConsoleMode::Config;
			if (trimmedLen == 0) { Serial.println("[ERR] No value, interval unchanged"); return; }
			uint32_t ms = 0;
			if (!parseUnsignedDecimal(s + start, s + end, UINT32_MAX, ms) || ms < 20 || ms > 10240)
				Serial.println("[ERR] Interval must be 20-10240 ms");
			else if (!Advertising::SetInterval(ProcessMenu::SelectedAdvInstance(), ms))
				Serial.println("[ERR] Advertising interval update failed");
			else
				Serial.printf("[CFG] Adv[%u] interval set to %u ms\n", ProcessMenu::SelectedAdvInstance(), static_cast<unsigned>(ms));
			return;
		}

		// --- Set conn params ---
		if (curMode == ConsoleMode::SetConnParams)
		{
			ProcessMenu::consoleMode = ConsoleMode::Config;
			if (trimmedLen == 0) { Serial.println("[ERR] No input, params unchanged"); return; }

			uint32_t minMs = 0, maxMs = 0, latency = 0, timeoutMs = 0;
			const char* p = s + start;
			const char* e = s + end;
			if (!parseNextUnsigned(p, e, UINT32_MAX, minMs) || !parseNextUnsigned(p, e, UINT32_MAX, maxMs) ||
				!parseNextUnsigned(p, e, UINT16_MAX, latency) || !parseNextUnsigned(p, e, UINT32_MAX, timeoutMs) || !hasOnlyWhitespace(p, e))
			{
				Serial.println("[ERR] Expected: minMs maxMs latency timeoutMs");
				return;
			}
			if (!BluetoothManager::ValidConnectionParameters(minMs, maxMs, static_cast<uint16_t>(latency), timeoutMs))
			{
				Serial.println("[ERR] Connection parameters are outside supported BLE ranges or timeout relation");
				return;
			}
			auto connection = ProcessMenu::GetSelectedConnectionId();
			if (!connection) { Serial.println("[ERR] No live connection selected"); return; }
			auto outcome = BluetoothManager::Instance().RequestConnectionParams(*connection, minMs, maxMs, static_cast<uint16_t>(latency), timeoutMs);
			Serial.println(outcome == BluetoothManager::Outcome::Dispatched ? "[BT] Connection parameter request dispatched" : "[ERR] Connection parameter request failed");
			return;
		}

		// --- Set data length ---
		if (curMode == ConsoleMode::SetDataLen)
		{
			ProcessMenu::consoleMode = ConsoleMode::Config;
			if (trimmedLen == 0) { Serial.println("[ERR] No input, data length unchanged"); return; }
			uint32_t octets = 0;
			if (!parseUnsignedDecimal(s + start, s + end, UINT16_MAX, octets))
			{
				Serial.println("[ERR] Data length must be a decimal value from 27 to 251");
				return;
			}
			auto connection = ProcessMenu::GetSelectedConnectionId();
			if (!connection) { Serial.println("[ERR] No live connection selected"); return; }
			auto outcome = BluetoothManager::Instance().RequestDataLength(*connection, static_cast<uint16_t>(octets));
			if (outcome == BluetoothManager::Outcome::Dispatched)
				Serial.printf("[BT] Requesting data length %u\n", static_cast<unsigned>(octets));
			else
			{
				Serial.println("[ERR] Data length must be 27-251");
			}
			return;
		}

		// --- Add advertising instance (ext-adv builds only) ---
		if (curMode == ConsoleMode::AddAdvInstance)
		{
			ProcessMenu::consoleMode = ConsoleMode::Config;
#if !CONFIG_BT_NIMBLE_EXT_ADV
			Serial.println("[ERR] Additional advertising instances require extended advertising (ext-adv build)");
			return;
#else
			if (trimmedLen == 0) { Serial.println("[ERR] No input, instance not added"); return; }

			const char* p = s + start;
			const char* e = s + end;
			while (p < e && (*p == ' ' || *p == '\t')) ++p;
			if (p >= e || *p < '0' || *p > '9') { Serial.println("[ERR] Expected: id name"); return; }
			const char* idStart = p;
			while (p < e && *p >= '0' && *p <= '9') ++p;
			uint32_t id = 0;
			if (!parseUnsignedDecimal(idStart, p, UINT32_MAX, id)) { Serial.println("[ERR] Invalid instance id"); return; }
			if (id == 0 || id >= CONFIG_BT_NIMBLE_MAX_EXT_ADV_INSTANCES) { Serial.printf("[ERR] Instance id must be from 1 to %u\n", CONFIG_BT_NIMBLE_MAX_EXT_ADV_INSTANCES - 1); return;}
			while (p < e && (*p == ' ' || *p == '\t')) ++p;
			if (p >= e) { Serial.println("[ERR] Expected: id name"); return; }
			std::string name(p, e);

			if (Advertising::AddDefaultInstance(static_cast<uint8_t>(id), name))
				Serial.printf("[CFG] Advertising instance %u added\n", static_cast<unsigned>(id));
			else
				Serial.println("[ERR] Advertising instance could not be added");
			return;
#endif
		}

		// --- Select directed-advertising bond target ---
		if (curMode == ConsoleMode::SelectBondTarget)
		{
			ProcessMenu::consoleMode = ConsoleMode::Config;
			if (trimmedLen == 0) { Serial.println("[ERR] No bond selected"); return; }
			uint32_t index = 0;
			if (!parseUnsignedDecimal(s + start, s + end, UINT32_MAX, index)) { Serial.println("[ERR] Bond index must be numeric"); return; }
			ProcessMenu::SelectBondTarget(static_cast<size_t>(index));
			return;
		}

		// --- Select advertising instance ---
		if (curMode == ConsoleMode::SelectAdvInstance)
		{
			ProcessMenu::consoleMode = ConsoleMode::Config;
			if (trimmedLen == 0) { Serial.println("[ERR] No advertising instance selected"); return; }
			uint32_t index = 0;
			if (!parseUnsignedDecimal(s + start, s + end, UINT32_MAX, index)) { Serial.println("[ERR] Advertising instance id must be numeric"); return; }
			ProcessMenu::SelectAdvInstance(static_cast<size_t>(index));
			return;
		}
	} // processBufferedLine


	void handlePairingInput()
	{
		if (discardingSerialLine)
		{
			purgeSerialLine(true);
			if (discardingSerialLine)
				return;
			prevMode = ProcessMenu::consoleMode;
			return;
		}

		ConsoleMode curMode = ProcessMenu::consoleMode;
		if (prevMode != ConsoleMode::Uninitialized && prevMode != curMode &&
			(isPairingInputMode(prevMode) || isPairingInputMode(curMode)))
		{
			const bool leavingPairingInput = isPairingInputMode(prevMode) && !isPairingInputMode(curMode);
			const bool hadPartialLine = buf_len > 0;
			const bool hadQueuedPairingInput = leavingPairingInput && Serial.available() > 0;
			if (hadPartialLine || hadQueuedPairingInput)
			{
				clearInputBuffer();
				purgeSerialLine(true);
			}
		}
		prevMode = curMode;
		if (!isLineInputMode(curMode))
			return;

		while (Serial.available() > 0)
		{
			int rv = Serial.read();
			if (rv < 0) break;
			char c = static_cast<char>(rv);
			if (c == '\r') continue;

			if (rv == 8 || rv == 127) // backspace / DEL
			{
				if (buf_len > 0)
				{
					--buf_len;
					buf[buf_len] = '\0';
					Serial.write('\b');
					Serial.write(' ');
					Serial.write('\b');
				}
				continue;
			}

			if (c == '\n')
			{
				Serial.println();
				processBufferedLine(buf, buf_len);
				clearInputBuffer();
				purgeSerialLine();
				return;
			}

			if (isprint((unsigned char)c))
			{
				if (buf_len >= MAX_BUF_LEN)
				{
					Serial.printf("\n[ERR] Input buffer overrun (max %u chars)\n", (unsigned)MAX_BUF_LEN);
					purgeSerialLine(true);
					clearInputBuffer();
					return;
				}

				// Passkey: auto-submit at 6 chars; PinConfirm: auto-submit at 1 char; all others: wait for Enter
				size_t limit = (curMode == ConsoleMode::Passkey) ? PASSKEY_LEN : (curMode == ConsoleMode::PinConfirm) ? SINGLE_LEN : MAX_BUF_LEN;
				if (buf_len < limit)
				{
					buf[buf_len++] = c;
					if (curMode == ConsoleMode::Passkey) Serial.write('*');
					else Serial.write(c);

					if (buf_len >= limit)
					{
						Serial.println();
						processBufferedLine(buf, buf_len);
						clearInputBuffer();
						// Passkey and yes/no responses auto-submit at their fixed length. Drain
						// the rest of that serial line even when submit() returns to Config,
						// so trailing bytes cannot become unrelated menu commands.
						if (curMode == ConsoleMode::Passkey || curMode == ConsoleMode::PinConfirm ||
							ProcessMenu::consoleMode != ConsoleMode::Config)
							purgeSerialLine(true);
						return;
					}
				}
			}
		}
	} // handlePairingInput


	void loopConsole()
	{
#if ARDUINO_USB_CDC_ON_BOOT
		static bool ready = false;
		if (!ready && Serial)
		{
			while (Serial.available() > 0) Serial.read();
			ProcessMenu::printConfigMenu();
			ready = true;
		}
		if (!ready) return;
#endif
		if (PairingCoordinator::changes() != observedChanges)
		{
			auto snapshot = PairingCoordinator::snapshot();
			observedChanges = snapshot.changes;
			size_t pending = 0;
			bool selectedLive = false;
			const Pairing::Request* single = nullptr;
			for (const auto& request : snapshot.requests)
			{
				if (!request.id || request.resolution != Pairing::Resolution::Pending) continue;
				if (request.type == Pairing::Type::Display)
				{
					bool alreadyDisplayed = false;
					for (const auto& id : displayedNotifications)
						alreadyDisplayed |= id == request.id;
					if (alreadyDisplayed) continue;
					const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time()) / 1000;
					if (request.deadlineMs <= nowMs) continue;
					const auto remainingMs = static_cast<unsigned long>(request.deadlineMs - nowMs);
					Serial.printf("[PAIR] Device-level passkey for remote entry (not tied to selected peer): %06" PRIu32 " (expires in %lu ms)\n",
					    request.value, remainingMs);
					displayedNotifications[nextDisplayedNotification] = request.id;
					nextDisplayedNotification = (nextDisplayedNotification + 1) % displayedNotifications.size();
					continue;
				}
				++pending;
				single = &request;
				selectedLive |= request.id == selectedRequest;
				Serial.printf("[PAIR] Request %016" PRIx64 "-%08" PRIx32 ": %s", request.id.boot, request.id.sequence,
				    request.type == Pairing::Type::PasskeyEntry ? "passkey entry" : "numeric comparison");
				if (request.type == Pairing::Type::NumericConfirmation) Serial.printf(" %06" PRIu32, request.value);
				Serial.println();
			}
			if (pending > 1 && !selectedExplicitly) selectedLive = false;
			if (!selectedLive)
			{
				const ConsoleMode currentMode = ProcessMenu::consoleMode;
				if (selectedRequest || isPairingInputMode(currentMode))
				{
					const bool hadPartialLine = buf_len > 0 || Serial.available() > 0;
					clearInputBuffer();
					if (hadPartialLine)
						purgeSerialLine(true);
				}
				selectedExplicitly = false;
				selectedRequest = {};
				if (isPairingInputMode(currentMode))
					ProcessMenu::consoleMode = ConsoleMode::Config;
			}
			if (pending > 1 && !selectedLive)
			{
				ProcessMenu::consoleMode = ConsoleMode::SelectPairingRequest;
				Serial.println("[PAIR] Enter the full request ID to select > ");
			}
			else if (pending == 1 && !selectedLive)
			{
				selectedRequest = single->id;
				selectedType = single->type;
				ProcessMenu::consoleMode = selectedType == Pairing::Type::PasskeyEntry ? ConsoleMode::Passkey : ConsoleMode::PinConfirm;
				Serial.println(selectedType == Pairing::Type::PasskeyEntry ? "[PAIR] Enter 6-digit passkey > " : "[PAIR] Accept? (y/n) > ");
			}
		}
		const bool configModeAtInputStart = ProcessMenu::consoleMode == ConsoleMode::Config;
		handlePairingInput();
		if (configModeAtInputStart && !discardingSerialLine && ProcessMenu::consoleMode == ConsoleMode::Config)
			ProcessMenu::handleConfigInput();
	}

} // namespace ProcessInput

#endif
