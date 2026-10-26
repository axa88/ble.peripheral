#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace GattValue
{
inline constexpr size_t MaxWebLength = 256;
using Buffer = std::array<uint8_t, MaxWebLength>;

// Whitespace separates complete bytes; an empty value is a valid payload.
inline bool ParseHex(std::string_view text, Buffer& bytes, size_t& length)
{
	length = 0;
	int high = -1;
	for (char c : text)
	{
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v')
		{
			if (high >= 0) return false;
			continue;
		}
		const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
		if (digit < 0) return false;
		if (high < 0) high = digit;
		else
		{
			if (length == bytes.size()) return false;
			bytes[length++] = static_cast<uint8_t>((high << 4) | digit);
			high = -1;
		}
	}
	return high < 0;
}
}
