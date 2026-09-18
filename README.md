# 小小指挥家

小小指挥家队 · 手表应用创新赛道

以腕部挥拍控制音乐节奏游戏：openvela 开发板采集惯性数据并输出拍点事件，Windows 团结引擎工程接收 28 字节 BTE1 事件，完成谱面判定和游戏反馈。当前交付包含 v28 original 板端应用、桌面工程、桥接工具、Skill 和作品文档。

## 当前验证范围

v28 original 已完成 ARM/allsyms 构建，仅通过 const-allsyms 修复符号表引起的 SRAM 溢出，原有链接布局不变。尚未完成启动布局兼容性、全新目录构建及 original 实机验收，不作为固定启动区烧录候选。bootfix 不提交。

历史 bootfix 桌面联调修正 QPC 原点后，7 条事件均产生游戏反馈；该结果不作为 original 或本次删曲后版本的验收。已移除《义勇军进行曲》，默认联调入口为 scene9。录屏中发现 scene1、scene2 跳转问题（ISS-051），暂不修复。视频已录制并包含硬件展示，视频文件单独交付。

## 目录

|目录|用途|
|---|---|
|app/openvela_ble_probe/|团队板端应用源码、Kconfig、CMake入口|
|board/v28_original/|original板配置及配置叠加记录|
|desktop/|团结工程Assets、Packages、ProjectSettings及BteIntegration工具与Skill|
|dependencies/|构建涉及仓库的固定版本及公共代码修改快照|
|patches/|公共仓补丁；不替代对应公共仓PR|
|artifacts/v28_original/|最终original固件、配置、构建结果|
|tools/|保持手工同步的数据采集工具|
|docs/|作品提交文档、依赖说明、已知问题和映射说明|
|logs/|AI日志提交位置，见目录说明|

## 拉取与板端构建

合并本次变更后，可在 Linux 使用比赛仓 manifest 拉取工作区：

```sh
repo init -u https://github.com/open-vela/contest2026_161_xiaoxiaozhihuijia -b dev-ai-contest-2026 -m contest2026_161_xiaoxiaozhihuijia.xml
repo sync -c -j8
```

本地未推送时，上述远端命令仍得到旧版。manifest 将团队应用映射到 `apps/examples/openvela_ble_probe`，并为 original 配置建立独立命名入口；16 个相关公共仓固定到归档基线。公共依赖补丁及新增源码仍须按对应仓处理，不能仅 repo sync 就认定等价于历史构建。

已完成构建来自原有 CMake/Ninja 环境。本次没有把历史绝对路径缓存当作可移植入口，也没有验证新的干净配置或标准 build.sh 构建。详见 [映射与复现边界](docs/仓库映射与复现边界.md)。烧录前须单独确认目标启动布局，不提供未经验证的烧录步骤。

BIN：2,557,620 字节；SHA-256：`77b8bb24c4e4dd136d0d6a406433986c06681edbb3ac2fb18c253d3ffd7bf5ea`。

## 电脑端运行

1. 用团结引擎 1.10.0／2022.3.62t12 打开 `desktop/`，等待导入；准备 Python 3，工具已携带 pyserial 3.5。
2. 打开 `Assets/Scenes/scene9.scene`，使用 `RhythmGame / v28 / Run real board input` 进入真实输入模式。
3. 在 `desktop/BteIntegration/` 运行 `python -X utf8 bridge.py --port <实际串口名>`，关闭其他串口读取者。板端未运行时，先核对 NSH 状态，再按工具说明使用 `--start-app`；不要重复启动、自动复位或烧录。
4. 按 session/event_id 核对串口、桥接和游戏日志。Ctrl+C 关闭桥接，再停止 Play。模拟输入不作为真实板验收。

Android BLE、精确双端同步、识别率和长期稳定性尚未完成验收；本次不优化算法、BLE或状态机。

## AI 使用

Unity 后端代码由 Qoder CN 生成；Codex 用于板端开发、联调和工具整理；图片及动画由即梦 AI 生成。音乐来源按团队确认，为公开且超过版权保护年限的音乐。团队负责需求、集成与验证。

Skill 位于 `desktop/BteIntegration/skills/v28-bte-desktop/SKILL.md`，是可复用联调规范。手动适配的部分 Codex 日志已通过官方格式校验，但尚未完成人工公开审阅，未装入此源码审阅目录；不宣称完整归集或官方认可有效工时。Qoder CN 不冒充 Codex 日志，即梦素材不计作编码日志。

## PR 与依赖

所有仓库变更经 PR 和实际配置的检查合入。NuttX、LVGL、ZBlue、Bluetooth、vendor/sifli 的公共修改需要各自的依赖 PR。源码目录和补丁可供审阅，不表示依赖已合入。此次仅本地准备，没有推送或合并。
