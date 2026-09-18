# bluetooth: retain host cleanup state and bounded diagnostics

目标仓库：https://github.com/open-vela/external_zblue

目标分支：`dev-ai-contest-2026`；准备时目标提交：`2e5bddcc5284534aaca189346570d32f1a5509f3`。

提交分支：`contest161/zblue-cleanup-diagnostics`；签署提交：`b46e044a24d8ee4ac244201a1c06cf832624caea`。

## Summary

保留 host 初始化与清理状态，补充 ATT/GATT、HCI 路径的有界诊断，支持对 RESET、关闭及资源回收进行关联排查。

关联作品：https://github.com/open-vela/contest2026_161_xiaoxiaozhihuijia/pull/3

## Impact

包含比赛期间保留的诊断与生命周期处理，需与 frameworks_bluetooth、SiFli 一起审阅。诊断开关和日志开销需要维护者评估。

## Testing

可应用于记录的最新目标分支；旧基线完成 ARM/allsyms 构建。RESET、停止和不复位重启仍未整体验收，本草稿不宣称已解决这些限制。

本次未修改算法、阈值或已交付固件，未执行 ARM 编译或烧录。历史构建采用 Linux、ARM GCC 10.3.1、CMake 3.22.1、Ninja 1.10.1，目标为黄山派 SF32LB52。原始验证范围见作品仓 evidence/v28_bootfix。

## 提交状态

本 PR 以 Draft 发布，保留上述未验证项。等待维护者审核及实际 CLA / CI 结果；补丁可应用不代表 CI 或完整集成测试通过。

涉及文件：

- `include/zephyr/bluetooth/bluetooth.h`
- `subsys/bluetooth/host/att.c`
- `subsys/bluetooth/host/gatt.c`
- `subsys/bluetooth/host/hci_core.c`


## 公共依赖关联

LVGL 与 const-allsyms 可独立审阅；HCI 快照接口、SiFli、ZBlue 与 Bluetooth 需协调审阅及集成验证。

- lvgl-buffer-ownership: https://github.com/open-vela/apps_graphics_lvgl/pull/44
- zblue-cleanup-diagnostics: https://github.com/open-vela/external_zblue/pull/233
- bluetooth-h4-cleanup: https://github.com/open-vela/frameworks_bluetooth/pull/593
- nuttx-const-allsyms: https://github.com/open-vela/nuttx/pull/388
- nuttx-hci-rx-snapshot: https://github.com/open-vela/nuttx/pull/389
- sifli-v28-platform: https://github.com/open-vela/vendor_sifli/pull/35
