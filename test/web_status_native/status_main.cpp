#include "status_test_types.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
	struct AllocatorState
	{
		size_t calls = 0;
		size_t failAt = 0;
		bool failed = false;
	};

	AllocatorState allocator;
	std::vector<Advertising::InstanceConfig> advertisingConfigs;
	std::vector<BluetoothManager::ConnectedPeerSnapshot> peers;
	constexpr int bondCount = 0;
	constexpr std::array<const char*, 3> gattTargetIds{{
		"123456789ABCDEF0-00000001", "123456789ABCDEF0-00000002", "123456789ABCDEF0-00000003"}};

	void* trackedMalloc(size_t size)
	{
		++allocator.calls;
		if (allocator.failAt && allocator.calls == allocator.failAt)
		{
			allocator.failed = true;
			return nullptr;
		}
		return std::malloc(size);
	}

	void trackedFree(void* pointer)
	{
		std::free(pointer);
	}

	bool require(bool condition, const char* message)
	{
		if (!condition)
			std::fprintf(stderr, "FAIL: %s\n", message);
		return condition;
	}

	bool addItem(cJSON* parent, const char* name, cJSON* item)
	{
		if (!parent || !item)
		{
			cJSON_Delete(item);
			return false;
		}
		if (!cJSON_AddItemToObject(parent, name, item))
		{
			cJSON_Delete(item);
			return false;
		}
		return true;
	}

	bool addString(cJSON* parent, const char* key, const char* value)
	{
		return parent && cJSON_AddStringToObject(parent, key, value) != nullptr;
	}

	bool addNumber(cJSON* parent, const char* key, double value)
	{
		return parent && cJSON_AddNumberToObject(parent, key, value) != nullptr;
	}

	bool addBool(cJSON* parent, const char* key, cJSON_bool value)
	{
		return parent && cJSON_AddBoolToObject(parent, key, value) != nullptr;
	}

	cJSON* makeObject(cJSON* parent, const char* key)
	{
		cJSON* child = cJSON_CreateObject();
		if (!addItem(parent, key, child)) return nullptr;
		return child;
	}

	cJSON* makeArray(cJSON* parent, const char* key)
	{
		cJSON* child = cJSON_CreateArray();
		if (!addItem(parent, key, child)) return nullptr;
		return child;
	}

	WorkerRecord makeGattRecord(WorkerOperation operation, Result result, SlotState state,
		bool storageVerified, bool publicationAttempted, bool publicationDispatched, bool includeData = true)
	{
		WorkerRecord record;
		record.id = {0x123456789ABCDEF0ULL, 7};
		record.command.operation = operation;
		std::strncpy(record.command.target, "gatt:temperature", sizeof(record.command.target) - 1);
		record.state = state;
		if (state == SlotState::Done) record.result = result;
		std::strncpy(record.detail, "GATT value operation completed.", sizeof(record.detail) - 1);
		record.data.hasGatt = includeData;
		record.data.storageVerified = storageVerified;
		record.data.publicationAttempted = publicationAttempted;
		record.data.publicationDispatched = publicationDispatched;
		return record;
	}

#if !EXPECT_KNOWN_BAD
	WorkerRecord makeLegacyRecord(WorkerOperation operation)
	{
		WorkerRecord record;
		record.id = {0x123456789ABCDEF0ULL, 8};
		record.command.operation = operation;
		std::strncpy(record.command.target, "legacy-target", sizeof(record.command.target) - 1);
		record.state = SlotState::Done;
		record.result = Result::Applied;
		std::strncpy(record.detail, "Legacy operation completed.", sizeof(record.detail) - 1);
		switch (operation)
		{
			case WorkerOperation::StartAllAdvertising:
				record.data.hasStartCounts = true;
				record.data.started = 1;
				record.data.startFailed = 0;
				break;
			case WorkerOperation::StopAllAdvertising:
				record.data.hasStopCounts = true;
				record.data.stopped = 1;
				record.data.stopFailed = 0;
				break;
			case WorkerOperation::DeleteAllBonds:
				record.data.hasBondCounts = true;
				record.data.deleted = 1;
				record.data.remaining = 0;
				break;
			case WorkerOperation::ReadPeerLink:
				record.data.hasLink = true;
				record.data.linkConnection = {0xABCDEF0123456789ULL, 9};
				record.data.hasRssi = true;
				record.data.rssiDbm = -42;
				record.data.hasPhy = true;
				record.data.txPhy = 1;
				record.data.rxPhy = 2;
				break;
			default:
				break;
		}
		return record;
	}
#endif

	cJSON* makeStatusEnvelope(const WorkerRecord* record, bool omitGattDispatch = false,
		bool wrongGattType = false, bool mixGattData = false, bool omitOperationData = false,
		bool omitFirmwareRevision = false, bool wrongFirmwareSourceType = false,
		bool wrongFirmwareRevisionType = false, bool malformedFirmwareSource = false,
		bool wrongFirmwareEnvironmentType = false)
	{
		cJSON* root = cJSON_CreateObject();
		if (!root) return nullptr;
		cJSON* device = nullptr;
		cJSON* network = nullptr;
		cJSON* security = nullptr;
		cJSON* capabilities = nullptr;
		cJSON* modes = nullptr;
		cJSON* resources = nullptr;

		device = makeObject(root, "device");
		if (!device || !addString(device, "name", "Test Device") ||
			!addString(device, "address", "AA:BB:CC:DD:EE:FF") || !addNumber(device, "uptimeMs", 1)) goto fail;
		if (wrongFirmwareRevisionType)
		{
			if (!addString(device, "firmwareRevision", "12")) goto fail;
		}
		else if (!omitFirmwareRevision && !addNumber(device, "firmwareRevision", 12)) goto fail;
		if (wrongFirmwareSourceType)
		{
			if (!addNumber(device, "firmwareSource", 1)) goto fail;
		}
		else if (!addString(device, "firmwareSource", malformedFirmwareSource ?
			"g123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" :
			"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")) goto fail;
		if (wrongFirmwareEnvironmentType)
		{
			if (!addNumber(device, "firmwareEnvironment", 1)) goto fail;
		}
		else if (!addString(device, "firmwareEnvironment", "test")) goto fail;

		network = makeObject(root, "network");
		if (!network || !addBool(network, "connected", false) || !addString(network, "ip", "") ||
			!addString(network, "hostname", "test-device")) goto fail;
		if (!addNumber(root, "preferredMtu", 23)) goto fail;

		if (!makeArray(root, "advertising")) goto fail;
		security = makeObject(root, "security");
		if (!security || !addNumber(security, "capabilities", 0) || !addBool(security, "bond", false) ||
			!addBool(security, "mitm", false) || !addBool(security, "secureConnections", true) ||
			!addBool(security, "ltk", true) || !addBool(security, "irk", true) ||
			!addBool(security, "csrk", true)) goto fail;

		if (!makeArray(root, "bonds") || !makeArray(root, "connections")) goto fail;
		capabilities = makeObject(root, "capabilities");
		if (!capabilities || !addBool(capabilities, "extendedAdvertising", false) ||
			!addBool(capabilities, "phyUpdate", false) || !addNumber(capabilities, "maxAdvertisingInstances", 1)) goto fail;
		modes = makeArray(capabilities, "supportedModes");
		if (!modes) goto fail;
		for (size_t index = 0; index < Advertising::supportedModes.size(); ++index)
		{
			cJSON* mode = cJSON_CreateObject();
			if (!mode || !addNumber(mode, "value", static_cast<double>(index)) ||
				!addString(mode, "label", "test mode") || !cJSON_AddItemToArray(modes, mode))
			{
				cJSON_Delete(mode);
				goto fail;
			}
		}
		if (!addString(root, "csrfToken", "0000000000000000000000000000000000000000000000000000000000000000")) goto fail;

		if (!record)
		{
			if (!addItem(root, "lastOperation", cJSON_CreateNull())) goto fail;
		}
		else
		{
			cJSON* lastOperation = WebStatusTest::operationRecordJson(*record);
			if (!addItem(root, "lastOperation", lastOperation)) goto fail;
			cJSON* data = cJSON_GetObjectItemCaseSensitive(lastOperation, "data");
			if (omitOperationData)
				cJSON_DeleteItemFromObjectCaseSensitive(lastOperation, "data");
			else if (data)
			{
				if (omitGattDispatch)
					cJSON_DeleteItemFromObjectCaseSensitive(data, "publicationDispatched");
				if (wrongGattType)
				{
					cJSON_DeleteItemFromObjectCaseSensitive(data, "publicationAttempted");
					if (!cJSON_AddStringToObject(data, "publicationAttempted", "true")) goto fail;
				}
				if (mixGattData && !addNumber(data, "started", 1)) goto fail;
			}
		}

		resources = makeObject(root, "resources");
		if (!resources || !addNumber(resources, "heapFreeBytes", 100000) ||
			!addNumber(resources, "heapMinimumFreeBytes", 90000) ||
			!addNumber(resources, "webWorkerStackHighWaterBytes", 1000) ||
			!addNumber(resources, "pairingExecutorStackHighWaterBytes", 1000) ||
			!addNumber(resources, "httpServerStackHighWaterBytes", 1000)) goto fail;
		return root;

	fail:
		cJSON_Delete(root);
		return nullptr;
	}

	bool statusValid(const WorkerRecord* record, bool omitGattDispatch = false,
		bool wrongGattType = false, bool mixGattData = false, bool omitOperationData = false,
		bool omitFirmwareRevision = false, bool wrongFirmwareSourceType = false,
		bool wrongFirmwareRevisionType = false, bool malformedFirmwareSource = false,
		bool wrongFirmwareEnvironmentType = false)
	{
		cJSON* root = makeStatusEnvelope(record, omitGattDispatch, wrongGattType, mixGattData, omitOperationData,
			omitFirmwareRevision, wrongFirmwareSourceType, wrongFirmwareRevisionType, malformedFirmwareSource,
			wrongFirmwareEnvironmentType);
		if (!root) return false;
		const bool valid = WebStatusTest::statusIsComplete(root, advertisingConfigs, peers, bondCount);
		cJSON_Delete(root);
		return valid;
	}

#if !EXPECT_KNOWN_BAD
	bool inspectGattFlags(const char* json, bool expectedStorage, bool expectedAttempted, bool expectedDispatched)
	{
		cJSON* root = cJSON_Parse(json);
		if (!root) return false;
		const cJSON* operation = cJSON_GetObjectItemCaseSensitive(root, "lastOperation");
		const cJSON* data = cJSON_GetObjectItemCaseSensitive(operation, "data");
		const cJSON* storage = cJSON_GetObjectItemCaseSensitive(data, "storageVerified");
		const cJSON* attempted = cJSON_GetObjectItemCaseSensitive(data, "publicationAttempted");
		const cJSON* dispatched = cJSON_GetObjectItemCaseSensitive(data, "publicationDispatched");
		const bool matches = cJSON_IsBool(storage) && cJSON_IsBool(attempted) && cJSON_IsBool(dispatched) &&
			cJSON_IsTrue(storage) == expectedStorage && cJSON_IsTrue(attempted) == expectedAttempted &&
			cJSON_IsTrue(dispatched) == expectedDispatched;
		cJSON_Delete(root);
		return matches;
	}
#endif

#if !EXPECT_KNOWN_BAD
	bool verifyGattSnapshot(const std::string& text)
	{
		cJSON* root = cJSON_Parse(text.c_str());
		if (!root) return false;
		const cJSON* values = cJSON_GetObjectItemCaseSensitive(root, "values");
		bool matches = cJSON_IsArray(values) && cJSON_GetArraySize(values) == 3;
		constexpr std::array<const char*, 3> names{{"Original notification", "Original indication", "Temperature Scalar"}};
		constexpr std::array<const char*, 3> serviceUuids{{
			"12345678-1234-5678-1234-56789abcdef0", "12345678-1234-5678-1234-56789abcdef0",
			"12345678-1234-5678-1234-56789abcdf00"}};
		constexpr std::array<const char*, 3> characteristicUuids{{
			"12345678-1234-5678-1234-56789abcdef2", "12345678-1234-5678-1234-56789abcdef3",
			"12345678-1234-5678-1234-56789abcdf05"}};
		constexpr std::array<const char*, 3> hexValues{{"0102", "0304", "EB00"}};
		for (int i = 0; matches && i < 3; ++i)
		{
			const cJSON* item = cJSON_GetArrayItem(values, i);
			const cJSON* targetId = cJSON_GetObjectItemCaseSensitive(item, "targetId");
			const cJSON* name = cJSON_GetObjectItemCaseSensitive(item, "name");
			const cJSON* serviceUuid = cJSON_GetObjectItemCaseSensitive(item, "serviceUuid");
			const cJSON* characteristicUuid = cJSON_GetObjectItemCaseSensitive(item, "characteristicUuid");
			const cJSON* notify = cJSON_GetObjectItemCaseSensitive(item, "notify");
			const cJSON* indicate = cJSON_GetObjectItemCaseSensitive(item, "indicate");
			const cJSON* maxLength = cJSON_GetObjectItemCaseSensitive(item, "maxLength");
			const cJSON* publicationLimit = cJSON_GetObjectItemCaseSensitive(item, "publicationLimit");
			const cJSON* minimumPeerMtu = cJSON_GetObjectItemCaseSensitive(item, "minimumPeerMtu");
			const cJSON* connectedPeerCount = cJSON_GetObjectItemCaseSensitive(item, "connectedPeerCount");
			const cJSON* currentHex = cJSON_GetObjectItemCaseSensitive(item, "currentHex");
			size_t fieldCount = 0;
			for (const cJSON* field = item ? item->child : nullptr; field; field = field->next) ++fieldCount;
			matches = item && fieldCount == 11 && cJSON_IsString(targetId) &&
				std::strcmp(targetId->valuestring, gattTargetIds[i]) == 0 && cJSON_IsString(name) &&
				std::strcmp(name->valuestring, names[i]) == 0 && cJSON_IsString(serviceUuid) &&
				std::strcmp(serviceUuid->valuestring, serviceUuids[i]) == 0 && cJSON_IsString(characteristicUuid) &&
				std::strcmp(characteristicUuid->valuestring, characteristicUuids[i]) == 0 &&
				cJSON_IsBool(notify) && cJSON_IsBool(indicate) &&
				cJSON_IsTrue(notify) == (i != 1) && cJSON_IsTrue(indicate) == (i == 1) &&
				cJSON_IsNumber(maxLength) && maxLength->valuedouble == 512 &&
				cJSON_IsNumber(publicationLimit) && publicationLimit->valuedouble == 20 &&
				cJSON_IsNumber(minimumPeerMtu) && minimumPeerMtu->valuedouble == 23 &&
				cJSON_IsNumber(connectedPeerCount) && connectedPeerCount->valuedouble == 2 &&
				cJSON_IsString(currentHex) && std::strcmp(currentHex->valuestring, hexValues[i]) == 0;
		}
		cJSON_Delete(root);
		return matches;
	}
#endif

#if EXPECT_KNOWN_BAD
	bool firstGattTargetIdIsMissing(const std::string& text)
	{
		cJSON* root = cJSON_Parse(text.c_str());
		if (!root) return false;
		const cJSON* values = cJSON_GetObjectItemCaseSensitive(root, "values");
		const cJSON* first = cJSON_GetArrayItem(values, 0);
		const bool missing = cJSON_IsObject(first) && !cJSON_GetObjectItemCaseSensitive(first, "targetId");
		cJSON_Delete(root);
		return missing;
	}
#endif
}

namespace WebValidation
{
	std::string formatId(ConnectionId id)
	{
		char buffer[40]{};
		std::snprintf(buffer, sizeof(buffer), "%016llX-%08X",
			static_cast<unsigned long long>(id.boot), static_cast<unsigned>(id.sequence));
		return buffer;
	}
}

namespace WebStatusTest
{
	MakeStatusMode makeStatusMode = MakeStatusMode::Valid;
	GattSnapshotMode gattSnapshotMode = GattSnapshotMode::Ready;
	const WorkerRecord* currentOperation = nullptr;
	ResponseCapture response;

	void armAllocationFailure(size_t ordinal)
	{
		allocator.calls = 0;
		allocator.failAt = ordinal;
		allocator.failed = false;
	}

	void disarmAllocationFailure() { allocator.failAt = 0; }
	bool allocationFailureTriggered() { return allocator.failed; }
	bool guardRequest(httpd_req_t*, bool) { return true; }

	cJSON* makeStatus(bool& snapshotBusy, bool& incompleteResponse)
	{
		snapshotBusy = false;
		incompleteResponse = false;
		if (makeStatusMode == MakeStatusMode::Busy)
		{
			snapshotBusy = true;
			return nullptr;
		}
		if (makeStatusMode == MakeStatusMode::AllocationFailure)
		{
			armAllocationFailure(1);
			cJSON* failedAllocation = cJSON_CreateObject();
			const bool allocationFailed = !failedAllocation && allocationFailureTriggered();
			disarmAllocationFailure();
			if (allocationFailed) return nullptr;
			return failedAllocation;
		}

		const bool malformed = makeStatusMode == MakeStatusMode::Incomplete;
		cJSON* status = makeStatusEnvelope(currentOperation, malformed, false, false, false);
		if (!status)
			return nullptr;
		if (!statusIsComplete(status, advertisingConfigs, peers, bondCount))
		{
			incompleteResponse = true;
			cJSON_Delete(status);
			return nullptr;
		}
		return status;
	}

	void sendFailure(httpd_req_t*, int status, const char* result, const char* detail)
	{
		response = ResponseCapture{};
		response.status = status;
		response.result = result ? result : "";
		response.detail = detail ? detail : "";
	}

	void sendJson(httpd_req_t*, int status, cJSON* body)
	{
		response = ResponseCapture{};
		if (!body)
		{
			response.status = 500;
			response.detail = "Response allocation failed.";
			return;
		}
		char* payload = body ? cJSON_PrintUnformatted(body) : nullptr;
		if (!payload)
		{
			response.status = 500;
			response.detail = "Response serialization failed.";
		}
		else
		{
			response.status = status;
			response.json = payload;
			response.sentJson = true;
			cJSON_free(payload);
		}
		cJSON_Delete(body);
	}
}

FixtureGatt::WebResult FixtureGatt::GetValues()
{
	switch (WebStatusTest::gattSnapshotMode)
	{
		case WebStatusTest::GattSnapshotMode::Failed: return {Outcome::Failed, "GATT completion allocation failed.", {}};
		case WebStatusTest::GattSnapshotMode::Busy: return {Outcome::Busy, "GATT host work is pending; retry after refreshing.", {}};
		case WebStatusTest::GattSnapshotMode::NotReady: return {Outcome::NotReady, "GATT fixture is not ready.", {}};
		case WebStatusTest::GattSnapshotMode::Ready: break;
	}
	return {Outcome::Ready, "Current stored values observed.", {
		{{0x123456789ABCDEF0ULL, 1}, "Original notification", "12345678-1234-5678-1234-56789abcdef0",
			"12345678-1234-5678-1234-56789abcdef2", NIMBLE_PROPERTY::NOTIFY, 512, 20, 23, 2, {0x01, 0x02}},
		{{0x123456789ABCDEF0ULL, 2}, "Original indication", "12345678-1234-5678-1234-56789abcdef0",
			"12345678-1234-5678-1234-56789abcdef3", NIMBLE_PROPERTY::INDICATE, 512, 20, 23, 2, {0x03, 0x04}},
		{{0x123456789ABCDEF0ULL, 3}, "Temperature Scalar", "12345678-1234-5678-1234-56789abcdf00",
			"12345678-1234-5678-1234-56789abcdf05", NIMBLE_PROPERTY::NOTIFY, 512, 20, 23, 2, {0xEB, 0x00}}
	}};
}

int main()
{
	cJSON_Hooks hooks{trackedMalloc, trackedFree};
	cJSON_InitHooks(&hooks);
#if EXPECT_KNOWN_BAD
	const WorkerRecord success = makeGattRecord(WorkerOperation::PublishValue, Result::Dispatched,
		SlotState::Done, true, true, true);
	if (!require(statusValid(nullptr), "known-bad baseline status remains valid")) return 1;
	if (!require(!statusValid(&success), "known-bad validator rejects completed GATT status")) return 1;
	std::puts("PASS: known-bad validator rejects GATT status while baseline remains valid");
	httpd_req_t request;
	WebStatusTest::gattSnapshotMode = WebStatusTest::GattSnapshotMode::Ready;
	WebStatusTest::armAllocationFailure(0);
	if (!require(WebStatusTest::getGatt(&request) == ESP_OK && WebStatusTest::response.status == 200 &&
		WebStatusTest::response.sentJson, "known-bad GATT snapshot succeeds without injected allocation failure")) return 1;
	const size_t allocationCount = allocator.calls;
	WebStatusTest::disarmAllocationFailure();
	bool reproducedIncompleteSuccess = false;
	for (size_t ordinal = 1; ordinal <= allocationCount && !reproducedIncompleteSuccess; ++ordinal)
	{
		WebStatusTest::armAllocationFailure(ordinal);
		WebStatusTest::getGatt(&request);
		const bool injected = WebStatusTest::allocationFailureTriggered();
		const bool incomplete200 = WebStatusTest::response.status == 200 && WebStatusTest::response.sentJson &&
			firstGattTargetIdIsMissing(WebStatusTest::response.json);
		WebStatusTest::disarmAllocationFailure();
		reproducedIncompleteSuccess = injected && incomplete200;
	}
	if (!require(reproducedIncompleteSuccess,
		"known-bad unchecked targetId allocation can return an incomplete 200 GATT snapshot")) return 1;
	std::puts("PASS: known-bad unchecked cJSON field allocation returns 200 with an incomplete GATT snapshot");
	return 0;
#else
	const WorkerRecord publish = makeGattRecord(WorkerOperation::PublishValue, Result::Dispatched,
		SlotState::Done, true, true, true);
	const WorkerRecord publishFailure = makeGattRecord(WorkerOperation::PublishValue, Result::Failed,
		SlotState::Done, true, true, false);
	const WorkerRecord preStorageFailure = makeGattRecord(WorkerOperation::PublishValue, Result::Failed,
		SlotState::Done, false, false, false);
	const WorkerRecord missingData = makeGattRecord(WorkerOperation::PublishValue, Result::Dispatched,
		SlotState::Done, false, false, false, false);
	const WorkerRecord running = makeGattRecord(WorkerOperation::PublishValue, Result::Failed,
		SlotState::Running, false, false, false, false);
	const std::array<WorkerRecord, 4> legacyRecords{{
		makeLegacyRecord(WorkerOperation::StartAllAdvertising),
		makeLegacyRecord(WorkerOperation::StopAllAdvertising),
		makeLegacyRecord(WorkerOperation::DeleteAllBonds),
		makeLegacyRecord(WorkerOperation::ReadPeerLink)
	}};

	if (!require(statusValid(nullptr), "baseline status without last operation passes")) return 1;
	if (!require(!statusValid(nullptr, false, false, false, false, true), "missing firmware revision is rejected")) return 1;
	if (!require(!statusValid(nullptr, false, false, false, false, false, true), "wrong-typed firmware source is rejected")) return 1;
	if (!require(!statusValid(nullptr, false, false, false, false, false, false, true), "wrong-typed firmware revision is rejected")) return 1;
	if (!require(!statusValid(nullptr, false, false, false, false, false, false, false, true), "malformed firmware source is rejected")) return 1;
	if (!require(!statusValid(nullptr, false, false, false, false, false, false, false, false, true), "wrong-typed firmware environment is rejected")) return 1;
	if (!require(statusValid(&publish), "successful publish record with all three booleans passes")) return 1;
	if (!require(statusValid(&publishFailure), "failed publish record with all three booleans passes")) return 1;
	if (!require(statusValid(&preStorageFailure), "pre-storage failure record with all three booleans passes")) return 1;
	if (!require(!statusValid(&missingData), "completed GATT operation without data is rejected")) return 1;
	if (!require(statusValid(&running), "running GATT operation without result data remains valid")) return 1;
	for (const auto& legacy : legacyRecords)
		if (!require(statusValid(&legacy), "existing start/stop/bond/link operation data remains valid")) return 1;
	if (!require(!statusValid(&publish, true), "missing GATT boolean is rejected")) return 1;
	if (!require(!statusValid(&publish, false, true), "wrong-typed GATT boolean is rejected")) return 1;
	if (!require(!statusValid(&publish, false, false, true), "mixed GATT and advertising data is rejected")) return 1;
	std::puts("PASS: extracted operation serializers and status validator accept valid GATT shapes and reject malformed shapes");

	const auto data = publish.data;
	WebStatusTest::armAllocationFailure(1);
	cJSON* failedData = WebStatusTest::operationDataJson(data);
	const bool dataAllocationFailed = !failedData && WebStatusTest::allocationFailureTriggered();
	WebStatusTest::disarmAllocationFailure();
	cJSON_Delete(failedData);
	if (!require(dataAllocationFailed, "real cJSON hook forces operation-data allocation failure")) return 1;

	cJSON* printable = WebStatusTest::operationRecordJson(publish);
	if (!require(printable != nullptr, "record serializer allocates with normal cJSON allocator")) return 1;
	WebStatusTest::armAllocationFailure(1);
	char* failedPrint = cJSON_PrintUnformatted(printable);
	const bool printAllocationFailed = !failedPrint && WebStatusTest::allocationFailureTriggered();
	WebStatusTest::disarmAllocationFailure();
	cJSON_free(failedPrint);
	cJSON_Delete(printable);
	if (!require(printAllocationFailed, "real cJSON hook forces compact serialization allocation failure")) return 1;
	std::puts("PASS: real cJSON allocator hooks inject object and print allocation failures");

	httpd_req_t request;
	WebStatusTest::gattSnapshotMode = WebStatusTest::GattSnapshotMode::Ready;
	WebStatusTest::armAllocationFailure(0);
	const bool gattSnapshotSent = WebStatusTest::getGatt(&request) == ESP_OK && WebStatusTest::response.status == 200 &&
		WebStatusTest::response.sentJson;
	const size_t gattAllocationCount = allocator.calls;
	WebStatusTest::disarmAllocationFailure();
	if (!require(gattSnapshotSent && verifyGattSnapshot(WebStatusTest::response.json),
		"extracted getGatt serializes all three exact target records and eleven fields")) return 1;
	for (size_t ordinal = 1; ordinal <= gattAllocationCount; ++ordinal)
	{
		WebStatusTest::armAllocationFailure(ordinal);
		WebStatusTest::getGatt(&request);
		const bool injected = WebStatusTest::allocationFailureTriggered();
		WebStatusTest::disarmAllocationFailure();
		if (!require(injected, "each observed cJSON allocation can be failed in isolation")) return 1;
		if (!require(WebStatusTest::response.status == 500 && !WebStatusTest::response.sentJson,
			"getGatt never returns 200 after any injected cJSON allocation failure")) return 1;
	}
	WebStatusTest::gattSnapshotMode = WebStatusTest::GattSnapshotMode::Failed;
	if (!require(WebStatusTest::getGatt(&request) == ESP_OK && WebStatusTest::response.status == 500 &&
		WebStatusTest::response.result == "Failed", "getGatt maps an explicit snapshot failure to HTTP 500")) return 1;
	WebStatusTest::gattSnapshotMode = WebStatusTest::GattSnapshotMode::Busy;
	if (!require(WebStatusTest::getGatt(&request) == ESP_OK && WebStatusTest::response.status == 409 &&
		WebStatusTest::response.result == "Busy", "getGatt preserves retryable host contention as HTTP 409")) return 1;
	WebStatusTest::gattSnapshotMode = WebStatusTest::GattSnapshotMode::NotReady;
	if (!require(WebStatusTest::getGatt(&request) == ESP_OK && WebStatusTest::response.status == 409 &&
		WebStatusTest::response.result == "Busy", "getGatt preserves not-ready snapshots as HTTP 409")) return 1;
	std::printf("PASS: extracted getGatt validates all fields and rejects failures across %zu cJSON allocations\n", gattAllocationCount);
	WebStatusTest::gattSnapshotMode = WebStatusTest::GattSnapshotMode::Ready;

	WebStatusTest::currentOperation = &publish;
	WebStatusTest::makeStatusMode = WebStatusTest::MakeStatusMode::Valid;
	std::string firstResponse;
	for (int attempt = 0; attempt < 3; ++attempt)
	{
		if (!require(WebStatusTest::getStatus(&request) == ESP_OK && WebStatusTest::response.status == 200 &&
			WebStatusTest::response.sentJson && inspectGattFlags(WebStatusTest::response.json.c_str(), true, true, true),
			"extracted getStatus maps valid publish to repeated 200 JSON responses")) return 1;
		if (attempt == 0) firstResponse = WebStatusTest::response.json;
		else if (!require(WebStatusTest::response.json == firstResponse, "repeated status output is deterministic")) return 1;
	}
	WebStatusTest::currentOperation = &publish;
	WebStatusTest::makeStatusMode = WebStatusTest::MakeStatusMode::Busy;
	if (!require(WebStatusTest::getStatus(&request) == ESP_OK && WebStatusTest::response.status == 409 &&
		WebStatusTest::response.result == "Busy", "getStatus maps busy snapshots to 409")) return 1;
	WebStatusTest::makeStatusMode = WebStatusTest::MakeStatusMode::Incomplete;
	if (!require(WebStatusTest::getStatus(&request) == ESP_OK && WebStatusTest::response.status == 500 &&
		WebStatusTest::response.detail == "Status response is incomplete or inconsistent.",
		"getStatus distinguishes schema-incomplete responses")) return 1;
	WebStatusTest::makeStatusMode = WebStatusTest::MakeStatusMode::AllocationFailure;
	if (!require(WebStatusTest::getStatus(&request) == ESP_OK && WebStatusTest::response.status == 500 &&
		WebStatusTest::response.detail == "Status response allocation failed.",
		"getStatus maps a genuine cJSON allocation failure separately")) return 1;
	std::puts("PASS: extracted getStatus maps valid, busy, incomplete, and allocation-failure cases");
	return 0;
#endif
}
