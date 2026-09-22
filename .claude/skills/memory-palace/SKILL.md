---
name: memory-palace
description: 使用 MemPalace(记忆宫殿)检索和存入本项目的长期记忆。当用户提到"记忆宫殿"、"palace"、"之前怎么修的"、"上次的事故/教训"、"查一下历史决策",或在本项目中开始新任务需要先回顾既往经验、修改完成后需要把经验归档时使用。开始 SAGE 引擎代码修改前,先搜宫殿里的避坑记录。
---

# 记忆宫殿 (MemPalace)

npm 全局包 `mempalace@2.0.0`，通过 `mempalace` MCP 与同名 CLI 使用。记忆加密签名后本地存储，按 short_id（如 `hgb9ink`，7 位）引用。

## 本机安装状态（2026-09-21 实测）

| 项 | 值 |
|---|---|
| npm 全局包 | `mempalace@2.0.0`（上游仓库 Camaraterie/memory-palace） |
| 命令 | `mempalace`（另有别名 `memory-palace`）→ `C:\Users\hjzh\AppData\Roaming\npm\mempalace.cmd` |
| 数据目录 | `C:\Users\hjzh\.memorypalace\config.json` |
| Palace ID | `1fbde8b0-2c0a-43a4-b0b4-e6cfb644bae7`（2026-09-21 在本机 `mempalace init` 新建） |
| MCP 配置 | 仓库根 `.mcp.json` 的 `mempalace` 项 |

⚠️ **本机宫殿是空的**。本机只从另一台机器复制了项目文件目录，工具与宫殿数据都没跟过来，`init` 是全新生成密钥并新注册的 Palace。**不要指望在这里查到另一台机器上存过的记忆。**

⚠️ 旧的 `.claude/settings.local.json` 里登记着一批 `mcp__mempalace__mempalace_search` / `_status` / `_diary_write` 之类的权限——那是**别的（更老的）版本的产物，在 2.0.0 里根本不存在**，别去调这些工具名。

## MCP 用法（CLI 更全，MCP 只有两个工具）

`.mcp.json`（**必须放 git 仓库根**）：

```json
"mempalace": {
  "type": "stdio",
  "command": "mempalace",
  "args": ["mcp"]
}
```

v2.0.0 **只暴露 2 个 MCP 工具**：

- `recover` — 入参 `short_id`（7 位），返回该条记忆的历史上下文
- `save` — 必填 `session_name`、`agent`、`status`、`outcome`（枚举 `succeeded`/`failed`/`partial`/`in_progress`）；可选 `built`、`decisions`、`next_steps`、`files`、`blockers`、`conversation_context`、`repo`、`branch`。配置了 `GEMINI_API_KEY` 时额外返回图片 URL

其余能力（`list` / `verify` / `scan` / `share` / `invite` 等）走 CLI。

## CLI 用法

```bash
mempalace list                       # 查看近期记忆（含 short_id）
mempalace save <json_file>           # 加密签名后存入新记忆
mempalace recover <short_id>         # 恢复某条记忆的上下文
mempalace verify <short_id>          # 校验签名
mempalace agents                     # 列出各 agent 及其 guest key
mempalace share <short_id>           # 生成自包含 Python 解密片段（给网页版 agent 用）
mempalace scan <image_path>          # 从图片提取记忆上下文
mempalace mcp                        # 手动拉起 MCP 服务（stdio）
```

## 工作流

**开始任务前** —— 修改 SAGE 引擎代码前先查避坑记录：`mempalace list` 看近期条目，`mempalace recover <short_id>` 深入某条。

**完成任务后** —— 把经验写成 JSON 文件再 `save`，内容应包含：**问题现象、根因、修复方案、避坑要点**。

```json
{
  "session_name": "W3X 贴图阴影丢失",
  "agent": "claude-code",
  "status": "已完成",
  "outcome": "succeeded",
  "decisions": ["真凶是凹凸衰减的数值通路，非指令/布局/fp16/雾"],
  "files": ["GameEngineDevice/Source/W3DDevice/GameClient/..."],
  "blockers": [],
  "next_steps": []
}
```

## 展示结果时

- 每条结果标注来源（short_id / 日期 / 录入 agent）
- 命中后给出下一步：`recover` 深入某条，或继续检索
