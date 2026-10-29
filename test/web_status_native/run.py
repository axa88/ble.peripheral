"""Build serializer/status and GATT snapshot regressions against installed cJSON."""

from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import hashlib
import os
import re
import subprocess
import sys

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[2]
TEST_DIR = Path(__file__).resolve().parent
OUTPUT = ROOT / ".pio" / "web-status" / "native"
NATIVE_BIN = ROOT / ".pio" / "native-tools" / "winlibs-16.2.0-ucrt-r2" / "mingw64" / "bin"


def function_body(source: str, signature: str) -> str:
	checker_path = ROOT / "test" / "verify-descriptor-fixture.py"
	spec = spec_from_file_location("verify_descriptor_fixture", checker_path)
	checker = module_from_spec(spec)
	sys.modules[spec.name] = checker
	spec.loader.exec_module(checker)
	return checker.function_body(source, signature)


def cjson_paths() -> tuple[Path, Path]:
	user_profile = Path(os.environ.get("USERPROFILE", Path.home()))
	packages = user_profile / ".platformio" / "packages"
	component = packages / "framework-espidf" / "components" / "json" / "cJSON"
	source = component / "cJSON.c"
	header = component / "cJSON.h"
	if not source.is_file() or not header.is_file():
		raise RuntimeError(f"installed cJSON source/header unavailable under {component}")

	component_hash = hashlib.sha256(header.read_bytes()).hexdigest()
	for board in ("esp32", "esp32c3"):
		board_header = packages / "framework-arduinoespressif32-libs" / board / "include" / "json" / "cJSON" / "cJSON.h"
		if not board_header.is_file() or hashlib.sha256(board_header.read_bytes()).hexdigest() != component_hash:
			raise RuntimeError(f"installed {board} cJSON header does not match the framework-espidf header")
	version = re.search(r"#define CJSON_VERSION_(MAJOR|MINOR|PATCH)\s+(\d+)", header.read_text(encoding="utf-8"))
	if not version:
		raise RuntimeError("could not identify installed cJSON header version")
	return source, component


def imported_bodies() -> dict[str, str]:
	web = (ROOT / "src" / "webInterface.cpp").read_text(encoding="utf-8")
	if "restoreValue" in web or "WorkerOperation::RestoreValue" in web:
		raise RuntimeError("removed restoreValue route/worker handling remains in production webInterface.cpp")
	if 'if (op != "publishValue") return unsupported(detail, "Unknown GATT operation.");' not in web:
		raise RuntimeError("the GATT route no longer rejects operations other than publishValue")
	signatures = {
		"operation_name": "std::string_view operationName(const WorkerCommand& command)",
		"state_name": "const char* stateName(SlotState state)",
		"result_name": "const char* resultName(Result result)",
		"operation_data": "cJSON* operationDataJson(const OperationData& data)",
		"operation_record": "cJSON* operationRecordJson(const WorkerRecord& record, bool queuedResponse = false)",
		"exact_keys": "bool hasExactKeys(const cJSON* object, std::initializer_list<std::string_view> allowed)",
		"exact_object_keys": "bool hasExactObjectKeys(const cJSON* object, std::initializer_list<std::string_view> expected)",
		"status_validator": "bool statusIsComplete(const cJSON* root",
		"get_status": "esp_err_t getStatus(httpd_req_t* request)",
		"hex_value": "std::string hexValue(const std::vector<uint8_t>& value)",
		"get_gatt": "esp_err_t getGatt(httpd_req_t* request)",
	}
	return {name: function_body(web, signature) for name, signature in signatures.items()}


def make_known_bad_validator(body: str) -> str:
	pattern = re.compile(
		r"\n\s*const cJSON\* storageVerified = .*?(?=\n\s*const cJSON\* started =)", re.DOTALL
	)
	mutated, count = pattern.subn("", body, count=1)
	if count != 1:
		raise RuntimeError("could not remove the production GATT-specific acceptance branch for the known-bad proof")
	return mutated


def make_known_bad_gatt(body: str) -> str:
	pattern = re.compile(
		r'(?P<indent>	*)if \(!cJSON_AddStringToObject\(value, "targetId", WebValidation::formatId\(entry\.id\)\.c_str\(\)\) \|\|\n'
		r'(?P=indent)	!cJSON_AddStringToObject\(value, "name", entry\.name\) \|\|'
	)
	match = pattern.search(body)
	if not match:
		raise RuntimeError("could not find the production targetId allocation check for the known-bad proof")
	indent = match.group("indent")
	unchecked = (
		'cJSON_AddStringToObject(value, "targetId", WebValidation::formatId(entry.id).c_str());\n'
		+ indent + 'if (!cJSON_AddStringToObject(value, "name", entry.name) ||'
	)
	mutated, count = pattern.subn(lambda _: unchecked, body, count=1)
	if count != 1:
		raise RuntimeError("could not remove the production targetId allocation check for the known-bad proof")
	return mutated


def generated_source(bodies: dict[str, str], known_bad: bool) -> str:
	validator = make_known_bad_validator(bodies["status_validator"]) if known_bad else bodies["status_validator"]
	gatt = make_known_bad_gatt(bodies["get_gatt"]) if known_bad else bodies["get_gatt"]
	parts = [
		'#include "status_test_types.h"\n#include <algorithm>\n#include <cmath>\n#include <cstring>\n#include <initializer_list>\n#include <limits>\n\nnamespace WebStatusTest {\n',
		f"std::string_view operationName(const WorkerCommand& command)\n{{\n{bodies['operation_name']}\n}}\n\n",
		f"const char* stateName(SlotState state)\n{{\n{bodies['state_name']}\n}}\n\n",
		f"const char* resultName(Result result)\n{{\n{bodies['result_name']}\n}}\n\n",
		f"cJSON* operationDataJson(const OperationData& data)\n{{\n{bodies['operation_data']}\n}}\n\n",
		f"cJSON* operationRecordJson(const WorkerRecord& record, bool queuedResponse)\n{{\n{bodies['operation_record']}\n}}\n\n",
		f"bool hasExactKeys(const cJSON* object, std::initializer_list<std::string_view> allowed)\n{{\n{bodies['exact_keys']}\n}}\n\n",
		f"bool hasExactObjectKeys(const cJSON* object, std::initializer_list<std::string_view> expected)\n{{\n{bodies['exact_object_keys']}\n}}\n\n",
		f"bool statusIsComplete(const cJSON* root, const std::vector<Advertising::InstanceConfig>& advConfigs,\n\tconst std::vector<BluetoothManager::ConnectedPeerSnapshot>& peers, int bondCount)\n{{\n{validator}\n}}\n\n",
		f"esp_err_t getStatus(httpd_req_t* request)\n{{\n{bodies['get_status']}\n}}\n",
		f"std::string hexValue(const std::vector<uint8_t>& value)\n{{\n{bodies['hex_value']}\n}}\n\n",
		f"esp_err_t getGatt(httpd_req_t* request)\n{{\n{gatt}\n}}\n",
		"}\n",
	]
	return "".join(parts)


def run(command: list[str], env: dict[str, str], label: str) -> None:
	result = subprocess.run(command, cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
	if result.returncode != 0:
		raise RuntimeError(f"{label} failed (exit {result.returncode}):\n{result.stdout}")
	if result.stdout:
		print(result.stdout, end="")


def main() -> None:
	OUTPUT.mkdir(parents=True, exist_ok=True)
	cjson_source, cjson_include = cjson_paths()
	bodies = imported_bodies()
	current_source = OUTPUT / "status-current.cpp"
	known_bad_source = OUTPUT / "status-known-bad.cpp"
	current_source.write_text(generated_source(bodies, False), encoding="utf-8", newline="\n")
	known_bad_source.write_text(generated_source(bodies, True), encoding="utf-8", newline="\n")

	gcc = NATIVE_BIN / "gcc.exe"
	gxx = NATIVE_BIN / "g++.exe"
	if not gcc.is_file() or not gxx.is_file():
		raise RuntimeError(f"repository-local native GCC/G++ not found under {NATIVE_BIN}")
	env = os.environ.copy()
	env["PATH"] = str(NATIVE_BIN) + os.pathsep + env.get("PATH", "")
	cjson_object = OUTPUT / "cJSON.o"
	run([str(gcc), "-std=gnu99", "-Wall", "-Wextra", "-I", str(cjson_include), "-c", str(cjson_source), "-o", str(cjson_object)], env, "compile installed cJSON source")

	for known_bad, source, executable in (
		(False, current_source, OUTPUT / "web-status-current.exe"),
		(True, known_bad_source, OUTPUT / "web-status-known-bad.exe"),
	):
		command = [
			str(gxx), "-std=c++17", "-Wall", "-Wextra", "-DCONFIG_BT_NIMBLE_EXT_ADV=0",
			f"-DEXPECT_KNOWN_BAD={1 if known_bad else 0}", "-I", str(TEST_DIR), "-I", str(cjson_include),
			str(source), str(TEST_DIR / "status_main.cpp"), str(cjson_object), "-o", str(executable),
		]
		run(command, env, "compile status regression harness")
		run([str(executable)], env, "run status regression harness")

	print("PASS: production status/GATT snapshot regressions and known-bad proofs; output retained in .pio/web-status/native")


if __name__ == "__main__":
	try:
		main()
	except Exception as error:
		print(f"FAIL: {error}", file=sys.stderr)
		sys.exit(1)
