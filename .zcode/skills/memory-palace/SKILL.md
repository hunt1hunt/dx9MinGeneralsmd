---
name: memory-palace
description: 使用 MemPalace(记忆宫殿)检索和存入本项目的长期记忆。当用户提到"记忆宫殿"、"palace"、"之前怎么修的"、"上次的事故/教训"、"查一下历史决策",或在本项目中开始新任务需要先回顾既往经验、修改完成后需要把经验归档时使用。开始 SAGE 引擎代码修改前,先搜宫殿里的避坑记录。
---

# 记忆宫殿(MemPalace)

本项目的宫殿位于 `C:\Users\hjzhhzc\.mempalace`,当前约 791 个抽屉,已建两翼:

- **wing `MinGenerals`**:rooms `root-cause`(根因分析)、`fix-in-progress`(进行中的修复)
- **wing `code`**:rooms `gameengine`(607)、`libraries`(107)、`build`(45)、`gameenginedevice`(15)、`general`

通过 `mempalace` MCP 使用,工具名以 `mcp__mempalace__` 开头。

## 开始任务前:先查记忆

修改 SAGE 引擎代码前,先检索相关避坑记录(如跨 DLL 调用、shader 签名、Edit 工具编码等历史事故):

- MCP 首选:`mempalace_search(query, wing?, room?)`,必要时 `mempalace_list_wings` / `mempalace_list_rooms(wing)` / `mempalace_get_taxonomy` / `mempalace_traverse(room)`
- CLI 兜底:`mempalace search "query" --wing MinGenerals`

## 完成任务后:归档经验

根因分析放入 `MinGenerals` 翼,代码知识放入 `code` 翼:

```bash
mempalace mine <目录或文件>            # 项目文件挖掘
mempalace search "关键词"              # 检索
mempalace status                       # 查看宫殿现状
mempalace wake-up                      # L0+L1 唤醒上下文(约 600-900 tokens)
```

## 展示结果时

- 每条结果标注来源(wing / room / drawer)
- 多条命中按 wing/room 分组
- 命中后给出下一步:深入某个 room、遍历知识图谱、或缩小查询
