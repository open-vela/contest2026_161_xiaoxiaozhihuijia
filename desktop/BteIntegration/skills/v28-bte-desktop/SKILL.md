---
name: v28-bte-desktop
description: 在现有小小指挥家团结工程中进行 v28 BTE1 串口到电脑音游联调、日志对账与重复事件核验。用于电脑真实输入链路，不用于板端算法、固件烧录、Android BLE 或服务器操作。
---

# v28 BTE1 电脑联调

本 Skill 与项目内 `BteIntegration` 工具一起使用。相对本文件，工具根目录是 `../..`；工程根目录是 `../../..`。执行前读取工具根目录的 `README.md` 和 `evidence/operator_notes.md`，已验证结果以实际 audit 文件和原始证据为准。不能因存在源码就宣称实机通过。

## 协议与边界

- 真实事件为 `@BTE1,28,<56 hex>,<4 hex CRC>`。Modbus CRC16 初值 FFFF、多项式 A001，覆盖28字节二进制 payload，不覆盖ASCII文本。解析实现为 `bte_protocol.py` 和工程中的 `BteProtocol.cs`。
- payload 小端 `<BBBBIIQQ`：版本1、事件1、flags仅bit0允许、reserved必须0、session、event_id、beat_time_us、detected_time_us。event=1只代表拍点，不能伪造方向或置信度。设备时间是单调微秒，不能与 Unix 时间直接比较。
- READY 含 session 和 build_id。已烧录事实与本轮捕获到 READY 是两个不同证据，归档 README 的旧描述不能推翻用户确认。
- 本轮范围禁止修改/编译/烧录固件、算法、阈值、时间估计器、BLE栈与连接参数；不动原采集工具；不默认授权SSH或GitHub写入。
- 串口只能一个读取者。读取前确认端口与占用，不杀别人的串口进程。保持DTR/RTS无主动复位脉冲；关闭工具只关闭主机句柄，不能发送板端stop或复位。
- `--start-app` 只在观察到NSH提示符且未见应用运行证据时发送一次 `openvela_ble_probe --ui-paused --diag-off -c`。观察到READY后不能重复启动。需要复位时请用户手动进行。
- 禁止发送历史 BEAT1，也不要运行期待连续 @IMU1 的旧采集器。

## 接入现有工程

核对实际目录、编辑器和活动场景。此工程中 scene7 是音量页，scene8～scene10 才挂有 BeatJudgeManager；旧自动配置脚本的假设不能代替实际检查。不要运行会覆盖场景的旧 Auto Setup 工具来替代联调。

桌面真实模式复用 localhost:9877 的 Unity监听/桥接连接方向，但使用JSONL封装完整BTE1文本，不走39字节解析器。旧模拟器在真实模式下停用。后台只收消息进入256条有界队列，主线程校验、去重、更新Unity对象和写应用日志。

Windows进程之间必须直接使用QueryPerformanceCounter及其频率。已实际发现 Unity Mono Stopwatch 原点不同，不能仅凭频率相同就混用两者。主机接收时间演示把串口接收QPC投影到现有音频timeSamples时钟；保留设备两种时间但不宣称板端同步。原场景判定窗口不得为演示调宽。

## 证据与验收

按README运行标准库解析测试，再运行桥接；证据写入每次唯一会话目录。保存serial.bin、分片位置和主机时间、parsed/forwarded记录、Unity received/processed/拒绝记录与应用版本。simulation与board_serial必须分开。

真实板验收需要READY/build_id、正确CRC事件、同一(session,event_id)贯穿链路、去重不重复评分及原界面真实反馈。用 `audit.py` 对账，保留旧失败窗口，不能以新结果覆盖旧证据。

请用户在音乐运行时完成明确数量动作；将用户动作数单独记录，与算法输出数不等不是本轮调算法授权。断连无补发，桥接SQLite保存去重状态；不要删除该文件后重放历史事件冒充实时数据。

记录尚未验证项及唯一主要阻塞。源码、依赖、证据可准备本地交付；本地任务记录不是赛事官方hook日志。服务器hook安装和赛事提交另行处理，不在本Skill内自动执行。
