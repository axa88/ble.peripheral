#pragma once

#include "connectionId.h"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace WebValidation
{
struct Authority
{
	std::string host;
	uint16_t port = 80;
};

bool parseAuthority(std::string_view value, Authority& out);
bool parseOrigin(std::string_view value, Authority& out);
bool validHostOrigin(std::string_view hostHeader, std::string_view originHeader,
	std::string_view currentIp, std::string_view otaHostname);
bool isJsonContentType(std::string_view value);
bool isFlatJsonObject(std::string_view value);
bool validBasicCredentials(std::string_view username, std::string_view password);
bool basicAuthMatches(std::string_view authorization, std::string_view username,
	std::string_view password);
bool parseId(std::string_view text, ConnectionId& out);
std::string formatId(ConnectionId id);
bool normalizeMac(std::string_view text, std::array<char, 18>& out);
}
