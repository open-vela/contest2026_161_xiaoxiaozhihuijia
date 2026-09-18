# 小小指挥家

小小指挥家队 · 手表应用创新方向

腕部挥拍驱动音乐节奏游戏：openvela 开发板采集惯性数据并输出 28 字节 BTE1 拍点事件，Windows 团结工程完成谱面判定与游戏反馈。团队原创代码、工具、Skill 和文档采用 [Apache 2.0](LICENSE)，第三方组件保留原许可，见 [许可范围](LICENSES/README.md)。

## 最终运行版本与证据

最终运行基准为 `rank1_v28_bootfix_20260917`。其 ARM/allsyms 构建、固定启动 ABI 校验以及板端会话 5413、1388、桌面会话 29778 的原始证据随仓库提交。original 构建仅作原布局对照，不能替代 bootfix 运行镜像。

bootfix BIN：2,557,596 字节，SHA-256：`3aed7f0574e906820c364eef8412faaa25de92414de688615bafdc2bf8d1a8d1`。实机 READY 与 build_id 对应；未记录当时已烧录 Flash 的逐字节哈希绑定。桌面 QPC 修正后七条事件全部产生反馈，前两条失败记录保留。该组测试发生在删曲前 scene8，不能改写为当前 scene9 的七条验收。

[演示视频](media/小小指挥家演示.mp4) 时长 2 分 17 秒（137.213 秒），团队确认包含硬件展示。[作品提交文档](docs/小小指挥家队-小小指挥家-作品提交文档.docx) · [实机证据](evidence/v28_bootfix/README.md) · [文件校验值](docs/最终交付文件校验.json)。

## 目录

|目录|内容|
|---|---|
|app/openvela_ble_probe|最终 bootfix 板端应用源码|
|board/v28_bootfix|运行版本配置|
|artifacts/v28_bootfix|固件、符号、构建记录及固定引导镜像|
|board/v28_original、artifacts/v28_original|original 对照源码差异、配置及构建产物|
|desktop|团结工程、BteIntegration 桥接与 Skill|
|dependencies、patches|固定基线、公共源码修改与补丁|
|tools|固定引导构建与校验；手工同步的原采集工具|
|evidence、media、docs|原始测试证据、演示视频与文档|
|logs/yangshuxuan1024|实际 AI 日志和 manifest|

## 复现

完整步骤见 [通用复现步骤](docs/通用复现步骤.md)。在已安装兼容固定引导的目标板使用交付 bootfix；电脑用团结引擎 1.10.0／2022.3.62t12 打开 desktop，进入 `Assets/Scenes/scene9.scene`，选择 `RhythmGame / v28 / Run real board input`。

在 desktop/BteIntegration 运行 `python -X utf8 bridge.py --port <实际串口名>`。串口只允许一个读取者；仅在确认 NSH 状态且应用未启动时使用 `--start-app`。按 session/event_id 核对日志。不要自动复位、重复启动或将模拟输入当作实机证据。

manifest 默认映射 bootfix 应用和配置，16 个公共依赖固定基线。公共修改仍需应用补丁／新增文件；仅 repo sync 不等于已完成全部修改。全新环境构建尚未验证，通用源码准备流程与已完成的历史 ARM 构建分别说明。

## AI 使用与已知限制

Unity 后端代码由 Qoder CN 生成；Codex 用于板端开发、诊断、联调及工具整理；图片和动画由即梦 AI 生成。音乐性质按团队确认，为公开、超过版权保护年限的音乐；第三方录音和素材不自动取得 Apache 授权。

已提交部分历史 Codex 日志：30 个会话、37,828 条记录，按原始日期分为 54 个 JSONL，未修改官方校验器结果 ALL OK。未改写原始内容，保留适配来源说明；不宣称全项目完整覆盖或官方认可有效工时。详见 [日志说明](logs/README.md)。Skill 位于 desktop/BteIntegration/skills/v28-bte-desktop/SKILL.md，尚未独立验证完整复现。

已移除《义勇军进行曲》。scene1、scene2 跳转问题（ISS-051）暂不修复。算法、识别率、BLE、状态机保持冻结；Android BLE、精确双端同步和长期稳定性未完成验收。

官方手表应用指引要求快应用框架与模拟器验证；本项目当前为原生板端与 Windows 联动，尚无对应快应用工程，方向符合性须向组委会确认，不能仅靠 README 名称证明满足要求。

## 提交状态

通过 [PR #3](https://github.com/open-vela/contest2026_161_xiaoxiaozhihuijia/pull/3) 提交，是否合入及检查结果以 PR 页面为准。CLA 不代替构建验证。NuttX、LVGL、ZBlue、Bluetooth、vendor/sifli 修改仍需对应公共仓 PR；本仓补丁不替代公共仓审核。
