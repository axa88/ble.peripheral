#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cJSON.h"

struct httpd_req_t {};
using esp_err_t = int;
inline constexpr esp_err_t ESP_OK = 0;

struct ConnectionId
{
	uint64_t boot = 1;
	uint32_t sequence = 1;
};

namespace NIMBLE_PROPERTY
{
	inline constexpr uint16_t NOTIFY = 0x0010;
	inline constexpr uint16_t INDICATE = 0x0020;
}

namespace FixtureGatt
{
	enum class Outcome { Ready, Failed, Busy, NotReady };
	struct ValueSnapshot
	{
		ConnectionId id{};
		const char* name = nullptr;
		const char* serviceUuid = nullptr;
		const char* characteristicUuid = nullptr;
		uint16_t properties = 0;
		size_t maxLength = 0;
		size_t publicationLimit = 0;
		size_t minimumPeerMtu = 0;
		size_t connectedPeerCount = 0;
		std::vector<uint8_t> current;
	};
	struct WebResult
	{
		Outcome outcome = Outcome::Failed;
		std::string detail = "GATT snapshot failed.";
		std::vector<ValueSnapshot> values;
	};
	WebResult GetValues();
}

namespace DynamicGatt
{
	enum class State { Ready, Applying, RecoveryRequired };
	inline constexpr size_t ServiceLimit = 4;
	struct ServiceSnapshot
	{
		ConnectionId id{};
		std::string serviceUuid;
		std::string characteristicUuid;
		bool protectedService = false;
	};
	struct Inventory
	{
		State state = State::Ready;
		bool available = true;
		std::vector<ServiceSnapshot> services;
		std::string reason;
	};
	Inventory GetInventory();
	bool Ready();
}

namespace Advertising
{
	struct InstanceConfig {};
#if CONFIG_BT_NIMBLE_EXT_ADV
	inline constexpr std::array<uint8_t, 6> supportedModes{};
#else
	inline constexpr std::array<uint8_t, 4> supportedModes{};
#endif
}

class BluetoothManager
{
public:
	struct ConnectedPeerSnapshot {};
};

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
	SetPreferredMtu,
	PublishValue,
	AddService,
	RemoveService,
	SendServiceChanged
};

enum class SlotState { Empty, Queued, Running, Done };
enum class Result { Applied, Accepted, Dispatched, Failed };

struct WorkerCommand
{
	WorkerOperation operation = WorkerOperation::PublishValue;
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
	bool hasGatt = false;
	bool storageVerified = false;
	bool publicationAttempted = false;
	bool publicationDispatched = false;
};

struct WorkerRecord
{
	ConnectionId id{};
	WorkerCommand command{};
	SlotState state = SlotState::Empty;
	std::optional<Result> result;
	char detail[256]{};
	char errorCode[48]{};
	bool errorHasInstanceId = false;
	uint8_t errorInstanceId = 0;
	OperationData data{};
};

namespace WebValidation
{
	std::string formatId(ConnectionId id);
}

namespace WebStatusTest
{
	struct ResponseCapture
	{
		int status = 0;
		std::string result;
		std::string detail;
		std::string json;
		bool sentJson = false;
	};

	enum class MakeStatusMode { Valid, Busy, Incomplete, AllocationFailure };
	enum class GattSnapshotMode { Ready, Failed, Busy, NotReady };

	extern MakeStatusMode makeStatusMode;
	extern GattSnapshotMode gattSnapshotMode;
	extern DynamicGatt::Inventory gattInventory;
	extern bool dynamicGattReady;
	extern const WorkerRecord* currentOperation;
	extern ResponseCapture response;
	void armAllocationFailure(size_t ordinal);
	void disarmAllocationFailure();
	bool allocationFailureTriggered();
	bool guardRequest(httpd_req_t* request, bool mutation);
	cJSON* makeStatus(bool& snapshotBusy, bool& incompleteResponse);
	void sendFailure(httpd_req_t* request, int status, const char* result, const char* detail);
	void sendJson(httpd_req_t* request, int status, cJSON* body);

	std::string_view operationName(const WorkerCommand& command);
	const char* stateName(SlotState state);
	const char* resultName(Result result);
	cJSON* operationDataJson(const OperationData& data);
	cJSON* operationRecordJson(const WorkerRecord& record, bool queuedResponse = false);
	bool hasExactKeys(const cJSON* object, std::initializer_list<std::string_view> allowed);
	bool hasExactObjectKeys(const cJSON* object, std::initializer_list<std::string_view> expected);
	bool statusIsComplete(const cJSON* root, const std::vector<Advertising::InstanceConfig>& advConfigs,
		const std::vector<BluetoothManager::ConnectedPeerSnapshot>& peers, int bondCount);
	esp_err_t getStatus(httpd_req_t* request);
	std::string hexValue(const std::vector<uint8_t>& value);
	esp_err_t getGatt(httpd_req_t* request);
}
