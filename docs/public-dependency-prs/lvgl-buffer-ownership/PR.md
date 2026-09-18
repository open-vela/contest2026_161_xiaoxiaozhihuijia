# fix(nuttx): release owned LCD pixel buffers

目标仓库：https://github.com/open-vela/apps_graphics_lvgl

目标分支：`dev-ai-contest-2026`；准备时目标提交：`0f2a49f588505a00e8b46e25a34581c87291a62a`。

提交分支：`contest161/lvgl-buffer-ownership`；签署提交：`6935b1f97ce2d3499c0ca9ed676fded9bde122ad`。

## Summary

NuttX LCD display_release_cb 原先尝试释放 display 的嵌入式缓冲描述符。记录 lv_malloc 返回的像素缓冲地址，在释放时回收实际分配，并补充初始化失败时的 display 清理。

关联作品：https://github.com/open-vela/contest2026_161_xiaoxiaozhihuijia/pull/3

## Impact

仅修改 NuttX LCD 驱动中的缓冲所有权和失败回收路径；保持显示接口不变。

## Testing

补丁基线与目标版本一致，应用与空白检查通过。既有 v28 ARM 构建已使用该修复；本轮未进行显示创建／销毁的实机压力测试。

本次未修改算法、阈值或已交付固件，未执行 ARM 编译或烧录。历史构建采用 Linux、ARM GCC 10.3.1、CMake 3.22.1、Ninja 1.10.1，目标为黄山派 SF32LB52。原始验证范围见作品仓 evidence/v28_bootfix。

## 提交状态

本 PR 以 Draft 发布，保留上述未验证项。等待维护者审核及实际 CLA / CI 结果；补丁可应用不代表 CI 或完整集成测试通过。

涉及文件：

- `src/drivers/nuttx/lv_nuttx_lcd.c`


## 公共依赖关联

LVGL 与 const-allsyms 可独立审阅；HCI 快照接口、SiFli、ZBlue 与 Bluetooth 需协调审阅及集成验证。

- lvgl-buffer-ownership: https://github.com/open-vela/apps_graphics_lvgl/pull/44
- zblue-cleanup-diagnostics: https://github.com/open-vela/external_zblue/pull/233
- bluetooth-h4-cleanup: https://github.com/open-vela/frameworks_bluetooth/pull/593
- nuttx-const-allsyms: https://github.com/open-vela/nuttx/pull/388
- nuttx-hci-rx-snapshot: https://github.com/open-vela/nuttx/pull/389
- sifli-v28-platform: https://github.com/open-vela/vendor_sifli/pull/35
