# 自定义 Skill 交付

位置：desktop/BteIntegration/skills/v28-bte-desktop/SKILL.md，保留原文件。
这是可复用的联调操作规范，不是识别算法，也不是独立软件；仅项目内保存，未全局安装。
用途：防止28B与39B协议混用；明确scene8～10入口、唯一串口读取者、启动一次限制；保存Windows QPC故障经验；按session/event_id对账并区分模拟和真实输入。
调用示例：请读取项目内 BteIntegration/skills/v28-bte-desktop/SKILL.md，按照流程进行电脑联调，并输出逐事件对账结果。
原规范要求读取README和operator_notes。历史operator_notes已与bootfix证据一起移出提交候选，提交候选已新增evidence/operator_notes.md说明本轮未联调；首次在original环境使用时应追加新的真实操作记录；本交付不声称该Skill已在原版固件完整复现。
原历史实测：修正后7条真实事件产生游戏反馈；初始2条失败保留。这个结论仅属于历史bootfix联调，不是本次original固件验收。
比赛官方 contest-log-collector 是上游工具，不计为本队原创Skill。AI日志兼容性及统计另见上一轮归集材料，本包未混入原始AI会话。
