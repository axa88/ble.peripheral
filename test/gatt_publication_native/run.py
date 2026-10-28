"""Exercise extracted production host-publication code with explicit peer MTUs."""
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import os
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = ROOT / ".pio/descriptor-fixture/native-publication"


def main():
    spec = spec_from_file_location("fixture_checker", ROOT / "test/verify-descriptor-fixture.py")
    checker = module_from_spec(spec)
    spec.loader.exec_module(checker)
    source = (ROOT / "src/fixtureGatt.cpp").read_text()
    body = lambda signature: checker.function_body(source, signature)
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
std::array<Entry,3> entries{};
enum class Work { Snapshot, Publish };
struct HostJob { Work work = Work::Publish; ConnectionId id{}; GattValue::Buffer bytes{}; size_t length = 0; };
struct PublicationLimits {
''' + body("struct PublicationLimits") + "\n};\n"
    text += "PublicationLimits publicationLimits() {\n" + body("PublicationLimits publicationLimits()") + "\n}\n"
    text += "bool equals(const NimBLELocalValueAttribute* attribute, const uint8_t* bytes, size_t length) {\n" + body("bool equals(") + "\n}\n"
    text += "WebResult executeHost(const HostJob& job) {\n" + body("WebResult executeHost(") + "\n}\n"
    text += '''
void nativeEntries(NimBLECharacteristic* a, NimBLECharacteristic* b, NimBLECharacteristic* c) {
 entries = {{{a,"notification",ServiceUuid,NotificationCharacteristicUuid},
 {b,"indication",ServiceUuid,IndicationCharacteristicUuid},{c,"temperature",DescriptorServiceUuid,TemperatureUuid}}};
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
    compiler = ROOT / ".pio/native-tools/winlibs-16.2.0-ucrt-r2/mingw64/bin/g++.exe"
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
