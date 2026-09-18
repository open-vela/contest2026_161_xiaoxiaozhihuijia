# tools: honor const mode in generated allsyms declarations

目标仓库：https://github.com/open-vela/nuttx

目标分支：`dev-ai-contest-2026`；准备时目标提交：`dd92bcf425738734d1b8aed09c2bd4dbe3f2e438`。

计划分支：`contest161/nuttx-const-allsyms`；本地签署提交：`1174b2d59d46710745f9d955f728908c215da9ac`（尚未发布）。

## Summary

默认生成的 allsyms 原先落入可写段，并且 extern 声明没有与 const 定义保持一致。修正默认 const 与 --noconst 的分支，令声明和定义保持一致。

关联作品：https://github.com/open-vela/contest2026_161_xiaoxiaozhihuijia/pull/3

## Impact

影响符号表生成，不修改应用算法。与 HCI 诊断头文件拆为独立 PR。

## Testing

本轮在最新目标分支执行默认模式及 --noconst 模式，均生成预期声明。历史 v28 ARM/allsyms 构建成功；未在本次目标分支重新构建完整固件。

本次未修改算法、阈值或已交付固件，未执行 ARM 编译或烧录。历史构建采用 Linux、ARM GCC 10.3.1、CMake 3.22.1、Ninja 1.10.1，目标为黄山派 SF32LB52。原始验证范围见作品仓 evidence/v28_bootfix。

## 提交状态

建议以 Draft 提交并如实保留未验证项。提交后填入实际依赖 PR 链接，检查各仓 CLA、代码规范和实际 CI；不能把补丁可应用视为 CI 或完整集成测试通过。

涉及文件：

- `tools/mkallsyms.py`
