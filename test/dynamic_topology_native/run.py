#!/usr/bin/env python3
"""Execute production FixtureIdentity capture/verify against a small native GATT map."""
from __future__ import annotations
from datetime import datetime
import os
from pathlib import Path
import re
import subprocess
import sys

UUID_NAMES = (
    "ServiceUuid", "ExistingCharacteristicUuid", "NotificationCharacteristicUuid",
    "IndicationCharacteristicUuid", "CustomDescriptorUuid", "DescriptorServiceUuid",
    "TemperatureUuid",
)
UUID_MARKERS = {
    "ServiceUuid": "SERVICE_UUID",
    "ExistingCharacteristicUuid": "EXISTING_CHARACTERISTIC_UUID",
    "NotificationCharacteristicUuid": "NOTIFICATION_CHARACTERISTIC_UUID",
    "IndicationCharacteristicUuid": "INDICATION_CHARACTERISTIC_UUID",
    "CustomDescriptorUuid": "CUSTOM_DESCRIPTOR_UUID",
    "DescriptorServiceUuid": "DESCRIPTOR_SERVICE_UUID",
    "TemperatureUuid": "TEMPERATURE_UUID",
}

def fail(message: str) -> None:
    print(f"dynamic-topology-native: {message}", file=sys.stderr)
    raise SystemExit(1)

def extract_function(source: str, signature: str) -> str:
    at = source.find(signature)
    if at < 0:
        fail(f"production helper missing: {signature}")
    brace = source.find("{", at + len(signature))
    if brace < 0:
        fail(f"production helper has no body: {signature}")
    depth = 0
    state = "code"
    escaped = False
    for i in range(brace, len(source)):
        c = source[i]
        n = source[i + 1] if i + 1 < len(source) else ""
        if state == "line":
            if c == "\n": state = "code"
        elif state == "block":
            if c == "*" and n == "/": state = "code"
        elif state in ("string", "char"):
            if escaped: escaped = False
            elif c == "\\": escaped = True
            elif (state == "string" and c == '"') or (state == "char" and c == "'"):
                state = "code"
        elif c == "/" and n == "/": state = "line"
        elif c == "/" and n == "*": state = "block"
        elif c == '"': state = "string"
        elif c == "'": state = "char"
        elif c == "{": depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0: return source[at:i + 1]
    fail(f"unterminated production helper: {signature}")

def main() -> int:
    root = Path(__file__).resolve().parents[2]
    source_path = root / "src/dynamicGatt.cpp"
    source = source_path.read_text(encoding="utf-8")
    fixture_header = (root / "src/fixtureGatt.h").read_text(encoding="utf-8")
    values: dict[str, str] = {}
    for name in UUID_NAMES:
        match = re.search(r"inline constexpr char " + re.escape(name) + r'\[\] = "([^"]+)";', fixture_header)
        if not match: fail(f"fixture UUID missing: {name}")
        values[name] = match.group(1)

    protected = extract_function(source, "std::array<NimBLEService*, 2> protectedServices()")
    begin = source.find("    struct Preserved")
    end = source.find("    bool protectedUuid(", begin)
    if begin < 0 or end < 0: fail("production FixtureIdentity type block missing")
    identity_types = source[begin:end].rstrip()
    for required in ("struct Preserved", "struct DescriptorIdentity", "struct FixtureIdentity",
                     "serverFeaturesHandle", "clientFeaturesHandle", "cccds"):
        if required not in identity_types: fail(f"production FixtureIdentity contract missing: {required}")

    template = (root / "test/dynamic_topology_native/topology_harness.cpp.in").read_text(encoding="utf-8")
    replacements = {
        "@@PROTECTED_SERVICES@@": protected,
        "@@IDENTITY_STRUCTS@@": identity_types,
    }
    for name, value in values.items(): replacements[f"@@{UUID_MARKERS[name]}@@"] = value
    generated = template
    for marker, value in replacements.items():
        if marker not in generated: fail(f"template marker missing: {marker}")
        generated = generated.replace(marker, value)
    if "@@" in generated: fail("unresolved generated-source marker")

    original = root.parent / "ble.peripheral"
    compiler = original / ".pio/native-tools/winlibs-16.2.0-ucrt-r2/mingw64/bin/g++.exe"
    if not compiler.is_file(): fail(f"WinLibs compiler missing: {compiler}")
    outroot = root / ".pio/dynamic-topology-native"
    outroot.mkdir(parents=True, exist_ok=True)
    outdir = outroot / (datetime.now().strftime("%Y%m%dT%H%M%S") + f"-{os.getpid()}")
    outdir.mkdir()
    cpp, exe = outdir / "topology_harness.cpp", outdir / "topology_harness.exe"
    cpp.write_bytes(generated.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
    env = os.environ.copy()
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    command = [str(compiler), "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)]
    print("Compiling production FixtureIdentity capture/verify with native GATT lookup stubs")
    subprocess.run(command, check=True, env=env)
    subprocess.run([str(exe)], check=True, env=env)
    print(f"Generated files retained in {outdir}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
