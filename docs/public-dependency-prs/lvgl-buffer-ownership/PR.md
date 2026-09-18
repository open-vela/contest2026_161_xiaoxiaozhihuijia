# fix(nuttx): release owned LCD pixel buffers

目标仓库：https://github.com/open-vela/apps_graphics_lvgl

目标分支：`dev-ai-contest-2026`；准备时目标提交：`0f2a49f588505a00e8b46e25a34581c87291a62a`。

计划分支：`contest161/lvgl-buffer-ownership`；本地签署提交：`75b9ccf4077439b6f4b2b9a8dc9bfc298c86c8ec`（尚未发布）。

## Summary

NuttX LCD display_release_cb 原先尝试释放 display 的嵌入式缓冲描述符。记录 lv_malloc 返回的像素缓冲地址，在释放时回收实际分配，并补充初始化失败时的 display 清理。

关联作品：https://github.com/open-vela/contest2026_161_xiaoxiaozhihuijia/pull/3

## Impact

仅修改 NuttX LCD 驱动中的缓冲所有权和失败回收路径；保持显示接口不变。

## Testing

补丁基线与目标版本一致，应用与空白检查通过。既有 v28 ARM 构建已使用该修复；本轮未进行显示创建／销毁的实机压力测试。

本次未修改算法、阈值或已交付固件，未执行 ARM 编译或烧录。历史构建采用 Linux、ARM GCC 10.3.1、CMake 3.22.1、Ninja 1.10.1，目标为黄山派 SF32LB52。原始验证范围见作品仓 evidence/v28_bootfix。

## 提交状态

建议以 Draft 提交并如实保留未验证项。提交后填入实际依赖 PR 链接，检查各仓 CLA、代码规范和实际 CI；不能把补丁可应用视为 CI 或完整集成测试通过。

涉及文件：

- `src/drivers/nuttx/lv_nuttx_lcd.c`
