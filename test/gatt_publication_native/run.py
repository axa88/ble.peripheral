"""Exercise extracted production host-publication code with explicit peer MTUs."""
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import os
import re
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = ROOT / ".pio/descriptor-fixture/native-publication"
CACHE_ROOT = Path(os.environ.get("BLE_PERIPHERAL_NATIVE_CACHE_ROOT", ROOT))



def extract_class(source: str, name: str) -> str:
    start = source.find(f"class {name}")
    if start < 0:
        raise RuntimeError(f"missing production callback class: {name}")
    brace = source.find("{", start)
    if brace < 0:
        raise RuntimeError(f"callback class has no body: {name}")
    depth = 0
    state = "code"
    escaped = False
    for index in range(brace, len(source)):
        char = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if state == "line":
            if char == "\n": state = "code"
        elif state == "block":
            if char == "*" and following == "/": state = "code"
        elif state in ("string", "char"):
            if escaped: escaped = False
            elif char == "\\": escaped = True
            elif (state == "string" and char == '"') or (state == "char" and char == "'"):
                state = "code"
        elif char == "/" and following == "/": state = "line"
        elif char == "/" and following == "*": state = "block"
        elif char == '"': state = "string"
        elif char == "'": state = "char"
        elif char == "{": depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                end = source.find(";", index)
                if end < 0: raise RuntimeError(f"unterminated callback class: {name}")
                return source[start:end + 1]
    raise RuntimeError(f"unterminated callback class: {name}")


def run_manager_diagnostics(compiler: Path, env: dict[str, str], checker) -> None:
    manager_source = (ROOT / "src/bluetoothManager.cpp").read_text(encoding="utf-8")
    fixture_source = (ROOT / "src/fixtureGatt.cpp").read_text(encoding="utf-8")
    require = lambda condition, message: (_ for _ in ()).throw(RuntimeError(message)) if not condition else None
    callback_base = extract_class(fixture_source, "SubscriptionCallback")
    callback_publication = extract_class(fixture_source, "PublicationCallback")
    require("void onStatus(NimBLECharacteristic*, int status) override" in callback_publication,
            "production publication callback must override only the two-argument onStatus overload")
    require("NimBLEConnInfo&" not in callback_publication and
            "RecordGattPublicationStatus(uuid_, status)" in callback_publication,
            "publication status callback must not attribute an event to a peer")
    require("temperatureSubscriptionCallback(TemperatureUuid)" in fixture_source and
            "if (i == 4) characteristic->setCallbacks(&temperatureSubscriptionCallback);" in fixture_source,
            "df05 must be registered with its own subscription observation callback")
    status_array = re.search(
        r"constexpr\s+std::array<std::string_view,\s*BluetoothManager::MaxGattPublicationStatusEntries>\s+gattPublicationStatusCharacteristics\{\{.*?\}\};",
        manager_source, re.S)
    require(status_array is not None, "production status allowlist array missing")
    template = (HERE / "diagnostics_harness.cpp.in").read_text(encoding="utf-8")
    definitions = []
    functions = [
        ("bool isGattSubscriptionCharacteristic(std::string_view uuid)", "bool isGattSubscriptionCharacteristic("),
        ("bool isGattPublicationStatusCharacteristic(std::string_view uuid)", "bool isGattPublicationStatusCharacteristic("),
        ("BluetoothManager::GattPublicationStatusKind gattPublicationStatusKind(int status)", "BluetoothManager::GattPublicationStatusKind gattPublicationStatusKind("),
        ("BluetoothManager::GattObservationSlot* BluetoothManager::FindGattObservationSlotLocked(ConnectionId connection, std::string_view characteristicUuid)", "BluetoothManager::GattObservationSlot* BluetoothManager::FindGattObservationSlotLocked("),
        ("BluetoothManager::GattPublicationStatusSlot* BluetoothManager::FindGattPublicationStatusSlotLocked(std::string_view characteristicUuid)", "BluetoothManager::GattPublicationStatusSlot* BluetoothManager::FindGattPublicationStatusSlotLocked("),
        ("void BluetoothManager::RecordGattSubscription(const NimBLEConnInfo& info, std::string_view characteristicUuid, uint16_t cccdValue)", "void BluetoothManager::RecordGattSubscription("),
        ("void BluetoothManager::RecordGattPublicationStatus(std::string_view characteristicUuid, int status)", "void BluetoothManager::RecordGattPublicationStatus("),
        ("BluetoothManager::GattSubscriptionDiagnosticsSnapshot BluetoothManager::GetGattSubscriptionDiagnosticsSnapshot()", "BluetoothManager::GattSubscriptionDiagnosticsSnapshot BluetoothManager::GetGattSubscriptionDiagnosticsSnapshot("),
        ("size_t BluetoothManager::CountObservedSubscribedPeers(const GattSubscriptionDiagnosticsSnapshot& snapshot, std::string_view characteristicUuid, uint16_t properties)", "size_t BluetoothManager::CountObservedSubscribedPeers("),
        ("void BluetoothManager::Disconnected(const NimBLEConnInfo& info)", "void BluetoothManager::Disconnected("),
        ("std::optional<NimBLEConnInfo> BluetoothManager::ResolveConnection(ConnectionId id)", "std::optional<NimBLEConnInfo> BluetoothManager::ResolveConnection("),
        ("bool BluetoothManager::IsCurrentConnectionLocked(ConnectionId id, uint16_t handle, const NimBLEAddress& peer)", "bool BluetoothManager::IsCurrentConnectionLocked("),
    ]
    for signature, needle in functions:
        body = checker.function_body(manager_source, needle)
        definitions.append(f"{signature} {{\n{body}\n}}")
    replacements = {
        "@@STATUS_UUID_ARRAY@@": status_array.group(0),
        "@@SUBSCRIPTION_PREDICATE@@": definitions[0],
        "@@PUBLICATION_PREDICATE@@": definitions[1],
        "@@STATUS_KIND@@": definitions[2],
        "@@MANAGER_METHODS@@": "\n".join(definitions[3:]),
        "@@SUBSCRIPTION_CALLBACK_CLASS@@": callback_base,
        "@@PUBLICATION_CALLBACK_CLASS@@": callback_publication,
    }
    for marker, value in replacements.items():
        require(marker in template, f"diagnostics template marker missing: {marker}")
        template = template.replace(marker, value)
    if "@@" in template:
        raise RuntimeError("unresolved diagnostics harness marker")
    out = OUT / "production-diagnostics.cpp"
    out.write_text(template, encoding="utf-8", newline="\n")
    executable = OUT / "diagnostics.exe"
    command = [str(compiler), "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
               "-I" + str(ROOT / "src"), str(out), "-o", str(executable)]
    for label, args in (("diagnostics-compile", command), ("diagnostics-run", [str(executable)])):
        result = subprocess.run(args, cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (OUT / (label + ".log")).write_text(result.stdout, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(result.stdout)
        if result.stdout:
            print(result.stdout, end="")

def main():
    spec = spec_from_file_location("fixture_checker", ROOT / "test/verify-descriptor-fixture.py")
    checker = module_from_spec(spec)
    spec.loader.exec_module(checker)
    source = (ROOT / "src/fixtureGatt.cpp").read_text()
    body = lambda signature: checker.function_body(source, signature)
    nimble = CACHE_ROOT / ".pio/libdeps/c3/NimBLE-Arduino/src"
    version = (nimble.parent / "library.properties").read_text(encoding="utf-8")
    if not re.search(r"(?m)^version=2\.5\.1\s*$", version):
        raise RuntimeError("publication adapter requires the pinned NimBLE-Arduino 2.5.1 package")
    characteristic_source = (nimble / "NimBLECharacteristic.cpp").read_text(encoding="utf-8")
    server_source = (nimble / "NimBLEServer.cpp").read_text(encoding="utf-8")
    status_dispatch = re.sub(r"\s+", " ", checker.function_body(
        server_source, "int NimBLEServer::handleGapEvent("))
    status_without_peer = "pChar->m_pCallbacks->onStatus(pChar, event->notify_tx.status);"
    status_with_peer = "pChar->m_pCallbacks->onStatus(pChar, peerInfo, event->notify_tx.status);"
    indication_gate = status_dispatch.find("if (event->notify_tx.indication)")
    indication_status = status_dispatch.find("if (event->notify_tx.status == 0)", indication_gate)
    indication_return = status_dispatch.find("return 0;", indication_status)
    first_dispatch = status_dispatch.find(status_without_peer)
    second_dispatch = status_dispatch.find(status_with_peer)
    default_peer_status = re.sub(r"\s+", " ", checker.function_body(
        characteristic_source,
        "void NimBLECharacteristicCallbacks::onStatus(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, int code)"))
    if not (status_dispatch.count(status_without_peer) == 1 and
            status_dispatch.count(status_with_peer) == 1 and
            0 <= indication_gate < indication_status < indication_return < first_dispatch < second_dispatch):
        raise RuntimeError("pinned NimBLE server no longer invokes the two-argument status callback before the peer-aware overload once per eligible event")
    if "RecordGattPublicationStatus" in default_peer_status or "onStatus: default" not in default_peer_status:
        raise RuntimeError("pinned peer-aware onStatus default behavior changed; verify event-once callback accounting")
    print("PASS pinned 2.5.1 server dispatches both status overloads once after indication TX acknowledgment; peer-aware default is inert")
    gatts_source = (nimble / "nimble/nimble/host/src/ble_gatts.c").read_text(encoding="utf-8")
    notify_default = re.sub(r"\s+", " ", checker.function_body(
        characteristic_source, "bool NimBLECharacteristic::notify(uint16_t connHandle) const"))
    indicate_default = re.sub(r"\s+", " ", checker.function_body(
        characteristic_source, "bool NimBLECharacteristic::indicate(uint16_t connHandle) const"))
    send_value = re.sub(r"\s+", " ", checker.function_body(
        characteristic_source, "bool NimBLECharacteristic::sendValue("))
    scheduler = re.sub(r"\s+", " ", checker.function_body(gatts_source, "ble_gatts_schedule_update("))
    require_pinned = (
        "return sendValue(nullptr, 0, true, connHandle);" in notify_default and
        "return sendValue(nullptr, 0, false, connHandle);" in indicate_default and
        "ble_gatts_chr_updated(m_handle);" in send_value and "return true;" in send_value
    )
    notify_priority = scheduler.find("else if (clt_cfg->flags & BLE_GATTS_CLT_CFG_F_NOTIFY)")
    indicate_priority = scheduler.find("else if (clt_cfg->flags & BLE_GATTS_CLT_CFG_F_INDICATE)")
    if not require_pinned or not (0 <= notify_priority < indicate_priority):
        raise RuntimeError("pinned NimBLE update path or CCCD-selected notification/indication precedence changed")
    print("PASS pinned 2.5.1 parameterless updates queue stored values; CCCD scheduler chooses Notify before Indicate")
    text = '''#include "fixtureGatt.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
namespace FixtureGatt {
bool nativeReady = true;
bool Ready() { return nativeReady; }
NimBLEServer* fixtureServer = nullptr;
uint64_t inventoryBoot = 99;
struct Entry { NimBLECharacteristic* characteristic; const char* name; const char* serviceUuid; const char* uuid; };
std::array<Entry,4> entries{};
enum class Work { Snapshot, Publish };
struct HostJob { Work work = Work::Publish; ConnectionId id{}; GattValue::Buffer bytes{}; size_t length = 0; };
struct PublicationLimits {
''' + body("struct PublicationLimits") + "\n};\n"
    text += "PublicationLimits publicationLimits() {\n" + body("PublicationLimits publicationLimits()") + "\n}\n"
    text += "bool equals(const NimBLELocalValueAttribute* attribute, const uint8_t* bytes, size_t length) {\n" + body("bool equals(") + "\n}\n"
    text += "WebResult executeHost(const HostJob& job) {\n" + body("WebResult executeHost(") + "\n}\n"
    text += '''
void nativeEntries(NimBLECharacteristic* a, NimBLECharacteristic* b, NimBLECharacteristic* c, NimBLECharacteristic* d) {
 entries = {{{a,"notification",ServiceUuid,NotificationCharacteristicUuid},
 {b,"indication",ServiceUuid,IndicationCharacteristicUuid},{c,"temperature",DescriptorServiceUuid,TemperatureUuid},
 {d,"dual",ServiceUuid,DualUpdateCharacteristicUuid}}};
}
WebResult nativeSnapshot() { HostJob job; job.work = Work::Snapshot; return executeHost(job); }
WebResult nativePublish(const std::vector<uint8_t>& bytes, ConnectionId id) {
 HostJob job; job.id=id; job.length=bytes.size();
 std::copy_n(bytes.begin(),std::min(bytes.size(),job.bytes.size()),job.bytes.begin());
 return executeHost(job);
}
}
'''
    OUT.mkdir(parents=True, exist_ok=True)
    generated = OUT / "production-publication.cpp"
    generated.write_text(text, encoding="utf-8", newline="\n")
    compiler = CACHE_ROOT / ".pio/native-tools/winlibs-16.2.0-ucrt-r2/mingw64/bin/g++.exe"
    env = os.environ.copy()
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    executable = OUT / "publication.exe"
    command = [str(compiler), "-std=c++17", "-Wall", "-Wextra", "-Werror", "-DBLE_INTERFACE_MODE=2",
               "-I" + str(HERE / "include"), "-I" + str(ROOT / "src"), str(generated), str(HERE / "main.cpp"), "-o", str(executable)]
    for label, args in (("compile", command), ("run", [str(executable)])):
        result = subprocess.run(args, cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (OUT / (label + ".log")).write_text(result.stdout, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(result.stdout)
        if result.stdout:
            print(result.stdout, end="")
    run_manager_diagnostics(compiler, env, checker)


if __name__ == "__main__":
    main()
