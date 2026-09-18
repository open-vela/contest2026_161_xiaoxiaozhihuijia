# 公共依赖 PR

已为五个公共仓准备六份独立签署提交、format-patch 和 PR 正文，目标均为 dev-ai-contest-2026。六份提交已于 2026-09-18 发布为 Draft PR，尚未合并。NuttX 按其单一功能要求拆为两份。

|仓库|草稿|文件数|目标提交|
|---|---|---:|---|
|apps_graphics_lvgl|[lvgl-buffer-ownership](lvgl-buffer-ownership/PR.md)|1|`0f2a49f58850`|
|external_zblue|[zblue-cleanup-diagnostics](zblue-cleanup-diagnostics/PR.md)|4|`2e5bddcc5284`|
|frameworks_bluetooth|[bluetooth-h4-cleanup](bluetooth-h4-cleanup/PR.md)|10|`028b4ba7e393`|
|nuttx|[nuttx-const-allsyms](nuttx-const-allsyms/PR.md)|1|`dd92bcf42573`|
|nuttx|[nuttx-hci-rx-snapshot](nuttx-hci-rx-snapshot/PR.md)|1|`dd92bcf42573`|
|vendor_sifli|[sifli-v28-platform](sifli-v28-platform/PR.md)|10|`af6f365eaa04`|

六份候选均完成空白检查。NuttX const / noconst 两种符号输出模式通过；其他项本轮未增加编译或实机验证。SiFli 已将冲突处理为保留上游默认配置和初始化内容，比赛配置仍由团队仓提供。

这些候选基于最新公共目标分支，尚未替换作品运行所用固定基线。公共 PR 审核与集成验证完成后，再更新运行 manifest；不把候选补丁与原固件哈希混为同一构建。

推荐协调顺序：NuttX 符号表修复和 LVGL 可独立审阅；NuttX HCI 快照、SiFli、ZBlue、Bluetooth 作为有关联的依赖组审阅。公共 PR 正文已互相填写实际链接。

归档中 apps.patch 与 external.patch 记录的是独立复制工作区的符号链接布局变化，不作为公共功能 PR 提交；团队应用由 manifest 映射。

应用步骤：在对应仓的记录目标提交上，使用 `git am <该目录/change.patch>`；若目标分支再次更新，先核对差异并重新检查。各提交含 Signed-off-by，未声明可直接合并。

## 已发布 PR

- [lvgl-buffer-ownership](https://github.com/open-vela/apps_graphics_lvgl/pull/44)：Draft，等待审核与验证。
- [zblue-cleanup-diagnostics](https://github.com/open-vela/external_zblue/pull/233)：Draft，等待审核与验证。
- [bluetooth-h4-cleanup](https://github.com/open-vela/frameworks_bluetooth/pull/593)：Draft，等待审核与验证。
- [nuttx-const-allsyms](https://github.com/open-vela/nuttx/pull/388)：Draft，等待审核与验证。
- [nuttx-hci-rx-snapshot](https://github.com/open-vela/nuttx/pull/389)：Draft，等待审核与验证。
- [sifli-v28-platform](https://github.com/open-vela/vendor_sifli/pull/35)：Draft，等待审核与验证。
