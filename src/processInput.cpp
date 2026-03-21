// processInput.cpp
#include <Arduino.h>
#include "processInput.h"
#include "processMenu.h"
#include <cctype>       // isspace, isprint, isdigit, toupper
#include <cstddef>      // size_t

namespace ProcessInput
{
	std::atomic<bool> passkeyReady{false};
	std::atomic<uint32_t> passkeyValue{0};
	std::atomic<bool> confirmReady{false};
	std::atomic<bool> confirmAccept{false};

	constexpr size_t SINGLE_LEN = 1;
	constexpr size_t PASSKEY_LEN = 6;
	constexpr size_t MAX_BUF_LEN = 64;
	static char buf[MAX_BUF_LEN + 1];
	static size_t buf_len = 0;

	static inline bool purgeSerialLine()
	{
		bool dropped = false;
		while (Serial.available() > 0)
		{
			int rv = Serial.read();
			if (rv < 0) break;
			dropped = true;
			char d = static_cast<char>(rv);
			if (d == '\n') break;
		}
		return dropped;
	} // purgeSerialLine

	void processBufferedLine(const char *s, size_t len)
	{
		// trim leading
		size_t start = 0;
		while (start < len && isspace((unsigned char)s[start]))
			++start;

		// trim trailing
		size_t end = len;
		while (end > start && isspace((unsigned char)s[end - 1]))
			--end;

		size_t trimmedLen = end - start;

		ConsoleMode curMode = ProcessMenu::consoleMode;

		if (curMode == ConsoleMode::Passkey)
		{
			if (trimmedLen == 0)
			{
				passkeyReady = false;
				Serial.println("Console: no passkey provided");
				return;
			}

			if (trimmedLen > PASSKEY_LEN)
			{
				passkeyReady = false;
				Serial.println("Console: invalid passkey format (digits only)");
				return;
			}

			// check digits and parse
			unsigned long pk = 0;
			for (size_t i = 0; i < trimmedLen; ++i)
			{
				char ch = s[start + i];
				if (ch < '0' || ch > '9')
				{
					passkeyReady = false;
					Serial.println("Console: invalid passkey format (digits only)");
					return;
				}
				pk = pk * 10 + (unsigned long)(ch - '0');
				// optional early bound check to avoid overflow
				if (pk > 999999UL)
					break;
			}

			if (pk <= 999999UL)
			{
				passkeyValue = static_cast<uint32_t>(pk);
				passkeyReady = true;
			}
			else
			{
				passkeyReady = false;
				Serial.println("Console: invalid passkey format (0..999999 expected)");
			}

			return;
		}

		if (curMode == ConsoleMode::PinConfirm)
		{
			if (trimmedLen > 0)
			{
				char first = s[start];
				confirmAccept = (first == 'y' || first == 'Y');
			}
			else
				confirmAccept = false;

			confirmReady = true;
			return;
		}
	} // processBufferedLine

	void handlePairingInput()
	{
		static ConsoleMode prevMode = ConsoleMode::Uninitialized;

		ConsoleMode curMode = ProcessMenu::consoleMode;
		if (curMode != ConsoleMode::Passkey && curMode != ConsoleMode::PinConfirm)
			return;

		if (prevMode != ConsoleMode::Uninitialized && prevMode != curMode && buf_len > 0)
		{
			Serial.println();
			processBufferedLine(buf, buf_len);
			buf_len = 0;
			purgeSerialLine();
		}
		prevMode = curMode;

		while (Serial.available() > 0) // read available
		{
            int rv = Serial.read();
            if (rv < 0) break;
            char c = static_cast<char>(rv);
            if (c == '\r') continue;

			if (rv == 8 || rv == 127) // backspace (BS=8) or DEL (127)
			{
				if (buf_len > 0)
				{
					--buf_len;
					// only maintain C-string if other code needs it; safe to keep one write
					buf[buf_len] = '\0';
					Serial.write('\b');
					Serial.write(' ');
					Serial.write('\b');
				}
				continue;
			}
			

			if (c == '\n') // terminate on new line -> submit
			{
				Serial.println(); // echo newline
				processBufferedLine(buf, buf_len);
				buf_len = 0;
				purgeSerialLine();
				return;
			}

			if (isprint((unsigned char)c)) // echo printable chars up to limit
			{
				if (buf_len >= MAX_BUF_LEN)
				{
					Serial.printf("\nConsole: input buffer overrun %u\n", (unsigned)MAX_BUF_LEN);
					purgeSerialLine();
					buf_len = 0;
					buf[0] = '\0';
					return;
				}

				size_t limit = (curMode == ConsoleMode::Passkey) ? PASSKEY_LEN : SINGLE_LEN;
				if (buf_len < limit)
				{
					buf[buf_len++] = c;
					Serial.write(c); // echo typed char
					
					if (buf_len >= limit) // buffer reached mode limit -> submit
					{
						Serial.println(); // echo newline
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