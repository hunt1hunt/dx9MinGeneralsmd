---
name: code-graph
description: 使用 CodeGraph MCP(代码导图)浏览和检索本仓库(SAGE 引擎 C++ 代码库)的结构化代码图谱。当用户要求"画/看代码导图"、"代码结构"、"调用关系"、"谁引用了这个函数/类"、"符号在哪里定义"、"依赖分析"、"影响面分析",或在大型 C++ 代码库中定位符号、追踪调用链时使用。凡是需要在 GeneralsMD/Code 里找代码的场合,优先走 codegraph MCP 而不是全文 grep。
---

# 代码导图(CodeGraph)

本仓库已建有 CodeGraph 索引(数据库位于仓库根 `.codegraph/codegraph.db`,约 1600+ 个文件已同步,守护进程带文件监听自动增量同步)。通过 `codegraph` MCP 使用,工具名以 `mcp__codegraph__` 开头。

## 何时用

- 定位函数/类/类型的定义与所有引用(比 grep 准确,理解 C++ 语义)
- 追踪调用链、分析改动影响面
- 浏览模块结构(GameEngine / GameEngineDevice / Libraries / Main / Base 等)
- 任何"这个符号在哪、谁用它"的问题

## 用法

优先调用 MCP 工具(本仓库此前已授权 `mcp__codegraph__*` 全量工具):

1. 符号检索:用 codegraph 的搜索工具按名称查符号,拿到定义位置与引用列表。
2. 调用/引用追踪:对目标符号查询 callers/callees/references。
3. 影响面:修改前先列出所有引用方,逐一确认。

若 MCP 不可用,CLI 兜底(在仓库根运行):

```bash
codegraph --help          # 查看子命令
codegraph serve --mcp     # 手动拉起 MCP 服务(daemon 默认自动运行)
```

## 本仓库注意点

- 图谱只覆盖文本源码;`.rar` 打包文件、`Build/` 产物不在图内。
- 文件监听对被占用的 `.exe`(如 test_vc6.exe)会报 EBUSY,属正常噪音,不影响索引。
- 索引增量同步较大改动后可能滞后几秒,查不到新符号时先等同步或手动触发 sync。
