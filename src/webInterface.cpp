#include "bleConfig.h"
#if BLE_ENABLE_WEB_INTERFACE
#include "webInterface.h"
#include "webAssets.h"
#include "webValidation.h"
#include "advertising.h"
#include "bluetoothManager.h"
#include "netManager.h"
#include "pairingCoordinator.h"
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <cJSON.h>
#include <esp_http_server.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(WEB_USERNAME) && defined(WEB_PASSWORD)
namespace
{
constexpr char kWebUsername[] = WEB_USERNAME;
constexpr char kWebPassword[] = WEB_PASSWORD;
}
#endif

namespace
{
constexpr size_t kMaxRequestBody = 1024;
constexpr size_t kMaxHeaderValue = 256;
constexpr size_t kWorkerStackBytes = 6144;
constexpr uint32_t kPhyTxMask1M = 1;
constexpr uint32_t kPhyRxMask2M = 2;

enum class Route { Advertising, Security, Bonds, Connections, Mtu };
enum class WorkerOperation
{
	SetAdvertisingActive,
	StartAllAdvertising,
	StopAllAdvertising,
	SetAdvertisingMode,
	SetAdvertisingDiscoverability,
	SetAdvertisingInterval,
	SetRestartAfterConnection,
	SetDirectedTarget,
	AddInstance,
	RemoveInstance,
	SetCapabilities,
	SetAuthenticationFlag,
	SetKeyDistributionFlag,
	DeleteConnectedBond,
	DeleteAllBonds,
	Disconnect,
	DisconnectAll,
	SetConnectionParameters,
	SetDataLength,
	RequestPhy,
	ReadPeerLink,
	SetPreferredMtu
};
enum class SlotState { Empty, Queued, Running, Done };
enum class Result { Applied, Accepted, Dispatched, Failed };

struct WorkerCommand
{
	WorkerOperation operation{};
	ConnectionId connection{};
	uint32_t p1 = 0;
	uint32_t p2 = 0;
	uint32_t p3 = 0;
	uint32_t p4 = 0;
	uint8_t instanceId = 0;
	uint8_t addressType = 0;
	bool flag = false;
	char name[32]{};
	char address[18]{};
	char target[26]{};
};

struct OperationData
{
	bool hasStartCounts = false;
	unsigned started = 0;
	unsigned startFailed = 0;
	bool blockedByDirectedMode = false;
	bool hasStopCounts = false;
	unsigned stopped = 0;
	unsigned stopFailed = 0;
	bool hasBondCounts = false;
	int deleted = 0;
	int remaining = 0;
	bool hasLink = false;
	ConnectionId linkConnection{};
	bool hasRssi = false;
	int8_t rssiDbm = 0;
	bool hasPhy = false;
	uint8_t txPhy = 0;
	uint8_t rxPhy = 0;
};

struct WorkerRecord
{
	ConnectionId id{};
	WorkerCommand command{};
	SlotState state = SlotState::Empty;
	std::optional<Result> result;
	char detail[96]{};
	char errorCode[48]{};
	bool errorHasInstanceId = false;
	uint8_t errorInstanceId = 0;
	OperationData data{};
};

struct ExecutionResult
{
	Result result = Result::Failed;
	OperationData data{};
	const char* detail = "Operation failed.";
	const char* errorCode = nullptr;
	std::optional<uint8_t> errorInstanceId;
};

enum class Admission { Ready, InvalidArgument, Unsupported, NotFound, Busy };

httpd_handle_t server = nullptr;
TaskHandle_t operationWorker = nullptr;
std::mutex operationMutex;
WorkerRecord operationRecord;
std::array<char, 65> csrfToken{};
uint64_t operationBoot = 0;
uint32_t operationSequence = 0;

char asciiLower(char c)
{
	return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
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

const char* statusText(int status)
{
	switch (status)
	{
		case 200: return "200 OK";
		case 202: return "202 Accepted";
		case 400: return "400 Bad Request";
		case 401: return "401 Unauthorized";
		case 403: return "403 Forbidden";
		case 404: return "404 Not Found";
		case 409: return "409 Conflict";
		default: return "500 Internal Server Error";
	}
}

void sendJson(httpd_req_t* request, int status, cJSON* body)
{
	if (!body)
	{
		httpd_resp_set_status(request, statusText(500));
		httpd_resp_set_type(request, "application/json; charset=utf-8");
		httpd_resp_set_hdr(request, "Cache-Control", "no-store");
		httpd_resp_sendstr(request, "{\"result\":\"Failed\",\"detail\":\"Response allocation failed.\"}");
		return;
	}
	char* payload = cJSON_PrintUnformatted(body);
	if (!payload)
	{
		cJSON_Delete(body);
		httpd_resp_set_status(request, statusText(500));
		httpd_resp_set_type(request, "application/json; charset=utf-8");
		httpd_resp_set_hdr(request, "Cache-Control", "no-store");
		httpd_resp_sendstr(request, "{\"result\":\"Failed\",\"detail\":\"Response serialization failed.\"}");
		return;
	}
	httpd_resp_set_status(request, statusText(status));
	httpd_resp_set_type(request, "application/json; charset=utf-8");
	httpd_resp_set_hdr(request, "Cache-Control", "no-store");
	httpd_resp_sendstr(request, payload);
	cJSON_free(payload);
	cJSON_Delete(body);
}

void sendFailure(httpd_req_t* request, int status, const char* result, const char* detail)
{
	cJSON* body = cJSON_CreateObject();
	if (body)
	{
		cJSON_AddStringToObject(body, "result", result);
		cJSON_AddStringToObject(body, "detail", detail);
	}
	sendJson(request, status, body);
}

bool readHeader(httpd_req_t* request, const char* name, std::array<char, kMaxHeaderValue + 1>& buffer, size_t& length)
{
	length = httpd_req_get_hdr_value_len(request, name);
	if (length == 0 || length > kMaxHeaderValue) return false;
	if (httpd_req_get_hdr_value_str(request, name, buffer.data(), buffer.size()) != ESP_OK) return false;
	buffer[length] = '\0';
	return true;
}

bool isAuthenticated(httpd_req_t* request)
{
#if defined(WEB_USERNAME) && defined(WEB_PASSWORD)
	std::array<char, kMaxHeaderValue + 1> authorization{};
	size_t length = 0;
	if (!readHeader(request, "Authorization", authorization, length)) return false;
	return WebValidation::basicAuthMatches(std::string_view(authorization.data(), length), kWebUsername, kWebPassword);
#else
	(void)request;
	return false;
#endif
}

void unauthorized(httpd_req_t* request)
{
	httpd_resp_set_hdr(request, "WWW-Authenticate", "Basic realm=\"BLE Peripheral\", charset=\"UTF-8\"");
	sendFailure(request, 401, "Failed", "Authentication is required.");
}

void closeRequestSession(httpd_req_t* request)
{
	const int socket = httpd_req_to_sockfd(request);
	if (socket >= 0) (void)httpd_sess_trigger_close(request->handle, socket);
}

bool hostOriginAllowed(httpd_req_t* request)
{
	std::array<char, kMaxHeaderValue + 1> host{};
	std::array<char, kMaxHeaderValue + 1> origin{};
	size_t hostLength = 0, originLength = 0;
	if (!readHeader(request, "Host", host, hostLength)) return false;
	bool hasOrigin = false;
	originLength = httpd_req_get_hdr_value_len(request, "Origin");
	if (originLength == 0)
	{
		char emptyOrigin[1]{};
		const esp_err_t originResult = httpd_req_get_hdr_value_str(request, "Origin", emptyOrigin, sizeof(emptyOrigin));
		if (originResult == ESP_OK) return false;
		if (originResult != ESP_ERR_NOT_FOUND) return false;
	}
	else
	{
		if (originLength > kMaxHeaderValue ||
			httpd_req_get_hdr_value_str(request, "Origin", origin.data(), origin.size()) != ESP_OK) return false;
		origin[originLength] = '\0';
		hasOrigin = true;
	}
	const std::string_view hostView(host.data(), hostLength);
	const std::string_view originView = hasOrigin ? std::string_view(origin.data(), originLength) : std::string_view{};
	String hostname = ArduinoOTA.getHostname();
	const std::string_view hostNameView(hostname.c_str(), hostname.length());
	const bool connected = WiFi.status() == WL_CONNECTED;
	const String currentIp = connected ? WiFi.localIP().toString() : String();
	const String configuredIp = NetMgr::local_IP.toString();
	if (connected && WebValidation::validHostOrigin(hostView, originView, currentIp.c_str(), hostNameView)) return true;
	return WebValidation::validHostOrigin(hostView, originView, configuredIp.c_str(), hostNameView);
}

bool guardRequest(httpd_req_t* request, bool mutation)
{
	if (!isAuthenticated(request))
	{
		unauthorized(request);
		if (mutation && request->content_len != 0) closeRequestSession(request);
		return false;
	}
	if (!hostOriginAllowed(request))
	{
		sendFailure(request, 403, "Failed", "Host or Origin is not allowed.");
		if (mutation && request->content_len != 0) closeRequestSession(request);
		return false;
	}
	if (mutation)
	{
		std::array<char, kMaxHeaderValue + 1> token{};
		size_t length = 0;
		if (!readHeader(request, "X-CSRF-Token", token, length) ||
			!constantTimeEqual(std::string_view(token.data(), length), std::string_view(csrfToken.data(), csrfToken.size() - 1)))
		{
			sendFailure(request, 403, "Failed", "A valid CSRF token is required.");
			if (request->content_len != 0) closeRequestSession(request);
			return false;
		}
	}
	return true;
}

bool hasExactKeys(const cJSON* object, std::initializer_list<std::string_view> allowed)
{
	if (!cJSON_IsObject(object)) return false;
	for (const cJSON* item = object->child; item; item = item->next)
	{
		if (!item->string) return false;
		const std::string_view key(item->string);
		bool found = false;
		for (auto candidate : allowed)
			if (key == candidate) { found = true; break; }
		if (!found) return false;
		for (const cJSON* previous = object->child; previous != item; previous = previous->next)
			if (previous->string && key == previous->string) return false;
	}
	return true;
}

bool hasExactObjectKeys(const cJSON* object, std::initializer_list<std::string_view> expected)
{
	if (!hasExactKeys(object, expected)) return false;
	size_t count = 0;
	for (const cJSON* item = object->child; item; item = item->next) ++count;
	return count == expected.size();
}

bool getString(const cJSON* object, const char* key, std::string_view& value, size_t maxLength, bool allowEmpty = false)
{
	const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
	if (!cJSON_IsString(item) || !item->valuestring) return false;
	const size_t length = std::strlen(item->valuestring);
	if (length > maxLength || (!allowEmpty && length == 0)) return false;
	value = std::string_view(item->valuestring, length);
	return true;
}

bool getBoolean(const cJSON* object, const char* key, bool& value)
{
	const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
	if (!cJSON_IsBool(item)) return false;
	value = cJSON_IsTrue(item);
	return true;
}

bool getUnsigned(const cJSON* object, const char* key, uint32_t minValue, uint32_t maxValue, uint32_t& value)
{
	const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
	if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) || std::floor(item->valuedouble) != item->valuedouble ||
		item->valuedouble < static_cast<double>(minValue) || item->valuedouble > static_cast<double>(maxValue)) return false;
	value = static_cast<uint32_t>(item->valuedouble);
	return true;
}

bool parseBody(httpd_req_t* request, std::array<char, kMaxRequestBody + 1>& buffer, cJSON*& json)
{
	json = nullptr;
	if (request->content_len == 0 || request->content_len > kMaxRequestBody) return false;
	std::array<char, kMaxHeaderValue + 1> contentType{};
	size_t contentTypeLength = 0;
	if (!readHeader(request, "Content-Type", contentType, contentTypeLength) ||
		!WebValidation::isJsonContentType(std::string_view(contentType.data(), contentTypeLength))) return false;
	size_t received = 0;
	const int64_t receiveDeadlineUs = esp_timer_get_time() + 2000000;
	while (received < request->content_len)
	{
		if (esp_timer_get_time() >= receiveDeadlineUs) return false;
		const int count = httpd_req_recv(request, buffer.data() + received, request->content_len - received);
		if (count <= 0) return false;
		received += static_cast<size_t>(count);
		if (esp_timer_get_time() > receiveDeadlineUs) return false;
	}
	buffer[received] = '\0';
	const std::string_view raw(buffer.data(), received);
	if (!WebValidation::isFlatJsonObject(raw)) return false;
	const char* parseEnd = nullptr;
	json = cJSON_ParseWithLengthOpts(buffer.data(), received + 1, &parseEnd, true);
	if (!json || !parseEnd) return false;
	const char* end = buffer.data() + received;
	while (parseEnd < end && (*parseEnd == ' ' || *parseEnd == '\t' || *parseEnd == '\r' || *parseEnd == '\n')) ++parseEnd;
	if (parseEnd != end)
	{
		cJSON_Delete(json);
		json = nullptr;
		return false;
	}
	return true;
}

std::string_view operationName(const WorkerCommand& command)
{
	switch (command.operation)
	{
		case WorkerOperation::SetAdvertisingActive: return "setActive";
		case WorkerOperation::StartAllAdvertising: return "startAll";
		case WorkerOperation::StopAllAdvertising: return "stopAll";
		case WorkerOperation::SetAdvertisingMode: return "setMode";
		case WorkerOperation::SetAdvertisingDiscoverability: return "setDiscoverability";
		case WorkerOperation::SetAdvertisingInterval: return "setInterval";
		case WorkerOperation::SetRestartAfterConnection: return "setRestartAfterConnection";
		case WorkerOperation::SetDirectedTarget: return "setDirectedTarget";
		case WorkerOperation::AddInstance: return "addInstance";
		case WorkerOperation::RemoveInstance: return "removeInstance";
		case WorkerOperation::SetCapabilities: return "setCapabilities";
		case WorkerOperation::SetAuthenticationFlag: return "setAuthentication";
		case WorkerOperation::SetKeyDistributionFlag: return "setKeyDistribution";
		case WorkerOperation::DeleteConnectedBond: return "deleteConnection";
		case WorkerOperation::DeleteAllBonds: return "deleteAll";
		case WorkerOperation::Disconnect: return "disconnect";
		case WorkerOperation::DisconnectAll: return "disconnectAll";
		case WorkerOperation::SetConnectionParameters: return "setConnectionParameters";
		case WorkerOperation::SetDataLength: return "setDataLength";
		case WorkerOperation::RequestPhy: return "requestPhy";
		case WorkerOperation::ReadPeerLink: return "readPeerLink";
		case WorkerOperation::SetPreferredMtu: return "setPreferred";
	}
	return "unknown";
}

const char* stateName(SlotState state)
{
	switch (state)
	{
		case SlotState::Queued: return "Queued";
		case SlotState::Running: return "Running";
		case SlotState::Done: return "Done";
		default: return "Empty";
	}
}

const char* resultName(Result result)
{
	switch (result)
	{
		case Result::Applied: return "Applied";
		case Result::Accepted: return "Accepted";
		case Result::Dispatched: return "Dispatched";
		default: return "Failed";
	}
}

Result mapOutcome(BluetoothManager::Outcome outcome)
{
	switch (outcome)
	{
		case BluetoothManager::Outcome::Applied: return Result::Applied;
		case BluetoothManager::Outcome::Accepted: return Result::Accepted;
		case BluetoothManager::Outcome::Dispatched: return Result::Dispatched;
		default: return Result::Failed;
	}
}

const char* defaultDetail(Result result)
{
	switch (result)
	{
		case Result::Applied: return "Operation applied.";
		case Result::Accepted: return "Native BLE API accepted the request; completion remains asynchronous.";
		case Result::Dispatched: return "Native BLE operation dispatched; completion is not observable here.";
		default: return "Operation failed.";
	}
}

void setTarget(WorkerCommand& command, std::string_view target)
{
	const size_t count = std::min(target.size(), sizeof(command.target) - 1);
	std::memcpy(command.target, target.data(), count);
	command.target[count] = '\0';
}

void setInstanceTarget(WorkerCommand& command, uint8_t instanceId)
{
	std::snprintf(command.target, sizeof(command.target), "%u", static_cast<unsigned>(instanceId));
}

bool parseConnectionId(const cJSON* json, ConnectionId& id)
{
	std::string_view text;
	return getString(json, "connectionId", text, 25) && WebValidation::parseId(text, id);
}

bool connectionSnapshotContains(ConnectionId id, bool& busy,
	BluetoothManager::ConnectedPeerSnapshot* snapshot = nullptr)
{
	busy = false;
	std::vector<BluetoothManager::ConnectedPeerSnapshot> peers;
	if (!BluetoothManager::Instance().TryGetConnectedPeerSnapshots(peers))
	{
		busy = true;
		return false;
	}
	const auto peer = std::find_if(peers.begin(), peers.end(), [id](const auto& candidate) { return candidate.connectionId == id; });
	if (peer == peers.end()) return false;
	if (snapshot) *snapshot = *peer;
	return true;
}

bool getAdvertisingSnapshot(std::vector<Advertising::InstanceConfig>& configs)
{
	return Advertising::TrySnapshot(configs);
}

const Advertising::InstanceConfig* findAdvertising(const std::vector<Advertising::InstanceConfig>& configs, uint8_t id)
{
	const auto it = std::find_if(configs.begin(), configs.end(), [id](const auto& config) { return config.id == id; });
	return it == configs.end() ? nullptr : &*it;
}

bool targetBondExists(const std::array<char, 18>& normalizedAddress, uint8_t addressType)
{
	const std::string address(normalizedAddress.data());
	const NimBLEAddress candidate(address, addressType);
	const int bondCount = NimBLEDevice::getNumBonds();
	for (int i = 0; i < bondCount; ++i)
	{
		const NimBLEAddress bond = NimBLEDevice::getBondedAddress(i);
		if (bond.getType() == addressType && bond == candidate) return true;
	}
	return false;
}

Admission invalid(const char*& detail, const char* message)
{
	detail = message;
	return Admission::InvalidArgument;
}

Admission unsupported(const char*& detail, const char* message)
{
	detail = message;
	return Admission::Unsupported;
}

Admission checkAdvertisingTarget(const cJSON* json, WorkerCommand& command, const char*& detail,
	std::vector<Advertising::InstanceConfig>& configs, const Advertising::InstanceConfig*& config)
{
	uint32_t id = 0;
	if (!getUnsigned(json, "instanceId", 0, 255, id)) return invalid(detail, "instanceId must be an integer from 0 to 255.");
	command.instanceId = static_cast<uint8_t>(id);
	if (!getAdvertisingSnapshot(configs))
	{
		detail = "Advertising configuration is busy; retry the request.";
		return Admission::Busy;
	}
	config = findAdvertising(configs, command.instanceId);
	if (!config)
	{
		detail = "Advertising instance was not found.";
		return Admission::NotFound;
	}
	setInstanceTarget(command, command.instanceId);
	return Admission::Ready;
}

Admission prepareWorkerCommand(Route route, const cJSON* json, WorkerCommand& command, const char*& detail)
{
	std::string_view op;
	if (!getString(json, "op", op, 40)) return invalid(detail, "Operation name is required.");
	std::vector<Advertising::InstanceConfig> advConfigs;
	const Advertising::InstanceConfig* advConfig = nullptr;
	uint32_t value = 0;
	ConnectionId connection{};
	bool busy = false;

	if (route == Route::Advertising)
	{
		if (op == "startAll")
		{
			if (!hasExactKeys(json, {"op"})) return invalid(detail, "Unexpected or duplicate request field.");
			command.operation = WorkerOperation::StartAllAdvertising;
			setTarget(command, "all");
			return Admission::Ready;
		}
		if (op == "stopAll")
		{
			if (!hasExactKeys(json, {"op"})) return invalid(detail, "Unexpected or duplicate request field.");
			command.operation = WorkerOperation::StopAllAdvertising;
			setTarget(command, "all");
			return Admission::Ready;
		}
		if (op == "addInstance")
		{
			if (!hasExactKeys(json, {"op", "instanceId", "name"})) return invalid(detail, "Unexpected or duplicate request field.");
#if !CONFIG_BT_NIMBLE_EXT_ADV
			return unsupported(detail, "Additional advertising instances require extended advertising.");
#else
			uint32_t instance = 0;
			std::string_view name;
			if (!getUnsigned(json, "instanceId", 0, 255, instance) || !getString(json, "name", name, 31))
				return invalid(detail, "instanceId or name is invalid.");
			for (unsigned char ch : name)
				if (ch < 0x20 || ch == 0x7F) return invalid(detail, "Name must not contain control characters.");
#if defined(CONFIG_BT_NIMBLE_MAX_EXT_ADV_INSTANCES)
			constexpr uint32_t maxInstances = CONFIG_BT_NIMBLE_MAX_EXT_ADV_INSTANCES;
#else
			constexpr uint32_t maxInstances = 1;
#endif
			if (instance >= maxInstances) return unsupported(detail, "Advertising instance ID exceeds this device's configured capacity.");
			if (!getAdvertisingSnapshot(advConfigs))
			{
				detail = "Advertising configuration is busy; retry the request.";
				return Admission::Busy;
			}
			if (findAdvertising(advConfigs, static_cast<uint8_t>(instance)))
			{
				detail = "Advertising instance ID is already in use.";
				return Admission::InvalidArgument;
			}
			command.operation = WorkerOperation::AddInstance;
			command.instanceId = static_cast<uint8_t>(instance);
			std::memcpy(command.name, name.data(), name.size());
			command.name[name.size()] = '\0';
			setInstanceTarget(command, command.instanceId);
			return Admission::Ready;
#endif
		}
		if (op == "setActive")
		{
			if (!hasExactKeys(json, {"op", "instanceId", "active"})) return invalid(detail, "Unexpected or duplicate request field.");
			const Admission checked = checkAdvertisingTarget(json, command, detail, advConfigs, advConfig);
			if (checked != Admission::Ready) return checked;
			if (!getBoolean(json, "active", command.flag)) return invalid(detail, "active must be a boolean.");
			if (command.flag && Advertising::modeIsDirected(advConfig->mode) && !advConfig->directedTarget.has_value())
				return invalid(detail, "Directed advertising requires a selected bonded target.");
			command.operation = WorkerOperation::SetAdvertisingActive;
			return Admission::Ready;
		}
		if (op == "setMode")
		{
			if (!hasExactKeys(json, {"op", "instanceId", "mode"})) return invalid(detail, "Unexpected or duplicate request field.");
			const Admission checked = checkAdvertisingTarget(json, command, detail, advConfigs, advConfig);
			if (checked != Admission::Ready) return checked;
			if (!getUnsigned(json, "mode", 0, 6, value)) return invalid(detail, "mode must be an integer from 0 to 6.");
			const auto mode = static_cast<Advertising::AdvertisementMode>(value);
			if (!Advertising::modeIsSupported(mode)) return unsupported(detail, "This advertising mode is not supported by the device.");
			command.operation = WorkerOperation::SetAdvertisingMode;
			command.p1 = value;
			return Admission::Ready;
		}
		if (op == "setDiscoverability")
		{
			if (!hasExactKeys(json, {"op", "instanceId", "mode"})) return invalid(detail, "Unexpected or duplicate request field.");
			const Admission checked = checkAdvertisingTarget(json, command, detail, advConfigs, advConfig);
			if (checked != Admission::Ready) return checked;
			if (!getUnsigned(json, "mode", 0, 2, value)) return invalid(detail, "mode must be an integer from 0 to 2.");
			command.operation = WorkerOperation::SetAdvertisingDiscoverability;
			command.p1 = value;
			return Admission::Ready;
		}
		if (op == "setInterval")
		{
			if (!hasExactKeys(json, {"op", "instanceId", "intervalMs"})) return invalid(detail, "Unexpected or duplicate request field.");
			const Admission checked = checkAdvertisingTarget(json, command, detail, advConfigs, advConfig);
			if (checked != Admission::Ready) return checked;
			if (!getUnsigned(json, "intervalMs", 20, 10240, command.p1)) return invalid(detail, "intervalMs must be from 20 to 10240.");
			command.operation = WorkerOperation::SetAdvertisingInterval;
			return Admission::Ready;
		}
		if (op == "setRestartAfterConnection")
		{
			if (!hasExactKeys(json, {"op", "instanceId", "enabled"})) return invalid(detail, "Unexpected or duplicate request field.");
			const Admission checked = checkAdvertisingTarget(json, command, detail, advConfigs, advConfig);
			if (checked != Admission::Ready) return checked;
			if (!getBoolean(json, "enabled", command.flag)) return invalid(detail, "enabled must be a boolean.");
			command.operation = WorkerOperation::SetRestartAfterConnection;
			return Admission::Ready;
		}
		if (op == "setDirectedTarget")
		{
			if (!hasExactKeys(json, {"op", "instanceId", "bondAddress", "addressType"})) return invalid(detail, "Unexpected or duplicate request field.");
			const Admission checked = checkAdvertisingTarget(json, command, detail, advConfigs, advConfig);
			if (checked != Admission::Ready) return checked;
			std::string_view macText;
			if (!getString(json, "bondAddress", macText, 17) || !getUnsigned(json, "addressType", 0, 1, value))
				return invalid(detail, "bondAddress or addressType is invalid.");
			std::array<char, 18> normalized{};
			if (!WebValidation::normalizeMac(macText, normalized)) return invalid(detail, "bondAddress must be a canonical MAC address.");
			command.addressType = static_cast<uint8_t>(value);
			if (!targetBondExists(normalized, command.addressType))
			{
				detail = "Directed target must be a currently stored bond.";
				return Admission::NotFound;
			}
			std::memcpy(command.address, normalized.data(), normalized.size());
			command.operation = WorkerOperation::SetDirectedTarget;
			return Admission::Ready;
		}
		if (op == "removeInstance")
		{
			if (!hasExactKeys(json, {"op", "instanceId"})) return invalid(detail, "Unexpected or duplicate request field.");
#if !CONFIG_BT_NIMBLE_EXT_ADV
			return unsupported(detail, "Removing advertising instances requires extended advertising.");
#else
			const Admission checked = checkAdvertisingTarget(json, command, detail, advConfigs, advConfig);
			if (checked != Admission::Ready) return checked;
			command.operation = WorkerOperation::RemoveInstance;
			return Admission::Ready;
#endif
		}
		return unsupported(detail, "Unknown advertising operation.");
	}

	if (route == Route::Security)
	{
		if (op == "setCapabilities")
		{
			if (!hasExactKeys(json, {"op", "value"})) return invalid(detail, "Unexpected or duplicate request field.");
			if (!getUnsigned(json, "value", 0, 4, command.p1)) return invalid(detail, "value must be an integer from 0 to 4.");
			command.operation = WorkerOperation::SetCapabilities;
			setTarget(command, "device");
			return Admission::Ready;
		}
		if (op == "setAuthentication" || op == "setKeyDistribution")
		{
			if (!hasExactKeys(json, {"op", "flag", "enabled"})) return invalid(detail, "Unexpected or duplicate request field.");
			std::string_view flag;
			if (!getString(json, "flag", flag, 32) || !getBoolean(json, "enabled", command.flag))
				return invalid(detail, "flag and enabled are required.");
			if (op == "setAuthentication")
			{
				if (flag == "bond") command.p1 = BLE_SM_PAIR_AUTHREQ_BOND;
				else if (flag == "mitm") command.p1 = BLE_SM_PAIR_AUTHREQ_MITM;
				else if (flag == "secureConnections") command.p1 = BLE_SM_PAIR_AUTHREQ_SC;
				else return invalid(detail, "Unknown authentication flag.");
				command.operation = WorkerOperation::SetAuthenticationFlag;
			}
			else
			{
				if (flag == "ltk") command.p1 = BLE_SM_PAIR_KEY_DIST_ENC;
				else if (flag == "irk") command.p1 = BLE_SM_PAIR_KEY_DIST_ID;
				else if (flag == "csrk") command.p1 = BLE_SM_PAIR_KEY_DIST_SIGN;
				else return invalid(detail, "Unknown key-distribution flag.");
				command.operation = WorkerOperation::SetKeyDistributionFlag;
			}
			setTarget(command, "device");
			return Admission::Ready;
		}
		return unsupported(detail, "Unknown security operation.");
	}

	if (route == Route::Bonds)
	{
		if (op == "deleteAll")
		{
			if (!hasExactKeys(json, {"op"})) return invalid(detail, "Unexpected or duplicate request field.");
			command.operation = WorkerOperation::DeleteAllBonds;
			setTarget(command, "all");
			return Admission::Ready;
		}
		if (op == "deleteConnection")
		{
			if (!hasExactKeys(json, {"op", "connectionId"}) || !parseConnectionId(json, connection))
				return invalid(detail, "connectionId must be a valid opaque connection ID.");
			BluetoothManager::ConnectedPeerSnapshot peer{};
			if (!connectionSnapshotContains(connection, busy, &peer))
			{
				if (busy) { detail = "Connection state is busy; retry the request."; return Admission::Busy; }
				detail = "Connection was not found.";
				return Admission::NotFound;
			}
			std::array<char, 18> identityAddress{};
			if (!WebValidation::normalizeMac(peer.identityAddress, identityAddress) ||
				!targetBondExists(identityAddress, peer.identityAddressType))
			{
				return invalid(detail, "Connection has no currently stored bond to delete.");
			}
			command.connection = connection;
			command.operation = WorkerOperation::DeleteConnectedBond;
			const auto id = WebValidation::formatId(connection);
			setTarget(command, id);
			return Admission::Ready;
		}
		return unsupported(detail, "Unknown bond operation.");
	}

	if (route == Route::Connections)
	{
		if (op == "disconnectAll")
		{
			if (!hasExactKeys(json, {"op"})) return invalid(detail, "Unexpected or duplicate request field.");
			command.operation = WorkerOperation::DisconnectAll;
			setTarget(command, "all");
			return Admission::Ready;
		}
		if (op == "disconnect" || op == "readPeerLink" || op == "requestPhy" || op == "setDataLength" ||
			op == "setConnectionParameters")
		{
			const bool exactFields = op == "setDataLength"
				? hasExactKeys(json, {"op", "connectionId", "octets"})
				: op == "setConnectionParameters"
					? hasExactKeys(json, {"op", "connectionId", "minIntervalMs", "maxIntervalMs", "latency", "supervisionTimeoutMs"})
					: hasExactKeys(json, {"op", "connectionId"});
			if (!exactFields || !parseConnectionId(json, connection))
				return invalid(detail, "connectionId or request fields are invalid.");
			if (!connectionSnapshotContains(connection, busy))
			{
				if (busy) { detail = "Connection state is busy; retry the request."; return Admission::Busy; }
				detail = "Connection was not found.";
				return Admission::NotFound;
			}
			command.connection = connection;
			setTarget(command, WebValidation::formatId(connection));
			if (op == "disconnect") command.operation = WorkerOperation::Disconnect;
			else if (op == "readPeerLink") command.operation = WorkerOperation::ReadPeerLink;
			else if (op == "requestPhy")
			{
#if defined(CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT) && CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT
				command.operation = WorkerOperation::RequestPhy;
#else
				return unsupported(detail, "PHY update is not supported by this build.");
#endif
			}
			else if (op == "setDataLength")
			{
				if (!getUnsigned(json, "octets", 27, 251, command.p1)) return invalid(detail, "octets must be from 27 to 251.");
				command.operation = WorkerOperation::SetDataLength;
			}
			else
			{
				if (!getUnsigned(json, "minIntervalMs", 8, 4000, command.p1) ||
					!getUnsigned(json, "maxIntervalMs", 8, 4000, command.p2) ||
					!getUnsigned(json, "latency", 0, 499, command.p3) ||
					!getUnsigned(json, "supervisionTimeoutMs", 100, 32000, command.p4) ||
					!BluetoothManager::ValidConnectionParameters(command.p1, command.p2, static_cast<uint16_t>(command.p3), command.p4))
					return invalid(detail, "Connection parameters are outside the supported ranges or violate timing constraints.");
				command.operation = WorkerOperation::SetConnectionParameters;
			}
			return Admission::Ready;
		}
		return unsupported(detail, "Unknown connection operation.");
	}

	if (route == Route::Mtu)
	{
		if (op != "setPreferred") return unsupported(detail, "Unknown MTU operation.");
		if (!hasExactKeys(json, {"op", "mtu"}) || !getUnsigned(json, "mtu", 23, 517, command.p1))
			return invalid(detail, "mtu must be an integer from 23 to 517.");
		command.operation = WorkerOperation::SetPreferredMtu;
		setTarget(command, "device");
		return Admission::Ready;
	}
	return unsupported(detail, "Unknown operation.");
}

bool handleAdmissionFailure(httpd_req_t* request, Admission admission, const char* detail)
{
	switch (admission)
	{
		case Admission::InvalidArgument: sendFailure(request, 400, "InvalidArgument", detail); break;
		case Admission::Unsupported: sendFailure(request, 400, "Unsupported", detail); break;
		case Admission::NotFound: sendFailure(request, 404, "Failed", detail); break;
		case Admission::Busy: sendFailure(request, 409, "Busy", detail); break;
		default: return false;
	}
	return true;
}

ConnectionId nextOperationIdLocked()
{
	if (operationBoot == 0) operationBoot = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
	if (operationBoot == 0) operationBoot = 1;
	if (++operationSequence == 0)
	{
		operationBoot = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
		if (operationBoot == 0) operationBoot = 1;
		operationSequence = 1;
	}
	return { operationBoot, operationSequence };
}

cJSON* operationDataJson(const OperationData& data)
{
	cJSON* json = cJSON_CreateObject();
	if (!json) return nullptr;
	if (data.hasStartCounts)
	{
		cJSON_AddNumberToObject(json, "started", data.started);
		cJSON_AddNumberToObject(json, "failed", data.startFailed);
		cJSON_AddBoolToObject(json, "blockedByDirectedMode", data.blockedByDirectedMode);
	}
	if (data.hasStopCounts)
	{
		cJSON_AddNumberToObject(json, "stopped", data.stopped);
		cJSON_AddNumberToObject(json, "failed", data.stopFailed);
	}
	if (data.hasBondCounts)
	{
		cJSON_AddNumberToObject(json, "deleted", data.deleted);
		cJSON_AddNumberToObject(json, "remaining", data.remaining);
	}
	if (data.hasLink)
	{
		const auto id = WebValidation::formatId(data.linkConnection);
		cJSON_AddStringToObject(json, "connectionId", id.c_str());
		if (data.hasRssi) cJSON_AddNumberToObject(json, "rssiDbm", data.rssiDbm);
		if (data.hasPhy)
		{
			cJSON_AddNumberToObject(json, "txPhy", data.txPhy);
			cJSON_AddNumberToObject(json, "rxPhy", data.rxPhy);
		}
		if (!data.hasRssi || !data.hasPhy)
		{
			cJSON* unavailable = cJSON_CreateArray();
			if (unavailable)
			{
				if (!data.hasRssi) cJSON_AddItemToArray(unavailable, cJSON_CreateString("rssiDbm"));
				if (!data.hasPhy)
				{
					cJSON_AddItemToArray(unavailable, cJSON_CreateString("txPhy"));
					cJSON_AddItemToArray(unavailable, cJSON_CreateString("rxPhy"));
				}
				cJSON_AddItemToObject(json, "unavailableFields", unavailable);
			}
		}
	}
	return json;
}

cJSON* operationRecordJson(const WorkerRecord& record, bool queuedResponse = false)
{
	cJSON* json = cJSON_CreateObject();
	if (!json) return nullptr;
	const auto id = WebValidation::formatId(record.id);
	cJSON_AddStringToObject(json, "operationId", id.c_str());
	const auto op = operationName(record.command);
	cJSON_AddStringToObject(json, "operation", std::string(op).c_str());
	cJSON_AddStringToObject(json, "target", record.command.target);
	cJSON_AddStringToObject(json, "state", stateName(record.state));
	if (queuedResponse) cJSON_AddStringToObject(json, "result", "Queued");
	else if (record.state == SlotState::Done && record.result) cJSON_AddStringToObject(json, "result", resultName(*record.result));
	else cJSON_AddNullToObject(json, "result");
	cJSON_AddStringToObject(json, "detail", record.detail);
	if (record.errorCode[0])
	{
		cJSON* error = cJSON_CreateObject();
		if (error)
		{
			cJSON_AddStringToObject(error, "code", record.errorCode);
			if (record.errorHasInstanceId)
				cJSON_AddNumberToObject(error, "instanceId", record.errorInstanceId);
			cJSON_AddItemToObject(json, "error", error);
		}
	}
	if (record.data.hasStartCounts || record.data.hasStopCounts || record.data.hasBondCounts || record.data.hasLink)
		cJSON_AddItemToObject(json, "data", operationDataJson(record.data));
	return json;
}

bool enqueueOperation(httpd_req_t* request, const WorkerCommand& command)
{
	if (!operationWorker)
	{
		sendFailure(request, 500, "Failed", "Operation worker is unavailable.");
		return false;
	}
	WorkerRecord responseRecord;
	bool busy = false;
	{
		std::lock_guard<std::mutex> lock(operationMutex);
		if (operationRecord.state == SlotState::Queued || operationRecord.state == SlotState::Running)
			busy = true;
		else
		{
			operationRecord = WorkerRecord{};
			operationRecord.id = nextOperationIdLocked();
			operationRecord.command = command;
			operationRecord.state = SlotState::Queued;
			std::strncpy(operationRecord.detail, "Operation accepted by the worker.", sizeof(operationRecord.detail) - 1);
			responseRecord = operationRecord;
			xTaskNotifyGive(operationWorker);
		}
	}
	if (busy)
	{
		sendFailure(request, 409, "Busy", "Another BLE operation is still active.");
		return false;
	}
	sendJson(request, 202, operationRecordJson(responseRecord, true));
	return true;
}

ExecutionResult execute(const WorkerCommand& command)
{
	ExecutionResult execution;
	auto& manager = BluetoothManager::Instance();
	switch (command.operation)
	{
		case WorkerOperation::SetAdvertisingActive:
			execution.result = (command.flag ? Advertising::Start(command.instanceId) : Advertising::Stop(command.instanceId)) ? Result::Applied : Result::Failed;
			break;
		case WorkerOperation::StartAllAdvertising:
		{
			const auto started = Advertising::StartAll();
			execution.data.hasStartCounts = true;
			execution.data.started = started.started;
			execution.data.startFailed = started.failed;
			execution.data.blockedByDirectedMode = started.blockedByDirectedMode;
			if (started.blockedByDirectedMode)
			{
				execution.result = Result::Failed;
				execution.detail = "Multiple configured instances include directed advertising; start one instance at a time.";
				execution.errorCode = "directed_advertising_exclusive";
			}
			else
			{
				execution.result = started.failed == 0 ? Result::Applied : Result::Failed;
				if (started.failed != 0)
					execution.detail = "Some advertising instances could not start; retry after pending stops or mode conflicts clear.";
			}
			break;
		}
		case WorkerOperation::StopAllAdvertising:
		{
			const auto stopped = Advertising::StopAll();
			execution.data.hasStopCounts = true;
			execution.data.stopped = stopped.stopped;
			execution.data.stopFailed = stopped.failed;
			execution.result = stopped.failed == 0 ? Result::Applied : Result::Failed;
			break;
		}
		case WorkerOperation::SetAdvertisingMode:
			execution.result = Advertising::SetMode(command.instanceId, static_cast<Advertising::AdvertisementMode>(command.p1)) ? Result::Applied : Result::Failed;
			break;
		case WorkerOperation::SetAdvertisingDiscoverability:
			execution.result = Advertising::SetDiscoverability(command.instanceId, static_cast<DiscoverableMode>(command.p1)) ? Result::Applied : Result::Failed;
			break;
		case WorkerOperation::SetAdvertisingInterval:
			execution.result = Advertising::SetInterval(command.instanceId, command.p1) ? Result::Applied : Result::Failed;
			break;
		case WorkerOperation::SetRestartAfterConnection:
			execution.result = Advertising::SetRestartAfterConnection(command.instanceId, command.flag) ? Result::Applied : Result::Failed;
			break;
		case WorkerOperation::SetDirectedTarget:
		{
			std::array<char, 18> address{};
			std::memcpy(address.data(), command.address, address.size());
			if (!targetBondExists(address, command.addressType))
			{
				execution.result = Result::Failed;
				execution.detail = "Directed target bond is no longer stored; refresh the bond list and choose a current bond.";
				break;
			}
			const auto update = Advertising::TrySetDirectedTarget(command.instanceId,
				NimBLEAddress(std::string(command.address), command.addressType));
			switch (update)
			{
				case Advertising::DirectedTargetUpdateResult::Applied:
					execution.result = Result::Applied;
					break;
				case Advertising::DirectedTargetUpdateResult::AdvertisingActive:
					execution.result = Result::Failed;
					execution.detail = "Stop advertising on this instance before changing its directed target.";
					execution.errorCode = "advertising_active";
					execution.errorInstanceId = command.instanceId;
					break;
				case Advertising::DirectedTargetUpdateResult::InstanceNotFound:
					execution.result = Result::Failed;
					execution.detail = "Advertising instance no longer exists; refresh advertising state.";
					break;
				case Advertising::DirectedTargetUpdateResult::InvalidTarget:
				case Advertising::DirectedTargetUpdateResult::TargetNotBonded:
					execution.result = Result::Failed;
					execution.detail = "Directed target bond is no longer stored; refresh the bond list and choose a current bond.";
					break;
			}
			break;
		}
		case WorkerOperation::AddInstance:
#if CONFIG_BT_NIMBLE_EXT_ADV
			execution.result = Advertising::AddDefaultInstance(command.instanceId, std::string_view(command.name)) ? Result::Applied : Result::Failed;
#else
			execution.result = Result::Failed;
#endif
			break;
		case WorkerOperation::RemoveInstance:
#if CONFIG_BT_NIMBLE_EXT_ADV
			execution.result = Advertising::RemoveInstance(command.instanceId) ? Result::Applied : Result::Failed;
#else
			execution.result = Result::Failed;
#endif
			break;
		case WorkerOperation::SetCapabilities:
			execution.result = manager.Capabilities(static_cast<uint8_t>(command.p1)) == command.p1 ? Result::Applied : Result::Failed;
			break;
		case WorkerOperation::SetAuthenticationFlag:
			execution.result = manager.SetAuthenticationFlag(static_cast<uint8_t>(command.p1), command.flag) ? Result::Applied : Result::Failed;
			break;
		case WorkerOperation::SetKeyDistributionFlag:
			execution.result = manager.SetKeyDistributionFlag(static_cast<uint8_t>(command.p1), command.flag) ? Result::Applied : Result::Failed;
			break;
		case WorkerOperation::DeleteConnectedBond:
		{
			const auto deleted = manager.DeleteConnectedPeerBond(command.connection);
			execution.result = mapOutcome(deleted.outcome);
			execution.data.hasBondCounts = true;
			execution.data.deleted = deleted.deleted;
			execution.data.remaining = deleted.remaining;
			break;
		}
		case WorkerOperation::DeleteAllBonds:
		{
			const auto deleted = manager.DeleteAllBonds();
			execution.result = mapOutcome(deleted.outcome);
			execution.data.hasBondCounts = true;
			execution.data.deleted = deleted.deleted;
			execution.data.remaining = deleted.remaining;
			break;
		}
		case WorkerOperation::Disconnect:
			execution.result = manager.Disconnect(command.connection) ? Result::Accepted : Result::Failed;
			break;
		case WorkerOperation::DisconnectAll:
			execution.result = mapOutcome(manager.DisconnectAll());
			break;
		case WorkerOperation::SetConnectionParameters:
			execution.result = mapOutcome(manager.RequestConnectionParams(command.connection, command.p1, command.p2,
				static_cast<uint16_t>(command.p3), command.p4));
			break;
		case WorkerOperation::SetDataLength:
			execution.result = mapOutcome(manager.RequestDataLength(command.connection, static_cast<uint16_t>(command.p1)));
			break;
		case WorkerOperation::RequestPhy:
			execution.result = mapOutcome(manager.RequestPhy(command.connection,
				BluetoothManager::PhyUpdate{static_cast<uint8_t>(kPhyTxMask1M), static_cast<uint8_t>(kPhyRxMask2M), 0}));
			break;
		case WorkerOperation::ReadPeerLink:
		{
			const auto link = manager.ReadPeerLink(command.connection);
			execution.result = mapOutcome(link.outcome);
			execution.data.hasLink = true;
			execution.data.linkConnection = command.connection;
			execution.data.hasRssi = link.rssiDbm.has_value();
			if (link.rssiDbm) execution.data.rssiDbm = *link.rssiDbm;
			execution.data.hasPhy = link.phy.has_value();
			if (link.phy)
			{
				execution.data.txPhy = link.phy->txPhy;
				execution.data.rxPhy = link.phy->rxPhy;
			}
			execution.detail = execution.result == Result::Applied ? "Controller RSSI and PHY reads completed." : "One or more controller fields were unavailable.";
			break;
		}
		case WorkerOperation::SetPreferredMtu:
			execution.result = mapOutcome(manager.SetPreferredMtu(static_cast<uint16_t>(command.p1)));
			break;
	}
	if (!execution.detail || std::strcmp(execution.detail, "Operation failed.") == 0)
		execution.detail = defaultDetail(execution.result);
	return execution;
}

void operationWorkerTask(void*)
{
	for (;;)
	{
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		WorkerCommand command{};
		ConnectionId id{};
		{
			std::lock_guard<std::mutex> lock(operationMutex);
			if (operationRecord.state != SlotState::Queued) continue;
			operationRecord.state = SlotState::Running;
			command = operationRecord.command;
			id = operationRecord.id;
		}
		const auto result = execute(command);
		{
			std::lock_guard<std::mutex> lock(operationMutex);
			if (operationRecord.id != id || operationRecord.state != SlotState::Running) continue;
			operationRecord.result = result.result;
			operationRecord.data = result.data;
			std::strncpy(operationRecord.detail, result.detail, sizeof(operationRecord.detail) - 1);
			if (result.errorCode)
				std::strncpy(operationRecord.errorCode, result.errorCode, sizeof(operationRecord.errorCode) - 1);
			if (result.errorInstanceId)
			{
				operationRecord.errorHasInstanceId = true;
				operationRecord.errorInstanceId = *result.errorInstanceId;
			}
			operationRecord.state = SlotState::Done;
		}
	}
}

bool enqueueRoute(httpd_req_t* request, Route route)
{
	std::array<char, kMaxRequestBody + 1> body{};
	cJSON* json = nullptr;
	if (!parseBody(request, body, json))
	{
		sendFailure(request, 400, "InvalidArgument", request->content_len > kMaxRequestBody ? "Request body exceeds 1024 bytes." : "A valid application/json body is required.");
		closeRequestSession(request);
		return false;
	}
	WorkerCommand command{};
	const char* detail = "Invalid operation.";
	const Admission admission = prepareWorkerCommand(route, json, command, detail);
	cJSON_Delete(json);
	if (handleAdmissionFailure(request, admission, detail)) return false;
	return enqueueOperation(request, command);
}

esp_err_t handlePairingPost(httpd_req_t* request)
{
	std::array<char, kMaxRequestBody + 1> body{};
	cJSON* json = nullptr;
	if (!parseBody(request, body, json))
	{
		sendFailure(request, 400, "InvalidArgument", request->content_len > kMaxRequestBody ? "Request body exceeds 1024 bytes." : "A valid application/json body is required.");
		closeRequestSession(request);
		return ESP_OK;
	}
	std::string_view op;
	ConnectionId id{};
	Pairing::Type type{};
	uint32_t value = 0;
	bool accepted = false;
	if (!getString(json, "op", op, 32))
	{
		cJSON_Delete(json);
		sendFailure(request, 400, "InvalidArgument", "Operation name is required.");
		return ESP_OK;
	}
	if (op == "enterPasskey")
	{
		if (!hasExactKeys(json, {"op", "requestId", "passkey"}))
		{
			cJSON_Delete(json);
			sendFailure(request, 400, "InvalidArgument", "Unexpected or duplicate request field.");
			return ESP_OK;
		}
		std::string_view idText;
		if (!getString(json, "requestId", idText, 25) || !WebValidation::parseId(idText, id) ||
			!getUnsigned(json, "passkey", 0, 999999, value))
		{
			cJSON_Delete(json);
			sendFailure(request, 400, "InvalidArgument", "requestId or passkey is invalid.");
			return ESP_OK;
		}
		type = Pairing::Type::PasskeyEntry;
	}
	else if (op == "confirm")
	{
		if (!hasExactKeys(json, {"op", "requestId", "accepted"}))
		{
			cJSON_Delete(json);
			sendFailure(request, 400, "InvalidArgument", "Unexpected or duplicate request field.");
			return ESP_OK;
		}
		std::string_view idText;
		if (!getString(json, "requestId", idText, 25) || !WebValidation::parseId(idText, id) || !getBoolean(json, "accepted", accepted))
		{
			cJSON_Delete(json);
			sendFailure(request, 400, "InvalidArgument", "requestId or accepted value is invalid.");
			return ESP_OK;
		}
		type = Pairing::Type::NumericConfirmation;
		value = accepted ? 1 : 0;
	}
	else
	{
		cJSON_Delete(json);
		sendFailure(request, 400, "Unsupported", "Unknown pairing operation.");
		return ESP_OK;
	}
	cJSON_Delete(json);
	const auto result = PairingCoordinator::respond(id, type, value);
	switch (result)
	{
		case PairingCoordinator::Result::Accepted: sendFailure(request, 200, "Accepted", "Native pairing response was accepted."); break;
		case PairingCoordinator::Result::Failed: sendFailure(request, 200, "Failed", "Native pairing response failed."); break;
		case PairingCoordinator::Result::NotFound: sendFailure(request, 404, "Failed", "Pairing request was not found."); break;
		case PairingCoordinator::Result::NotPending: sendFailure(request, 409, "Failed", "Pairing request is no longer pending."); break;
		case PairingCoordinator::Result::Expired: sendFailure(request, 404, "Failed", "Pairing request has expired."); break;
		case PairingCoordinator::Result::InvalidArgument: sendFailure(request, 400, "InvalidArgument", "Pairing response is invalid for this request."); break;
	}
	return ESP_OK;
}

void addOperationRecord(cJSON* json)
{
	WorkerRecord snapshot;
	{
		std::lock_guard<std::mutex> lock(operationMutex);
		snapshot = operationRecord;
	}
	if (snapshot.state == SlotState::Empty) cJSON_AddNullToObject(json, "lastOperation");
	else cJSON_AddItemToObject(json, "lastOperation", operationRecordJson(snapshot));
}

void addStatusSecurity(cJSON* root)
{
	auto& manager = BluetoothManager::Instance();
	cJSON* security = cJSON_CreateObject();
	if (!security) return;
	const uint8_t capabilities = manager.Capabilities();
	const uint8_t auth = manager.Authentication();
	const uint8_t keyDistribution = manager.Encryption();
	cJSON_AddNumberToObject(security, "capabilities", capabilities);
	cJSON_AddBoolToObject(security, "bond", (auth & BLE_SM_PAIR_AUTHREQ_BOND) != 0);
	cJSON_AddBoolToObject(security, "mitm", (auth & BLE_SM_PAIR_AUTHREQ_MITM) != 0);
	cJSON_AddBoolToObject(security, "secureConnections", (auth & BLE_SM_PAIR_AUTHREQ_SC) != 0);
	cJSON_AddBoolToObject(security, "ltk", (keyDistribution & BLE_SM_PAIR_KEY_DIST_ENC) != 0);
	cJSON_AddBoolToObject(security, "irk", (keyDistribution & BLE_SM_PAIR_KEY_DIST_ID) != 0);
	cJSON_AddBoolToObject(security, "csrk", (keyDistribution & BLE_SM_PAIR_KEY_DIST_SIGN) != 0);
	cJSON_AddItemToObject(root, "security", security);
}

void addStatusCapabilities(cJSON* root)
{
	cJSON* capabilities = cJSON_CreateObject();
	if (!capabilities) return;
#if CONFIG_BT_NIMBLE_EXT_ADV
	cJSON_AddBoolToObject(capabilities, "extendedAdvertising", true);
#if defined(CONFIG_BT_NIMBLE_MAX_EXT_ADV_INSTANCES)
	cJSON_AddNumberToObject(capabilities, "maxAdvertisingInstances", CONFIG_BT_NIMBLE_MAX_EXT_ADV_INSTANCES);
#else
	cJSON_AddNumberToObject(capabilities, "maxAdvertisingInstances", 1);
#endif
#else
	cJSON_AddBoolToObject(capabilities, "extendedAdvertising", false);
	cJSON_AddNumberToObject(capabilities, "maxAdvertisingInstances", 1);
#endif
#if defined(CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT) && CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT
	cJSON_AddBoolToObject(capabilities, "phyUpdate", true);
#else
	cJSON_AddBoolToObject(capabilities, "phyUpdate", false);
#endif
	cJSON* modes = cJSON_CreateArray();
	if (modes)
	{
		for (auto mode : Advertising::supportedModes)
		{
			cJSON* item = cJSON_CreateObject();
			if (!item) continue;
			cJSON_AddNumberToObject(item, "value", static_cast<uint8_t>(mode));
			cJSON_AddStringToObject(item, "label", Advertising::modeToString(mode).data());
			cJSON_AddItemToArray(modes, item);
		}
		cJSON_AddItemToObject(capabilities, "supportedModes", modes);
	}
	cJSON_AddItemToObject(root, "capabilities", capabilities);
}

bool statusIsComplete(const cJSON* root, const std::vector<Advertising::InstanceConfig>& advConfigs,
	const std::vector<BluetoothManager::ConnectedPeerSnapshot>& peers, int bondCount)
{
	if (!hasExactObjectKeys(root, {"device", "network", "preferredMtu", "advertising", "security", "bonds",
		"connections", "capabilities", "csrfToken", "lastOperation", "resources"})) return false;
	const cJSON* device = cJSON_GetObjectItemCaseSensitive(root, "device");
	const cJSON* network = cJSON_GetObjectItemCaseSensitive(root, "network");
	const cJSON* advertising = cJSON_GetObjectItemCaseSensitive(root, "advertising");
	const cJSON* security = cJSON_GetObjectItemCaseSensitive(root, "security");
	const cJSON* bonds = cJSON_GetObjectItemCaseSensitive(root, "bonds");
	const cJSON* connections = cJSON_GetObjectItemCaseSensitive(root, "connections");
	const cJSON* capabilities = cJSON_GetObjectItemCaseSensitive(root, "capabilities");
	const cJSON* supportedModes = cJSON_GetObjectItemCaseSensitive(capabilities, "supportedModes");
	const cJSON* resources = cJSON_GetObjectItemCaseSensitive(root, "resources");
	const cJSON* lastOperation = cJSON_GetObjectItemCaseSensitive(root, "lastOperation");
	if (!hasExactObjectKeys(device, {"name", "address", "uptimeMs"}) ||
		!hasExactObjectKeys(network, {"connected", "ip", "hostname"}) ||
		!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(root, "preferredMtu")) ||
		!cJSON_IsString(cJSON_GetObjectItemCaseSensitive(root, "csrfToken")) ||
		!hasExactObjectKeys(security, {"capabilities", "bond", "mitm", "secureConnections", "ltk", "irk", "csrk"}) ||
		!cJSON_IsArray(advertising) || cJSON_GetArraySize(advertising) != static_cast<int>(advConfigs.size()) ||
		!cJSON_IsArray(bonds) || cJSON_GetArraySize(bonds) != bondCount ||
		!cJSON_IsArray(connections) || cJSON_GetArraySize(connections) != static_cast<int>(peers.size()) ||
		!hasExactObjectKeys(capabilities, {"extendedAdvertising", "phyUpdate", "maxAdvertisingInstances", "supportedModes"}) ||
		!cJSON_IsArray(supportedModes) || cJSON_GetArraySize(supportedModes) != static_cast<int>(Advertising::supportedModes.size()) ||
		!hasExactObjectKeys(resources, {"heapFreeBytes", "heapMinimumFreeBytes", "webWorkerStackHighWaterBytes",
			"pairingExecutorStackHighWaterBytes", "httpServerStackHighWaterBytes"})) return false;
	for (const cJSON* item = advertising->child; item; item = item->next)
	{
		if (!hasExactObjectKeys(item, {"instanceId", "name", "mode", "discoverability", "active",
			"restartAfterConnection", "intervalMs", "directedTarget"})) return false;
		const cJSON* target = cJSON_GetObjectItemCaseSensitive(item, "directedTarget");
		if (cJSON_IsObject(target) && !hasExactObjectKeys(target, {"address", "addressType"})) return false;
		if (!cJSON_IsObject(target) && !cJSON_IsNull(target)) return false;
	}
	for (const cJSON* item = bonds->child; item; item = item->next)
		if (!hasExactObjectKeys(item, {"address", "addressType"})) return false;
	for (const cJSON* item = connections->child; item; item = item->next)
		if (!hasExactObjectKeys(item, {"connectionId", "handle", "address", "identityAddress", "intervalMs", "timeoutMs",
			"latency", "mtu", "bonded", "encrypted", "authenticated", "keySize"})) return false;
	for (const cJSON* item = supportedModes->child; item; item = item->next)
		if (!hasExactObjectKeys(item, {"value", "label"})) return false;
	if (!cJSON_IsNull(lastOperation))
	{
		if (!hasExactKeys(lastOperation, {"operationId", "operation", "target", "state", "result", "detail", "data", "error"})) return false;
		const char* required[] = {"operationId", "operation", "target", "state", "detail"};
		for (const char* key : required)
			if (!cJSON_IsString(cJSON_GetObjectItemCaseSensitive(lastOperation, key))) return false;
		const cJSON* result = cJSON_GetObjectItemCaseSensitive(lastOperation, "result");
		if (!cJSON_IsString(result) && !cJSON_IsNull(result)) return false;
		size_t count = 0;
		for (const cJSON* item = lastOperation->child; item; item = item->next) ++count;
		const cJSON* data = cJSON_GetObjectItemCaseSensitive(lastOperation, "data");
		const cJSON* error = cJSON_GetObjectItemCaseSensitive(lastOperation, "error");
		if (count != 6 + (data ? 1u : 0u) + (error ? 1u : 0u)) return false;
		if (error)
		{
			if (!hasExactKeys(error, {"code", "instanceId"}) ||
				!cJSON_IsString(cJSON_GetObjectItemCaseSensitive(error, "code"))) return false;
			const cJSON* errorInstance = cJSON_GetObjectItemCaseSensitive(error, "instanceId");
			if (errorInstance && !cJSON_IsNumber(errorInstance)) return false;
			size_t errorCount = 0;
			for (const cJSON* item = error->child; item; item = item->next) ++errorCount;
			if (errorCount != 1 + (errorInstance ? 1u : 0u)) return false;
		}
		if (data)
		{
			if (!cJSON_IsObject(data) || !hasExactKeys(data, {"started", "stopped", "failed", "blockedByDirectedMode",
				"deleted", "remaining", "connectionId", "rssiDbm", "txPhy", "rxPhy", "unavailableFields"})) return false;
			const cJSON* started = cJSON_GetObjectItemCaseSensitive(data, "started");
			const cJSON* stopped = cJSON_GetObjectItemCaseSensitive(data, "stopped");
			const cJSON* failed = cJSON_GetObjectItemCaseSensitive(data, "failed");
			const cJSON* blockedByDirectedMode = cJSON_GetObjectItemCaseSensitive(data, "blockedByDirectedMode");
			if (started && (!cJSON_IsNumber(started) || !cJSON_IsNumber(failed) ||
				!cJSON_IsBool(blockedByDirectedMode) || stopped)) return false;
			if (stopped && (!cJSON_IsNumber(stopped) || !cJSON_IsNumber(failed) || started || blockedByDirectedMode)) return false;
			if (!started && !stopped && (failed || blockedByDirectedMode)) return false;
			const cJSON* deleted = cJSON_GetObjectItemCaseSensitive(data, "deleted");
			const cJSON* remaining = cJSON_GetObjectItemCaseSensitive(data, "remaining");
			if ((deleted && !cJSON_IsNumber(deleted)) || (remaining && !cJSON_IsNumber(remaining)) || ((deleted != nullptr) != (remaining != nullptr))) return false;
			const cJSON* connectionId = cJSON_GetObjectItemCaseSensitive(data, "connectionId");
			const cJSON* rssi = cJSON_GetObjectItemCaseSensitive(data, "rssiDbm");
			const cJSON* txPhy = cJSON_GetObjectItemCaseSensitive(data, "txPhy");
			const cJSON* rxPhy = cJSON_GetObjectItemCaseSensitive(data, "rxPhy");
			const cJSON* unavailable = cJSON_GetObjectItemCaseSensitive(data, "unavailableFields");
			if (connectionId && (!cJSON_IsString(connectionId) || (rssi && !cJSON_IsNumber(rssi)) ||
				(txPhy && !cJSON_IsNumber(txPhy)) || (rxPhy && !cJSON_IsNumber(rxPhy)) ||
				((txPhy != nullptr) != (rxPhy != nullptr)) || (unavailable && !cJSON_IsArray(unavailable)) ||
				(!rssi && !txPhy && !unavailable))) return false;
			if (!connectionId && (rssi || txPhy || rxPhy || unavailable)) return false;
			if (!started && !stopped && !deleted && !connectionId) return false;
		}
	}
	return true;
}

cJSON* makeStatus(bool& snapshotBusy)
{
	snapshotBusy = false;
	std::vector<Advertising::InstanceConfig> advConfigs;
	std::vector<BluetoothManager::ConnectedPeerSnapshot> peers;
	if (!Advertising::TrySnapshot(advConfigs) || !BluetoothManager::Instance().TryGetConnectedPeerSnapshots(peers))
	{
		snapshotBusy = true;
		return nullptr;
	}
	cJSON* root = cJSON_CreateObject();
	if (!root) return nullptr;

	cJSON* device = cJSON_CreateObject();
	if (!device) { cJSON_Delete(root); return nullptr; }
	cJSON_AddStringToObject(device, "name", BluetoothManager::Instance().AdvertisingName().c_str());
	const std::string address = NimBLEDevice::getAddress().toString();
	cJSON_AddStringToObject(device, "address", address.c_str());
	cJSON_AddNumberToObject(device, "uptimeMs", millis());
	cJSON_AddItemToObject(root, "device", device);

	cJSON* network = cJSON_CreateObject();
	if (!network) { cJSON_Delete(root); return nullptr; }
	const bool connected = WiFi.status() == WL_CONNECTED;
	const String ip = connected ? WiFi.localIP().toString() : String();
	String hostname = ArduinoOTA.getHostname();
	cJSON_AddBoolToObject(network, "connected", connected);
	cJSON_AddStringToObject(network, "ip", ip.c_str());
	cJSON_AddStringToObject(network, "hostname", hostname.c_str());
	cJSON_AddItemToObject(root, "network", network);
	cJSON_AddNumberToObject(root, "preferredMtu", NimBLEDevice::getMTU());

	cJSON* advertising = cJSON_CreateArray();
	if (!advertising) { cJSON_Delete(root); return nullptr; }
	for (const auto& config : advConfigs)
	{
		cJSON* item = cJSON_CreateObject();
		if (!item) continue;
		cJSON_AddNumberToObject(item, "instanceId", config.id);
		cJSON_AddStringToObject(item, "name", config.name.c_str());
		cJSON_AddNumberToObject(item, "mode", static_cast<uint8_t>(config.mode));
		cJSON_AddNumberToObject(item, "discoverability", static_cast<uint8_t>(config.discoverable));
		cJSON_AddBoolToObject(item, "active", Advertising::IsActive(config.id));
		cJSON_AddBoolToObject(item, "restartAfterConnection", config.restartAfterConnection);
		cJSON_AddNumberToObject(item, "intervalMs", config.intervalMs);
		if (config.directedTarget)
		{
			cJSON* target = cJSON_CreateObject();
			if (target)
			{
				const std::string targetAddress = config.directedTarget->toString();
				cJSON_AddStringToObject(target, "address", targetAddress.c_str());
				cJSON_AddNumberToObject(target, "addressType", config.directedTarget->getType());
				cJSON_AddItemToObject(item, "directedTarget", target);
			}
		}
		else cJSON_AddNullToObject(item, "directedTarget");
		cJSON_AddItemToArray(advertising, item);
	}
	cJSON_AddItemToObject(root, "advertising", advertising);

	addStatusSecurity(root);
	cJSON* bonds = cJSON_CreateArray();
	if (!bonds) { cJSON_Delete(root); return nullptr; }
	const int bondCount = std::min(NimBLEDevice::getNumBonds(), 64);
	for (int i = 0; i < bondCount; ++i)
	{
		const NimBLEAddress bond = NimBLEDevice::getBondedAddress(i);
		cJSON* item = cJSON_CreateObject();
		if (!item) continue;
		const std::string bondAddress = bond.toString();
		cJSON_AddStringToObject(item, "address", bondAddress.c_str());
		cJSON_AddNumberToObject(item, "addressType", bond.getType());
		cJSON_AddItemToArray(bonds, item);
	}
	cJSON_AddItemToObject(root, "bonds", bonds);

	cJSON* connections = cJSON_CreateArray();
	if (!connections) { cJSON_Delete(root); return nullptr; }
	for (const auto& peer : peers)
	{
		cJSON* item = cJSON_CreateObject();
		if (!item) continue;
		const auto connectionId = WebValidation::formatId(peer.connectionId);
		cJSON_AddStringToObject(item, "connectionId", connectionId.c_str());
		cJSON_AddNumberToObject(item, "handle", peer.handle);
		cJSON_AddStringToObject(item, "address", peer.address.c_str());
		cJSON_AddStringToObject(item, "identityAddress", peer.identityAddress.c_str());
		cJSON_AddNumberToObject(item, "intervalMs", peer.intervalMs);
		cJSON_AddNumberToObject(item, "timeoutMs", peer.timeoutMs);
		cJSON_AddNumberToObject(item, "latency", peer.latency);
		cJSON_AddNumberToObject(item, "mtu", peer.mtu);
		cJSON_AddBoolToObject(item, "bonded", peer.bonded);
		cJSON_AddBoolToObject(item, "encrypted", peer.encrypted);
		cJSON_AddBoolToObject(item, "authenticated", peer.authenticated);
		cJSON_AddNumberToObject(item, "keySize", peer.keySize);
		cJSON_AddItemToArray(connections, item);
	}
	cJSON_AddItemToObject(root, "connections", connections);
	addStatusCapabilities(root);
	cJSON_AddStringToObject(root, "csrfToken", csrfToken.data());
	addOperationRecord(root);

	cJSON* resources = cJSON_CreateObject();
	if (resources)
	{
		cJSON_AddNumberToObject(resources, "heapFreeBytes", esp_get_free_heap_size());
		cJSON_AddNumberToObject(resources, "heapMinimumFreeBytes", esp_get_minimum_free_heap_size());
		cJSON_AddNumberToObject(resources, "webWorkerStackHighWaterBytes",
			operationWorker ? uxTaskGetStackHighWaterMark(operationWorker) : 0);
		cJSON_AddNumberToObject(resources, "pairingExecutorStackHighWaterBytes", PairingCoordinator::stackHighWaterBytes());
		cJSON_AddNumberToObject(resources, "httpServerStackHighWaterBytes", uxTaskGetStackHighWaterMark(nullptr));
		cJSON_AddItemToObject(root, "resources", resources);
	}
	if (!statusIsComplete(root, advConfigs, peers, bondCount))
	{
		cJSON_Delete(root);
		return nullptr;
	}
	return root;
}

esp_err_t getRoot(httpd_req_t* request)
{
	if (!guardRequest(request, false)) return ESP_OK;
	httpd_resp_set_type(request, "text/html; charset=utf-8");
	httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
	httpd_resp_set_hdr(request, "Cache-Control", "no-store");
	return httpd_resp_send(request, reinterpret_cast<const char*>(WebAssets::indexGzip), WebAssets::indexGzipSize);
}

esp_err_t getStatus(httpd_req_t* request)
{
	if (!guardRequest(request, false)) return ESP_OK;
	bool snapshotBusy = false;
	cJSON* status = makeStatus(snapshotBusy);
	if (!status)
	{
		if (snapshotBusy) sendFailure(request, 409, "Busy", "A short-lived BLE snapshot is busy; retry the status request.");
		else sendFailure(request, 500, "Failed", "Status response allocation failed.");
		return ESP_OK;
	}
	sendJson(request, 200, status);
	return ESP_OK;
}

esp_err_t getPairing(httpd_req_t* request)
{
	if (!guardRequest(request, false)) return ESP_OK;
	const auto snapshot = PairingCoordinator::snapshot();
	const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time()) / 1000;
	cJSON* root = cJSON_CreateObject();
	cJSON* requests = cJSON_CreateArray();
	if (!root || !requests)
	{
		cJSON_Delete(root);
		cJSON_Delete(requests);
		sendFailure(request, 500, "Failed", "Pairing response allocation failed.");
		return ESP_OK;
	}
	for (const auto& pairing : snapshot.requests)
	{
		if (pairing.resolution != Pairing::Resolution::Pending || pairing.deadlineMs <= nowMs) continue;
		cJSON* item = cJSON_CreateObject();
		if (!item) continue;
		const auto requestId = WebValidation::formatId(pairing.id);
		cJSON_AddStringToObject(item, "requestId", requestId.c_str());
		if (pairing.connection)
		{
			const auto connectionId = WebValidation::formatId(pairing.connection);
			cJSON_AddStringToObject(item, "connectionId", connectionId.c_str());
		}
		switch (pairing.type)
		{
			case Pairing::Type::PasskeyEntry: cJSON_AddStringToObject(item, "type", "PasskeyEntry"); break;
			case Pairing::Type::NumericConfirmation: cJSON_AddStringToObject(item, "type", "NumericConfirmation"); cJSON_AddNumberToObject(item, "value", pairing.value); break;
			case Pairing::Type::Display: cJSON_AddStringToObject(item, "type", "Display"); cJSON_AddNumberToObject(item, "value", pairing.value); break;
		}
		cJSON_AddNumberToObject(item, "expiresInMs", static_cast<double>(pairing.deadlineMs - nowMs));
		cJSON_AddItemToArray(requests, item);
	}
	cJSON_AddItemToObject(root, "requests", requests);
	sendJson(request, 200, root);
	return ESP_OK;
}

esp_err_t postAdvertising(httpd_req_t* request) { if (!guardRequest(request, true)) return ESP_OK; enqueueRoute(request, Route::Advertising); return ESP_OK; }
esp_err_t postSecurity(httpd_req_t* request) { if (!guardRequest(request, true)) return ESP_OK; enqueueRoute(request, Route::Security); return ESP_OK; }
esp_err_t postBonds(httpd_req_t* request) { if (!guardRequest(request, true)) return ESP_OK; enqueueRoute(request, Route::Bonds); return ESP_OK; }
esp_err_t postConnections(httpd_req_t* request) { if (!guardRequest(request, true)) return ESP_OK; enqueueRoute(request, Route::Connections); return ESP_OK; }
esp_err_t postMtu(httpd_req_t* request) { if (!guardRequest(request, true)) return ESP_OK; enqueueRoute(request, Route::Mtu); return ESP_OK; }
esp_err_t postPairing(httpd_req_t* request) { if (!guardRequest(request, true)) return ESP_OK; return handlePairingPost(request); }

bool registerRoutes(httpd_handle_t handle)
{
	struct Registration { const char* uri; httpd_method_t method; esp_err_t (*handler)(httpd_req_t*); };
	static constexpr Registration routes[] = {
		{ "/", HTTP_GET, getRoot },
		{ "/api/status", HTTP_GET, getStatus },
		{ "/api/advertising", HTTP_POST, postAdvertising },
		{ "/api/security", HTTP_POST, postSecurity },
		{ "/api/bonds", HTTP_POST, postBonds },
		{ "/api/connections", HTTP_POST, postConnections },
		{ "/api/mtu", HTTP_POST, postMtu },
		{ "/api/pairing", HTTP_GET, getPairing },
		{ "/api/pairing", HTTP_POST, postPairing }
	};
	for (const auto& route : routes)
	{
		httpd_uri_t registration{};
		registration.uri = route.uri;
		registration.method = route.method;
		registration.handler = route.handler;
		if (httpd_register_uri_handler(handle, &registration) != ESP_OK) return false;
	}
	return true;
}
}

bool WebInterface::begin()
{
	if (server) return true;
#if !defined(WEB_USERNAME) || !defined(WEB_PASSWORD)
	return false;
#else
	if (!WebValidation::validBasicCredentials(kWebUsername, kWebPassword)) return false;
#endif
	std::array<uint8_t, 32> tokenBytes{};
	esp_fill_random(tokenBytes.data(), tokenBytes.size());
	constexpr char hex[] = "0123456789abcdef";
	for (size_t i = 0; i < tokenBytes.size(); ++i)
	{
		csrfToken[i * 2] = hex[tokenBytes[i] >> 4];
		csrfToken[i * 2 + 1] = hex[tokenBytes[i] & 0x0F];
	}
	csrfToken.back() = '\0';
	operationBoot = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
	if (!operationBoot) operationBoot = 1;

	if (xTaskCreate(operationWorkerTask, "web-operation", kWorkerStackBytes, nullptr, 4, &operationWorker) != pdPASS)
	{
		operationWorker = nullptr;
		return false;
	}
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.max_uri_handlers = 9;
	config.max_open_sockets = 4;
	config.stack_size = 6144;
	config.max_req_hdr_len = 1024;
	config.max_uri_len = 512;
	config.recv_wait_timeout = 2;
	config.send_wait_timeout = 2;
	config.lru_purge_enable = true;
	if (httpd_start(&server, &config) != ESP_OK)
	{
		server = nullptr;
		vTaskDelete(operationWorker);
		operationWorker = nullptr;
		return false;
	}
	if (!registerRoutes(server))
	{
		httpd_stop(server);
		server = nullptr;
		vTaskDelete(operationWorker);
		operationWorker = nullptr;
		return false;
	}
	return true;
}
#endif
