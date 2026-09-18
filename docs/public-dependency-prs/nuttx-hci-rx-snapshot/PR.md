# wireless: define HCI RX ring diagnostic snapshot

目标仓库：https://github.com/open-vela/nuttx

目标分支：`dev-ai-contest-2026`；准备时目标提交：`dd92bcf425738734d1b8aed09c2bd4dbe3f2e438`。

计划分支：`contest161/nuttx-hci-rx-snapshot`；本地签署提交：`8d633733d3d140a658d12a3fcc717383e57445b6`（尚未发布）。

## Summary

新增 HCI RX ring 快照结构和查询声明，用于在 SiFli 与 H4 之间以一致结构读取接收诊断计数。

关联作品：https://github.com/open-vela/contest2026_161_xiaoxiaozhihuijia/pull/3

## Impact

这是诊断接口，必须与 SiFli 实现及 Bluetooth 消费者一起审阅；没有为所有平台提供默认实现。

## Testing

新增头文件已纳入补丁并检查许可。未进行跨平台编译；不能把旧基线 ARM 构建视为最新分支验证。

本次未修改算法、阈值或已交付固件，未执行 ARM 编译或烧录。历史构建采用 Linux、ARM GCC 10.3.1、CMake 3.22.1、Ninja 1.10.1，目标为黄山派 SF32LB52。原始验证范围见作品仓 evidence/v28_bootfix。

## 提交状态

建议以 Draft 提交并如实保留未验证项。提交后填入实际依赖 PR 链接，检查各仓 CLA、代码规范和实际 CI；不能把补丁可应用视为 CI 或完整集成测试通过。

涉及文件：

- `include/nuttx/wireless/bluetooth/bt_hci_rx_snapshot.h`
