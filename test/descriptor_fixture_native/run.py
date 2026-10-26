#!/usr/bin/env python3
"""Run bounded native regression cases against the production fixture source."""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = ROOT / ".pio" / "descriptor-fixture" / "native-regression"
NIMBLE = ROOT / ".pio" / "libdeps" / "c3" / "NimBLE-Arduino" / "src"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def source_contract() -> None:
    gatt = (NIMBLE / "nimble" / "nimble" / "host" / "include" / "host" / "ble_gatt.h").read_text(encoding="utf-8")
    att = (NIMBLE / "nimble" / "nimble" / "host" / "include" / "host" / "ble_att.h").read_text(encoding="utf-8")
    local = (NIMBLE / "NimBLELocalValueAttribute.h").read_text(encoding="utf-8")
    descriptor = (NIMBLE / "NimBLEDescriptor.cpp").read_text(encoding="utf-8")
    service = (NIMBLE / "NimBLEService.cpp").read_text(encoding="utf-8")
    server = (NIMBLE / "NimBLEServer.cpp").read_text(encoding="utf-8")
    cpf = (NIMBLE / "NimBLE2904.cpp").read_text(encoding="utf-8")

    expected = {
        "BLE_GATT_CHR_F_READ": "0x0002",
        "BLE_GATT_CHR_F_WRITE": "0x0008",
        "BLE_GATT_CHR_F_NOTIFY": "0x0010",
        "BLE_GATT_CHR_F_INDICATE": "0x0020",
    }
    for name, value in expected.items():
        require(re.search(rf"#define\s+{name}\s+{value}\b", gatt) is not None,
                f"pinned host header contract mismatch: {name} must be {value}")
    for name, value in {"BLE_ATT_F_READ": "0x01", "BLE_ATT_F_WRITE": "0x02"}.items():
        require(re.search(rf"#define\s+{name}\s+{value}\b", att) is not None,
                f"pinned ATT header contract mismatch: {name} must be {value}")

    require(re.search(r"READ\s*=\s*BLE_GATT_CHR_F_READ", local) is not None,
            "pinned NimBLE NIMBLE_PROPERTY::READ enum no longer uses GATT characteristic flags")
    compact_descriptor = re.sub(r"\s+", " ", descriptor)
    require(re.search(r"if\s*\(properties\s*&\s*NIMBLE_PROPERTY::READ\)\s*\{\s*descProperties\s*\|=\s*BLE_ATT_F_READ\s*;", compact_descriptor) is not None,
            "pinned NimBLE descriptor constructor no longer converts READ to ATT read permission")
    require(re.search(r"if\s*\(properties\s*&\s*\(NIMBLE_PROPERTY::WRITE_NR\s*\|\s*NIMBLE_PROPERTY::WRITE\)\)\s*\{\s*descProperties\s*\|=\s*BLE_ATT_F_WRITE\s*;", compact_descriptor) is not None,
            "pinned NimBLE descriptor constructor no longer converts WRITE to ATT write permission")
    require("setProperties(descProperties);" in compact_descriptor,
            "pinned NimBLE descriptor constructor no longer stores converted permission bits")
    compact_service = re.sub(r"\s+", " ", service)
    require("pDscs[j].att_flags = dsc->getProperties();" in compact_service,
            "pinned NimBLE service registration no longer consumes descriptor ATT permissions")
    compact_server = re.sub(r"\s+", " ", server)
    require(re.search(r"if\s*\(m_gattsStarted\)\s*\{\s*return true\s*;", compact_server) is not None,
            "pinned NimBLE server no longer returns early when GATT is already started")
    compact_cpf = re.sub(r"\s+", " ", cpf)
    require(re.search(r"NimBLEUUID\(\(uint16_t\)0x2904\),\s*BLE_GATT_CHR_F_READ", compact_cpf) is not None,
            "pinned NimBLE 0x2904 constructor no longer passes GATT characteristic READ flag")

    print("PASS: pinned NimBLE characteristic and ATT flag contracts grounded in installed source")


def run(command: list[str], log_path: Path, expected: int = 0, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log_path.write_text(result.stdout, encoding="utf-8", newline="\n")
    if result.returncode != expected:
        raise RuntimeError(f"command returned {result.returncode}, expected {expected}; see {log_path}\n{result.stdout}")
    return result


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    source_contract()

    cpp = ROOT / "src" / "fixtureGatt.cpp"
    adapter = HERE / "adapter.cpp"
    main_cpp = HERE / "main.cpp"
    include = HERE / "include"
    source_hash = hashlib.sha256(cpp.read_bytes()).hexdigest()
    compiler = shutil.which("g++")
    if not compiler:
        bundled = ROOT / ".pio" / "native-tools" / "winlibs-16.2.0-ucrt-r2" / "mingw64" / "bin" / "g++.exe"
        require(bundled.is_file(), "native g++ is unavailable in PATH and the existing repository toolchain was not found")
        compiler = str(bundled)
    compile_env = os.environ.copy()
    compile_env["PATH"] = str(Path(compiler).resolve().parent) + os.pathsep + compile_env.get("PATH", "")
    common = [compiler, "-std=c++17", "-Wall", "-Wextra", "-DBLE_INTERFACE_MODE=0",
              f"-I{include}", f"-I{ROOT / 'src'}"]
    binary = OUT / "fixture-gatt-native.exe"
    compile_command = common + [str(cpp), str(adapter), str(main_cpp), "-o", str(binary)]
    run(compile_command, OUT / "compile-current.log", env=compile_env)
    print(f"PASS: compiled production src/fixtureGatt.cpp ({source_hash})")

    cases = ["success", "permission", "zero-handle", "duplicate-handle", "aggregate-readback",
             "registration-failure", "create-failure"]
    for case in cases:
        result = run([str(binary), case], OUT / f"case-{case}.log", env=compile_env)
        require(f"PASS: {case}" in result.stdout, f"case output missing PASS marker: {case}")
        print(f"PASS: native fixture case {case}")

    production_text = cpp.read_text(encoding="utf-8")
    wrong_domain = "getProperties() != NIMBLE_PROPERTY::READ"
    original = "getProperties() != BLE_ATT_F_READ"
    require(production_text.count(original) == 4, "expected exactly four production descriptor permission comparisons")
    bad_text = production_text.replace(original, wrong_domain)
    require(bad_text.count(wrong_domain) == 4, "known-bad fixture source mutation was incomplete")
    bad_source = OUT / "fixtureGatt-known-bad.cpp"
    bad_source.write_text(bad_text, encoding="utf-8", newline="\n")
    bad_binary = OUT / "fixture-gatt-known-bad.exe"
    run(common + [str(bad_source), str(adapter), str(main_cpp), "-o", str(bad_binary)],
        OUT / "compile-known-bad.log", env=compile_env)
    bad_result = run([str(bad_binary), "success"], OUT / "known-bad-success-case.log", expected=1, env=compile_env)
    require("expected finalization success but got false" in bad_result.stdout,
            "known-bad descriptor-domain variant failed for an unexpected reason")
    print("PASS: known-bad READ-domain variant fails the normal success case as expected")

    require(hashlib.sha256(cpp.read_bytes()).hexdigest() == source_hash,
            "production fixtureGatt.cpp changed during native validation")
    print(f"PASS: production source unchanged; logs and native outputs retained in {OUT}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
