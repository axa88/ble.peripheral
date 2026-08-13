"""
Pre-build script: applies the application-specific NimBLE configuration
to the installed NimBLE-Arduino nimconfig.h for the current PlatformIO
environment.

The installed NimBLE header remains the baseline. This script removes a
previous generated application block, inserts one include for the selected
application fragment before nimconfig_rename.h, and atomically replaces the
generated dependency copy. The dependency header is therefore never a
checked-in source of configuration defaults.

Application fragments live in:
    include/override/<group>/nimble_app_config.h

Only the base firmware environments are mapped here. The *-com environments
exist for application control and are intentionally left untouched.
"""

import os
import re
import tempfile
from pathlib import Path

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)


ENV_TO_GROUP = {
    "c3": "c3",
    "wrover": "wrover",
}

BLOCK_BEGIN = b"/* ble.peripheral: begin application NimBLE config */"
BLOCK_END = b"/* ble.peripheral: end application NimBLE config */"
NIMCONFIG_ANCHOR = b'#include "nimconfig_rename.h"'


def _line_ending(data):
    if b"\r\n" in data:
        return b"\r\n"
    if b"\n" in data:
        return b"\n"
    raise RuntimeError("NimBLE nimconfig.h has no line endings")


def _remove_generated_block(data):
    begin_count = data.count(BLOCK_BEGIN)
    end_count = data.count(BLOCK_END)

    if begin_count != end_count:
        raise RuntimeError(
            "NimBLE nimconfig.h contains an incomplete generated application block"
        )

    if begin_count == 0:
        return data

    block_pattern = re.compile(
        re.escape(BLOCK_BEGIN)
        + rb".*?"
        + re.escape(BLOCK_END)
        + rb"(?:\r\n|\n)?",
        re.DOTALL,
    )
    cleaned, removed_count = block_pattern.subn(b"", data)
    if removed_count != begin_count:
        raise RuntimeError(
            "NimBLE nimconfig.h contains a malformed generated application block"
        )
    return cleaned


def _render_nimconfig(data, include_name):
    newline = _line_ending(data)
    cleaned = _remove_generated_block(data)

    anchor_positions = [
        match.start()
        for match in re.finditer(re.escape(NIMCONFIG_ANCHOR), cleaned)
    ]
    if len(anchor_positions) != 1:
        raise RuntimeError(
            'Expected exactly one #include "nimconfig_rename.h" anchor in '
            "NimBLE nimconfig.h"
        )

    block = newline.join(
        (
            BLOCK_BEGIN,
            f'#include "{include_name}"'.encode("ascii"),
            BLOCK_END,
            b"",
        )
    )
    anchor = anchor_positions[0]
    return cleaned[:anchor] + block + cleaned[anchor:]


def _atomic_write(path, data):
    temporary_path = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb",
            dir=path.parent,
            prefix=f".{path.name}.",
            suffix=".tmp",
            delete=False,
        ) as temporary:
            temporary_path = Path(temporary.name)
            temporary.write(data)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.replace(temporary_path, path)
    finally:
        if temporary_path is not None and temporary_path.exists():
            temporary_path.unlink()


def restore_nimconfig(target=None, source=None, env=env):
    project_dir = Path(env.subst("$PROJECT_DIR"))
    pioenv = env.subst("$PIOENV")

    group = ENV_TO_GROUP.get(pioenv)
    if group is None:
        print(
            f"[nimconfig] Environment '{pioenv}' is application-control-only; "
            "leaving NimBLE nimconfig.h unchanged."
        )
        return

    src_file = (
        project_dir
        / "include"
        / "override"
        / group
        / "nimble_app_config.h"
    )
    dst_dir = project_dir / ".pio" / "libdeps" / pioenv / "NimBLE-Arduino" / "src"
    dst_file = dst_dir / "nimconfig.h"

    if not src_file.is_file():
        raise RuntimeError(f"[nimconfig] Application fragment not found: {src_file}")

    if not dst_dir.is_dir():
        print(f"[nimconfig] Target dir not found (library not yet installed?): {dst_dir}")
        return

    if not dst_file.is_file():
        raise RuntimeError(f"[nimconfig] NimBLE nimconfig.h not found: {dst_file}")

    original = dst_file.read_bytes()
    include_name = os.path.relpath(src_file, dst_dir).replace(os.sep, "/")
    generated = _render_nimconfig(original, include_name)

    if generated != original:
        _atomic_write(dst_file, generated)

    print(f"[nimconfig] Applied '{group}' application config -> {dst_file}")


env.AddPreAction("buildprog", restore_nimconfig)
restore_nimconfig(env=env)
