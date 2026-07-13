// processInput.cpp
#include <Arduino.h>
#include "processInput.h"
#include "processMenu.h"
#include "bluetoothManager.h"
#include <NimBLEDevice.h>
#include <cctype>
#include <cstddef>
#include <chrono>

namespace ProcessInput
{
	std::atomic<bool> passkeyReady{false};
	std::atomic<uint32_t> passkeyValue{0};
	std::atomic<bool> confirmReady{false};
	std::atomic<bool> confirmAccept{false};

	constexpr size_t SINGLE_LEN   = 1;
	constexpr size_t PASSKEY_LEN  = 6;
	constexpr size_t MAX_BUF_LEN  = 64;
	static char   buf[MAX_BUF_LEN + 1];
	static size_t buf_len = 0;

	static inline bool purgeSerialLine()
	{
		bool dropped = false;
		while (Serial.available() > 0)
		{
			int rv = Serial.read();
			if (rv < 0) break;
			dropped = true;
			if (static_cast<char>(rv) == '\n') break;
		}
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

		// --- Passkey ---
		if (curMode == ConsoleMode::Passkey)
		{
			if (trimmedLen == 0 || trimmedLen > PASSKEY_LEN)
			{
				passkeyReady = false;
				Serial.println(trimmedLen == 0 ? "[PAIR] No passkey provided" : "[ERR] Invalid passkey format");
				return;
			}
			unsigned long pk = 0;
			for (size_t i = 0; i < trimmedLen; ++i)
			{
				char ch = s[start + i];
				if (ch < '0' || ch > '9') { passkeyReady = false; Serial.println("[ERR] Passkey digits only"); return; }
				pk = pk * 10 + (unsigned long)(ch - '0');
			}
			if (pk > 999999UL) { passkeyReady = false; Serial.println("[ERR] Passkey 0-999999"); return; }
			passkeyValue = static_cast<uint32_t>(pk);
			passkeyReady = true;
			return;
		}

		// --- Pin confirm ---
		if (curMode == ConsoleMode::PinConfirm)
		{
			confirmAccept = (trimmedLen > 0 && (s[start] == 'y' || s[start] == 'Y'));
			confirmReady  = true;
			return;
		}

		// --- Set MTU ---
		if (curMode == ConsoleMode::SetMtu)
		{
			ProcessMenu::consoleMode = ConsoleMode::Config;
			if (trimmedLen == 0) { Serial.println("[ERR] No value, MTU unchanged"); return; }
			unsigned long mtu = 0; bool valid = true;
			for (size_t i = 0; i < trimmedLen; ++i)
			{
				char ch = s[start + i];
				if (ch < '0' || ch > '9') { valid = false; break; }
				mtu = mtu * 10 + (unsigned long)(ch - '0');
			}
			if (!valid || mtu < 23 || mtu > 517)
				Serial.println("[ERR] MTU must be 23-517");
			else
			{
				NimBLEDevice::setMTU(static_cast<uint16_t>(mtu));
				Serial.printf("[CFG] Local MTU set to %lu (takes effect on next connection)\n", mtu);
			}
			return;
		}

		// --- Set adv interval ---
		if (curMode == ConsoleMode::SetAdvInterval)
		{
			ProcessMenu::consoleMode = ConsoleMode::Config;
			if (trimmedLen == 0) { Serial.println("[ERR] No value, interval unchanged"); return; }
			unsigned long ms = 0; bool valid = true;
			for (size_t i = 0; i < trimmedLen; ++i)
			{
				char ch = s[start + i];
				if (ch < '0' || ch > '9') { valid = false; break; }
				ms = ms * 10 + (unsigned long)(ch - '0');
			}
			if (!valid || ms < 20 || ms > 10240)
				Serial.println("[ERR] Interval must be 20-10240 ms");
			else
				BluetoothManager::Instance().AdvertisingInterval(static_cast<uint32_t>(ms));
			return;
		}

		// --- Set conn params ---
		if (curMode == ConsoleMode::SetConnParams)
		{
			ProcessMenu::consoleMode = ConsoleMode::Config;
			if (trimmedLen == 0) { Serial.println("[ERR] No input, params unchanged"); return; }

			unsigned long minMs = 0, maxMs = 0, latency = 0, timeoutMs = 0;
			const char* p = s + start;
			const char* e = s + end;
			auto parseNext = [&](unsigned long& out) -> bool {
				while (p < e && (*p == ' ' || *p == '\t')) ++p;
				if (p >= e || *p < '0' || *p > '9') return false;
				out = 0;
				while (p < e && *p >= '0' && *p <= '9') out = out * 10 + (*p++ - '0');
				return true;
			};
			if (!parseNext(minMs) || !parseNext(maxMs) || !parseNext(latency) || !parseNext(timeoutMs))
			{
				Serial.println("[ERR] Expected: minMs maxMs latency timeoutMs");
				return;
			}
			if (minMs < 8 || maxMs < minMs || maxMs > 4000 || timeoutMs < 100 || timeoutMs > 32000)
			{
				Serial.println("[ERR] Range: minMs>=8, maxMs<=4000, minMs<=maxMs, timeoutMs 100-32000");
				return;
			}
			auto handle = ProcessMenu::GetSelectedHandle();
			if (!handle.has_value()) { Serial.println("[ERR] No connection selected"); return; }
			Serial.printf("[BT] Conn params: min=%lums max=%lums latency=%lu timeout=%lums\n", minMs, maxMs, latency, timeoutMs);
			BluetoothManager::Instance().UpdateConnectionParams(
				handle.value(),
				std::chrono::milliseconds(minMs),
				std::chrono::milliseconds(maxMs),
				static_cast<uint16_t>(latency),
				std::chrono::milliseconds(timeoutMs));
			return;
		}

		// --- Set data length ---
		if (curMode == ConsoleMode::SetDataLen)
		{
			ProcessMenu::consoleMode = ConsoleMode::Config;
			if (trimmedLen == 0) { Serial.println("[ERR] No input, data length unchanged"); return; }
			unsigned long octets = 0; bool valid = true;
			for (size_t i = 0; i < trimmedLen; ++i)
			{
				char ch = s[start + i];
				if (ch < '0' || ch > '9') { valid = false; break; }
				octets = octets * 10 + (unsigned long)(ch - '0');
			}
			if (!valid || octets < 27 || octets > 251)
			{
				Serial.println("[ERR] Data length must be 27-251");
				return;
			}
			auto handle = ProcessMenu::GetSelectedHandle();
			if (!handle.has_value()) { Serial.println("[ERR] No connection selected"); return; }
			Serial.printf("[BT] Requesting data length %lu\n", octets);
			BluetoothManager::Instance().RequestDataLength(handle.value(), static_cast<uint16_t>(octets));
			return;
		}
	} // processBufferedLine


	void handlePairingInput()
	{
		static ConsoleMode prevMode = ConsoleMode::Uninitialized;

		ConsoleMode curMode = ProcessMenu::consoleMode;
		if (curMode != ConsoleMode::Passkey      && curMode != ConsoleMode::PinConfirm
		 && curMode != ConsoleMode::SetMtu        && curMode != ConsoleMode::SetAdvInterval
		 && curMode != ConsoleMode::SetConnParams && curMode != ConsoleMode::SetDataLen)
			return;

		if (prevMode != ConsoleMode::Uninitialized && prevMode != curMode && buf_len > 0)
		{
			Serial.println();
			processBufferedLine(buf, buf_len);
			buf_len = 0;
			purgeSerialLine();
		}
		prevMode = curMode;

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
				buf_len = 0;
				purgeSerialLine();
				return;
			}

			if (isprint((unsigned char)c))
			{
				if (buf_len >= MAX_BUF_LEN)
				{
					Serial.printf("\n[ERR] Input buffer overrun (max %u chars)\n", (unsigned)MAX_BUF_LEN);
					purgeSerialLine();
					buf_len = 0;
					buf[0] = '\0';
					return;
				}

				// Passkey: auto-submit at 6 chars; PinConfirm: auto-submit at 1 char; all others: wait for Enter
				size_t limit = (curMode == ConsoleMode::Passkey)    ? PASSKEY_LEN
				             : (curMode == ConsoleMode::PinConfirm) ? SINGLE_LEN
				             :                                         MAX_BUF_LEN;
				if (buf_len < limit)
				{
					buf[buf_len++] = c;
					Serial.write(c);

					if (buf_len >= limit)
					{
						Serial.println();
						processBufferedLine(buf, buf_len);
						buf_len = 0;
						purgeSerialLine();
						return;
					}
				}
			}
		}
	} // handlePairingInput

} // namespace ProcessInput
