# Official Library Change Inventory

This inventory records the read-only audit performed before freezing on
2026-08-31. It covered 254 Git worktrees under the OpenVela checkout: 243 were
clean and 11 had tracked or untracked changes. The OpenVela checkout root is a
repo-managed workspace, not itself a usable Git worktree.

## 冻结纳入

- The seven untracked v2 Probe application files from the `apps` repository,
  copied verbatim under `source/apps/examples/openvela_ble_probe/`.
- The verified v2 `.config` and `nuttx.bin` only; no build directory, object,
  ELF, or log is included.
- `patches/vendor_sifli_platform.patch`, containing exactly vendor/sifli commits
  `7f222a5a22e2628ba6844e242ef95c6244785db1` and
  `4518591ab91876aea505742d838e09449941d0b9`.

## 已提交待上游

The following platform support is committed on the local `vendor/sifli`
branch `fix/huangshan-probe-platform-support` and has no configured upstream:

- `chips/sf32lb52/sifli_allocateheap.c` and
  `chips/sf32lb52/sifli_start.c` in commit `7f222a5a22e2628ba6844e242ef95c6244785db1`.
- `boards/sf32lb52/lckfb_huangshan_pi/configs/nsh/defconfig`,
  `include/board.h`, `src/bsp_pinmux.c`, and `src/sifli_ap.c` in commit
  `4518591ab91876aea505742d838e09449941d0b9`.

No public `vendor_sifli` or `apps` PR was created and nothing was pushed to a
public upstream as part of this freeze.

## 明确排除

Framework/SAL/ZBlue/BTH4 diagnostic or experimental tracked modifications:

- `frameworks/connectivity/bluetooth/service/profiles/gatt/gatts_service.c`
- `frameworks/connectivity/bluetooth/service/stacks/zephyr/sal_le_advertise_interface.c`
- `external/zblue/zblue/subsys/bluetooth/host/adv.c`
- `external/zblue/zblue/subsys/bluetooth/host/adv.h`
- `external/zblue/zblue/subsys/bluetooth/host/hci_core.c`
- `vendor/sifli/chips/sf32lb52/sf32lb52_bt_adapter.c`
- `vendor/sifli/chips/sf32lb52/sf32lb52_bth4.c`

Experimental untracked Huangshan configurations:

- `vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/btsak/defconfig`
- `vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/hcitest/defconfig`
- `vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh_heapdiag/defconfig`
- `vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh_heapguard12/defconfig`
- `vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh_imu_id/defconfig`

Unrelated dirty or untracked repositories/items were also excluded:

- `.local/SiFli-SDK`: `.bootstrap-venv/` and `.local-bin/` tool bootstrap files.
- `apps/testing/drivers/nist-sts`: imported metadata, patches, build files, and
  the untracked `sts/` source/data tree (visible both from the parent `apps`
  repository and its nested repository).
- `external/freetype/freetype/subprojects/dlg` and
  `vendor/infineon/chips/aurix/illd/tc4x/Libraries`: modified nested-repository
  pointers/worktrees.
- `packages/apps/contest2026_161_hello_quickapp`,
  `packages/demos/contest2026_161_hello_app`, and
  `vendor/openvela/boards/contest2026_161_board`: unrelated untracked entries.
- The generated v2 build tree under `cmake_out/`, except for the explicitly
  frozen binary and configuration.

## 未修改

- The two committed files `chips/sf32lb52/sifli_start.c` and
  `chips/sf32lb52/sifli_allocateheap.c` have no additional working-tree changes
  relative to `vendor/sifli` HEAD.
- The four board files changed by commit `4518591...` have no additional
  working-tree changes relative to `vendor/sifli` HEAD. Only the five
  experimental configuration directories listed above are untracked beneath
  `boards/sf32lb52/lckfb_huangshan_pi/`.
- Apart from the seven explicitly listed diagnostic/experimental tracked files,
  the audited Framework Bluetooth, ZBlue, and vendor/sifli paths had no other
  tracked modifications.
- The remaining 243 audited Git worktrees were clean.
