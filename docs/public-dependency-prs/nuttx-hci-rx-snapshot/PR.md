# wireless: define HCI RX ring diagnostic snapshot

目标仓库：https://github.com/open-vela/nuttx

目标分支：`dev-ai-contest-2026`；准备时目标提交：`dd92bcf425738734d1b8aed09c2bd4dbe3f2e438`。

提交分支：`contest161/nuttx-hci-rx-snapshot`；签署提交：`44ef43643cf8e9e46b1ccb55aa0aaa3198641bbe`。

## Summary

新增 HCI RX ring 快照结构和查询声明，用于在 SiFli 与 H4 之间以一致结构读取接收诊断计数。

关联作品：https://github.com/open-vela/contest2026_161_xiaoxiaozhihuijia/pull/3

## Impact

这是诊断接口，必须与 SiFli 实现及 Bluetooth 消费者一起审阅；没有为所有平台提供默认实现。

## Testing

新增头文件已纳入补丁并检查许可。未进行跨平台编译；不能把旧基线 ARM 构建视为最新分支验证。

本次未修改算法、阈值或已交付固件，未执行 ARM 编译或烧录。历史构建采用 Linux、ARM GCC 10.3.1、CMake 3.22.1、Ninja 1.10.1，目标为黄山派 SF32LB52。原始验证范围见作品仓 evidence/v28_bootfix。

## 提交状态

本 PR 以 Draft 发布，保留上述未验证项。等待维护者审核及实际 CLA / CI 结果；补丁可应用不代表 CI 或完整集成测试通过。

涉及文件：

- `include/nuttx/wireless/bluetooth/bt_hci_rx_snapshot.h`


## 公共依赖关联

LVGL 与 const-allsyms 可独立审阅；HCI 快照接口、SiFli、ZBlue 与 Bluetooth 需协调审阅及集成验证。

- lvgl-buffer-ownership: https://github.com/open-vela/apps_graphics_lvgl/pull/44
- zblue-cleanup-diagnostics: https://github.com/open-vela/external_zblue/pull/233
- bluetooth-h4-cleanup: https://github.com/open-vela/frameworks_bluetooth/pull/593
- nuttx-const-allsyms: https://github.com/open-vela/nuttx/pull/388
- nuttx-hci-rx-snapshot: https://github.com/open-vela/nuttx/pull/389
- sifli-v28-platform: https://github.com/open-vela/vendor_sifli/pull/35
