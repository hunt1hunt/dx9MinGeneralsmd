---
name: memory-palace
description: 使用 MemPalace(记忆宫殿)检索和存入本项目的长期记忆。当用户提到"记忆宫殿"、"palace"、"之前怎么修的"、"上次的事故/教训"、"查一下历史决策",或在本项目中开始新任务需要先回顾既往经验、修改完成后需要把经验归档时使用。开始 SAGE 引擎代码修改前,先搜宫殿里的避坑记录。
---

# 记忆宫殿(MemPalace)

本机宫殿(npm 全局 `mempalace@2.0.0`,数据位于 `C:\Users\Administrator\.memorypalace`,已初始化)通过 `mempalace` MCP 使用,工具名以 `mcp__mempalace__` 开头。记忆为加密存储,按 short_id(如 `hgb9ink`)引用。

## 开始任务前:先查记忆

修改 SAGE 引擎代码前,先检索相关避坑记录:

- MCP 首选:宫殿的 search / list / recover 工具
- CLI 兜底:`mempalace list`、`mempalace recover <short_id>`

## 完成任务后:归档经验

```bash
mempalace list                       # 查看近期记忆
mempalace save <json_file>           # 加密签名后存入新记忆
mempalace recover <short_id>         # 恢复某条记忆的上下文
mempalace mcp                        # 手动拉起 MCP 服务(stdio)
```

存入时把经验写成 JSON 文件再 `save`,内容应包含:问题现象、根因、修复方案、避坑要点。

## 展示结果时

- 每条结果标注来源(short_id / 日期 / 录入 agent)
- 命中后给出下一步:recover 深入某条记忆、或继续检索
