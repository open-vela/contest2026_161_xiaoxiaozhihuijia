# sf32lb52: prepare Huangshan IMU and v28 boot support

目标仓库：https://github.com/open-vela/vendor_sifli

目标分支：`dev-ai-contest-2026`；准备时目标提交：`af6f365eaa04a674af0467aa1a803bc4c77691ba`。

计划分支：`contest161/sifli-v28-platform`；本地签署提交：`867c9b49f9a0bbb2691f783f30cc30c545d547d5`（尚未发布）。

## Summary

整理黄山派 IMU INT1/I2C3 接入、固定启动应用分区／堆边界以及 H4/RX 诊断支持，为 v28 运行所需公共平台修改提供独立审阅入口。

关联作品：https://github.com/open-vela/contest2026_161_xiaoxiaozhihuijia/pull/3

## Impact

保留最新上游的异步 LCD、LSM6DS3 与 SPI_DRIVER 初始化。未覆盖公共 nsh 默认配置，比赛配置保留在团队仓库。此草稿仍包含板级集成与诊断，应由维护者确认是否继续拆分；不改变已归档 v28 固件。

## Testing

原补丁在 nsh/defconfig 与 sifli_ap.c 存在冲突，前者从公共候选移除，后者保留上游内容并加入原有 INT1/I2C3 代码。生成补丁和空白检查通过；最新分支未重新构建或烧录。

本次未修改算法、阈值或已交付固件，未执行 ARM 编译或烧录。历史构建采用 Linux、ARM GCC 10.3.1、CMake 3.22.1、Ninja 1.10.1，目标为黄山派 SF32LB52。原始验证范围见作品仓 evidence/v28_bootfix。

## 提交状态

建议以 Draft 提交并如实保留未验证项。提交后填入实际依赖 PR 链接，检查各仓 CLA、代码规范和实际 CI；不能把补丁可应用视为 CI 或完整集成测试通过。

涉及文件：

- `boards/sf32lb52/lckfb_huangshan_pi/include/board.h`
- `boards/sf32lb52/lckfb_huangshan_pi/scripts/ld.script`
- `boards/sf32lb52/lckfb_huangshan_pi/src/bsp_pinmux.c`
- `boards/sf32lb52/lckfb_huangshan_pi/src/sifli_ap.c`
- `chips/sf32lb52/sf32lb52_bt_adapter.c`
- `chips/sf32lb52/sf32lb52_bt_adapter.h`
- `chips/sf32lb52/sf32lb52_bth4.c`
- `chips/sf32lb52/sf32lb52_lcpu_boot.c`
- `chips/sf32lb52/sifli_allocateheap.c`
- `chips/sf32lb52/sifli_start.c`
