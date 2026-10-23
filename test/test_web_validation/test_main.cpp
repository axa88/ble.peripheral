#include "../../src/webValidation.h"
#include "../../src/bleValidation.h"
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace
{
unsigned passed = 0;
unsigned failed = 0;

void check(bool condition, const char* name)
{
	if (condition)
	{
		++passed;
		std::printf("PASS %s\n", name);
	}
	else
	{
		++failed;
		std::printf("FAIL %s\n", name);
	}
}

void testAuthorities()
{
	WebValidation::Authority parsed;
	check(WebValidation::parseAuthority("192.168.1.160", parsed) && parsed.host == "192.168.1.160" && parsed.port == 80,
		"authority-ip-default-port");
	check(WebValidation::parseAuthority("BLE-WROVER-1.LoCaL:80", parsed) && parsed.host == "ble-wrover-1.local" && parsed.port == 80,
		"authority-normalizes-host-and-port");
	check(WebValidation::parseAuthority("ble-wrover-1.local:81", parsed) && parsed.port == 81,
		"authority-parses-nondefault-port-for-policy-check");
	check(!WebValidation::parseAuthority("user@ble-wrover-1.local", parsed), "authority-rejects-userinfo");
	check(!WebValidation::parseAuthority("ble-wrover-1.local/path", parsed), "authority-rejects-path");
	check(!WebValidation::parseAuthority("ble-wrover-1.local,evil.local", parsed), "authority-rejects-combined-duplicate-value");
	check(!WebValidation::parseAuthority("ble-wrover-1.local\r\nHost: evil.local", parsed), "authority-rejects-header-injection");
	check(!WebValidation::parseAuthority("[::1]", parsed), "authority-rejects-unconfigured-ipv6-literal");
}

void testHostOrigin()
{
	check(WebValidation::validHostOrigin("192.168.1.160", "", "192.168.1.160", "ble-wrover-1"),
		"host-current-ip-without-origin");
	check(WebValidation::validHostOrigin("BLE-WROVER-1.LOCAL:80", "http://ble-wrover-1.local", "192.168.1.160", "ble-wrover-1"),
		"host-mdns-and-same-origin");
	check(WebValidation::validHostOrigin("ble-wrover-1.local", "http://BLE-WROVER-1.LOCAL:80", "192.168.1.160", "ble-wrover-1"),
		"origin-normalizes-case-and-explicit-default-port");
	check(!WebValidation::validHostOrigin("evil.local", "", "192.168.1.160", "ble-wrover-1"),
		"host-rejects-unknown-name");
	check(!WebValidation::validHostOrigin("192.168.1.160:81", "", "192.168.1.160", "ble-wrover-1"),
		"host-rejects-non-http-port");
	check(!WebValidation::validHostOrigin("ble-wrover-1.local", "https://ble-wrover-1.local", "192.168.1.160", "ble-wrover-1"),
		"origin-rejects-https");
	check(!WebValidation::validHostOrigin("ble-wrover-1.local", "http://ble-wrover-1.local/", "192.168.1.160", "ble-wrover-1"),
		"origin-rejects-path");
	check(!WebValidation::validHostOrigin("ble-wrover-1.local", "http://192.168.1.160", "192.168.1.160", "ble-wrover-1"),
		"origin-requires-authority-match");
	check(!WebValidation::validHostOrigin("ble-wrover-1.local,evil.local", "", "192.168.1.160", "ble-wrover-1"),
		"host-rejects-combined-value");
}

void testContentType()
{
	check(WebValidation::isJsonContentType("application/json"), "content-type-json");
	check(WebValidation::isJsonContentType("Application/JSON; charset=utf-8"), "content-type-json-utf8");
	check(!WebValidation::isJsonContentType("application/jsonp"), "content-type-rejects-jsonp");
	check(!WebValidation::isJsonContentType("text/plain"), "content-type-rejects-text");
	check(!WebValidation::isJsonContentType("application/json; charset=latin1"), "content-type-rejects-other-charset");
	check(!WebValidation::isJsonContentType("application/json\r\nX: y"), "content-type-rejects-control");
}

void testFlatJsonObject()
{
	check(WebValidation::isFlatJsonObject("{\"op\":\"status\",\"value\":1}"), "json-flat-object-accepted");
	check(WebValidation::isFlatJsonObject("{\"value\":\"braces { [ ] } remain string data\"}"),
		"json-nesting-characters-in-string-accepted");
	check(WebValidation::isFlatJsonObject("{\"value\":\"escaped quote \\\" and slash \\\\\"}"),
		"json-escaped-quote-and-backslash-accepted");
	check(!WebValidation::isFlatJsonObject("{\"nested\":{\"value\":1}}"), "json-nested-object-rejected");
	check(!WebValidation::isFlatJsonObject("{\"items\":[1,2]}"), "json-array-rejected");
	check(!WebValidation::isFlatJsonObject("{\"value\":\"\\u0000\"}"), "json-nul-escape-rejected");
	check(WebValidation::isFlatJsonObject("{\"value\":\"literal \\\\u0000 sequence\"}"),
		"json-literal-escaped-unicode-text-accepted");
	const char rawNul[] = {'{', '"', 'x', '"', ':', '"', '\0', '"', '}'};
	check(!WebValidation::isFlatJsonObject(std::string_view(rawNul, sizeof(rawNul))), "json-raw-nul-rejected");
	check(!WebValidation::isFlatJsonObject("[]"), "json-root-array-rejected");
	check(!WebValidation::isFlatJsonObject("{\"x\":1} trailing"), "json-trailing-data-rejected");
}

void testBasicAuth()
{
	check(WebValidation::basicAuthMatches("Basic d2ViOnNlY3JldA==", "web", "secret"), "basic-auth-valid");
	check(WebValidation::basicAuthMatches("Basic d2ViOnBhOnNz", "web", "pa:ss"), "basic-auth-allows-colon-in-password");
	check(WebValidation::basicAuthMatches("bAsIc d2ViOnNlY3JldA==", "web", "secret"), "basic-auth-scheme-case-insensitive");
	check(!WebValidation::basicAuthMatches("Basic d2ViOnNlY3JldA==", "web", "wrong"), "basic-auth-rejects-password-mismatch");
	check(!WebValidation::basicAuthMatches("Basic d2ViOnNlY3JldA==", "admin", "secret"), "basic-auth-rejects-user-mismatch");
	check(!WebValidation::basicAuthMatches("Basic d2ViOnNlY3JldA=", "web", "secret"), "basic-auth-rejects-bad-padding");
	check(!WebValidation::basicAuthMatches("Basic d2ViOnNlY3JldA== extra", "web", "secret"), "basic-auth-rejects-extra-token");
	check(!WebValidation::basicAuthMatches("Bearer d2ViOnNlY3JldA==", "web", "secret"), "basic-auth-rejects-other-scheme");
	check(!WebValidation::basicAuthMatches("Basic d2ViOnNlY3JldA==", "", "secret"), "basic-auth-fails-closed-empty-config");
	check(WebValidation::validBasicCredentials("web", "pa:ss"), "basic-config-allows-colon-in-password");
	check(!WebValidation::validBasicCredentials("web:name", "secret"), "basic-config-rejects-colon-in-username");
	check(!WebValidation::validBasicCredentials("web\n", "secret"), "basic-config-rejects-control-in-username");
	check(!WebValidation::validBasicCredentials("web", "sec\rret"), "basic-config-rejects-control-in-password");
	check(!WebValidation::validBasicCredentials(std::string(126, 'u'), "p"), "basic-config-rejects-too-long-credentials");
}

void testIdsAndAddresses()
{
	const ConnectionId expected{ 0x0123456789abcdefULL, 0x1234abcdU };
	const auto formatted = WebValidation::formatId(expected);
	ConnectionId parsed{};
	check(formatted == "0123456789abcdef-1234abcd" && WebValidation::parseId(formatted, parsed) && parsed == expected,
		"connection-id-roundtrip");
	check(WebValidation::parseId("0123456789ABCDEF-1234ABCD", parsed) && parsed == expected,
		"connection-id-accepts-hex-case");
	check(!WebValidation::parseId("0000000000000000-00000001", parsed), "connection-id-rejects-empty-boot");
	check(!WebValidation::parseId("0123456789abcdef-00000000", parsed), "connection-id-rejects-empty-sequence");
	check(!WebValidation::parseId("0123456789abcdef_1234abcd", parsed), "connection-id-rejects-separator");
	check(!WebValidation::parseId("0123456789abcdef-1234abcdx", parsed), "connection-id-rejects-trailing-data");
	std::array<char, 18> mac{};
	check(WebValidation::normalizeMac("aa:bb:cc:00:12:ef", mac) && std::string_view(mac.data()) == "AA:BB:CC:00:12:EF",
		"bond-address-normalizes-canonical-mac");
	check(!WebValidation::normalizeMac("AA-BB-CC-00-12-EF", mac), "bond-address-rejects-noncanonical-separators");
	check(!WebValidation::normalizeMac("A:BB:CC:00:12:EF", mac), "bond-address-rejects-short-octet");
}
}

int main()
{
	const auto parameters = BleValidation::connectionParameters(15, 30, 0, 4000);
	check(parameters && parameters->minIntervalUnits == 12 && parameters->maxIntervalUnits == 24 &&
		parameters->supervisionTimeoutUnits == 400, "connection-parameters-native-units");
	check(!BleValidation::connectionParameters(8, 101, 0, 203), "connection-timeout-rejects-invalid-after-rounding");
	check(BleValidation::connectionParameters(8, 101, 0, 210).has_value(), "connection-timeout-valid-after-rounding");
	check(!BleValidation::connectionParameters(8, 50, 0, 100), "connection-timeout-strict-boundary");
	testAuthorities();
	testHostOrigin();
	testContentType();
	testFlatJsonObject();
	testBasicAuth();
	testIdsAndAddresses();
	std::printf("%u passed, %u failed\n", passed, failed);
	return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
