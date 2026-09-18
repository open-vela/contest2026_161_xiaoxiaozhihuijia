# 小小智汇家 - 手腕体感音游 使用说明

## 一、项目概述

本项目是基于 Tuanjie Engine 2022.3 (团结引擎) 开发的智能手表体感音乐节奏游戏。玩家通过佩戴小米"黄山派"智能手表，利用手腕手势动作（如点击、上滑等）来击打音符，实现体感交互的音乐游戏体验。

### 核心特性

- 通过 BLE 蓝牙连接小米手表，实时接收手腕手势数据
- 支持 TCP 模拟器进行无硬件开发测试
- 基于 BPM 自动生成音符谱面
- Perfect / Good / Miss 三级判定系统
- Combo 连击与评分评级（D ~ SSS）
- 加速度计数据滤波处理

---

## 二、项目结构

```
Assets/
├── Scripts/
│   ├── RhythmGame/                  # 音游业务层
│   │   ├── BLEBridge.cs             # BLE 桥接器（核心入口）
│   │   ├── IBlePlugin.cs            # BLE 插件接口
│   │   ├── TcpBleSimulator.cs       # TCP 模拟器（开发测试用）
│   │   ├── RealBlePlugin.cs         # 真实 BLE 插件（Android 原生实现）
│   │   ├── BleGattCallback.cs       # Android BLE 回调代理
│   │   ├── RhythmGameController.cs  # 音游主控制器
│   │   ├── NoteTrack.cs             # 音符轨道视觉管理
│   │   ├── SongConfig.cs            # 歌曲配置 ScriptableObject
│   │   └── SelectedSongInfo.cs      # 当前选中歌曲静态信息
│   │
│   └── WristConductor/
│       └── Core/                    # 手腕输入核心层
│           ├── WristConductorController.cs  # 总控制器（组合各子系统）
│           ├── WristConductorData.cs        # 数据结构定义
│           ├── BLECommunicationManager.cs   # BLE 协议解析
│           ├── ScoreEngine.cs               # 评分引擎
│           ├── LightweightTimeSync.cs       # 轻量时间同步
│           ├── AccelerometerProcessor.cs    # 加速度计处理器
│           └── SignalFilter.cs              # 信号滤波器
│
├── Scenes/
│   └── zhengquedu.cs                # 节拍判定管理器（另一场景实现）
│
└── Plugins/
    └── Android/
        └── AndroidManifest.xml      # Android 蓝牙权限声明

ble_simulator.py                     # Python 手表模拟器（项目根目录）
```

---

## 三、系统架构与数据流

```
┌─────────────┐    BLE/TCP     ┌───────────┐    原始字节    ┌──────────────────────┐
│  小米手表    │ ──────────────→ │ BLEBridge │ ──────────────→ │ BLECommunicationMgr  │
│ (或模拟器)   │   39字节/包    │           │                │ (协议解析)            │
└─────────────┘                └───────────┘                └──────────┬───────────┘
                                                                       │ WatchDataPacket
                                                                       ↓
                                                          ┌──────────────────────┐
                                                          │WristConductorController│
                                                          │ (时间同步+数据预处理)   │
                                                          └──────────┬───────────┘
                                                                     │ ProcessedData
                                                            ┌────────┴────────┐
                                                            ↓                 ↓
                                                  ┌──────────────┐  ┌──────────────┐
                                                  │ ScoreEngine  │  │ 游戏控制器    │
                                                  │ (评分/连击)   │  │ (UI/音效)     │
                                                  └──────────────┘  └──────────────┘
```

### 数据流详解

1. **BLE 接收**：手表通过 BLE 发送 39 字节原始数据，由 `IBlePlugin` 实现接收
2. **桥接转发**：`BLEBridge` 将原始字节转发给 `BLECommunicationManager`
3. **协议解析**：`BLECommunicationManager.ParsePacket()` 将字节解析为 `WatchDataPacket` 结构体
4. **时间同步**：`LightweightTimeSync` 将手表时间戳转换为本地时间
5. **信号处理**：`AccelerometerProcessor` 对加速度数据进行滤波
6. **评分判定**：`ScoreEngine` 将处理后的数据与谱面音符进行匹配判定
7. **UI 反馈**：游戏控制器更新分数、Combo、判定文字等 UI

---

## 四、BLE 通信协议

### 4.1 数据包格式

每个 BLE 数据包固定 **39 字节**，结构如下：

| 偏移量 | 字节数 | 数据类型 | 字段名 | 说明 |
|--------|--------|----------|--------|------|
| 0 | 1 | byte | version | 协议版本号（当前为 1） |
| 1-2 | 2 | uint16 | packetId | 包序号，用于去重 |
| 3-10 | 8 | int64 | timestamp | Unix 时间戳（毫秒） |
| 11 | 1 | byte | gesture | 手势类型（见下表） |
| 12 | 1 | byte | confidence | 置信度（0-255，除以 255 得 0.0-1.0） |
| 13 | 1 | byte | flags | 标志位 |
| 14-17 | 4 | float | accelX | 加速度 X 轴 |
| 18-21 | 4 | float | accelY | 加速度 Y 轴 |
| 22-25 | 4 | float | accelZ | 加速度 Z 轴 |
| 26-29 | 4 | float | gyroX | 陀螺仪 X 轴 |
| 30-33 | 4 | float | gyroY | 陀螺仪 Y 轴 |
| 34-37 | 4 | float | gyroZ | 陀螺仪 Z 轴 |
| 38 | 1 | byte | battery | 电池电量（0-100） |

### 4.2 手势类型

| 枚举值 | 名称 | 说明 |
|--------|------|------|
| 0 | None | 无手势 |
| 1 | Tap | 点击 |
| 2 | SwipeUp | 上滑 |
| 3 | SwipeDown | 下滑 |
| 4 | SwipeLeft | 左滑 |
| 5 | SwipeRight | 右滑 |
| 6 | LongPress | 长按 |
| 7 | Flick | 弹指 |

### 4.3 字节序

所有多字节数值使用 **Little-Endian** 字节序（与 `BitConverter` 默认行为一致）。

---

## 五、两种输入模式

### 5.1 TCP 模拟器模式（开发测试）

适用于没有手表硬件时的开发调试，在 Unity 编辑器中即可运行。

**工作原理：**
- Unity 端启动 TCP 服务器监听指定端口（默认 9877）
- 外部 Python 脚本作为 TCP 客户端连接并发送模拟的 39 字节数据包
- BLEBridge 收到数据后走与真实 BLE 完全相同的数据流

**使用步骤：**

1. 在 Unity Inspector 中，确保 BLEBridge 组件的 `useTcpSimulator` **勾选**
2. 设置 `tcpPort`（默认 9877）
3. 运行 Unity 场景
4. 在命令行中运行模拟器脚本：

```bash
# 安装 Python 3.6+ 即可，无需额外依赖

# 模拟连续点击，每 0.5 秒一次，共 20 次
python ble_simulator.py --tap --interval 0.5 --count 20

# 模拟上滑手势
python ble_simulator.py --swipe-up

# 模拟下滑手势
python ble_simulator.py --swipe-down

# 自定义间隔和次数
python ble_simulator.py --tap --interval 1.0 --count 10
```

**模拟器参数：**

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--tap` | 发送点击手势 | - |
| `--swipe-up` | 发送上滑手势 | - |
| `--swipe-down` | 发送下滑手势 | - |
| `--interval` | 发送间隔（秒） | 0.5 |
| `--count` | 发送次数 | 10 |
| `--port` | TCP 端口 | 9877 |

### 5.2 真实 BLE 模式（Android 真机）

适用于连接小米手表进行实际游戏。

**工作原理：**
- `RealBlePlugin` 通过 Unity 的 `AndroidJavaObject` 调用 Android 原生 BLE API
- 自动扫描、连接手表，订阅特征值通知
- 收到数据后通过线程安全队列转发到 Unity 主线程

**使用步骤：**

1. 在 Unity Inspector 中，BLEBridge 组件的 `useTcpSimulator` **取消勾选**
2. 在同一个 GameObject 上添加 `RealBlePlugin` 组件
3. 配置 RealBlePlugin 的参数（详见下方）
4. 打包为 Android APK 安装到手机
5. 打开手机蓝牙，运行 App

**RealBlePlugin Inspector 参数：**

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `deviceName` | 手表蓝牙名称关键字，扫描到包含此名称的设备即连接 | `Mi Watch` |
| `serviceUuid` | 手表 BLE 服务 UUID | `0000fff0-...` |
| `characteristicUuid` | 手表 BLE 特征 UUID（数据通道） | `0000fff1-...` |

---

## 六、确定手表 BLE 参数

由于小米"黄山派"手表的 BLE 服务 UUID 尚未确认，首次连接时需要探测：

### 6.1 修改设备名称

在手机上查看手表的蓝牙名称：
- 打开手机 **设置 → 蓝牙**
- 找到手表设备，记下其名称（如 "Mi Watch"、"Xiaomi Watch" 等）
- 将该名称填入 RealBlePlugin 的 `deviceName` 字段

### 6.2 自动枚举服务和特征

连接成功后，Console 会自动打印手表的所有 BLE 服务和特征：

```
[RealBlePlugin] 共发现 5 个服务:
  服务[0]: 00001800-0000-1000-8000-00805f9b34fb
    特征[0]: 00002a00-... [R--]
  服务[1]: 0000fff0-0000-1000-8000-00805f9b34fb
    特征[0]: 0000fff1-... [RWN]
```

**特征标记说明：**
- `R` = 可读 (Read)
- `W` = 可写 (Write)
- `N` = 可通知 (Notify) ← **数据通道必须有 N 标记**

找到带 `N` 标记的特征，将其所属的服务 UUID 和特征 UUID 填入 Inspector。

### 6.3 查看原始数据

连接并订阅成功后，手表发送的原始数据会打印到 Console（十六进制格式），可据此判断数据格式是否与 39 字节协议匹配。

---

## 七、场景搭建指南

### 7.1 音游场景 (RhythmScene)

#### 必需 GameObject 及组件

**GameManager** (空 GameObject)
- `RhythmGameController` 组件
- `BLEBridge` 组件
- `RealBlePlugin` 组件（真实 BLE 模式时）

**Canvas** (UI Canvas)
- `ScoreText` (TMP_Text) — 显示当前分数
- `ComboText` (TMP_Text) — 显示 Combo 连击
- `SongNameText` (TMP_Text) — 显示歌曲名
- `JudgeCanvas` (空 RectTransform) — 判定文字弹出容器

**NoteTrack** (空 GameObject)
- `NoteTrack` 组件
- `NoteContainer` (子 RectTransform) — 音符容器

#### RhythmGameController 组件 Inspector 配置

| 参数 | 类型 | 说明 |
|------|------|------|
| `bleBridge` | BLEBridge | 拖入同 GameObject 上的 BLEBridge 组件 |
| `audioSource` | AudioSource | 留空会自动创建 |
| `audioMixer` | AudioMixer | 可选，音频混合器 |
| `noteTrack` | NoteTrack | 拖入 NoteTrack 组件 |
| `scoreText` | TMP_Text | 拖入分数文字 UI |
| `comboText` | TMP_Text | 拖入 Combo 文字 UI |
| `songNameText` | TMP_Text | 拖入歌曲名文字 UI |
| `judgeTextPrefab` | GameObject | 判定文字预制体（包含 TMP_Text 组件） |
| `judgeCanvas` | Transform | 判定文字弹出的父容器 |
| `judgeMoveUpSpeed` | float | 判定文字上飘速度（默认 80） |
| `judgeFadeTime` | float | 判定文字淡出时间（默认 1 秒） |
| `perfectWindow` | float | Perfect 判定窗口（秒，默认 0.15） |
| `goodWindow` | float | Good 判定窗口（秒，默认 0.3） |
| `noteDensity` | int | 音符密度 1-4（1=每两拍一个, 2=每拍一个） |
| `noteToleranceMs` | float | 音符判定容差（毫秒，默认 150） |

#### BLEBridge 组件 Inspector 配置

| 参数 | 类型 | 说明 |
|------|------|------|
| `useTcpSimulator` | bool | 勾选=TCP模拟器, 取消=真实BLE |
| `tcpPort` | int | TCP 模拟器端口（默认 9877） |
| `realBlePluginComponent` | MonoBehaviour | 可选，手动指定 BLE 插件组件（不指定则自动查找） |
| `autoConnectOnStart` | bool | 是否在 Start 时自动连接（默认 true） |

### 7.2 歌曲配置

通过菜单 **Create → RhythmGame → SongConfig** 创建歌曲配置资产：

| 参数 | 类型 | 说明 |
|------|------|------|
| `songName` | string | 歌曲名称 |
| `audioClip` | AudioClip | 歌曲音频文件 |
| `bpm` | int | 歌曲 BPM（每分钟节拍数） |
| `totalDuration` | float | 歌曲总时长（秒） |
| `startDelay` | float | 开始前延迟（秒，默认 3） |
| `backgroundSprite` | Sprite | 背景图片（可选） |
| `noteDensity` | int | 音符密度（0=使用控制器默认值） |
| `songIndex` | int | 歌曲编号 |

---

## 八、评分系统

### 8.1 判定规则

| 判定 | 条件 | 基础分 |
|------|------|--------|
| Perfect | 时间差 ≤ 容差窗口 × 0.3 | 100 |
| Good | 时间差 ≤ 容差窗口 | 50 |
| Miss | 超出容差窗口或未操作 | 0 |

### 8.2 Combo 加成

每 50 Combo 增加 10 分加成：

```
最终得分 = (基础分 + Combo加成) × 难度加成
Combo加成 = (当前Combo / 50) × 10
```

### 8.3 评级标准

| 评级 | 得分率 | Perfect 率 |
|------|--------|------------|
| SSS | ≥ 98% | ≥ 95% |
| SS | ≥ 95% | ≥ 85% |
| S | ≥ 90% | - |
| A | ≥ 80% | - |
| B | ≥ 70% | - |
| C | ≥ 60% | - |
| D | ≥ 50% | - |
| None | < 50% | - |

---

## 九、Android 打包配置

### 9.1 权限

项目已包含 `Assets/Plugins/Android/AndroidManifest.xml`，声明了以下权限：

| 权限 | 用途 | 适用 Android 版本 |
|------|------|-------------------|
| `BLUETOOTH` | 基础蓝牙通信 | 4.3+ |
| `BLUETOOTH_ADMIN` | 蓝牙管理 | 4.3+ |
| `ACCESS_FINE_LOCATION` | BLE 扫描需要定位 | 6.0-11 |
| `ACCESS_COARSE_LOCATION` | 备用定位权限 | 6.0-11 |
| `BLUETOOTH_SCAN` | 新蓝牙扫描权限 | 12+ |
| `BLUETOOTH_CONNECT` | 新蓝牙连接权限 | 12+ |

### 9.2 Player Settings

在 Tuanjie 编辑器中：
1. **File → Build Settings** → 选择 **Android**
2. **Player Settings** 中确认：
   - **Minimum API Level**: 22 (Android 5.1) 或更高
   - **Target API Level**: 自动或 33+
   - **Scripting Backend**: IL2CPP 或 Mono
   - **Target Architectures**: ARMv7 和/或 ARM64

### 9.3 打包步骤

1. 确保 `useTcpSimulator` 取消勾选
2. 确保 BLEBridge 所在 GameObject 上有 `RealBlePlugin` 组件
3. Build 并安装到 Android 手机
4. 首次运行会弹出权限请求，允许蓝牙和定位权限

---

## 十、调试与排错

### 10.1 常用日志标签

在 Console 或 `adb logcat` 中过滤以下标签：

| 标签 | 来源 | 说明 |
|------|------|------|
| `[BLEBridge]` | BLEBridge.cs | BLE 桥接状态 |
| `[RealBlePlugin]` | RealBlePlugin.cs | BLE 连接过程 |
| `[TcpBleSimulator]` | TcpBleSimulator.cs | TCP 模拟器状态 |
| `[BLE]` | BLECommunicationManager | 协议解析错误 |

### 10.2 常见问题

**Q: 扫描超时，未找到设备**
- 确认手机蓝牙已开启
- 确认手表处于可发现状态
- 检查 `deviceName` 是否与手表蓝牙名一致（区分大小写）
- 确认已授予定位权限（Android 6-11 BLE 扫描必需）

**Q: 连接成功但未收到数据**
- 查看 Console 中枚举的服务列表，确认 `serviceUuid` 和 `characteristicUuid` 正确
- 确认目标特征有 `N`（Notify）属性
- 查看是否有 `订阅通知失败` 的错误日志

**Q: 收到数据但判定不生效**
- 确认数据格式是否为 39 字节
- 检查 `BLECommunicationManager.ParsePacket()` 是否有解析错误
- 确认 `BLEBridge.SetConnection(true)` 被调用（当前代码在 InitWristController 中自动调用）

**Q: 编辑器中 BLE 不工作**
- 正常现象。`RealBlePlugin` 仅在 Android 真机上运行
- 编辑器中请使用 TCP 模拟器模式测试

**Q: 权限被拒绝**
- Android 设置 → 应用 → 找到本应用 → 权限 → 开启"位置信息"和"附近的设备"

### 10.3 adb logcat 调试命令

```bash
# 查看所有 RealBlePlugin 日志
adb logcat | grep RealBlePlugin

# 查看所有 BLE 相关日志
adb logcat | grep -E "BLE|BLEBridge|RealBlePlugin|TcpBleSimulator"

# 查看 Unity 日志
adb logcat -s Unity

# 查看错误
adb logcat *:E | grep -i ble
```

---

## 十一、扩展开发

### 11.1 添加新手势类型

1. 在 `WristConductorData.cs` 的 `GestureType` 枚举中添加新手势
2. 手表固件端添加对应的手势识别
3. 在 `RhythmGameController` 或 `BeatJudgeManager` 中处理新手势的判定逻辑

### 11.2 自定义谱面

当前音符根据 BPM 自动生成。如需自定义谱面：
1. 修改 `RhythmGameController.GenerateMusicNotes()` 方法
2. 或创建外部谱面文件（JSON/CSV），在运行时加载
3. 每个 `MusicNote` 支持独立的 `gestureType`、`timestamp`、`toleranceWindow`

### 11.3 接入其他 BLE SDK

如需替换 Android 原生 API 为第三方 BLE SDK（如 Unity BLE Central 等）：
1. 创建新类实现 `IBlePlugin` 接口
2. 实现 `Begin()`、`Stop()`、`IsConnected`、三个事件
3. 在 BLEBridge Inspector 中将 `realBlePluginComponent` 指向新组件
4. 或将新组件挂到同一 GameObject，BLEBridge 会自动发现

### 11.4 IBlePlugin 接口规范

```csharp
public interface IBlePlugin
{
    bool IsConnected { get; }                              // 当前连接状态
    event Action<byte[]> OnDataReceived;                   // 收到原始数据
    event Action<bool> OnConnectionChanged;                // 连接状态变化
    event Action<string> OnError;                          // 错误信息

    void Begin();   // 启动（扫描/连接）
    void Stop();    // 停止（断开/清理）
}
```

实现此接口后，BLEBridge 会自动适配，无需修改其他代码。

---

## 十二、文件版本记录

| 文件 | 说明 |
|------|------|
| `BLEBridge.cs` | BLE 桥接器，支持 TCP/真实 BLE 双模式切换 |
| `IBlePlugin.cs` | BLE 插件接口定义 |
| `TcpBleSimulator.cs` | TCP 模拟器，端口 9877 接收 39 字节包 |
| `RealBlePlugin.cs` | Android 原生 BLE 实现，自动扫描/连接/订阅 |
| `BleGattCallback.cs` | Android BLE 回调代理（GATT + Scan） |
| `RhythmGameController.cs` | 音游主控制器，管理游戏流程 |
| `NoteTrack.cs` | 音符轨道视觉管理 |
| `SongConfig.cs` | 歌曲配置 ScriptableObject |
| `SelectedSongInfo.cs` | 当前选中歌曲静态类 |
| `zhengquedu.cs` | 节拍判定管理器（独立场景实现） |
| `AndroidManifest.xml` | Android 蓝牙权限声明 |
| `ble_simulator.py` | Python TCP 手表模拟器 |
