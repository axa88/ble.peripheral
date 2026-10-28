"""PlatformIO pre-hook for stable firmware identity and upload verification."""

from pathlib import Path
import sys

Import("env")
from SCons.Script import COMMAND_LINE_TARGETS


project_root = Path(env.subst("$PROJECT_DIR")).resolve()
if env.subst("$PIOPLATFORM").casefold() != "native" and "clean" not in COMMAND_LINE_TARGETS:
	sys.dont_write_bytecode = True
	tools_dir = project_root / "tools"
	if str(tools_dir) not in sys.path:
		sys.path.insert(0, str(tools_dir))

	from firmware_revision import (
		FirmwareIdentity,
		prepare_project,
		write_identity_header,
	)

	environment = env.subst("$PIOENV")
	if not environment:
		raise RuntimeError("PlatformIO did not provide PIOENV for firmware identity.")

	state = prepare_project(project_root, sys.executable)
	identity = FirmwareIdentity(state.revision, state.source_id, environment)
	env["BLE_FIRMWARE_PREPARED_IDENTITY"] = identity

	build_dir = Path(env.subst("$BUILD_DIR")).resolve()
	header_dir = build_dir / "generated"
	header_path = header_dir / "firmware_identity.generated.h"
	write_identity_header(header_path, identity)
	env.Append(CPPPATH=[str(header_dir)])
	print(f"Firmware source identity: revision {identity.revision}, source {identity.source_id}, environment {identity.environment}.")
