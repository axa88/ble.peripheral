#include "bleConfig.h"
#if BLE_ENABLE_WEB_INTERFACE
#include "webValidation.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <limits>

namespace
{
char asciiLower(char c)
{
	return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equalAsciiInsensitive(std::string_view left, std::string_view right)
{
	if (left.size() != right.size()) return false;
	for (size_t i = 0; i < left.size(); ++i)
		if (asciiLower(left[i]) != asciiLower(right[i])) return false;
	return true;
}

std::string lowerAscii(std::string_view text)
{
	std::string result;
	result.reserve(text.size());
	for (char c : text) result.push_back(asciiLower(c));
	return result;
}

bool parsePort(std::string_view text, uint16_t& port)
{
	if (text.empty()) return false;
	uint32_t value = 0;
	for (char c : text)
	{
		if (c < '0' || c > '9') return false;
		value = value * 10U + static_cast<uint32_t>(c - '0');
		if (value > 65535U) return false;
	}
	if (value == 0) return false;
	port = static_cast<uint16_t>(value);
	return true;
}

bool validHostname(std::string_view host)
{
	if (host.empty() || host.size() > 253 || host.front() == '.' || host.back() == '.') return false;
	bool previousDot = false;
	for (char c : host)
	{
		const bool dot = c == '.';
		if (dot && previousDot) return false;
		if (!(dot || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
		previousDot = dot;
	}
	for (size_t i = 0; i < host.size(); ++i)
		if (host[i] == '-' && (i == 0 || i + 1 == host.size() || host[i - 1] == '.' || host[i + 1] == '.')) return false;
	return true;
}

uint8_t hexValue(char c)
{
	if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
	if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
	if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
	return 0xFF;
}

bool constantTimeEqual(std::string_view left, std::string_view right)
{
	size_t count = std::max(left.size(), right.size());
	uint32_t difference = static_cast<uint32_t>(left.size() ^ right.size());
	for (size_t i = 0; i < count; ++i)
	{
		const uint8_t a = i < left.size() ? static_cast<uint8_t>(left[i]) : 0;
		const uint8_t b = i < right.size() ? static_cast<uint8_t>(right[i]) : 0;
		difference |= static_cast<uint32_t>(a ^ b);
	}
	return difference == 0;
}

int base64Value(char c)
{
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

bool decodeBasic(std::string_view token, std::array<char, 128>& decoded, size_t& decodedLength)
{
	decodedLength = 0;
	if (token.empty() || token.size() > 172 || token.size() % 4 != 0) return false;
	for (size_t i = 0; i < token.size(); i += 4)
	{
		const bool last = i + 4 == token.size();
		const int a = base64Value(token[i]);
		const int b = base64Value(token[i + 1]);
		const int c = token[i + 2] == '=' ? -2 : base64Value(token[i + 2]);
		const int d = token[i + 3] == '=' ? -2 : base64Value(token[i + 3]);
		if (a < 0 || b < 0 || c == -1 || d == -1 || (!last && (c < 0 || d < 0))) return false;
		if (c == -2 && (d != -2 || (b & 0x0F) != 0)) return false;
		if (d == -2 && c >= 0 && (c & 0x03) != 0) return false;
		const uint32_t packed = (static_cast<uint32_t>(a) << 18) |
			(static_cast<uint32_t>(b) << 12) |
			(static_cast<uint32_t>(c < 0 ? 0 : c) << 6) |
			static_cast<uint32_t>(d < 0 ? 0 : d);
		const size_t count = c == -2 ? 1 : (d == -2 ? 2 : 3);
		if (decodedLength + count >= decoded.size()) return false;
		decoded[decodedLength++] = static_cast<char>((packed >> 16) & 0xFF);
		if (count > 1) decoded[decodedLength++] = static_cast<char>((packed >> 8) & 0xFF);
		if (count > 2) decoded[decodedLength++] = static_cast<char>(packed & 0xFF);
	}
	decoded[decodedLength] = '\0';
	return true;
}
}

bool WebValidation::parseAuthority(std::string_view value, Authority& out)
{
	if (value.empty() || value.size() > 256) return false;
	for (unsigned char c : value)
		if (c <= 0x20 || c >= 0x7F || c == ',' || c == '/' || c == '\\' || c == '@' || c == '?' || c == '#' || c == ';') return false;

	const size_t colon = value.find(':');
	std::string_view host = value;
	uint16_t port = 80;
	if (colon != std::string_view::npos)
	{
		if (value.find(':', colon + 1) != std::string_view::npos) return false;
		host = value.substr(0, colon);
		if (!parsePort(value.substr(colon + 1), port)) return false;
	}
	if (host.empty()) return false;
	auto normalizedHost = lowerAscii(host);
	if (!validHostname(normalizedHost)) return false;
	out.host = std::move(normalizedHost);
	out.port = port;
	return true;
}

bool WebValidation::parseOrigin(std::string_view value, Authority& out)
{
	constexpr std::string_view scheme = "http://";
	if (value.size() <= scheme.size() || !equalAsciiInsensitive(value.substr(0, scheme.size()), scheme)) return false;
	auto authority = value.substr(scheme.size());
	if (authority.find('/') != std::string_view::npos || authority.find('?') != std::string_view::npos || authority.find('#') != std::string_view::npos)
		return false;
	return parseAuthority(authority, out);
}

bool WebValidation::validHostOrigin(std::string_view hostHeader, std::string_view originHeader,
	std::string_view currentIp, std::string_view otaHostname)
{
	Authority host;
	if (!parseAuthority(hostHeader, host) || host.port != 80) return false;
	Authority ip;
	if (!parseAuthority(currentIp, ip) || ip.port != 80) return false;
	const auto expectedMdns = lowerAscii(otaHostname) + ".local";
	if (host.host != ip.host && host.host != expectedMdns) return false;
	if (originHeader.empty()) return true;
	Authority origin;
	return parseOrigin(originHeader, origin) && origin.host == host.host && origin.port == host.port;
}

bool WebValidation::isJsonContentType(std::string_view value)
{
	if (value.size() > 128) return false;
	for (unsigned char c : value)
		if (c < 0x20 || c >= 0x7F) return false;
	const size_t semicolon = value.find(';');
	auto mediaType = value.substr(0, semicolon);
	while (!mediaType.empty() && (mediaType.front() == ' ' || mediaType.front() == '\t')) mediaType.remove_prefix(1);
	while (!mediaType.empty() && (mediaType.back() == ' ' || mediaType.back() == '\t')) mediaType.remove_suffix(1);
	if (!equalAsciiInsensitive(mediaType, "application/json")) return false;
	if (semicolon == std::string_view::npos) return true;
	auto parameters = value.substr(semicolon + 1);
	while (!parameters.empty() && (parameters.front() == ' ' || parameters.front() == '\t')) parameters.remove_prefix(1);
	while (!parameters.empty() && (parameters.back() == ' ' || parameters.back() == '\t')) parameters.remove_suffix(1);
	return parameters.empty() || equalAsciiInsensitive(parameters, "charset=utf-8") ||
		equalAsciiInsensitive(parameters, "charset=\"utf-8\"");
}

bool WebValidation::isFlatJsonObject(std::string_view value)
{
	while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r' || value.front() == '\n'))
		value.remove_prefix(1);
	if (value.empty() || value.front() != '{') return false;

	bool inString = false;
	bool escaped = false;
	bool closed = false;
	size_t closeIndex = std::string_view::npos;
	for (size_t i = 0; i < value.size(); ++i)
	{
		const char c = value[i];
		if (c == '\0') return false;
		if (inString)
		{
			if (escaped)
			{
				if (c == 'u' && i + 4 < value.size() && value[i + 1] == '0' && value[i + 2] == '0' &&
					value[i + 3] == '0' && value[i + 4] == '0') return false;
				escaped = false;
			}
			else if (c == '\\') escaped = true;
			else if (c == '"') inString = false;
			continue;
		}

		if (c == '"')
		{
			inString = true;
			continue;
		}
		if (c == '[') return false;
		if (c == '{')
		{
			if (i != 0 || closed) return false;
		}
		else if (c == '}')
		{
			if (closed) return false;
			closed = true;
			closeIndex = i;
		}
	}
	if (inString || escaped || !closed) return false;
	for (size_t i = closeIndex + 1; i < value.size(); ++i)
		if (value[i] != ' ' && value[i] != '\t' && value[i] != '\r' && value[i] != '\n') return false;
	return true;
}

bool WebValidation::validBasicCredentials(std::string_view username, std::string_view password)
{
	if (username.empty() || password.empty() || username.size() > 125 || password.size() > 125 ||
		username.size() + password.size() + 1 > 126 || username.find(':') != std::string_view::npos) return false;
	for (unsigned char c : username)
		if (c < 0x20 || c == 0x7F) return false;
	for (unsigned char c : password)
		if (c < 0x20 || c == 0x7F) return false;
	return true;
}

bool WebValidation::basicAuthMatches(std::string_view authorization, std::string_view username,
	std::string_view password)
{
	if (authorization.size() > 256 || !validBasicCredentials(username, password)) return false;
	const size_t space = authorization.find(' ');
	if (space == std::string_view::npos || !equalAsciiInsensitive(authorization.substr(0, space), "Basic")) return false;
	const auto token = authorization.substr(space + 1);
	if (token.empty() || token.find(' ') != std::string_view::npos || token.find('\t') != std::string_view::npos) return false;
	std::array<char, 128> decoded{};
	size_t decodedLength = 0;
	if (!decodeBasic(token, decoded, decodedLength)) return false;
	std::string_view credentials(decoded.data(), decodedLength);
	const size_t colon = credentials.find(':');
	if (colon == std::string_view::npos) return false;
	for (unsigned char c : credentials)
		if (c == 0 || c < 0x20 || c == 0x7F) return false;
	return constantTimeEqual(credentials.substr(0, colon), username) &&
		constantTimeEqual(credentials.substr(colon + 1), password);
}

bool WebValidation::parseId(std::string_view text, ConnectionId& out)
{
	if (text.size() != 25 || text[16] != '-') return false;
	uint64_t boot = 0;
	uint32_t sequence = 0;
	for (size_t i = 0; i < 16; ++i)
	{
		const uint8_t digit = hexValue(text[i]);
		if (digit == 0xFF) return false;
		boot = (boot << 4) | digit;
	}
	for (size_t i = 17; i < 25; ++i)
	{
		const uint8_t digit = hexValue(text[i]);
		if (digit == 0xFF) return false;
		sequence = (sequence << 4) | digit;
	}
	if (boot == 0 || sequence == 0) return false;
	out = { boot, sequence };
	return true;
}

std::string WebValidation::formatId(ConnectionId id)
{
	char value[26]{};
	std::snprintf(value, sizeof(value), "%016llx-%08x",
		static_cast<unsigned long long>(id.boot), static_cast<unsigned>(id.sequence));
	return value;
}

bool WebValidation::normalizeMac(std::string_view text, std::array<char, 18>& out)
{
	if (text.size() != 17) return false;
	for (size_t i = 0; i < text.size(); ++i)
	{
		if ((i + 1) % 3 == 0)
		{
			if (text[i] != ':') return false;
			out[i] = ':';
		}
		else
		{
			const uint8_t value = hexValue(text[i]);
			if (value == 0xFF) return false;
			out[i] = value < 10 ? static_cast<char>('0' + value) : static_cast<char>('A' + value - 10);
		}
	}
	out[17] = '\0';
	return true;
}
#endif
