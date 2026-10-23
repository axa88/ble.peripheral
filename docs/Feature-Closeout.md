# Feature Closeout Procedure

Use this procedure for a scoped feature when its implementation is ready for review. Keep the record and validation proportional to the change.

## Establish the scope

Record the repository, branch, HEAD, working-tree state, and the exact files the feature owns. Read applicable repository instructions. Preserve unrelated and untracked work. Confirm who owns device testing, deployment, commits, local promotion, branch retirement, and remote push before those actions.

## Audit the implementation

Review the complete feature diff and trace its paths to runtime consumers. For suspected stale or unused code, search references and inspect conditional compilation, registration, callbacks, generated inputs, and runtime initialization before removal. Keep cleanup inside the authorized feature scope and remove individually reviewed paths.

## Run relevant checks

Choose checks that cover the changed behavior: focused unit or regression checks for pure logic and UI, static checks for narrow invariants, and builds for affected targets when compilation inputs changed. Record the command, result, source snapshot, and material limitation. Keep software tests, builds, firmware upload, device behavior, and user acceptance distinct. A successful build or test is not physical acceptance.

## Protect credentials and evidence

Do not read, print, or copy local credential values into reports. Keep credentials in ignored local configuration and rotate them through the owner's approved workflow. Remove credentials, authorization data, CSRF values, and passkeys from retained logs. Record device or peer identifiers when needed to establish which fixture produced an acceptance result; keep such evidence local and access-controlled, and avoid unrelated identifiers.

Maintain a current user guide, a completed plan, and one concise implementation/validation/acceptance record where those artifacts help users or reviewers. Retain raw logs, patches, manifests, and binaries only when each has a distinct review, reproduction, rollback, or user-action purpose. Before removing evidence, preserve any unique conclusion in the concise record and review exact paths.

Before moving or deleting files, canonicalize each exact target and confirm it stays under the repository or the explicitly approved output root. Check every path component for reparse points or junctions. Prepare an exact allowlist and dry-run that shows the resolved paths and intended action, then operate on those literal paths individually. Keep generated outputs needed for an authorized user action until their owner confirms they are no longer needed. Do not use broad cleanup commands.

After approved cleanup removes generated output, run a small allowlisted regeneration pilot and verify that only the expected artifacts return. Complete that regeneration check before local promotion; do not build a general cleanup automation framework for one feature.

## Review before promotion

Have an independent reviewer inspect the final diff and evidence. Confirm that reported results match the reviewed source snapshot and list every unverified manual or physical check. Verify the scoped text files use repository line endings and UTF-8 without a byte-order mark. Resolve links in maintained documentation.

## Promote locally and retire the feature branch

Local promotion and branch retirement require explicit authorization. Before promotion, confirm the intended feature tip is committed, reviewed, and descends from the current local master; confirm unrelated work is preserved. Record the exact source and destination refs.

From the local master branch, use a fast-forward-only merge to the authorized feature branch. Verify that local master points to the intended feature tip and that the working tree is in the expected state. Delete the local feature branch only after that verification and only when no unmerged work remains.

Remote push is a separate operation. Local promotion or feature-branch deletion does not authorize a push; confirm the exact remote and refspec separately.
