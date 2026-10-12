# Ble.Peripheral — NimBLE Configuration Migration

Version: 21  
Status: Completed; source review and manual check confirmed by user on 2026-10-12  
Workspace: `C:\Git\esp-ble\ble.peripheral`  
Scope: ESP32-C3 and ESP32 WROVER  
Deployment: OTA only  
Implementation worker: GPT-6 Luna, reasoning effort Max (`gpt-6-luna`, `max`)

## 1. Objective and execution boundaries

Replace `scripts/configure_nimble.py`, which patches the installed NimBLE-Arduino `nimconfig.h`, with the existing project-owned configuration headers supplied through PlatformIO `build_flags`.

Preserve existing NimBLE settings and observable BLE behavior. Do not change MTU, advertising functionality, security behavior, dependency versions, or unrelated application code. Do not introduce another configuration abstraction or merge the two target headers during this migration.

The root agent delegates implementation to one GPT-6 Luna Max subagent and reviews its diff and evidence. When spawning the worker, select `gpt-6-luna` with `max` reasoning and provide this plan and the exact workspace explicitly. Read applicable repository instructions first. Work in the existing checkout; do not create or switch branches or create worktrees without explicit authorization. Preserve unrelated changes. If a question requires a user answer, wait for the answer before continuing the dependent work.

Use only `c3` and `wrover`. Do not run build, clean, or upload commands for `c3-com` or `wrover-com`, and do not edit their environment sections. Their effective configuration will change through inheritance; their stale dependency copies remain outside this migration. Before future use, those environments need fresh stock NimBLE dependencies and a rebuild.

Do not use serial flashing or direct flash operations. Serial observation, if already available, is not a flashing operation.

## 2. Establish the baseline

### Fix dependency selection without upgrading

Record the current branch, HEAD, working-tree changes, and original contents of the files that will change. Preserve a source rollback copy outside the build/dependency directories being deleted later. Do not require a checkpoint commit.

The current `platform = espressif32` does not explicitly select the intended PIOArduino release. The installed PIOArduino package metadata records this URL:

```ini
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.38-1/platform-espressif32.zip
```

Confirm the installed package metadata still identifies that release, then pin this URL in `[env]` before the baseline builds. Keep it unchanged for the migrated builds. This makes the existing dependency selection explicit; it is not an upgrade. If package identity or baseline build versions disagree, investigate before proceeding rather than changing versions to make the migration pass.

Expected baseline:

| Component | Version or selection |
| --- | --- |
| PIOArduino | Release `55.03.38-1`; platform version `55.03.38` / `55.3.38` |
| Arduino-ESP32 | `3.3.8` |
| ESP-IDF | `5.5.4` |
| NimBLE-Arduino | `2.5.1` |
| C3 SDK variant | `qio_qspi` |
| WROVER SDK variant | `dio_qspi` |

The release's [official notes](https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.38-1) confirm the Arduino and IDF versions. Verify the actual build selection, not just the presence of installed packages.

### Add small configuration regression checks

Keep `configure_nimble.py` active. Create `src/NimBLEConfigChecks.cpp` and explicitly include `<nimconfig.h>` before checking macros. This checks the final configuration after SDK settings, overrides, compatibility normalization, and NimBLE defaults.

Use compile-time assertions and `#error` checks; add no runtime code. Select target-specific checks using the SDK target macros and reject unexpected targets. Account for settings that are undefined rather than defined as zero.

| Setting | C3 | WROVER |
| --- | --- | --- |
| Central and observer roles | `0` | `0` |
| Peripheral and broadcaster roles | `1` | `1` |
| Legacy central/observer aliases | Undefined | Undefined |
| Uppercase address formatting | Enabled | Enabled |
| Return-code diagnostic text | Enabled | Enabled |
| Extended advertising | Enabled | Disabled |
| Maximum extended advertising instances | `4` | Not enabled |
| Transport event size | `257` | `70` |
| GAP-event diagnostic text | Enabled | Undefined |
| Preferred ATT MTU | `256` | `255` |
| Periodic advertising | Preserve baseline | Disabled |
| Legacy pairing, secure connections, NVS persistence | Enabled | Enabled |

Confirm expectations against the original headers. If an assertion fails, investigate the baseline rather than changing configuration to satisfy it. Record C3's actual periodic-advertising setting and check it unchanged after migration.

Build the original injection method:

```text
pio run -e c3 -t clean
pio run -e c3 -v
pio run -e wrover -t clean
pio run -e wrover -v
```

Both builds must pass the checks. Record resolved versions and SDK variants. Archive each successful baseline `firmware.bin`, its SHA-256 hash, the patched `nimconfig.h`, and the existing OTA uploader command privately outside the directories deleted in section 4. Do not commit firmware, credentials, or verbose build logs.

### Record focused baseline behavior

For each device, record representative advertising/scan-response data, GATT discovery, characteristic read/write/notification, connection/reconnection, negotiated MTU, and bonding where used. For C3, include use of multiple advertising instances. Record relevant Windows pairing and device bonding conditions and any application MTU override used during testing.

Identify the running firmware where possible. Do not automatically reflash a baseline merely because its identity is uncertain. Record that limitation; use the archived baseline through OTA if a controlled comparison becomes necessary. An unknown running image cannot support a definitive before/after equivalence claim.

## 3. Replace configuration injection

First inspect project source and dependencies actually compiled for uses of `CONFIG_BT_NIMBLE_*`, `CONFIG_NIMBLE_ROLE_*`, and `CONFIG_NIMBLE_CPP_*`. Resolve conditional-compilation concerns caused by earlier macro visibility. Keep this inspection focused; no comprehensive framework audit is required.

Retain:

```text
include/override/c3/nimble_app_config.h
include/override/wrover/nimble_app_config.h
```

Immediately after `#pragma once` in each header, add:

```cpp
#include <sdkconfig.h>
```

Update script-related comments. Preserve every existing `#undef` and `#define` directive unchanged. The intended order is SDK settings, application overrides, then NimBLE normalization/defaults. Do not replace the overrides with plain `-D` options.

In `platformio.ini`, remove:

```ini
extra_scripts = pre:scripts/configure_nimble.py
```

Retain:

```ini
lib_deps = h2zero/NimBLE-Arduino@2.5.1
```

Append to the existing `c3` build flags:

```ini
-include include/override/c3/nimble_app_config.h
```

Append to the existing `wrover` build flags:

```ini
-include include/override/wrover/nimble_app_config.h
```

Preserve unrelated build flags, settings, and OTA configuration. Delete `scripts/configure_nimble.py` and remove its remaining obsolete references as part of this edit. Retain the configuration-check source.

PlatformIO documents this forced-include mechanism in its [build flags reference](https://docs.platformio.org/en/latest/projectconf/sections/env/options/build/build_flags.html). It affects source compiled by the project; it does not rebuild or reconfigure precompiled SDK libraries.

## 4. Build and verify the migration

After confirming the baseline archive is safe, delete only:

```text
.pio/libdeps/c3
.pio/libdeps/wrover
.pio/build/c3
.pio/build/wrover
```

Verify resolved paths remain inside this workspace before recursive deletion. Preserve other environments and the baseline archive. Allow PlatformIO to reinstall stock NimBLE-Arduino 2.5.1.

Run:

```text
pio run -e c3 -v
pio run -e wrover -v
```

Require:

- Both builds succeed and the configuration checks pass unchanged.
- Resolved dependencies and SDK variants match the baseline.
- Compiler commands for both NimBLE C and C++ sources receive the correct target's `-include` flag.
- Existing override directives are preserved, and no relevant new compiler warnings appear.
- Each installed `nimconfig.h` matches the stock header from the same 2.5.1 library package and contains no generated application patch block. Use the cached or official package header; do not reconstruct patch removal or write a custom comparison framework.

The main risk is earlier and broader macro visibility. Successful application assertions alone do not prove the library received the flags; inspect the actual library compiler commands.

## 5. Verify both devices through OTA

After successful builds, upload the migrated firmware through each device's existing OTA mechanism. Confirm successful upload, boot, Wi-Fi connectivity, and continued OTA availability.

Repeat the focused baseline BLE checks using comparable peer, pairing, bonding, advertising, and application MTU conditions. Distinguish the compiled preferred MTU from the negotiated runtime MTU. No exhaustive advertising/security matrix or raw NVS restoration is required.

If the subagent cannot perform a physical check, hand the root agent the exact image, hash, and remaining checks. Mark that check pending; do not claim hardware acceptance without observations. The root agent handles manual testing with the user where needed.

Stop deployment at the first unexplained material difference. Investigate only the affected path. If needed, use the archived baseline for controlled OTA comparison, inspect affected preprocessed source or compiled objects, or inspect compiler/linker diagnostics. Do not perform these diagnostics preemptively. A baseline worktree is troubleshooting-only and requires explicit authorization.

For device rollback, upload the exact archived baseline image through the preserved OTA command while OTA remains available. Restore the saved source configuration separately if reverting the migration. Do not substitute a later rebuild for the archived image. Firmware that loses boot/network functionality may need separate recovery; serial recovery remains outside this plan.

## 6. Review and acceptance

The worker returns the scoped diff and a short record of baseline/migrated builds, resolved versions, configuration checks, stock-header verification, OTA results, and BLE observations or pending limitations. The root independently reviews the diff and evidence. Commit only when authorized by the execution request or subsequent review; do not push or promote branches as part of this plan.

The migration is complete when both target builds and unchanged configuration checks pass, NimBLE's installed source remains stock, existing settings are preserved, both devices retain OTA, focused BLE checks show no unexplained change, and the patch script and its references are removed.

Do not repeat builds merely because an already-unreferenced script was deleted. Repeat affected validation only after changes to compilation inputs or when a failure requires it. No separate audit report, general configuration redesign, dependency upgrade, or MTU change is required.

### Version 21 execution closeout (2026-10-12)

- Baseline and migrated `c3` and `wrover` builds succeeded with Espressif32 55.3.38, Arduino 3.3.8, ESP-IDF 5.5.4, and NimBLE-Arduino 2.5.1. SDK variants were `qio_qspi` for C3 and `dio_qspi` for WROVER. Compile-time configuration checks passed, NimBLE C/C++ compiler commands received the target forced include, and installed `nimconfig.h` matched the official stock 2.5.1 header.
- One OTA upload per target succeeded. Post-upload network checks and OTA challenge probes responded. Verbose builds compiled and linked but failed in SCons final firmware packaging/output with a `TypeError`; nonverbose builds completed successfully and supplied the preserved images.
- On 2026-10-12, the user confirmed that the code was reviewed and the manual check was confirmed. This is user-reported confirmation; detailed BLE observations were not captured. Baseline BLE observations were skipped, so no captured before/after equivalence claim is made.
- `c3-com` and `wrover-com` were not built, cleaned, uploaded, or edited. Before future use, refresh their NimBLE dependencies to stock and rebuild them.
- The private evidence note and retained images, source rollback copies, headers, and logs remain in the external v21 archive.
