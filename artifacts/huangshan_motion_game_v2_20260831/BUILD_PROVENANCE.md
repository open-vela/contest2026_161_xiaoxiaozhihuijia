# Huangshan Motion Game v2 Build Provenance

## Firmware

- Source path: `cmake_out/huangshan_openvela_ble_probe_game_visible_fifo_chunk24_time64_ordered_v2/nuttx.bin`
- Snapshot path: `artifacts/huangshan_motion_game_v2_20260831/nuttx.bin`
- Size: 2,405,480 bytes
- Generated: 2026-08-31 09:23:56.874120168 +0800
- SHA-256: `aa7ce08ca7460c8effdd78ad410631d277c5bee438c1ff0cb5c21da9c8228f3b`
- Build directory: `cmake_out/huangshan_openvela_ble_probe_game_visible_fifo_chunk24_time64_ordered_v2`
- Configuration evidence: `CONFIG_BSP_USING_I2C3=y`

## Probe source snapshots

| Snapshot path | SHA-256 |
| --- | --- |
| `source/apps/examples/openvela_ble_probe/CMakeLists.txt` | `de413a645740e84ffa29abc5f8a46b3dbd2530e6af804217f9b789f283df41ba` |
| `source/apps/examples/openvela_ble_probe/Kconfig` | `5a2ad5ee7601847ab8fa3e9f94dc25657ddd158f16d89b1a22227608ebaca1b0` |
| `source/apps/examples/openvela_ble_probe/openvela_ble_probe_main.c` | `c04978af22a014c3935cb5b6ec99eafbe95adbbe8f7a2d50956070688bc37153` |
| `source/apps/examples/openvela_ble_probe/probe_game.h` | `e124aa2fdd2ff7c53e351c9f4b8fe93c6ef9555412a63e164b3f40dc7985b768` |
| `source/apps/examples/openvela_ble_probe/probe_game.c` | `1dd83e80d87b4293f0e1e56d736389898ef02e1079c6b488a9cb7969ce86000f` |
| `source/apps/examples/openvela_ble_probe/probe_ui.h` | `c42f4a77fa75691185b42cc0e949a6d28c2fdf895a05adaace91e685885689ff` |
| `source/apps/examples/openvela_ble_probe/probe_ui.c` | `bf57c63425df3038033763a8a1039090d1bec9655f36b4412c72440d9a61e426` |

The v2 firmware was produced by recompiling the Probe application and performing
multi-stage linking on top of the previously verified v1 CMake configuration.
This run did **not** perform or prove a clean full configure. A clean full
configure and rebuild is still required to complete the reproducibility
evidence.

The relevant Probe objects were generated after their corresponding source
files; the final ELF (`nuttx`, 2026-08-31 09:23:56.786118880 +0800) and binary
were generated after the newest Probe source.
