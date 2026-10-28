"""Stable source revision and build-artifact identity helpers for PlatformIO."""

from __future__ import annotations

import argparse
from contextlib import contextmanager
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile
import time
from typing import Iterator, Optional


STATE_NAME = "firmware-revision.json"
LOCK_NAME = ".firmware-revision.lock"
SCHEMA_VERSION = 1
MAX_REVISION = 0xFFFFFFFF
_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


@dataclass(frozen=True)
class RevisionState:
	"""The project-wide monotonic revision and its source-content fingerprint."""

	revision: int
	source_id: str


@dataclass(frozen=True)
class FirmwareIdentity:
	"""The firmware-visible identity for one PlatformIO environment."""

	revision: int
	source_id: str
	environment: str


class RevisionError(RuntimeError):
	"""Raised when source identity state is invalid or cannot be updated safely."""


class StaleFirmwareArtifactError(RuntimeError):
	"""Raised when an upload would use a missing or out-of-date binary."""


def _is_excluded(relative: str) -> bool:
	path = PurePosixPath(relative.replace("\\", "/"))
	parts = tuple(part.casefold() for part in path.parts)
	name = path.name.casefold()
	if not parts:
		return True
	if relative.casefold() in {STATE_NAME, LOCK_NAME, "platformio.local.ini"}:
		return True
	if any(part in {".git", ".pio", "__pycache__"} for part in parts):
		return True
	if name.endswith((".pyc", ".pyo", ".tmp")):
		return True
	if name.startswith(".firmware-revision."):
		return True
	if any(token in part for part in parts for token in ("credential", "secret", "password", "token")):
		return True
	if name.endswith((".pem", ".key")):
		return True
	return False


def _git_paths(project_root: Path) -> list[str]:
	commands = (
		["git", "ls-files", "--cached", "-z"],
		["git", "ls-files", "--others", "--exclude-standard", "-z"],
	)
	paths: set[str] = set()
	for command in commands:
		try:
			result = subprocess.run(
				command,
				cwd=project_root,
				check=True,
				stdout=subprocess.PIPE,
				stderr=subprocess.PIPE,
			)
		except (OSError, subprocess.CalledProcessError) as error:
			raise RevisionError("Unable to enumerate tracked and nonignored project files with Git.") from error
		for raw in result.stdout.split(b"\0"):
			if raw:
				paths.add(os.fsdecode(raw).replace("\\", "/"))
	return sorted(path for path in paths if not _is_excluded(path))


def source_fingerprint(project_root: Path) -> str:
	"""Hash path names and content for tracked and nonignored project files.

	The Git commit, ignored files, local credential config, revision state, and
	generated build outputs are deliberately outside the fingerprint.
	"""
	root = project_root.resolve()
	digest = hashlib.sha256()
	digest.update(b"ble-peripheral-source-fingerprint-v1\0")
	for relative in _git_paths(root):
		encoded_path = relative.encode("utf-8", errors="surrogateescape")
		digest.update(encoded_path)
		digest.update(b"\0")
		path = root.joinpath(*PurePosixPath(relative).parts)
		try:
			if path.is_symlink():
				digest.update(b"L\0")
				digest.update(os.fsencode(os.readlink(path)))
				digest.update(b"\0")
			elif not path.is_file():
				digest.update(b"MISSING\0")
			else:
				digest.update(b"F\0")
				with path.open("rb") as stream:
					while True:
						chunk = stream.read(1024 * 1024)
						if not chunk:
							break
						digest.update(chunk)
				digest.update(b"\0")
		except OSError as error:
			raise RevisionError(f"Unable to fingerprint project input: {relative}") from error
	return digest.hexdigest()


@contextmanager
def _exclusive_lock(lock_path: Path) -> Iterator[None]:
	lock_path.parent.mkdir(parents=True, exist_ok=True)
	with lock_path.open("a+b") as stream:
		stream.seek(0, os.SEEK_END)
		if stream.tell() == 0:
			stream.write(b"\0")
			stream.flush()
		fd = stream.fileno()
		if os.name == "nt":
			import msvcrt

			deadline = time.monotonic() + 30
			while True:
				stream.seek(0)
				try:
					msvcrt.locking(fd, msvcrt.LK_NBLCK, 1)
					break
				except OSError as error:
					if time.monotonic() >= deadline:
						raise RevisionError("Firmware revision lock remained busy for 30 seconds.") from error
					time.sleep(0.05)
			try:
				yield
			finally:
				stream.seek(0)
				msvcrt.locking(fd, msvcrt.LK_UNLCK, 1)
		else:
			import fcntl

			fcntl.flock(fd, fcntl.LOCK_EX)
			try:
				yield
			finally:
				fcntl.flock(fd, fcntl.LOCK_UN)


def _atomic_write(path: Path, content: bytes) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	if path.is_file() and path.read_bytes() == content:
		return
	fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
	try:
		with os.fdopen(fd, "wb") as stream:
			stream.write(content)
			stream.flush()
			os.fsync(stream.fileno())
		os.replace(temporary, path)
	finally:
		if os.path.exists(temporary):
			os.unlink(temporary)


def _read_state(state_path: Path) -> tuple[int, Optional[str]]:
	if not state_path.exists():
		return 0, None
	try:
		value = json.loads(state_path.read_text(encoding="utf-8"))
	except (OSError, UnicodeError, json.JSONDecodeError) as error:
		raise RevisionError("Firmware revision state is unreadable; refusing to reset it.") from error
	if not isinstance(value, dict) or value.get("schemaVersion") != SCHEMA_VERSION:
		raise RevisionError("Firmware revision state has an unsupported schema.")
	revision = value.get("revision")
	fingerprint = value.get("contentFingerprint")
	if isinstance(revision, bool) or not isinstance(revision, int) or not 0 <= revision <= MAX_REVISION:
		raise RevisionError("Firmware revision state has an invalid revision number.")
	if fingerprint is not None and (not isinstance(fingerprint, str) or not _SHA256_RE.fullmatch(fingerprint)):
		raise RevisionError("Firmware revision state has an invalid content fingerprint.")
	if (revision == 0) != (fingerprint is None):
		raise RevisionError("Firmware revision state is partially initialized; refusing to change it.")
	return revision, fingerprint


def _sync_revision_locked(root: Path) -> RevisionState:
	state_path = root / STATE_NAME
	fingerprint = source_fingerprint(root)
	old_revision, old_fingerprint = _read_state(state_path)
	if old_fingerprint is None:
		revision = 1
	elif old_fingerprint == fingerprint:
		revision = old_revision
	else:
		if old_revision >= MAX_REVISION:
			raise RevisionError("Firmware revision number is exhausted.")
		revision = old_revision + 1
	state = {
		"schemaVersion": SCHEMA_VERSION,
		"revision": revision,
		"contentFingerprint": fingerprint,
	}
	serialized = (json.dumps(state, indent=2, sort_keys=True) + "\n").replace("\n", "\r\n").encode("utf-8")
	_atomic_write(state_path, serialized)
	return RevisionState(revision, fingerprint)


def sync_revision(project_root: Path) -> RevisionState:
	"""Advance once when project content differs from the last synchronized state."""
	root = project_root.resolve()
	with _exclusive_lock(root / LOCK_NAME):
		return _sync_revision_locked(root)

def current_identity(project_root: Path, environment: str) -> FirmwareIdentity:
	"""Load the already-synchronized identity without changing project state."""
	if not re.fullmatch(r"[A-Za-z0-9_.-]+", environment):
		raise RevisionError("PlatformIO environment name cannot be represented safely in firmware identity.")
	revision, fingerprint = _read_state(project_root.resolve() / STATE_NAME)
	if fingerprint is None or revision < 1:
		raise RevisionError("Firmware identity has not been synchronized before the post-build hook.")
	return FirmwareIdentity(revision, fingerprint, environment)


def _cpp_string(value: str) -> str:
	return json.dumps(value, ensure_ascii=True)


def write_identity_header(path: Path, identity: FirmwareIdentity) -> None:
	if not _SHA256_RE.fullmatch(identity.source_id):
		raise RevisionError("Cannot generate identity header with a malformed source ID.")
	if not re.fullmatch(r"[A-Za-z0-9_.-]+", identity.environment):
		raise RevisionError("PlatformIO environment name cannot be represented safely in the firmware header.")
	if not 1 <= identity.revision <= MAX_REVISION:
		raise RevisionError("Cannot generate identity header with an invalid revision.")
	short_id = identity.source_id[:12]
	summary = f"Firmware revision {identity.revision} | source {short_id} | environment {identity.environment}"
	content = (
		"#pragma once\n"
		f"#define BLE_FIRMWARE_REVISION {identity.revision}U\n"
		f"#define BLE_FIRMWARE_SOURCE_ID {_cpp_string(identity.source_id)}\n"
		f"#define BLE_FIRMWARE_ENVIRONMENT {_cpp_string(identity.environment)}\n"
		f"#define BLE_FIRMWARE_SUMMARY {_cpp_string(summary)}\n"
	).encode("utf-8")
	_atomic_write(path, content)


def _sha256_file(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		while True:
			chunk = stream.read(1024 * 1024)
			if not chunk:
				break
			digest.update(chunk)
	return digest.hexdigest()


def _stamp_bytes(binary_path: Path, identity: FirmwareIdentity) -> bytes:
	if not binary_path.is_file():
		raise StaleFirmwareArtifactError("Firmware image is missing; build before upload.")
	stamp = {
		"schemaVersion": 1,
		"revision": identity.revision,
		"sourceId": identity.source_id,
		"environment": identity.environment,
		"firmwareSha256": _sha256_file(binary_path),
	}
	return (json.dumps(stamp, indent=2, sort_keys=True) + "\n").encode("utf-8")


def write_artifact_stamp(binary_path: Path, stamp_path: Path, identity: FirmwareIdentity) -> None:
	"""Record identity and content hash after the PlatformIO image is linked."""
	_atomic_write(stamp_path, _stamp_bytes(binary_path, identity))


def verify_artifact_stamp(binary_path: Path, stamp_path: Path, identity: FirmwareIdentity) -> None:
	"""Reject an upload if its image predates the current source identity."""
	if not binary_path.is_file() or not stamp_path.is_file():
		raise StaleFirmwareArtifactError("Firmware identity stamp is missing; run a normal build before upload.")
	try:
		stamp = json.loads(stamp_path.read_text(encoding="utf-8"))
	except (OSError, UnicodeError, json.JSONDecodeError) as error:
		raise StaleFirmwareArtifactError("Firmware identity stamp is unreadable; refusing upload.") from error
	expected = {
		"schemaVersion": 1,
		"revision": identity.revision,
		"sourceId": identity.source_id,
		"environment": identity.environment,
		"firmwareSha256": _sha256_file(binary_path),
	}
	if stamp != expected:
		raise StaleFirmwareArtifactError("Firmware image does not match current source identity; run a normal build before upload.")


def requires_fresh_build(
	binary_path: Path,
	stamp_path: Path,
	identity: FirmwareIdentity,
	command_targets: list[str],
) -> bool:
	"""Force a normal build when the image lacks a valid current identity stamp."""
	if "nobuild" in command_targets:
		return False
	try:
		verify_artifact_stamp(binary_path, stamp_path, identity)
		return False
	except StaleFirmwareArtifactError:
		return True


def make_build_stamp_action(identity: FirmwareIdentity):
	"""Create a SCons post-action with the callback's required keyword names."""
	def action(target, source, env):
		if not target:
			raise RevisionError("PlatformIO completed without an application binary target.")
		node = target[0]
		binary = Path(getattr(node, "abspath", str(node))).resolve()
		stamp = binary.with_suffix(binary.suffix + ".identity.json")
		write_artifact_stamp(binary, stamp, identity)
		return 0
	return action


def make_upload_verification_action(identity: FirmwareIdentity):
	"""Create a SCons pre-action that verifies the final upload source node."""
	def action(target, source, env):
		binary = Path(env.subst("$BUILD_DIR/${PROGNAME}.bin")).resolve()
		stamp = binary.with_suffix(binary.suffix + ".identity.json")
		verify_artifact_stamp(binary, stamp, identity)
		return 0
	return action


def register_firmware_identity_actions(env, binary_path: Path, identity: FirmwareIdentity, command_targets: list[str]) -> None:
	"""Register post-build stamping and fail-closed upload checks on final SCons nodes."""
	binary = Path(binary_path).resolve()
	no_build = "nobuild" in command_targets
	if not no_build:
		stamp = binary.with_suffix(binary.suffix + ".identity.json")
		if requires_fresh_build(binary, stamp, identity, command_targets):
			env.AlwaysBuild(env.File(str(binary)))
		env.AddPostAction(str(binary), make_build_stamp_action(identity))
	upload = env.Alias("upload")
	if not upload:
		raise RevisionError("PlatformIO did not create its upload target before the identity post-hook.")
	env.AddPreAction(upload, make_upload_verification_action(identity))


def ensure_web_assets(project_root: Path, python_executable: str = sys.executable) -> bool:
	"""Verify the gzip asset and regenerate only when it differs from web/index.html."""
	root = project_root.resolve()
	generator = root / "tools" / "generate-web-assets.py"
	if not generator.is_file():
		raise RevisionError("Web asset generator is missing.")
	check = subprocess.run([python_executable, str(generator), "--check"], cwd=root, check=False)
	if check.returncode == 0:
		return False
	generate = subprocess.run([python_executable, str(generator)], cwd=root, check=False)
	if generate.returncode != 0:
		raise RevisionError("Web asset generation failed.")
	verify = subprocess.run([python_executable, str(generator), "--check"], cwd=root, check=False)
	if verify.returncode != 0:
		raise RevisionError("Generated Web asset did not pass verification.")
	return True


def prepare_project(project_root: Path, python_executable: str = sys.executable) -> RevisionState:
	"""Prepare generated assets before syncing source identity."""
	root = project_root.resolve()
	with _exclusive_lock(root / LOCK_NAME):
		ensure_web_assets(root, python_executable)
		return _sync_revision_locked(root)


def _main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("command", choices=("prepare",))
	parser.add_argument("--project-root", default=".")
	args = parser.parse_args()
	state = prepare_project(Path(args.project_root))
	print(f"Firmware source identity prepared: revision {state.revision}, source {state.source_id}")
	return 0


if __name__ == "__main__":
	try:
		sys.exit(_main())
	except RevisionError as error:
		print(f"Firmware identity error: {error}", file=sys.stderr)
		sys.exit(1)
