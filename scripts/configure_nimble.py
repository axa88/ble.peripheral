"""
Pre-build script: ensures the correct nimconfig.h override is in place
for the current PlatformIO environment.

This runs on every build (Run, Clean, etc.) so that even if PlatformIO
re-fetches/overwrites the NimBLE-Arduino library (e.g. after a clean or
a `pio pkg update`), our project-specific config is restored before
compilation.

Source-of-truth files live in:
    include/override/<group>/nimconfig.h

Mapping from PIOENV -> group is defined in ENV_TO_GROUP below.
"""

import shutil
from pathlib import Path

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)


def restore_nimconfig(target=None, source=None, env=env):
    project_dir = Path(env.subst("$PROJECT_DIR"))
    pioenv = env.subst("$PIOENV")

    # Map each PlatformIO environment to its override group.
    env_to_group = {
        "c3": "c3",
        "c3-com": "c3",
        "wrover": "wrover",
        "wrover-com": "wrover",
    }

    group = env_to_group.get(pioenv)
    if group is None:
        print(f"[nimconfig] No override mapping for env '{pioenv}', skipping.")
        return

    src_file = project_dir / "include" / "override" / group / "nimconfig.h"
    dst_dir = project_dir / ".pio" / "libdeps" / pioenv / "NimBLE-Arduino" / "src"
    dst_file = dst_dir / "nimconfig.h"

    if not src_file.is_file():
        print(f"[nimconfig] Source override not found: {src_file}")
        return

    if not dst_dir.is_dir():
        print(f"[nimconfig] Target dir not found (library not yet installed?): {dst_dir}")
        return

    shutil.copyfile(src_file, dst_file)
    print(f"[nimconfig] Applied '{group}' override -> {dst_file}")


env.AddPreAction("buildprog", restore_nimconfig)
restore_nimconfig(env=env)
