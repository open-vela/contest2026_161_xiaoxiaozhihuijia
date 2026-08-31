# Huangshan Motion Game v2

面向黄山派 SF32LB52 的 BLE 动作游戏可靠性原型。设备通过
LSM6DS3TR-C FIFO 连续采集 IMU 数据，接收手机端 Gesture Event，执行测试判定，
在屏幕显示 Game/UI 状态，并通过带 ACK/retry 的 BLE 通道返回结果。

本作品验证的是数据通路、跨时间回绕运行和可靠传输，不宣称已实现通用动作识别产品。

## 架构与硬件

- **BLE**：自定义 GATT 服务 `FFF0`；手机写入 RX `FFF1`，订阅 TX `FFF2`。
  Gesture 使用包序号、ACK 和有限重试，重复包可被识别而不重复计入事件。
- **IMU FIFO**：LSM6DS3TR-C 通过 I2C3 接入，水位中断驱动读取，单次块读
  24 bytes，并维护 overrun、recovery、gap 和 I2C 错误统计。
- **Game**：独立队列消费 Gesture 和 IMU 样本，当前使用确定性的测试判定逻辑，
  用于端到端可靠性验收。
- **UI**：LVGL 页面显示 Motion Game、判定和统计；它不是手机应用替代品。

所需硬件：黄山派 SF32LB52、板载/连接的 LSM6DS3TR-C、USB-UART 和可运行
BLE GATT 测试程序的手机。

## 目录

- `patches/`：apps Probe 与 vendor/sifli 平台支持的独立补丁。
- `scripts/`：安全应用补丁和构建脚本。
- `artifacts/huangshan_motion_game_v2_20260831/`：不可变 v2 固件、配置、源码快照和验证证据。
- `docs/SUBMISSION_VERIFICATION.md`：公共基线、补丁校验及未完成事项。
- `logs/<your-github-login>/`：按官方手册导出并脱敏后的 AI Coding 日志位置；
  提交前必须移除凭据、token、设备标识和无关私人信息，不提交原始串口全文日志。

## 从同步到构建

以下命令从空目录开始。正式作品分支是
`feat/huangshan-motion-game-submission`。

```bash
mkdir openvela-motion-game && cd openvela-motion-game
repo init -u https://github.com/open-vela/contest2026_161_xiaoxiaozhihuijia \
  -b dev-ai-contest-2026 \
  -m contest2026_161_xiaoxiaozhihuijia.xml
repo sync -c -j8

# Draft PR 合入前，从比赛专属 fork 取得正式作品分支。
git -C contest2026_161_xiaoxiaozhihuijia fetch \
  https://github.com/yangshuxuan1024/contest2026_161_xiaoxiaozhihuijia.git \
  feat/huangshan-motion-game-submission
git -C contest2026_161_xiaoxiaozhihuijia switch --detach FETCH_HEAD

./contest2026_161_xiaoxiaozhihuijia/scripts/apply_openvela_patches.sh "$PWD"
./contest2026_161_xiaoxiaozhihuijia/scripts/build_huangshan_motion_game.sh "$PWD"
sha256sum cmake_out/huangshan_motion_game_submission/nuttx.bin
```

应用脚本要求 `apps` 和 `vendor/sifli` 均位于记录的干净
`dev-ai-contest-2026` 基线；它会先检查两个补丁，全部成功后才应用。构建脚本拒绝
覆盖已有构建目录，并以冻结的实际 `.config` 初始化新 CMake/Ninja 构建，其中必须有：

```text
CONFIG_BSP_USING_I2C3=y
CONFIG_EXAMPLES_OPENVELA_BLE_PROBE=y
```

当前已冻结固件位于
`artifacts/huangshan_motion_game_v2_20260831/nuttx.bin`，SHA-256 为：

```text
aa7ce08ca7460c8effdd78ad410631d277c5bee438c1ff0cb5c21da9c8228f3b
```

该 v2 固件是在已验证 v1 CMake 配置基础上重编译 Probe 并多阶段链接得到；本仓不
伪称它已经过干净全量 configure。干净全量构建仍是待补充的可复现性证据。

## 烧录与运行

构建产物或冻结固件写入 SF32LB52 NOR 的 `0x12010000`：

```bash
sftool -c SF32LB52 -p /dev/ttyUSB0 -b 1000000 \
  --before default_reset --after soft_reset \
  write_flash cmake_out/huangshan_motion_game_submission/nuttx.bin@0x12010000

picocom -b 1000000 --noreset --lower-rts --lower-dtr /dev/ttyUSB0
```

如仅复核已验证版本，把烧录命令中的路径替换为冻结 `nuttx.bin`，并在烧录前运行
`sha256sum`，确认与上述 SHA 完全一致。启动后应用名为 `openvela_ble_probe`，设备
广播名为 `ov_ble_probe`。

## 手机端测试

1. 扫描并连接 `ov_ble_probe`。
2. 发现服务 `FFF0`，开启 TX 特征 `FFF2` 的 notification。
3. 按协议向 RX 特征 `FFF1` 写入 Gesture Event；记录事件 packet ID。
4. 验证 `FFF2` 返回对应 ACK/结果。测试端在超时场景重发相同 packet ID，设备应
   ACK 重传但不得把同一事件重复入队。
5. 同时观察串口 Gesture、FIFO 和连接统计；测试结束后的主动断开
   `reason=0x13` 不计为异常。

手机端报文的确切字段、长度和类型常量以冻结的
`openvela_ble_probe_main.c` 为准，避免客户端自行猜测协议布局。

## 已验证结果

- Gesture Event：3000/3000 成功，failed=0。
- 600 次预期重传全部成功，`retry_proofs=600`、`retransmissions_seen=600`。
- 运行约 4,758,490 ms，跨越旧 32-bit 微秒回绕点。
- FIFO：overrun=0、recovery=0、gap=0、unexpected=0、i2c_errors=0。
- FIFO 使用 24-byte 块读。
- 首事件延迟：median `252.71 ms`、p95 `322.98 ms`、max `920.24 ms`。
  最大值是待优化的尾延迟，不构成“低延迟保证”。

这些结论只适用于设备所烧录固件 SHA 与冻结 SHA 一致的情况。

## 明确边界

尚未实现或未完成验收：真实动作识别、LED/马达反馈、OTA、多连接、Bond/配对/
加密验收、低功耗。Framework GATTS、SAL、ZBlue、BTH4/H4、广告、heap 与
Legacy HCI 的诊断实验也不属于本比赛基线，未包含在正式补丁中。

AI Coding 对话需按官方手册导出和脱敏后放入 `logs/<your-github-login>/`；仓库当前
只提供目录规范，不声称已提交本次会话的脱敏导出。
