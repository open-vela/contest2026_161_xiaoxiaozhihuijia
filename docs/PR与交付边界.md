# PR与交付边界

官方仓说明：https://github.com/open-vela/contest2026_161_xiaoxiaozhihuijia/blob/dev-ai-contest-2026/README.md
旧提交参考：https://github.com/open-vela/contest2026_161_xiaoxiaozhihuijia/pull/3

标准PR-CI：作品变更提交自己的fork，经PR进入专属仓；公共仓的修改需分别fork对应公共仓并向dev-ai-contest-2026提交PR，由组委会review。需完成CLA及相关检查。源码归档里的依赖补丁不等于公共仓PR已经通过。
本轮没有push、PR修改、评论、GitHub登录配置变更，也没有编译、烧录和算法/连接优化。

submission为本地提交候选，不是已经可最终上传的包。review_only不进入提交：历史bootfix桌面证据、特定机器/会话的历史汇总工具、完整源码审计归档等。
bootfix固件、固定启动区程序和相关构建脚本均不提交。原方式失败前的中间nuttx.bin也不提交。
目前缺项：v28 original启动布局兼容性检查与实机验收；独立目录干净构建；官方AI日志补录；公共依赖PR-CI。
已有7条事件联调使用的是排除的bootfix版本，因此不能移用为original版验收。
