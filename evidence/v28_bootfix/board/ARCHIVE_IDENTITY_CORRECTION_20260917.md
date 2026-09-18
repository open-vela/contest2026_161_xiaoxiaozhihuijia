# v28 archive identity correction

This is a documentation-only correction. No source, build, or firmware was
changed and no rebuild or flash operation was performed.

## Actual file

Path (resolved absolute path):

`/vdb/huangshan-motion-game-clean.20260901/deliveries/v28/bootfix/nuttx.bin`

At verification time:

- size: 2,557,596 bytes
- mtime: `2026-09-17 21:35:31.562401875 +0800`
- SHA-256: `3aed7f0574e906820c364eef8412faaa25de92414de688615bafdc2bf8d1a8d1`

The same-size BIN at
`/vdb/huangshan-motion-game-clean.20260901/cmake_out/v28_bootfix_20260917/nuttx.bin`
has the same SHA-256 and `cmp` reported `IDENTICAL`.

The retained `bootfix/check_app.json` and `SHA256SUMS` agree with this value.

## History and discrepancy

- Historical delivery value: `3aed7f0574e906820c364eef8412faaa25de92414de688615bafdc2bf8d1a8d1` — matches the actual file.
- One later archive-report value was a documentation typo:
  `3aed7f0574e906820e41fe0097234a55059cf2fedb7d7458a1307d2017b720ce`.
  It does not match any retained v28 BIN and is not a measured artifact hash.

The two serial sessions identify `build_id=rank1_v28_bootfix_20260917`, but no
separate hash of the physically flashed image was recorded. Therefore the
archive BIN is verified and matches the retained build BIN, while the
bit-for-bit binding to the image used on the board remains unrecorded.

The two-window C-mode acceptance conclusion remains unchanged and is limited
to the supplied session evidence.
