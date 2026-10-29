"""Compile selected production advertising and serial admission bodies natively."""
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import os
import subprocess
import sys
from datetime import datetime

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "test" / "verify-descriptor-fixture.py"
spec = spec_from_file_location("fixture_checker", CHECKER)
checker = module_from_spec(spec)
sys.modules[spec.name] = checker
spec.loader.exec_module(checker)


def body(source, signature):
    return checker.function_body(source, signature)


def extract_if_block(source, condition):
    start = source.find(condition)
    if start < 0:
        raise RuntimeError(f"missing production input branch: {condition}")
    opening = source.find("{", start + len(condition))
    if opening < 0:
        raise RuntimeError(f"missing branch body: {condition}")
    depth = 0
    state = "code"
    escaped = False
    index = opening
    while index < len(source):
        char = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if state == "line":
            if char == "\n":
                state = "code"
        elif state == "block":
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
            state = "line"
            index += 1
        elif char == "/" and following == "*":
            state = "block"
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
                return source[start:index + 1]
        index += 1
    raise RuntimeError(f"unterminated production input branch: {condition}")


def definition(source, signature):
    return signature + " {\n" + body(source, signature) + "\n}\n"


def generate():
    adv = (ROOT / "src" / "advertising.cpp").read_text(encoding="utf-8")
    menu = (ROOT / "src" / "processMenu.cpp").read_text(encoding="utf-8")
    input_source = (ROOT / "src" / "processInput.cpp").read_text(encoding="utf-8")
    template = (ROOT / "test" / "dynamic_advertising_native" / "harness.cpp.in").read_text(encoding="utf-8")

    common = (
        "bool hostAdvertisingActive(uint8_t instanceId)",
        "uint32_t advertisingIntentEpoch(uint8_t instanceId)",
        "void advanceAdvertisingIntentEpoch(uint8_t instanceId)",
        "bool restartPolicyEnabledLocked(const Advertising::InstanceConfig& cfg) noexcept",
        "bool invalidateRestartLocked(uint8_t instanceId)",
        "void cancelRestartLocked(uint8_t instanceId, const char* reason)",
        "bool markRestartPendingLocked(uint8_t instanceId, const char* trigger)",
        "void cancelRestartTimer(uint8_t instanceId)",
        "bool scheduleRestartTimer(uint8_t instanceId)",
        "bool rearmRestartAfterRebuild(uint8_t instanceId)",
        "void processRestartTimer(uint8_t instanceId, uint32_t generation)",
        "void tryRestart(uint8_t instanceId, uint32_t generation)",
        "void processConnectionEstablished()",
        "bool hasDeferredCallbacksLocked(const DeferredCallbacks& callbacks)",
        "void mergeDeferredCallbacksLocked(const DeferredCallbacks& source)",
        "void queueDeferredAdvertisingEventIfReady()",
        "void deferConnectionEstablished()",
        "void deferRestartTimer(uint8_t instanceId, uint32_t generation)",
        "void deferredAdvertisingEventHandler(ble_npl_event* event)",
    )
    helpers = "".join(definition(adv, signature) for signature in common)
    ext_helpers = (
        "uint32_t beginStopRequestLocked(uint8_t instanceId)",
        "bool publishExplicitStopCompletionLocked(uint8_t instanceId, uint32_t generation, bool stopSucceeded, bool inactive)",
        "void processAdvertisingStopped(uint8_t instanceId, int reason, bool activeAfterCallback, bool restartEligible, bool clearDirected)",
        "void deferAdvertisingStopped(uint8_t instanceId, int reason, bool activeAfterCallback, bool restartEligible, bool clearDirected)",
    )
    helpers += "#if CONFIG_BT_NIMBLE_EXT_ADV\n"
    helpers += "".join(definition(adv, signature) for signature in ext_helpers[:2])
    helpers += "".join(definition(adv, signature) for signature in ext_helpers[2:3])
    helpers += "".join(definition(adv, signature) for signature in ext_helpers[3:])
    helpers += "#endif\n"

    replacements = {
        "@@ADVERTISING_HELPERS@@": helpers,
        "@@STOP_BODY@@": body(adv, "bool Stop(uint8_t instanceId)"),
        "@@STOP_ALL_BODY@@": body(adv, "StopAllResult StopAll()"),
        "@@CAPTURE_BODY@@": body(adv, "bool CaptureForRebuild(RebuildSnapshot& out)"),
        "@@RESTORE_BODY@@": body(adv, "bool RestoreAfterRebuild(const RebuildSnapshot& snapshot)"),
        "@@CONNECTION_BODY@@": body(adv, "void OnConnectionEstablished()"),
        "@@STOPPED_CALLBACK_BODY@@": body(adv, "void OnAdvertisingStopped(uint8_t instanceId, int reason)"),
        "@@DRAIN_BODY@@": body(adv, "void DrainDeferredCallbacks()"),
        "@@CONFIG_INPUT_BODY@@": body(menu, "void handleConfigInput()"),
        "@@REQUIRES_ENTRY_BODY@@": body(menu, "bool requiresDynamicGattEntry(const ConsoleCommand::Descriptor& command, char key)"),
        "@@PRINT_ECHO_BODY@@": body(menu, "void printCommandEcho(const ConsoleCommand::Descriptor& command, char key)"),
        "@@READ_COMMAND_BODY@@": body(menu, "std::optional<char> readCommandChar()"),
        "@@PARSE_UNSIGNED_BODY@@": body(input_source, "static bool parseUnsignedDecimal(const char* first, const char* last, uint32_t maximum, uint32_t& value)"),
        "@@SET_INTERVAL_BLOCK@@": extract_if_block(input_source, "if (curMode == ConsoleMode::SetAdvInterval)"),
        "@@ADD_INSTANCE_BLOCK@@": extract_if_block(input_source, "if (curMode == ConsoleMode::AddAdvInstance)"),
        "@@BOND_TARGET_BLOCK@@": extract_if_block(input_source, "if (curMode == ConsoleMode::SelectBondTarget)"),
        "@@TRY_RESTART_STALE_TEST@@": "tryRestart(0, 7); assert(deferredCallbacks_.restartTimerPending[0]);",
        "@@TRY_RESTART_CURRENT_TEST@@": "tryRestart(0, 8); assert(deferredCallbacks_.restartTimerPending[0]);",
    }
    for marker, value in replacements.items():
        if marker not in template:
            raise RuntimeError(f"missing harness marker: {marker}")
        template = template.replace(marker, value)
    if "@@" in template:
        raise RuntimeError("unresolved harness marker")
    return template


def main():
    compiler = ROOT.parent / "ble.peripheral" / ".pio" / "native-tools" / "winlibs-16.2.0-ucrt-r2" / "mingw64" / "bin" / "g++.exe"
    if not compiler.is_file():
        raise RuntimeError(f"repository-local native compiler missing: {compiler}")
    outroot = ROOT / ".pio" / "dynamic-advertising-native"
    outroot.mkdir(parents=True, exist_ok=True)
    outdir = outroot / (datetime.now().strftime("%Y%m%dT%H%M%S") + f"-{os.getpid()}")
    outdir.mkdir()
    env = os.environ.copy()
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    generated = generate()
    for ext in (0, 1):
        cpp = outdir / f"advertising-{ext}.cpp"
        exe = outdir / f"advertising-{ext}.exe"
        cpp.write_text(generated, encoding="utf-8", newline="\n")
        command = [
            str(compiler), "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-parameter",
            f"-DCONFIG_BT_NIMBLE_EXT_ADV={ext}", "-DCONFIG_BT_NIMBLE_MAX_EXT_ADV_INSTANCES=4",
            str(cpp), "-o", str(exe),
        ]
        result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, check=False)
        if result.returncode:
            sys.stdout.write(result.stdout)
            sys.stderr.write(result.stderr)
            raise RuntimeError(f"native compile failed for CONFIG_BT_NIMBLE_EXT_ADV={ext}")
        result = subprocess.run([str(exe)], cwd=ROOT, env=env, capture_output=True, text=True, check=False)
        if result.returncode:
            sys.stdout.write(result.stdout)
            sys.stderr.write(result.stderr)
            raise RuntimeError(f"native scenarios failed for CONFIG_BT_NIMBLE_EXT_ADV={ext}")
        sys.stdout.write(result.stdout)
    print(f"Generated native evidence retained in {outdir}")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"FAIL {error}", file=sys.stderr)
        raise
