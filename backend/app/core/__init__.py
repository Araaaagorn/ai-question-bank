"""横切能力层：安全 / AI 客户端 / 额度 / 审计 / 重复检测。

规划（对应 docs/AI题库项目整体预期功能说明书.md）：
- security.py   ：JWT 鉴权、密码哈希、API Key 加密（cryptography）
- ai.py         ：大模型统一代理客户端（OpenAI 兼容协议，多供应商）
- quota.py      ：额度层级（老师默认/学生级/题目级）与扣减
- audit.py      ：审计日志（AI 调用、审核操作、额度扣减）
- duplicate.py  ：重复提问检测（向量检索 + 关键词，P1）
"""
