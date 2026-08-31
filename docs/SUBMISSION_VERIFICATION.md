# Submission reproducibility verification

## Fixed public baselines

- `apps` (`nuttx-apps`), branch `dev-ai-contest-2026`:
  `dcc6a95c3b323e533c98fde8fb209f99e24f0fdd`
- `vendor/sifli`, branch `dev-ai-contest-2026`:
  `f5bde0b5f99bfccf87faf791d8b51fdfcb963c9b`

The team manifest maps example application, quick-app, and board directories,
but it does not map team files into the independent `apps` or `vendor/sifli`
repositories. The submission therefore uses repository-specific patches.

## Patch verification

Both patches passed `git apply --check` in detached isolated worktrees at the
baselines above. After application, all seven files under
`apps/examples/openvela_ble_probe/` matched the frozen source snapshot hashes.

| Patch | SHA-256 |
| --- | --- |
| `patches/apps_openvela_ble_probe.patch` | `40e9065b0501e098201f5cd7e7573686af8299cf573696e5c28f550527c60d43` |
| `patches/vendor_sifli_platform.patch` | `24bfaa302b5173e9e705972a0b6d7ea50d514dadb7a0de36a1807ce3db24e4e8` |

The apps patch changes only the seven Probe files. The vendor patch contains
only commits `7f222a5a22e2628ba6844e242ef95c6244785db1` and
`4518591ab91876aea505742d838e09449941d0b9`.

## Whitespace evidence

The repository's only workflow is the shared CLA check. It does not checkout
the repository, build it, run `git diff --check`, or scan artifact patches.
There is therefore no scoped `.gitattributes` exception.

`git diff --check` reports 17 trailing-whitespace lines inside
`vendor_sifli_platform.patch`. They are preserved lines in the immutable patch
payload exported from the two platform commits, not whitespace in Markdown,
scripts, Probe snapshots, firmware metadata, or newly authored source. The
same patch passes `git apply --check`. Checks excluding this preserved patch
payload must pass without findings.

## Clean-build status

Patch applicability and source identity are verified. A clean, isolated,
full configure and rebuild was not run during this delivery because the
available OpenVela workspace contains pre-existing changes in multiple public
repositories. No clean-build success is claimed. Run
`scripts/build_huangshan_motion_game.sh` in a freshly synced workspace after
applying the patches to collect that remaining reproducibility evidence.
