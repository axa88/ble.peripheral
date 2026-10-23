# Web Interface v1.4 Completion Record

**Record status:** Software closeout passed on the final reviewed source. On 2026-10-23, the user reported physical acceptance completed on both boards since feedback-05. No detailed per-case results or tested-image provenance were supplied. No separate device verification is recorded for the two later closeout fixes or current rebuilt images; see below.

## Scope

The v1.4 implementation adds four selectable interface modes, pairing state and coordination independent of serial input, shared BLE operations, an authenticated HTTP interface, bounded deferred operations, and a browser UI for advertising, pairing, bonds, connections, and link settings.

The latest browser feedback-05 update uses device-reported state for the advertising and restart controls, folds Overview and Operation into disclosures, and shows queued work as applying until the device reports its result.

The closeout removed unused APIs and accessors plus the obsolete separate directed-target cache. Bond deletion now reconciles stale directed targets and stops only affected directed advertising while retaining truthful active state on a failed stop. Fixed-length passkey and confirmation input now drains the rest of its serial line so trailing bytes cannot become menu commands.

## Software validation

These results apply to the final reviewed source snapshot. They validate software and builds, not physical acceptance.

| Snapshot | Recorded result | Limit |
| --- | --- | --- |
| Final closeout matrix and software checks | All 10 firmware builds passed with 32 source-input gates using PlatformIO Core 6.1.19. Native pairing-state tests passed 8/8; web validation passed 60/60; Node browser regression, inline-script syntax, embedded assets, and advertising-stop static checks passed. The embedded page is 12,090 gzip bytes and 44,096 decompressed HTML bytes. Root verified `.pio/web-interface-v1.4/closeout/validation-manifest.json` with all 32 source hashes, 10 passed/0 failed builds, all software statuses passing, and all 10 BIN/2 ELF hashes and sizes plus asset flags. Manifest SHA-256: `D70B793BBD6D6B7620BC03A61A83E20A770F8F832EE1132169A73B76DA3324C8`. | This software evidence does not identify which image was used for the physical acceptance reported below. |
| Feedback-03 deployment | The user-authorized feedback-03 images were uploaded to both fixtures. Reboot and exact served-page checks succeeded on both. | This verifies that earlier UI round's deployment only; it does not verify the final closeout images or production BLE acceptance. |

## Exploratory observations retained

These pre-production observations explain earlier design decisions. They are not acceptance results for the production implementation.

- In the WROVER pairing spike, NimBLE host events continued during an unanswered 10-second trial. The application deadline requested a disconnect; authentication then failed and the disconnect event reported local host termination. A successful immediate same-central retry without reboot, reset, or manual cleanup was not observed.
- The earlier Gate A WROVER bond-deletion baseline recorded one stored bond remaining after native deletion failure (1 to 1), one bond removed after success (1 to 0), and an empty store remaining empty (0 to 0). These observations do not establish final production deletion behavior, partial-failure behavior, directed-target reconciliation, or C3 behavior.

## User-reported physical acceptance and remaining device checks

On 2026-10-23, the user reported physical acceptance completed on both boards since feedback-05. No detailed per-case results or tested-image provenance were supplied. The original detailed acceptance scope remains in the [archived plan](Ble.Peripheral-Web-Interface-Plan-v1.4.md).

The final closeout added two changes after feedback-05: directed-advertising bond-delete reconciliation and serial trailing-byte handling for auto-submitted passkey and confirmation input. Both passed software validation. No separate device verification is recorded for these fixes or the current rebuilt mode-3 images.

The remaining focused device checks for the closeout changes are:

- On both boards, delete the bond of the peer currently targeted by directed advertising. Confirm only the directed instance stops and ordinary advertising remains unaffected; if stopping fails, confirm the operation reports failure and device-reported state remains active until termination is observed.
- Exercise serial trailing input `123456X` and `yX` with a delayed newline, including successful and rejected responses; confirm trailing bytes never trigger another menu action.

The user owns OTA upload and post-upload checks for the current closeout mode-3 images; no agent upload is recorded. The original broader acceptance scope is in the archived plan. The application uses plain HTTP. Keep it on a trusted local network. The [user guide](../../WebInterface.md) describes the duplicate-header parser boundary and the operating controls.

## Evidence retention

The final closeout images, ELF files, and manifest are under the ignored `.pio/web-interface-v1.4/closeout/` directory (13 files, 51,746,568 bytes). The mode-3 images intended for user upload are `.pio/web-interface-v1.4/closeout/wrover_mode3.bin` and `.pio/web-interface-v1.4/closeout/c3_mode3.bin`.

The sibling `.pio/web-interface-v1.4/retained/` directory keeps feedback-03 deployed images and compact metadata, the pre-feature rollback baseline, and the pre-closeout user-build snapshot (14 files, 81,972,486 bytes). The snapshot preserves the user's images that existed before the final rebuild; its source provenance is unknown, so it is not evidence of the current source. Final build/test logs and validation metadata remain available for review. These are intentionally retained local artifacts, not unresolved scratch work or source-controlled evidence.

The obsolete pairing-spike worktree and `codex/deferred-pairing-spike-v1.4` branch were removed, discarding the six reviewed experimental changes. Only the main worktree and local `master` branch remain. Five reproducible scratch helpers and the empty evidence directory were also removed (223,377 bytes). The generated script, build helper, source-gate helper, ELF inspection helper, and native test executable can be recreated if needed.

An earlier reviewed exact-allowlist cleanup removed 60 entries: 2,241 files and 503 directories totaling 576,926,860 logical bytes. Root verified those targets absent and the then-retained artifacts and canonical manifest unchanged. Before that cleanup, the one-file pilot rejected an outside-root path. After cleanup, root checked ancestor paths for reparse points and repeated literal deletion/re-extraction with the same SHA-256, `node --check`, and the embedded asset check. The pilot is limited to that generated script. The raw evidence collection and historical exploratory copies were removed; their unique useful physical conclusions are summarized above. Feature-owned branches, worktrees, scratch files, and artifacts now have an explicit removed or retained disposition.
