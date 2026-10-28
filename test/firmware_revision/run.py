"""Executable tests for stable source-driven firmware revision identity."""

from __future__ import annotations

import gzip
import hashlib
import importlib.util
import json
import multiprocessing
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tempfile


sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / ".pio" / "descriptor-fixture" / "native-revision"
REVISION_MODULE_PATH = ROOT / "tools" / "firmware_revision.py"
SPEC = importlib.util.spec_from_file_location("firmware_revision_test_module", REVISION_MODULE_PATH)
revision_module = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = revision_module
SPEC.loader.exec_module(revision_module)


def git(project: Path, *arguments: str) -> None:
	result = subprocess.run(
		["git", *arguments],
		cwd=project,
		stdout=subprocess.PIPE,
		stderr=subprocess.PIPE,
		check=False,
	)
	if result.returncode:
		raise RuntimeError(f"Git fixture command failed ({arguments[0]}), exit code {result.returncode}.")


def make_project(root: Path) -> Path:
	(root / "tools").mkdir(parents=True)
	(root / "src").mkdir()
	(root / "web").mkdir()
	(root / ".gitignore").write_text(
		".pio/\nplatformio.local.ini\n*.secret\n.firmware-revision.lock\n",
		encoding="utf-8",
	)
	(root / "src" / "sample.cpp").write_text("int sample() { return 1; }\n", encoding="utf-8")
	(root / "web" / "index.html").write_text("<h1>revision fixture one</h1>\n", encoding="utf-8")
	shutil.copy2(ROOT / "tools" / "generate-web-assets.py", root / "tools" / "generate-web-assets.py")
	subprocess.run([sys.executable, str(root / "tools" / "generate-web-assets.py")], cwd=root, check=True)
	git(root, "init", "-q")
	git(root, "config", "user.name", "Firmware Revision Tests")
	git(root, "config", "user.email", "firmware-revision@example.invalid")
	git(root, "add", ".")
	git(root, "commit", "-qm", "fixture baseline")
	return root


def sync_worker(project_path: str) -> int:
	return revision_module.sync_revision(Path(project_path)).revision


def require(condition: bool, message: str) -> None:
	if not condition:
		raise AssertionError(message)


def python_cases() -> None:
	OUTPUT.mkdir(parents=True, exist_ok=True)
	with tempfile.TemporaryDirectory(prefix="fixture-", dir=OUTPUT, ignore_cleanup_errors=True) as temporary:
		project = make_project(Path(temporary))
		first = revision_module.sync_revision(project)
		require(first.revision == 1, "unassigned state must initialize at revision 1")
		require(revision_module.sync_revision(project) == first, "unchanged source must keep its revision and ID")

		c3_header = project / ".pio" / "build" / "c3" / "generated" / "firmware_identity.generated.h"
		wrover_header = project / ".pio" / "build" / "wrover" / "generated" / "firmware_identity.generated.h"
		revision_module.write_identity_header(c3_header, revision_module.FirmwareIdentity(first.revision, first.source_id, "c3"))
		revision_module.write_identity_header(wrover_header, revision_module.FirmwareIdentity(first.revision, first.source_id, "wrover"))
		require(c3_header.read_text(encoding="utf-8") != wrover_header.read_text(encoding="utf-8"),
			"environment headers must distinguish PIOENV without changing project revision")
		require(revision_module.sync_revision(project) == first, "multiple environment builds must share the same identity")

		fingerprint_before_ignored = revision_module.source_fingerprint(project)
		(project / ".pio" / "build" / "c3" / "generated" / "ignored.h").write_text("generated", encoding="utf-8")
		(project / ".firmware-revision.lock").write_text("locked", encoding="utf-8")
		(project / "platformio.local.ini").write_text("OTA_PASSWORD=TEST_SENTINEL_DO_NOT_READ\n", encoding="utf-8")
		(project / "machine.secret").write_text("TEST_SENTINEL_DO_NOT_READ\n", encoding="utf-8")
		require(revision_module.source_fingerprint(project) == fingerprint_before_ignored,
			"ignored build outputs, lock, and local credential files must not affect source ID")

		git(project, "add", "firmware-revision.json")
		git(project, "commit", "-qm", "record revision state")
		require(revision_module.sync_revision(project) == first, "Git commit alone must not increment revision")

		(project / "src" / "sample.cpp").write_text("int sample() { return 2; }\n", encoding="utf-8")
		second = revision_module.sync_revision(project)
		require(second.revision == first.revision + 1, "tracked source content change must increment revision once")
		(project / "new-source.cpp").write_text("int new_source() { return 3; }\n", encoding="utf-8")
		third = revision_module.sync_revision(project)
		require(third.revision == second.revision + 1, "nonignored untracked source must participate in identity")

		(project / "web" / "index.html").write_text("<h1>revision fixture two</h1>\n", encoding="utf-8")
		fourth = revision_module.prepare_project(project)
		generated = (project / "src" / "webAssets.cpp").read_bytes()
		assets = revision_module.ensure_web_assets(project)
		require(not assets, "verified generated asset should not be rewritten on a second check")
		require(fourth.revision == third.revision + 1, "asset and HTML change must advance exactly one revision")
		packed_text = (project / "src" / "webAssets.cpp").read_text(encoding="utf-8")
		packed_array = packed_text.split("indexGzip[] = {", 1)[1].split("};", 1)[0]
		import re
		packed = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", packed_array))
		require(gzip.decompress(packed) == (project / "web" / "index.html").read_bytes(),
			"asset generation must finish before the content fingerprint is stored")
		require(revision_module.prepare_project(project) == fourth, "repeated sync after asset regeneration must stay stable")
		require((project / "src" / "webAssets.cpp").read_bytes() == generated,
			"a current generated asset must not be rewritten during later builds")

		build_dir = project / ".pio" / "build" / "c3"
		shutil.rmtree(build_dir.parent)
		revision_module.write_identity_header(c3_header, revision_module.FirmwareIdentity(fourth.revision, fourth.source_id, "c3"))
		require(revision_module.sync_revision(project) == fourth, "cleaned build outputs must not reset the tracked revision")

		binary = build_dir / "firmware.bin"
		stamp = build_dir / "firmware.bin.identity.json"
		binary.parent.mkdir(parents=True, exist_ok=True)
		binary.write_bytes(b"fixture firmware image")
		identity = revision_module.FirmwareIdentity(fourth.revision, fourth.source_id, "c3")
		class FakeNode:
			def __init__(self, path: Path):
				self.abspath = str(path)

		class FakeSConsEnv:
			def subst(self, value: str) -> str:
				require(value == "$BUILD_DIR/${PROGNAME}.bin", "upload callback requested the wrong SCons binary variable")
				return str(binary)

		build_action = revision_module.make_build_stamp_action(identity)
		build_action(target=[FakeNode(binary)], source=[], env=FakeSConsEnv())
		upload_action = revision_module.make_upload_verification_action(identity)
		upload_action(target=[], source=[], env=FakeSConsEnv())
		require(not revision_module.requires_fresh_build(binary, stamp, identity, ["upload"]),
			"matching image identity should not force an unnecessary build")
		(project / "src" / "sample.cpp").write_text("int sample() { return 9; }\n", encoding="utf-8")
		stale_state = revision_module.sync_revision(project)
		stale_identity = revision_module.FirmwareIdentity(stale_state.revision, stale_state.source_id, "c3")
		require(revision_module.requires_fresh_build(binary, stamp, stale_identity, ["upload"]),
			"normal upload must request a fresh build for stale identity")
		require(not revision_module.requires_fresh_build(binary, stamp, stale_identity, ["nobuild", "upload"]),
			"nobuild upload must leave the stale artifact for the fail-closed pre-upload guard")
		stale_upload_action = revision_module.make_upload_verification_action(stale_identity)
		try:
			stale_upload_action(target=[], source=[], env=FakeSConsEnv())
		except revision_module.StaleFirmwareArtifactError:
			pass
		else:
			raise AssertionError("stale nobuild upload image must be rejected")

		context = multiprocessing.get_context("spawn")
		with context.Pool(4) as pool:
			parallel_revisions = pool.map(sync_worker, [str(project)] * 4)
		require(parallel_revisions == [stale_state.revision] * 4,
			"concurrent environment syncs must serialize without extra increments")

	print("PASS source revision: initialization, stable builds/environments/commit, tracked and untracked edits, ignored outputs, asset regeneration, clean, stale upload, concurrent sync")


def hook_skip_case() -> None:
	"""Run both real PIO hooks through their native/clean early-return paths."""
	with tempfile.TemporaryDirectory(prefix="hook-skip-", dir=OUTPUT, ignore_cleanup_errors=True) as temporary:
		project = Path(temporary)
		state = project / "firmware-revision.json"
		asset = project / "src" / "webAssets.cpp"
		generated_dir = project / ".pio" / "build" / "c3" / "generated"
		generated_header = generated_dir / "firmware_identity.generated.h"
		for path, data in ((state, b"tracked revision bytes\r\n"), (asset, b"generated asset bytes\r\n"),
			(generated_header, b"environment header bytes\r\n")):
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_bytes(data)
		paths = (state, asset, generated_header)
		initial_bytes = {path: path.read_bytes() for path in paths}
		initial_generated_files = sorted(path.relative_to(generated_dir) for path in generated_dir.rglob("*"))
		script_names = ("SCons", "SCons.Script", "firmware_revision")
		missing = object()
		original_modules = {name: sys.modules.get(name, missing) for name in script_names}
		original_path = list(sys.path)
		original_dont_write = sys.dont_write_bytecode
		original_revision_module = original_modules["firmware_revision"]

		class SkipEnvironment:
			def __init__(self, platform: str):
				self.platform = platform
				self.substitutions = []

			def subst(self, value: str) -> str:
				self.substitutions.append(value)
				if value == "$PROJECT_DIR": return str(project)
				if value == "$PIOPLATFORM": return self.platform
				raise AssertionError("a skipped hook requested additional build settings")

		def execute_hook(script: Path, platform: str, targets: list[str]) -> None:
			from types import ModuleType

			package = ModuleType("SCons")
			package.__path__ = []
			scons_script = ModuleType("SCons.Script")
			scons_script.COMMAND_LINE_TARGETS = targets
			package.Script = scons_script
			sys.modules["SCons"] = package
			sys.modules["SCons.Script"] = scons_script
			environment = SkipEnvironment(platform)
			namespace = {
				"Import": lambda name: require(name == "env", "hook requested an unexpected SCons import"),
				"env": environment,
			}
			runpy.run_path(str(script), init_globals=namespace)
			expected_substitutions = ["$PROJECT_DIR", "$PIOPLATFORM"] if script.name == "pio_firmware_identity.py" else ["$PIOPLATFORM"]
			require(environment.substitutions == expected_substitutions,
				"a skipped hook accessed additional build settings")

		try:
			for platform, targets in (("native", ["upload"]), ("espressif32", ["clean"])):
				for script_name in ("pio_firmware_identity.py", "pio_firmware_identity_post.py"):
					execute_hook(ROOT / "tools" / script_name, platform, targets)
					require({path: path.read_bytes() for path in paths} == initial_bytes,
						f"{script_name} modified revision, asset, or generated identity state on a skip path")
					require(sorted(path.relative_to(generated_dir) for path in generated_dir.rglob("*")) == initial_generated_files,
						f"{script_name} changed generated-header files on a skip path")
				require(sys.path == original_path, f"{script_name} changed sys.path on a skip path")
				require(sys.dont_write_bytecode == original_dont_write,
					f"{script_name} changed bytecode-import behavior on a skip path")
				require(sys.modules.get("firmware_revision", missing) is original_revision_module,
					f"{script_name} imported the firmware identity module on a skip path")
		finally:
			path_unchanged = sys.path == original_path
			sys.path[:] = original_path
			sys.dont_write_bytecode = original_dont_write
			for name, module in original_modules.items():
				if module is missing:
					sys.modules.pop(name, None)
				else:
					sys.modules[name] = module
			require(path_unchanged, "hook skip test changed sys.path before cleanup")
	print("PASS PIO hook skips: native and clean targets leave revision, assets, generated headers, and Python imports unchanged")


def find_compiler() -> Path:
	candidates = [
		ROOT / ".pio" / "native-tools" / "winlibs-16.2.0-ucrt-r2" / "mingw64" / "bin" / "g++.exe",
		Path(shutil.which("g++")) if shutil.which("g++") else None,
	]
	for candidate in candidates:
		if candidate and candidate.is_file():
			return candidate
	raise RuntimeError("Native g++ was not found; expected the repository-local WinLibs compiler.")


def cpp_identity_case() -> None:
	OUTPUT.mkdir(parents=True, exist_ok=True)
	identity = revision_module.FirmwareIdentity(73, "a" * 64, "unit-test")
	header = OUTPUT / "firmware_identity.generated.h"
	revision_module.write_identity_header(header, identity)
	compiler = find_compiler()
	environment = os.environ.copy()
	environment["PATH"] = str(compiler.parent) + os.pathsep + environment.get("PATH", "")
	executable = OUTPUT / "firmware-identity-api.exe"
	command = [
		str(compiler), "-std=c++17", "-Wall", "-Wextra", "-Werror",
		"-I", str(OUTPUT), "-I", str(ROOT / "src"),
		str(ROOT / "src" / "firmwareIdentity.cpp"),
		str(Path(__file__).with_name("identity_api_main.cpp")),
		"-o", str(executable),
	]
	subprocess.run(command, cwd=ROOT, env=environment, check=True)
	subprocess.run([str(executable)], cwd=ROOT, env=environment, check=True)
	print("PASS C++ identity getters use the generated per-environment contract")


def scons_upload_graph_case() -> None:
	"""Exercise the actual SCons callback order without loading PlatformIO or uploading."""
	OUTPUT.mkdir(parents=True, exist_ok=True)
	scons_candidates = [
		Path.home() / ".platformio" / "packages" / "tool-scons" / "scons.py",
	]
	scons_script = next((path for path in scons_candidates if path.is_file()), None)
	if scons_script is None:
		print("SKIP SCons upload graph: installed local SCons launcher is unavailable")
		return

	with tempfile.TemporaryDirectory(prefix="scons-fixture-", dir=OUTPUT, ignore_cleanup_errors=True) as temporary:
		project = Path(temporary)
		scons_source = f'''from pathlib import Path
import os
from SCons.Script import COMMAND_LINE_TARGETS, Environment
from firmware_revision import FirmwareIdentity, register_firmware_identity_actions

root = Path.cwd()
binary = root / "firmware.bin"
stamp = root / "firmware.bin.identity.json"
identity = FirmwareIdentity(int(os.environ["FW_TEST_REVISION"]), os.environ["FW_TEST_SOURCE_ID"], "scons-test")
no_build = "nobuild" in COMMAND_LINE_TARGETS
env = Environment(tools=[], BUILD_DIR=str(root), PROGNAME="firmware", ENV={{"PATH": os.environ.get("PATH", "")}})
env._CacheDir_path = None
env.Decider("MD5")

def build_binary(target, source, env):
    count_path = root / "build-count.txt"
    count = int(count_path.read_text(encoding="ascii")) if count_path.exists() else 0
    count_path.write_text(str(count + 1), encoding="ascii")
    Path(str(target[0])).write_bytes(b"fixture-image")
    return 0

def fake_upload(target, source, env):
    count_path = root / "upload-count.txt"
    count = int(count_path.read_text(encoding="ascii")) if count_path.exists() else 0
    count_path.write_text(str(count + 1), encoding="ascii")
    return 0

if no_build:
    firmware = env.File(str(binary))
else:
    firmware = env.Command(str(binary), str(root / "input.txt"), build_binary)
upload = env.Alias("upload", firmware, fake_upload)
env.AlwaysBuild(upload)
env.Alias("nobuild")
register_firmware_identity_actions(env, binary, identity, COMMAND_LINE_TARGETS)
env.Default(firmware)
'''
		def initialize_project(path: Path) -> None:
			path.mkdir(parents=True, exist_ok=True)
			(path / "input.txt").write_text("stable SCons source\n", encoding="utf-8")
			(path / "SConstruct").write_text(scons_source, encoding="utf-8")

		initialize_project(project)
		nobuild_project = project / "nobuild-case"
		initialize_project(nobuild_project)
		tools_path = str(ROOT / "tools")
		base_environment = {key: os.environ[key] for key in ("PATH", "SYSTEMROOT", "WINDIR", "TEMP", "TMP") if key in os.environ}
		base_environment["PYTHONPATH"] = tools_path

		def invoke_scons(project_path: Path, revision: int, source_id: str, targets=None):
			process_environment = base_environment.copy()
			process_environment["FW_TEST_REVISION"] = str(revision)
			process_environment["FW_TEST_SOURCE_ID"] = source_id
			command = [sys.executable, str(scons_script)]
			if targets:
				command.extend(targets if isinstance(targets, list) else [targets])
			return subprocess.run(command, cwd=project_path, env=process_environment,
				stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False)

		first_id = "b" * 64
		first_build = invoke_scons(project, 1, first_id)
		require(first_build.returncode == 0 and (project / "firmware.bin.identity.json").is_file(),
			"default SCons build must run the binary post-action and write its identity stamp; " +
			f"stdout={first_build.stdout!r}; stderr={first_build.stderr!r}")
		first_upload = invoke_scons(project, 1, first_id, targets="upload")
		count_path = project / "upload-count.txt"
		first_count = count_path.read_text(encoding="ascii") if count_path.exists() else "missing"
		first_build_count = (project / "build-count.txt").read_text(encoding="ascii")
		require(first_upload.returncode == 0 and first_count == "1" and first_build_count == "1",
			"upload alias must verify its binary dependency before running the uploader action; " +
			f"upload_count={first_count!r}; build_count={first_build_count!r}; stdout={first_upload.stdout!r}; stderr={first_upload.stderr!r}")
		stamp_path = project / "firmware.bin.identity.json"
		stamp_path.unlink()
		missing_stamp_upload = invoke_scons(project, 1, first_id, targets="upload")
		missing_stamp_build_count = (project / "build-count.txt").read_text(encoding="ascii")
		missing_stamp_upload_count = count_path.read_text(encoding="ascii")
		require(missing_stamp_upload.returncode == 0 and missing_stamp_build_count == "2" and
			missing_stamp_upload_count == "2" and stamp_path.is_file(),
			"normal upload must rebuild an unchanged image whose identity stamp is missing")
		second_upload = invoke_scons(project, 2, "c" * 64, targets="upload")
		second_build_count = (project / "build-count.txt").read_text(encoding="ascii")
		require(second_upload.returncode == 0 and (project / "upload-count.txt").read_text(encoding="ascii") == "3" and second_build_count == "3",
			"normal upload must rebuild a stale image before verification and upload")
		nobuild_build = invoke_scons(nobuild_project, 3, "d" * 64)
		require(nobuild_build.returncode == 0 and (nobuild_project / "firmware.bin.identity.json").is_file(),
			"nobuild regression fixture must first have a valid baseline image")
		baseline_build_count = (nobuild_project / "build-count.txt").read_text(encoding="ascii")
		stale_no_build = invoke_scons(nobuild_project, 4, "e" * 64, targets=["nobuild", "upload"])
		nobuild_count = nobuild_project / "upload-count.txt"
		final_nobuild_count = (nobuild_project / "build-count.txt").read_text(encoding="ascii")
		require(stale_no_build.returncode != 0 and not nobuild_count.exists() and final_nobuild_count == baseline_build_count,
			"nobuild upload must stop at the stale identity guard before uploader action; " +
			f"upload_count={'present' if nobuild_count.exists() else 'absent'}; build_count={final_nobuild_count!r}; stdout={stale_no_build.stdout!r}; stderr={stale_no_build.stderr!r}")
	print("PASS SCons graph: default stamp, unchanged upload, missing-stamp repair, normal rebuild, stale nobuild blocked before uploader")


def main() -> None:
	python_cases()
	hook_skip_case()
	cpp_identity_case()
	scons_upload_graph_case()
	print("Native-only firmware identity checks passed; no PlatformIO or device behavior was exercised.")


if __name__ == "__main__":
	try:
		main()
	except Exception as error:
		print(f"FAIL {error}", file=sys.stderr)
		sys.exit(1)
