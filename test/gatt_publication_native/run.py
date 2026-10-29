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


if __name__ == "__main__":
    main()
