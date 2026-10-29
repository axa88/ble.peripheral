#!/usr/bin/env python3
"""Compile production orchestration with exact pinned NimBLE lifecycle bodies."""
from __future__ import annotations
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
from datetime import datetime
from typing import NoReturn

VERSION = "2.5.1"
ENVS = ("c3-com", "wrover-com")
SOURCES = {
    "ble_att_svr.c": Path("src/nimble/nimble/host/src/ble_att_svr.c"),
    "ble_gatts.c": Path("src/nimble/nimble/host/src/ble_gatts.c"),
    "NimBLEServer.cpp": Path("src/NimBLEServer.cpp"),
}
EXTRACT = {
    "ble_att_svr.c": {"ble_att_svr_reset": "@@ATT_RESET@@"},
    "ble_gatts.c": {
        "ble_gatts_svc_set_visibility": "@@SERVICE_VISIBILITY@@",
        "ble_gatts_count_cfg": "@@COUNT_CFG@@",
        "ble_gatts_reset": "@@GATTS_RESET@@",
        "ble_gatts_stop": "@@GATTS_STOP@@",
    },
    "NimBLEServer.cpp": {"NimBLEServer::removeService": "@@REMOVE_SERVICE@@"},
}


DYNAMIC_EXTRACT = {
    "CanonicalUuid": "@@CANONICAL_UUID@@",
    "protectedServices": "@@PROTECTED_SERVICES@@",
    "protectedUuid": "@@PROTECTED_UUID@@",
    "validateAdd": "@@VALIDATE_ADD@@",
}
FIXTURE_UUID_NAMES = (
    "ServiceUuid", "DescriptorServiceUuid", "ExistingCharacteristicUuid",
    "NotificationCharacteristicUuid", "IndicationCharacteristicUuid",
    "TemperatureUuid",
)

def fail(message: str) -> NoReturn:
    print(f"dynamic-gatt-native: {message}", file=sys.stderr)
    raise SystemExit(1)

def extract(source: str, name: str) -> str:
    """Return one exact function with the configured NimBLE branches selected."""
    needle = name + "("
    search = 0
    position = -1
    start = -1
    while True:
        candidate = source.find(needle, search)
        if candidate < 0:
            break
        line_start = source.rfind("\n", 0, candidate) + 1
        current = source[line_start:candidate]
        prev_end = line_start - 1
        prev_start = source.rfind("\n", 0, prev_end) + 1 if prev_end >= 0 else 0
        previous = source[prev_start:prev_end].strip() if prev_end >= 0 else ""
        trimmed = current.strip()
        is_definition = (
            trimmed.startswith(("int ", "void ", "bool ", "Result ", "std::array<"))
            or trimmed in ("int", "void", "bool", "Result")
            or previous in ("int", "void", "bool", "Result")
        )
        if is_definition:
            position = candidate
            start = prev_start if previous in ("int", "void", "bool", "Result") else line_start
            break
        search = candidate + len(needle)
    if position < 0:
        fail(f"missing pinned function definition: {name}")

    open_paren = position + len(name)
    parens = 0
    for end in range(open_paren, len(source)):
        if source[end] == "(":
            parens += 1
        elif source[end] == ")":
            parens -= 1
            if parens == 0:
                break
    else:
        fail(f"bad signature: {name}")
    brace = source.find("{", end + 1)
    semicolon = source.find(";", end + 1)
    if brace < 0 or (semicolon >= 0 and semicolon < brace):
        fail(f"not a definition: {name}")

    macros = {
        "CONFIG_BT_NIMBLE_EXT_ADV": True,
        "CONFIG_BT_NIMBLE_ROLE_BROADCASTER": True,
        "CONFIG_NIMBLE_CPP_LOG_LEVEL": 0,
        "ESP_PLATFORM": True,
    }

    def test_condition(expression: str) -> bool:
        expression = expression.strip()
        expression = expression.replace(
            "MYNEWT_VAL(BLE_DYNAMIC_SERVICE)", "0"
        ).replace("MYNEWT_VAL(BLE_SVC_HID_SERVICE)", "0")
        for key, value in macros.items():
            expression = expression.replace(key, "1" if value else "0")
        expression = expression.replace("&&", " and ").replace("||", " or ")
        expression = expression.replace("!", " not ")
        try:
            return bool(eval(expression, {"__builtins__": {}}, {}))
        except Exception as exc:
            fail(f"unhandled preprocessor condition {expression}: {exc}")

    prefix = source[start:brace + 1]
    selected = []
    state = "code"
    escaped = False
    brace_depth = 1
    active = True
    stack = []
    for line in source[brace + 1:].splitlines(keepends=True):
        directive = line.lstrip()[1:].strip() if state == "code" and line.lstrip().startswith("#") else ""
        if directive:
            parts = directive.split(None, 1)
            kind = parts[0]
            expression = parts[1] if len(parts) > 1 else ""
            if kind in ("if", "ifdef", "ifndef"):
                parent = active
                if kind == "ifdef":
                    chosen = expression in macros
                elif kind == "ifndef":
                    chosen = expression not in macros
                else:
                    chosen = test_condition(expression)
                stack.append([parent, chosen])
                active = parent and chosen
            elif kind == "elif":
                parent, used = stack[-1]
                chosen = test_condition(expression)
                active = parent and not used and chosen
                stack[-1][1] = used or chosen
            elif kind == "else":
                parent, used = stack[-1]
                active = parent and not used
                stack[-1][1] = True
            elif kind == "endif":
                parent, _ = stack.pop()
                active = parent
            continue
        if not active:
            continue
        selected.append(line)
        for index, char in enumerate(line):
            following = line[index + 1] if index + 1 < len(line) else ""
            if state == "line":
                if char == "\n":
                    state = "code"
            elif state == "block":
                if char == "*" and following == "/":
                    state = "code"
            elif state in ("string", "char"):
                if escaped:
                    escaped = False
                elif char == "\\":
                    escaped = True
                elif (state == "string" and char == '"') or (
                    state == "char" and char == "'"
                ):
                    state = "code"
            elif char == "/" and following == "/":
                state = "line"
            elif char == "/" and following == "*":
                state = "block"
            elif char == '"':
                state = "string"
            elif char == "'":
                state = "char"
            elif char == "{":
                brace_depth += 1
            elif char == "}":
                brace_depth -= 1
                if brace_depth == 0:
                    return prefix + "".join(selected)
    fail(f"unclosed pinned body: {name}")

def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main() -> int:
    root = Path(__file__).resolve().parents[2]
    original = root.parent / "ble.peripheral"
    if not original.is_dir(): fail(f"original dependency checkout missing: {original}")
    packages = [original / ".pio/libdeps" / env / "NimBLE-Arduino" for env in ENVS]
    for package in packages:
        prop = package / "library.properties"
        if not prop.is_file() or not re.search(r"(?m)^version=2\.5\.1\s*$", prop.read_text(encoding="utf-8")):
            fail(f"pinned NimBLE-Arduino 2.5.1 package missing: {package}")
    for relative in SOURCES.values():
        left, right = packages[0]/relative, packages[1]/relative
        if not left.is_file() or not right.is_file() or digest(left) != digest(right):
            fail(f"C3/WROVER pinned source mismatch: {relative}")
    api = packages[0]/"src/nimble/nimble/host/include/host/ble_esp_gatt.h"
    if not re.search(r"(?m)^\s*void\s+ble_gatts_stop\s*\(\s*void\s*\)\s*;", api.read_text(encoding="utf-8")):
        fail("ble_gatts_stop export missing")

    server_cpp = (packages[0]/SOURCES["NimBLEServer.cpp"]).read_text(encoding="utf-8")
    wrapper_template = (root/"test/dynamic_gatt_native/wrapper_harness.cpp.in").read_text(encoding="utf-8")
    wrapper_generated = wrapper_template.replace("@@ADD_SERVICE@@", extract(server_cpp, "NimBLEServer::addService"))
    wrapper_generated = wrapper_generated.replace("@@SERVER_START@@", extract(server_cpp, "NimBLEServer::start"))
    if "@@" in wrapper_generated: fail("unresolved pinned wrapper marker")

    generated = (root/"test/dynamic_gatt_native/pinned_harness.cpp.in").read_text(encoding="utf-8")
    for filename, symbols in EXTRACT.items():
        source = (packages[0]/SOURCES[filename]).read_text(encoding="utf-8")
        for symbol, marker in symbols.items():
            if marker not in generated: fail(f"missing template marker: {marker}")
            generated = generated.replace(marker, extract(source, symbol))
    if "@@" in generated: fail("unresolved pinned-source marker")

    outroot = root/".pio/dynamic-gatt-native"
    outroot.mkdir(parents=True, exist_ok=True)
    outdir = outroot/(datetime.now().strftime("%Y%m%dT%H%M%S")+f"-{os.getpid()}")
    outdir.mkdir()
    cpp, exe = outdir/"pinned_harness.cpp", outdir/"pinned_harness.exe"
    cpp.write_bytes(generated.replace("\r\n","\n").replace("\n","\r\n").encode("utf-8"))
    compiler = original/".pio/native-tools/winlibs-16.2.0-ucrt-r2/mingw64/bin/g++.exe"
    if not compiler.is_file(): fail(f"WinLibs compiler missing: {compiler}")
    env = os.environ.copy()
    env["PATH"] = str(compiler.parent)+os.pathsep+env.get("PATH","")
    command = [str(compiler),"-std=c++17","-Wall","-Wextra","-Werror",
               "-Wno-missing-field-initializers","-I",str(root),str(cpp),"-o",str(exe)]
    input_template = (root/"test/dynamic_gatt_native/input_harness.cpp.in").read_text(encoding="utf-8")
    fixture_header = (root/"src/fixtureGatt.h").read_text(encoding="utf-8")
    fixture_values = []
    for name in FIXTURE_UUID_NAMES:
        needle = f'inline constexpr char {name}[] = "'
        at = fixture_header.find(needle)
        if at < 0: fail(f"fixture UUID missing from production header: {name}")
        value_start = at + len(needle)
        value_end = fixture_header.find('"', value_start)
        if value_end < 0: fail(f"fixture UUID is unterminated in production header: {name}")
        fixture_values.append(f'inline constexpr char {name}[] = "{fixture_header[value_start:value_end]}";')
    input_generated = input_template.replace("@@FIXTURE_UUIDS@@", "\n".join(fixture_values))
    dynamic_source = (root/"src/dynamicGatt.cpp").read_text(encoding="utf-8")
    for name, marker in DYNAMIC_EXTRACT.items():
        if marker not in input_generated: fail(f"missing input marker: {marker}")
        input_generated = input_generated.replace(marker, extract(dynamic_source, name))
    if "@@" in input_generated: fail("unresolved production input marker")
    input_cpp, input_exe = outdir/"input_harness.cpp", outdir/"input_harness.exe"
    input_cpp.write_bytes(input_generated.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
    wrapper_cpp, wrapper_exe = outdir/"wrapper_harness.cpp", outdir/"wrapper_harness.exe"
    wrapper_cpp.write_bytes(wrapper_generated.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
    print(f"NimBLE-Arduino {VERSION}: C3/WROVER source hashes match")
    print("Compiling production gattRebuild.h and extracted pinned functions")
    subprocess.run(command,check=True,env=env)
    subprocess.run([str(exe)],check=True,env=env)
    print("Compiling extracted production UUID and add-validation functions")
    input_command = [str(compiler),"-std=c++17","-Wall","-Wextra","-Werror","-I",str(root),str(input_cpp),"-o",str(input_exe)]
    subprocess.run(input_command,check=True,env=env)
    subprocess.run([str(input_exe)],check=True,env=env)
    print("Compiling and executing exact pinned addService/start bodies")
    wrapper_command = [str(compiler),"-std=c++17","-Wall","-Wextra","-Werror",str(wrapper_cpp),"-o",str(wrapper_exe)]
    subprocess.run(wrapper_command,check=True,env=env)
    subprocess.run([str(wrapper_exe)],check=True,env=env)
    print(f"Generated files retained in {outdir}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
