"""Register artifact stamping and upload checks after the platform builder."""

from pathlib import Path
import sys

Import("env")
from SCons.Script import COMMAND_LINE_TARGETS


if env.subst("$PIOPLATFORM").casefold() != "native" and "clean" not in COMMAND_LINE_TARGETS:
	sys.dont_write_bytecode = True
	project_root = Path(env.subst("$PROJECT_DIR")).resolve()
	tools_dir = project_root / "tools"
	if str(tools_dir) not in sys.path:
		sys.path.insert(0, str(tools_dir))

	from firmware_revision import (
		RevisionError,
		current_identity,
		register_firmware_identity_actions,
	)

	environment = env.subst("$PIOENV")
	if not environment:
		raise RevisionError("PlatformIO did not provide PIOENV for upload verification.")
	identity = env.get("BLE_FIRMWARE_PREPARED_IDENTITY")
	if identity is None or identity != current_identity(project_root, environment):
		raise RevisionError("Firmware source identity changed during build setup; refusing a mixed-source build.")
	binary_path = Path(env.subst("$BUILD_DIR/${PROGNAME}.bin")).resolve()
	if not binary_path.is_absolute():
		raise RevisionError("PlatformIO did not resolve the firmware binary path.")
	# The platform builder has finalized PROGNAME and the upload alias by this point.
	register_firmware_identity_actions(env, binary_path, identity, list(COMMAND_LINE_TARGETS))
