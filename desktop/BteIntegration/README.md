# 小小指挥家 v28 电脑联调

工程：仓库 `desktop/`。团结引擎 1.10.0 / 2022.3.62t12。
路线：真实开发板 BTE1 串口 → Python 本地桥接 → TCP 127.0.0.1:9877 → 原工程 BeatJudgeManager → 原游戏反馈。

## 启动

1. 保持开发板连接，关闭其他 <实际串口名> 读取者。电脑需 Python 3；本目录 vendor 已带 pyserial 3.5，不修改原采集工具。
2. 团结 Hub 打开上述正式工程。等待编译；若刚从外部修改脚本，点击编辑器后使用 Assets / Refresh，确认 Console 无编译错误。
3. 打开 `Assets/Scenes/scene9.scene`，通过菜单 `RhythmGame / v28 / Run real board input` 进入真实输入模式。所有 Windows 场景默认启用真实 BTE1，不启动原39字节模拟器。scene7 是音量页，不能用于拍点验收。
4. PowerShell 执行以下命令（应用已经在板端运行时，不加 `--start-app`）：

```powershell
Set-Location '<工程目录>/BteIntegration'
python -X utf8 bridge.py --port <实际串口名>
```

如果本次准备捕获冷启动 READY，可执行 `python -X utf8 bridge.py --port <实际串口名> --start-app`，再由用户手动复位板。工具只在捕获 NSH 提示符时发送一次已验证命令 `openvela_ble_probe --ui-paused --diag-off -c`，不自动复位，不自动stop，不烧录。若没有新提示符也没有READY，先查原始日志和板端状态，不盲发命令。

`--observed-nsh-evidence` 仅用于刚捕获NSH后关闭并重新打开主机串口的恢复操作；要求证据末尾为NSH且不超过5分钟。普通启动不需要这个参数。

5. 查看界面状态：未连接 → TCP已连接 → 串口已打开 → 板端READY → 正在接收。桥接断开或超过4秒未收到桥接状态时显示连接中断。READY已在桥接历史中捕获时，TCP重连会重新通报状态，不重放事件。
6. 点击浮层“重新开始原有拍点场景 scene9”，在音乐播放期间手动打拍，观察真实判定反馈和逐事件分数。按当前曲目场景的播放时长自动返回选曲页；可再次点击按钮重开音乐，设备事件号继续递增。

## 关闭

先在桥接控制台按 Ctrl+C，或在该次桥接证据目录创建名为 `STOP` 的文件。等待看到 `Stopped host bridge only` 与 `summary.json`。随后停止团结引擎 Play。
这只关闭电脑端串口与TCP，不向开发板发送停止或复位。板端v28 stop/重启限制仍然存在；如需复位，由用户手动操作。
不要删除 `board_serial_dedup.sqlite3` 来重播旧事件。桥接重启保存历史去重；TCP断线时新事件会记录为 not_forwarded，不补发。

## 协议

`@BTE1,28,<56 hex>,<CRC4>\n`，支持CRLF、串口分片、多行、混杂诊断文本。
payload小端：0 version=1；1 event=1；2 flags(bit0估计时间)；3 reserved=0；4 session u32；8 event_id u32；12 beat_time_us u64；20 detected_time_us u64。
CRC16 Modbus，初值FFFF，多项式A001，覆盖二进制payload。实现与本机既有 `board_rank1_20260913/parse_bte1.py` 交叉测试。

TCP使用一行一个JSON，schema=`bte_bridge_v1`、source=`board_serial`。event消息保留完整原始BTE1以及 host_rx_ticks、forward_ticks、qpc_frequency、run_id；Unity独立验证CRC与字段。
source=`simulation` 只能在显式模拟测试模式接收，不作为实机验收。旧39B输入不能接入真实模式端口。

独立软件集成测试监听9878，与真实9877端口分离。`test_unity_transport.py` 只发送source=simulation的固定样例；原工程Unity日志保留坏CRC、过期及重连重复的拒绝证据。测试模式通过项目本地编辑器命令play-simulation开启；使用菜单Run real board input恢复真实模式。普通使用不需要模拟测试模式。

## 时间与评分

设备 beat_time_us、detected_time_us 原样保存，均是设备单调微秒。未实现设备与电脑时钟同步。
使用主机串口接收时间演示：Python和Unity直接调用Windows QPC，将主机接收事件的时刻投影到原场景 AudioSource.timeSamples/frequency；设备到串口的检测/传输延迟未消除，音频卡顿等仍可能影响评分。
scene8 既有窗口 Perfect=0.1s、Good=0.2s 原样保留，没有补拍或自动Perfect。通用拍点入口不伪造方向、六轴、置信度或电量。错过的谱面位置仅在真实事件到达时推进，不自行生成反馈或加分。

## 日志与对账

每次桥接生成唯一 `evidence/<时间>_<随机值>_board_serial/`，包含：

- `serial.bin`：完整原始字节，未过滤诊断。
- `bridge.jsonl`：字节片段偏移与QPC、解析、去重拒绝、转发、应用回执和会话切换。
- `summary.json`：结束计数。
- Unity另写 `evidence/unity_<UTC时间>_board_serial.jsonl`，记录应用版本、场景、设备时间、接收与处理QPC和实际判定结果。

对账命令：

```powershell
python -X utf8 audit.py 'evidence\某次会话\bridge.jsonl' --unity 'evidence\unity_某次_board_serial.jsonl' --output 'evidence\audit_新的文件名.json'
```

可给 `--unity` 多个文件合并同一桥接会话。输出文件已存在时拒绝覆盖。对账区分received、processed与真实评分反馈，菜单页接收会标记 ignored_no_gameplay；不能把菜单页处理数量当作评分次数。

## 本地测试

```powershell
python -X utf8 -m unittest -v test_protocol
dotnet restore ProtocolTests/ProtocolTests.csproj --configfile ProtocolTests/NuGet.Config
dotnet run --project ProtocolTests --no-restore -- '@BTE1,28,010101000a0000000100000087d612000000000020d6130000000000,EE1E'
```

第一组覆盖CRC、片段边界、坏包、READY、缓冲上限、去重、session切换与既有解析器一致性。第二组直接编译生产C#解析器。上述均标记为软件/模拟测试，不能替代真实板端验收。

## 保留的问题与边界

- 旧 BLE 设备名、FFF1/FFF2、39B长度检查、AndroidJavaProxy等问题只记录，未扩展修改。Android真机BLE未验收。
- v28算法准确率、板端积压、PENDING、长期稳定性、双端时钟同步未在本轮修复或验收。
- 原工程仍保留旧玩法代码。启动Windows独立应用时可显式使用 `--legacy-input` 回到旧输入；真实模式与旧模拟输入不能同时开启。
- 原选曲按钮索引错位已修正：kaishi1/2/3分别按名称进入scene8/9/10；不改场景素材、音乐、BPM或原判定窗口。
- 本地源码和证据可用于后续提交准备；未连接服务器、未修改GitHub、未验证或安装赛事官方hook。此处日志不是已提交的赛事官方日志。
- 团队Word未修改。后续编辑需按用户指定调整杨树轩、睢国钥贡献，并以原资料核对其他成员，不能猜测。

## 曲目与来源更新

scene8 仅保留场景索引。当前默认联调入口为 scene9，历史 scene8 测试证据保持原样，不能当作本次曲目变更后的验收。Unity 后端代码由 Qoder CN 生成；图片和动画由即梦 AI 生成。团队确认音乐使用公开、超过版权保护年限的音乐；具体曲目及录音版本的来源依据另行整理。
