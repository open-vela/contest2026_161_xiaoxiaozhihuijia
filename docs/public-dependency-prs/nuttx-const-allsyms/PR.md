# tools: honor const mode in generated allsyms declarations

目标仓库：https://github.com/open-vela/nuttx

目标分支：`dev-ai-contest-2026`；准备时目标提交：`dd92bcf425738734d1b8aed09c2bd4dbe3f2e438`。

提交分支：`contest161/nuttx-const-allsyms`；签署提交：`1174b2d59d46710745f9d955f728908c215da9ac`。

## Summary

默认生成的 allsyms 原先落入可写段，并且 extern 声明没有与 const 定义保持一致。修正默认 const 与 --noconst 的分支，令声明和定义保持一致。

关联作品：https://github.com/open-vela/contest2026_161_xiaoxiaozhihuijia/pull/3

## Impact

影响符号表生成，不修改应用算法。与 HCI 诊断头文件拆为独立 PR。

## Testing

本轮在最新目标分支执行默认模式及 --noconst 模式，均生成预期声明。历史 v28 ARM/allsyms 构建成功；未在本次目标分支重新构建完整固件。

本次未修改算法、阈值或已交付固件，未执行 ARM 编译或烧录。历史构建采用 Linux、ARM GCC 10.3.1、CMake 3.22.1、Ninja 1.10.1，目标为黄山派 SF32LB52。原始验证范围见作品仓 evidence/v28_bootfix。

## 提交状态

本 PR 以 Draft 发布，保留上述未验证项。等待维护者审核及实际 CLA / CI 结果；补丁可应用不代表 CI 或完整集成测试通过。

涉及文件：

- `tools/mkallsyms.py`


## 公共依赖关联

LVGL 与 const-allsyms 可独立审阅；HCI 快照接口、SiFli、ZBlue 与 Bluetooth 需协调审阅及集成验证。

- lvgl-buffer-ownership: https://github.com/open-vela/apps_graphics_lvgl/pull/44
- zblue-cleanup-diagnostics: https://github.com/open-vela/external_zblue/pull/233
- bluetooth-h4-cleanup: https://github.com/open-vela/frameworks_bluetooth/pull/593
- nuttx-const-allsyms: https://github.com/open-vela/nuttx/pull/388
- nuttx-hci-rx-snapshot: https://github.com/open-vela/nuttx/pull/389
- sifli-v28-platform: https://github.com/open-vela/vendor_sifli/pull/35
