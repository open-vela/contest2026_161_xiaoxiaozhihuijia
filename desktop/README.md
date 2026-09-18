# 小小指挥家桌面工程

正式队名：小小指挥家队。本次源码版本：bte-desktop-2-qpc。

使用团结引擎 1.10.0 / 2022.3.62t12 打开本目录。当前演示入口为 Assets/Scenes/scene9.scene；保留 scene10。scene8 已清空原音频，仅保留场景以维持历史索引。
新版输入为28字节BTE1，经Python桥接进入本机TCP 9877，详见 BteIntegration/README.md。
README_legacy_39B.md 和 ble_simulator.py 是旧协议资料，不作为新版真实输入入口。

交付约束：本次板端提交只采用 v28 original，不提交 bootfix。已有桌面7条真实事件通过的历史联调使用了 bootfix 固件，其证据放在交付根目录 review_only，不能作为 original 固件联调验收。
编辑器已进入选歌页 Play 模式；视频录制完成并包含硬件展示。scene1、scene2 跳转异常列入 ISS-051，暂不修复。未构建 EXE/APK，未完成 original 实机验收；Android BLE仍未验收。

Assets（含.meta）、Packages、ProjectSettings完整保留。Library、Temp、Logs、UserSettings和测试bin/obj为可再生内容，不交付。
去重数据库不从开发机拷入新环境，原机数据库未删除；新运行不得借此把旧事件重放为实时事件。
Unity 后端代码由 Qoder CN 生成；图片和动画由即梦 AI 生成；音乐使用公开、超过版权保护年限的音乐（团队确认）。
