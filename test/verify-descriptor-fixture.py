"""Read-only static safety checks for the descriptor-driven GATT fixture.

These checks inspect source structure only. They do not establish runtime,
assigned-handle, subscription, delivery, or hardware behavior.
"""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
DIRECT_ADVERTISING_START = re.compile(
    r"\b(?:adv|advertising\w*|extadv\w*)\s*(?:\(\s*\))?\s*->\s*start\s*\(|"
    r"\bNimBLEDevice\s*::\s*(?:startAdvertising\s*\(|getAdvertising\s*\([^)]*\)\s*->\s*start\s*\()",
    re.IGNORECASE,
)


def read_source(relative_path):
    return (ROOT / relative_path).read_text(encoding="utf-8")


def function_body(source, signature):
    """Return a C++ function body, skipping an earlier forward declaration."""
    for match in re.finditer(re.escape(signature), source):
        opening = source.find("{", match.end())
        semicolon = source.find(";", match.end())
        if opening < 0 or (semicolon >= 0 and semicolon < opening):
            continue

        depth = 0
        state = "code"
        escaped = False
        index = opening
        while index < len(source):
            char = source[index]
            following = source[index + 1] if index + 1 < len(source) else ""
            if state == "line_comment":
                if char == "\n":
                    state = "code"
            elif state == "block_comment":
                if char == "*" and following == "/":
                    state = "code"
                    index += 1
            elif state in ("string", "character"):
                if escaped:
                    escaped = False
                elif char == "\\":
                    escaped = True
                elif (state == "string" and char == '"') or (state == "character" and char == "'"):
                    state = "code"
            elif char == "/" and following == "/":
                state = "line_comment"
                index += 1
            elif char == "/" and following == "*":
                state = "block_comment"
                index += 1
            elif char == '"':
                state = "string"
            elif char == "'":
                state = "character"
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    return source[opening + 1:index]
            index += 1
        raise AssertionError(f"unterminated function body: {signature}")
    raise AssertionError(f"function definition not found: {signature}")


def require(condition, description):
    if not condition:
        raise AssertionError(description)
    print(f"PASS {description}")


def main():
    bluetooth = read_source("src/bluetoothManager.cpp")
    advertising = read_source("src/advertising.cpp")
    fixture = read_source("src/fixtureGatt.cpp")
    fixture_header = read_source("src/fixtureGatt.h")
    web = read_source("src/webInterface.cpp")
    value_header = read_source("src/gattValue.h")

    setup = function_body(bluetooth, "void BluetoothManager::SetupBluetooth()")
    startup_gates = [
        re.search(r"if\s*\(!FixtureGatt::Create\(\*server_\)\)\s*\{[^{}]*GATT fixture creation failed[^{}]*return;", setup, re.DOTALL),
        re.search(r"if\s*\(!server_->start\(\)\)\s*\{[^{}]*GATT registration failed[^{}]*return;", setup, re.DOTALL),
        re.search(r"if\s*\(!FixtureGatt::Finalize\(\)\)\s*\{[^{}]*GATT descriptor finalization failed[^{}]*return;", setup, re.DOTALL),
    ]
    require(
        all(startup_gates) and
        [gate.start() for gate in startup_gates if gate] == sorted(gate.start() for gate in startup_gates if gate),
        "stage-specific startup failures return in Create -> server start -> Finalize order",
    )
    require(
        setup.find("FixtureGatt::Finalize()") < setup.find("Advertising::Start(0)"),
        "initial advertising starts only after successful fixture finalization",
    )

    finalize = function_body(fixture, "bool Finalize()")
    schema_creation = re.search(r"\b(?:createService|createCharacteristic|createDescriptor|create2904|setCallbacks)\s*\(", finalize)
    require(schema_creation is None, "finalization only updates and verifies registered descriptor values")
    aggregate_write = finalize.find("aggregate->setValue(handles, sizeof(handles))")
    aggregate_check = finalize.find("!equals(aggregate, handles, sizeof(handles))")
    ready_publish = finalize.find("ready.store(true)")
    require(
        0 <= aggregate_write < aggregate_check < ready_publish,
        "readiness publishes only after Aggregate handle bytes pass readback",
    )
    require("ready{false}" in fixture, "fixture readiness defaults to false")

    ready_gated_functions = {
        "bool rebuildLegacyAdvertisement(const Advertising::InstanceConfig& cfg, bool restartIfActive)":
            function_body(advertising, "bool rebuildLegacyAdvertisement(const Advertising::InstanceConfig& cfg, bool restartIfActive)"),
        "bool startLegacyDirectedLocked(const Advertising::InstanceConfig& cfg, const NimBLEAddress& target)":
            function_body(advertising, "bool startLegacyDirectedLocked(const Advertising::InstanceConfig& cfg, const NimBLEAddress& target)"),
        "bool applyExtendedDataLocked(const Advertising::InstanceConfig& cfg, bool restartIfActive)":
            function_body(advertising, "bool applyExtendedDataLocked(const Advertising::InstanceConfig& cfg, bool restartIfActive)"),
    }
    for name, body in ready_gated_functions.items():
        calls = list(DIRECT_ADVERTISING_START.finditer(body))
        require(
            all(re.search(r"FixtureGatt::Ready\(\)\s*&&\s*$", body[max(0, call.start() - 80):call.start()]) for call in calls),
            f"advertising reconfiguration start is readiness-gated: {name.split('(')[0]}",
        )

    start_instance = function_body(advertising, "bool startInstanceLocked(uint8_t instanceId)")
    first_start = DIRECT_ADVERTISING_START.search(start_instance)
    explicit_guard = re.search(r"if\s*\(!FixtureGatt::Ready\(\)\)\s*\{[^{}]*return false;", start_instance, re.DOTALL)
    require(
        first_start is not None and explicit_guard is not None and explicit_guard.end() < first_start.start(),
        "manual, directed, and delayed advertising starts pass the shared readiness gate",
    )
    known_start_count = sum(len(DIRECT_ADVERTISING_START.findall(body)) for body in ready_gated_functions.values())
    known_start_count += len(DIRECT_ADVERTISING_START.findall(start_instance))
    total_start_count = len(DIRECT_ADVERTISING_START.findall(advertising))
    require(total_start_count == known_start_count, "all direct NimBLE advertising starts are covered by reviewed guards")
    source_start_sites = []
    for cpp_file in (ROOT / "src").rglob("*.cpp"):
        cpp_source = read_source(cpp_file.relative_to(ROOT))
        source_start_sites.extend(
            (cpp_file.relative_to(ROOT).as_posix(), cpp_source.count("\n", 0, match.start()) + 1)
            for match in DIRECT_ADVERTISING_START.finditer(cpp_source)
        )
    require(
        source_start_sites and all(path == "src/advertising.cpp" for path, _ in source_start_sites) and
        len(source_start_sites) == total_start_count,
        "known direct advertising-start spellings occur only at reviewed sites in advertising.cpp",
    )

    create = function_body(fixture, "NimBLECharacteristic* Create(NimBLEServer& server)")
    expected_constants = {
        "ServiceUuid": "12345678-1234-5678-1234-56789abcdef0",
        "ExistingCharacteristicUuid": "12345678-1234-5678-1234-56789abcdef1",
        "NotificationCharacteristicUuid": "12345678-1234-5678-1234-56789abcdef2",
        "IndicationCharacteristicUuid": "12345678-1234-5678-1234-56789abcdef3",
        "CustomDescriptorUuid": "12345678-1234-5678-1234-56789abcdef4",
        "ExistingCharacteristicValue": "Ima Characteristic",
        "NotificationCharacteristicValue": "Notify Characteristic",
        "IndicationCharacteristicValue": "Indicate Characteristic",
        "CustomDescriptorValue": "Custom Descriptor",
    }
    require(
        all(re.search(rf'inline constexpr char {name}\[\]\s*=\s*"{re.escape(value)}"\s*;', fixture_header)
            for name, value in expected_constants.items()),
        "original fixture UUID and value constants match the approved manifest",
    )
    normalized_create = re.sub(r"\s+", " ", create)
    require(
        all(token in normalized_create for token in (
            "service->createCharacteristic(ExistingCharacteristicUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE)",
            "service->createCharacteristic(NotificationCharacteristicUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY)",
            "service->createCharacteristic(IndicationCharacteristicUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE)",
            "customDescriptor->setValue(CustomDescriptorValue)",
            "notificationCharacteristic->setCallbacks(&notifyCallback)",
            "indicationCharacteristic->setCallbacks(&indicateCallback)",
        )),
        "original fixture properties, custom descriptor, and ATT callbacks remain registered",
    )
    require(
        "characteristic->notify(payload, payloadLength)" in fixture and
        "characteristic->indicate(payload, payloadLength)" in fixture,
        "legacy callback payload publication paths remain present",
    )

    guard = function_body(web, "bool guardRequest(httpd_req_t* request, bool mutation)")
    require(
        all(token in guard for token in ("isAuthenticated(request)", "hostOriginAllowed(request)", "X-CSRF-Token")),
        "shared HTTP guard retains Basic auth, Host/Origin, and mutation CSRF checks",
    )
    get_gatt = function_body(web, "esp_err_t getGatt(httpd_req_t* request)")
    post_gatt = function_body(web, "esp_err_t postGatt(httpd_req_t* request)")
    require("guardRequest(request, false)" in get_gatt, "GATT snapshot route uses the authenticated read boundary")
    require(
        "guardRequest(request, true)" in post_gatt and "enqueueRoute(request, Route::Gatt)" in post_gatt,
        "GATT mutation route uses the authenticated CSRF boundary and existing worker",
    )
    routes = function_body(web, "bool registerRoutes(httpd_handle_t handle)")
    registered_route_count = len(re.findall(r'\{\s*"[^"]+"\s*,\s*HTTP_(?:GET|POST)\s*,\s*\w+\s*\}', routes))
    route_capacity = re.search(r"config\.max_uri_handlers\s*=\s*(\d+)\s*;", web)
    require(
        len(re.findall(r'\{\s*"/api/gatt"\s*,\s*HTTP_GET\s*,\s*getGatt\s*\}', routes)) == 1 and
        len(re.findall(r'\{\s*"/api/gatt"\s*,\s*HTTP_POST\s*,\s*postGatt\s*\}', routes)) == 1 and
        route_capacity is not None and registered_route_count <= int(route_capacity.group(1)),
        "GET and POST GATT routes are registered within the counted handler capacity",
    )

    require(
        "MaxWebLength = 256" in value_header and "using Buffer = std::array<uint8_t, MaxWebLength>" in value_header,
        "decoded Web payload has a fixed 256-byte owned buffer",
    )
    cap_guard_index = value_header.find("if (length == bytes.size()) return false;")
    buffer_write_index = value_header.find("bytes[length++] =")
    require(
        cap_guard_index >= 0 and buffer_write_index >= 0 and cap_guard_index < buffer_write_index,
        "hex parser rejects over-cap values before writing the fixed buffer",
    )
    worker_command = function_body(web, "struct WorkerCommand")
    require(
        "GattValue::Buffer value{}" in worker_command and "size_t valueLength = 0" in worker_command and
        "httpd_req_t" not in worker_command,
        "queued worker command owns bytes and explicit length without retaining the request",
    )
    enqueue_route = function_body(web, "bool enqueueRoute(httpd_req_t* request, Route route)")
    require(
        enqueue_route.find("cJSON_Delete(json)") < enqueue_route.find("enqueueOperation(request, command)"),
        "request JSON is released before the owned worker command is queued",
    )
    prepare = function_body(web, "Admission prepareWorkerCommand(Route route, const cJSON* json, WorkerCommand& command, const char*& detail)")
    gatt_branch = prepare[prepare.find("if (route == Route::Gatt)"):prepare.find("if (route == Route::Advertising)")]
    require(
        "hasExactKeys" in gatt_branch and "GattValue::ParseHex(hex, command.value, command.valueLength)" in gatt_branch,
        "GATT commands require exact request fields and copy parsed bytes into fixed worker storage",
    )

    execute_host = function_body(fixture, "WebResult executeHost(const HostJob& job)")
    runtime_schema_creation = re.search(r"\b(?:createService|createCharacteristic|createDescriptor|create2904)\s*\(", execute_host)
    require(runtime_schema_creation is None, "runtime snapshots and value changes do not create or re-register GATT schema")

    print("STATIC ONLY: passing checks do not establish assigned handles, BLE delivery, or hardware acceptance.")
    print("STATIC ONLY: advertising scan covers known NimBLE start spellings; new wrappers or APIs need review.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"FAIL {error}", file=sys.stderr)
        sys.exit(1)
