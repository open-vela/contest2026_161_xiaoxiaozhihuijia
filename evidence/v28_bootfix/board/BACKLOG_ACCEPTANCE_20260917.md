# v28 C-mode backlog acceptance

## Scope

This is an archive-only acceptance of the already-built
`rank1_v28_bootfix_20260917`. No source, firmware, or build was changed in
this step. The two session records below are saved from the user's supplied
serial-log evidence; raw capture files were not available in the workspace.

## Measured windows

| Session | Window | FIFO max | FIFO overrun/recovery | Queue full | GAP/reset | Final generated=enqueued=consumed | Depth / high-water | Export delta |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 5413 | 119.294 s | 120 words | 0 / 0 | 0 | 0 / 0 | 12785 | 0 / 41 | +48 / +48 / +48 / +0 |
| 1388 | 231.633 s | 126 words | 0 / 0 | 0 | 0 / 0 | 24767 | 0 / 57 | +25 / +25 / +25 / +0 |

Both sessions used `ui-paused`, `diag-off`, `sched-c`, consumer priority 101,
FIFO priority 102, with setup and runtime query successful. `snapshot_fallbacks`
was zero in both exports. The largest recorded event queue waits were
201819 us and 218193 us; these are event-record maxima, not maxima over every
sample.

## Acceptance conclusion

> v28 C模式通过本次两个实测窗口的积压验收：未记录FIFO溢出、队列丢样或GAP，队列在导出时已排空，导出期间生产和消费继续推进。

This conclusion is limited to these two measured windows (approximately 119 s
and 232 s). No action start/end markers, beat count, recognition rate, or
post-export interval is inferred or included in the tested windows.

## Retained limitations

- Software short-term queuing remains: high-water 41/57 and recorded event
  queue-wait maxima above 200 ms.
- Complete 5-second interval, lock-wait, and batch-service timing evidence was
  not obtained.
- B-mode FIFO overrun remains unresolved.
- Batch-association `PENDING`, sample-time estimation, and BLE lifecycle
  limitations remain as previously documented.
- This is not an all-mode or long-duration stability claim.

## Artifact identity

The delivery BIN was re-hashed without rebuilding:

`deliveries/v28/bootfix/nuttx.bin`

SHA-256: `3aed7f0574e906820c364eef8412faaa25de92414de688615bafdc2bf8d1a8d1`

The archive identity correction and comparison with the retained build BIN are
recorded in [ARCHIVE_IDENTITY_CORRECTION_20260917.md](ARCHIVE_IDENTITY_CORRECTION_20260917.md).

The serial `build_id` agrees with the delivered image identity, but the hash,
not the serial text, is the artifact check. The original-layout build was
subsequently repaired with the const-allsyms-only change; its separate
artifacts and hashes are recorded in `original/BUILD_FIX.md`. This does not
change the C-mode hardware acceptance or mark original as fixed-boot-area
compatible.

## Round history

- Round 1: v27, B failed while C improved in one window.
- Round 2: v28, count snapshot consistency fix and bootfix build.
- Round 3: source audit only, no candidate generated.
- Extended limit: v30 opportunity was not used; the scoped C acceptance target
  is now met, so backlog repair is ended early. No v29/v30 firmware is made.
