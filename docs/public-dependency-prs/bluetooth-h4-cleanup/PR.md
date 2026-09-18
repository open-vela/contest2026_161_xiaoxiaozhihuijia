# bluetooth: close H4 transport and propagate cleanup results

目标仓库：https://github.com/open-vela/frameworks_bluetooth

目标分支：`dev-ai-contest-2026`；准备时目标提交：`028b4ba7e39328acdf23fed0921b7c65aba2001a`。

计划分支：`contest161/bluetooth-h4-cleanup`；本地签署提交：`238c974d78a969122b0dc361a3d85771c6cd95a7`（尚未发布）。

## Summary

H4 驱动原先没有 close 回调，关闭请求返回 -ENOSYS。补充 poll/UART 资源关闭，向上层传播清理结果，并保留服务跟踪和 GATT user_data 所有权辅助逻辑。

关联作品：https://github.com/open-vela/contest2026_161_xiaoxiaozhihuijia/pull/3

## Impact

依赖 ZBlue host 状态接口和 HCI RX 诊断接口；新增 service_trace.h 与 sal_gatt_db_ownership.h 已随补丁纳入并添加许可标识。

## Testing

最新目标分支补丁应用及空白检查通过。旧基线 ARM/allsyms 已完成；真实控制器 RESET、在途 RX/TX 关闭及重复启动未整体通过验收。

本次未修改算法、阈值或已交付固件，未执行 ARM 编译或烧录。历史构建采用 Linux、ARM GCC 10.3.1、CMake 3.22.1、Ninja 1.10.1，目标为黄山派 SF32LB52。原始验证范围见作品仓 evidence/v28_bootfix。

## 提交状态

建议以 Draft 提交并如实保留未验证项。提交后填入实际依赖 PR 链接，检查各仓 CLA、代码规范和实际 CI；不能把补丁可应用视为 CI 或完整集成测试通过。

涉及文件：

- `framework/api/bluetooth.c`
- `service/common/service_loop.c`
- `service/common/service_trace.h`
- `service/src/adapter_service.c`
- `service/stacks/include/sal_adapter_le_interface.h`
- `service/stacks/zephyr/hci_h4.c`
- `service/stacks/zephyr/sal_adapter_le_interface.c`
- `service/stacks/zephyr/sal_gatt_db_ownership.h`
- `service/stacks/zephyr/sal_gatt_server_interface.c`
- `service/stacks/zephyr/sal_le_advertise_interface.c`
