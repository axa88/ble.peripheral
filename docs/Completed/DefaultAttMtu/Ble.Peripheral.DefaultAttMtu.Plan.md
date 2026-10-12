# Phase 2 — Default ATT MTU 517 and Console Error Handling

Status: Implemented and validated; uncommitted for source review

## Objective

Make **517 bytes the default preferred ATT MTU** on both ESP32 targets and correct the existing console command to report whether an MTU change succeeded.

Preserve the existing dynamic MTU configuration. Preferred MTU is the local negotiation preference; it does not guarantee the peer negotiates 517.

**Already verified by the user:** Setting the preferred MTU to 517 through the console results in a negotiated MTU of 517 with the Windows client. No new negotiation investigation is required.

## 1. Change the startup preferred MTU to 517

Modify:

- `include/override/c3/nimble_app_config.h`
- `include/override/wrover/nimble_app_config.h`

Add to both files:

```cpp
#undef CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU
#define CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU 517
```

Continue using the existing PlatformIO forced-include mechanism, with SDK settings loaded before application overrides. Do not add redundant runtime initialization calls or modify installed NimBLE headers.

Update `src/NimBLEConfigChecks.cpp`:

- ESP32-C3: change expected MTU and its error message from 256 to 517.
- ESP32 WROVER: change expected MTU and its error message from 255 to 517.

Preserve all other override directives and configuration checks.

**Expected behavior:** Both devices start with preferred MTU 517; the existing `C` command can override it at runtime; rebooting restores 517.

## 2. Correct console MTU error handling

Modify only the existing `ConsoleMode::SetMtu` handler in `src/processInput.cpp`.

Currently, `NimBLEDevice::setMTU()` is called without checking its boolean return value, and success is always reported.

Change the handler to:

1. Check the return value of `NimBLEDevice::setMTU()`.
2. Report success only when the call succeeds.
3. On failure, report an error and the current preferred MTU from `NimBLEDevice::getMTU()`.
4. Preserve the input range of 23–517 and the existing command/interaction.
5. During numeric parsing, reject the input as soon as the accumulated value exceeds 517, before another multiplication. This prevents long numeric input from overflowing into the accepted range; do not refactor other parsers.

Example success:

```text
[CFG] Local preferred MTU set to 517
```

Do not retain the current “takes effect on next connection” wording: NimBLE also updates established connections that have not exchanged MTU yet. The message should report setter success without promising negotiation timing.

Example failure:

```text
[ERR] Failed to set local preferred MTU to 517
[CFG] Current local MTU=247 bytes
```

Do not introduce another command or change the existing `C` and `V` commands.

## 3. Build and verify

Build only:

```text
pio run -e c3
pio run -e wrover
```

Require both builds to succeed and all compile-time configuration checks to pass. No clean rebuild, dependency refresh, or broad test matrix is required unless a failure justifies it.

Review both setter-return branches. In the installed NimBLE version, setter failure is caused by out-of-range input, which the console already rejects. Exercise the false-return branch only if readily possible; otherwise record it as code-reviewed, not physically observed. Do not add failure-injection infrastructure.

For device verification, deploy through the existing OTA mechanism only, then perform these focused checks on each target:

1. After reboot, press `C` and confirm the displayed current preferred MTU is 517. No new startup logging is needed.
2. Enter 247, confirm the success message, then press `C` again and confirm 247.
3. Verify `22`, `518`, nonnumeric input, and an oversized numeric string are rejected without changing the preferred MTU. Complete each `C` interaction before starting the next.
4. Reboot and confirm `C` displays 517 again.
5. Perform a representative connect/read/write/notification smoke check; report any observed regression.

Keep compiled preferred MTU distinct from negotiated peer MTU. Windows negotiation at 517 has already been verified by the user and does not require a new investigation.

If manual checks cannot be observed, list them as pending and wait for the user's results. Stop further deployment if an unexplained material difference appears.

**Never build, clean, upload, test, or use `c3-com` or `wrover-com`; do not edit their environment sections.**

Use the existing checkout and branch, preserve unrelated changes, and leave implementation uncommitted for review unless separately authorized. Do not create branches/worktrees, push, or promote changes.

## 4. Out of scope

- Large GATT payload testing.
- New GATT characteristics, descriptors, or commands.
- Notification or indication changes.
- BLE buffer or memory tuning without evidence of a problem.
- Changes to negotiated-MTU reporting.
- Changes to advertising, pairing, security, or connection management.
- Changes to the Phase 1 configuration architecture or dependency versions.
- Persistent storage of the runtime-selected MTU.

## Completion criteria

Phase 2 is complete when:

1. Both targets default to preferred MTU 517 and their configuration assertions pass.
2. The dynamic MTU command accurately handles setter success/failure and rejects invalid input without changing the setting.
3. Runtime override and reboot restoration checks pass on both targets.
4. A focused connect/read/write/notification smoke check shows no regression.

Attribute manual results to the observer and distinguish reviewed branches from exercised branches.

**Expected implementation scope:** Four existing files, two small functional changes, and basic verification. This plan document is not an implementation change.

## Execution record (2026-10-13)

- Both target builds and unchanged non-MTU configuration checks passed with no compiler warnings. Root independently reviewed the four-file implementation.
- One OTA upload per target succeeded; each device replied to all three post-upload network checks.
- The user confirmed that both devices passed every listed manual check: startup preferred MTU 517, runtime override to 247, invalid-input rejection without changing the setting, reboot restoration to 517, and connect/read/write/notification smoke checks.
- Setter failure handling was code-reviewed only; it was not physically exercised. Manual results are user-reported.
- Existing dirty documentation changes were preserved. Implementation remains uncommitted for source review.
