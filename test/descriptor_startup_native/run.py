"""Compile the production Bluetooth startup function against the shared GATT adapter."""

from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import os
import shutil
import subprocess
import sys

# Keep the imported source checker from creating generated files under test/.
sys.dont_write_bytecode = True


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / ".pio" / "descriptor-fixture" / "native-startup-firmware-id"
TEST_DIR = Path(__file__).resolve().parent
ADAPTER_DIR = ROOT / "test" / "descriptor_fixture_native"


def load_function_body():
	checker_path = ROOT / "test" / "verify-descriptor-fixture.py"
	spec = spec_from_file_location("verify_descriptor_fixture", checker_path)
	checker = module_from_spec(spec)
	sys.modules[spec.name] = checker
	spec.loader.exec_module(checker)
	return checker.function_body


def generate_translation_unit():
	function_body = load_function_body()
	bluetooth_source = (ROOT / "src" / "bluetoothManager.cpp").read_text(encoding="utf-8")
	advertising_source = (ROOT / "src" / "advertising.cpp").read_text(encoding="utf-8")

	setup_bluetooth = function_body(bluetooth_source, "void BluetoothManager::SetupBluetooth()")
	setup_advertising = function_body(advertising_source, "void Setup(const InstanceConfig& config)")
	rebuild_advertisement = function_body(advertising_source, "bool rebuildAdvertisement(const Advertising::InstanceConfig& cfg, bool restartIfActive)")
	get_config = function_body(advertising_source, "std::optional<InstanceConfig> GetConfig(uint8_t instanceId)")
	try_snapshot = function_body(advertising_source, "bool TrySnapshot(std::vector<InstanceConfig>& out)")

	preamble = r'''#include "startup_support.h"
#include <map>
#include <mutex>

namespace
{
	std::mutex configMutex_;
	std::map<uint8_t, Advertising::InstanceConfig> configs_;

	void cancelRestartLocked(uint8_t, const char*) {}
	void cancelRestartTimer(uint8_t) {}

#if CONFIG_BT_NIMBLE_EXT_ADV
	bool applyExtendedDataLocked(const Advertising::InstanceConfig& config, bool)
	{
		NimBLEExtAdvertisement data;
		return NimBLEDevice::getAdvertising()->setInstanceData(config.id, data);
	}
#else
	bool rebuildLegacyAdvertisement(const Advertising::InstanceConfig&, bool)
	{
		++StartupTest::state().legacyRebuildCalls;
		return true;
	}
#endif

	bool rebuildAdvertisement(const Advertising::InstanceConfig& cfg, bool restartIfActive)
	{
'''

	parts = [preamble, rebuild_advertisement, "\n\t}\n}\n\nnamespace Advertising\n{\n\tvoid Setup(const InstanceConfig& config)\n\t{\n", setup_advertising,
		"\n\t}\n\n\tbool TrySnapshot(std::vector<InstanceConfig>& out)\n\t{\n", try_snapshot,
		"\n\t}\n\n\tstd::optional<InstanceConfig> GetConfig(uint8_t instanceId)\n\t{\n", get_config,
		"\n\t}\n}\n\nnamespace StartupTest\n{\n\tbool tryProductionRebuild(const Advertising::InstanceConfig& config)\n\t{\n\t\treturn rebuildAdvertisement(config, false);\n\t}\n}\n\nvoid BluetoothManager::SetupBluetooth()\n{\n",
		setup_bluetooth, "\n}\n"]
	return "".join(parts)


def find_compiler():
	candidates = [
		ROOT / ".pio" / "native-tools" / "winlibs-16.2.0-ucrt-r2" / "mingw64" / "bin" / "g++.exe",
		Path(shutil.which("g++")) if shutil.which("g++") else None,
	]
	for candidate in candidates:
		if candidate and candidate.is_file():
			return candidate
	raise RuntimeError("native g++ was not found; expected the repository-local WinLibs compiler")


def main():
	OUTPUT.mkdir(parents=True, exist_ok=True)
	generated = OUTPUT / "startup_boot.cpp"
	generated.write_text(generate_translation_unit(), encoding="utf-8", newline="\n")
	compiler = find_compiler()
	environment = os.environ.copy()
	environment["PATH"] = str(compiler.parent) + os.pathsep + environment.get("PATH", "")

	for extended in (0, 1):
		executable = OUTPUT / f"startup-{extended}.exe"
		command = [
			str(compiler), "-std=c++17", "-O0", "-Wall", "-Wextra",
			"-DBLE_INTERFACE_MODE=0", f"-DCONFIG_BT_NIMBLE_EXT_ADV={extended}",
			"-I", str(TEST_DIR), "-I", str(ADAPTER_DIR / "include"), "-I", str(ROOT / "src"),
			str(generated), str(TEST_DIR / "startup_support.cpp"),
			str(TEST_DIR / "startup_main.cpp"), str(ADAPTER_DIR / "adapter.cpp"), str(ROOT / "src" / "fixtureGatt.cpp"),
			"-o", str(executable),
		]
		subprocess.run(command, cwd=ROOT, env=environment, check=True)
		for scenario in ("success", "registration-failure", "finalization-failure"):
			result = subprocess.run([str(executable), scenario], cwd=ROOT, env=environment,
				capture_output=True, text=True, check=False)
			if result.returncode != 0:
				sys.stdout.write(result.stdout)
				sys.stderr.write(result.stderr)
				raise RuntimeError(f"Startup scenario {scenario} failed with exit code {result.returncode}.")
			identity_line = "Firmware revision 7 | source 0123456789ab | environment native-startup"
			if result.stdout.splitlines().count(identity_line) != 1:
				raise RuntimeError(f"Startup scenario {scenario} did not log the firmware identity exactly once.")
		print(f"PASS CONFIG_BT_NIMBLE_EXT_ADV={extended}: all three startup scenarios")

	print("Native-only startup harness passed; this does not validate radio behavior or hardware acceptance.")


if __name__ == "__main__":
	try:
		main()
	except Exception as error:
		print(f"FAIL {error}", file=sys.stderr)
		sys.exit(1)
