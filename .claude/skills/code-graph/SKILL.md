---
name: code-graph
description: 使用 CodeGraph(代码导图)查询本仓库(SAGE 引擎 C++ 代码库)的结构化代码图谱。当用户要求"画/看代码导图"、"代码结构"、"调用关系"、"谁引用了这个函数/类"、"符号在哪里定义"、"依赖分析"、"影响面分析",或在大型 C++ 代码库中定位符号、追踪调用链时使用。凡是需要在 GeneralsMD/Code 里找代码的场合,优先走 codegraph 而不是全文 grep。
---

# 代码导图 (CodeGraph)

本仓库通过 `codegraph` MCP 提供结构化代码图谱（符号 / 调用图 / 依赖 / 影响面）。

## 本机安装状态（2026-09-25 实测复修）

| 项 | 值 |
|---|---|
| npm 全局包 | `@astudioplus/codegraph-mcp@0.20.1`（Rust 自包含二进制，`--version` 报 `codegraph-server 0.20.1`） |
| 命令 | `codegraph-mcp` → `C:\Users\Administrator\AppData\Roaming\npm\codegraph-mcp.cmd` |
| 索引库 | `%USERPROFILE%\.codegraph\graph.db.<n>\`（RocksDB 目录，按 project slug 分命名空间） |
| MCP 配置 | 仓库根 `.mcp.json` 的 `codegraph` 项 |

⚠️ **本机没有叫 `codegraph` 的命令**——别照抄网上教程。可执行文件是 `codegraph-mcp`。
⚠️ 仓库根 `.codegraph\codegraph.db`（724MB，2026-08-14）是**旧包 `@colbymchenry/codegraph` 的遗留物**，当前工具**不使用**它（已在 `.gitignore`，可留可删）。

## MCP 用法（首选）

`.mcp.json`（**必须放在 git 仓库根**，Claude Code 不读子目录里的 `.mcp.json`）：

```json
"codegraph": {
  "type": "stdio",
  "command": "codegraph-mcp",
  "args": ["--mcp", "-w", "E:\\Source\\repos\\MinGeneralsfreebuild2ok"]
}
```

两个易错点（2026-09-25 断掉的就是这两处）：
- **没有 `serve` 子命令，命令名也不是 `codegraph`**——直接 `codegraph-mcp --mcp`。
- **工作区参数是 `-w/--workspace`，不是 `-p`**。本会话 cwd 常在 `GeneralsMD\Code`，必须显式给仓库根（`-w` 可重复给多个）。

**改完 `.mcp.json` 要重启 Claude Code 才生效**（启动时读一次）。权限侧全局 `settings.json` 的 `permissions.allow` 需含 `"mcp__codegraph__*"` 通配（已加，热加载）。

**默认 profile 暴露 42 个 MCP 工具**，全部以 `codegraph_` 前缀命名，常用的：

| 用途 | 工具 |
|---|---|
| 按名搜符号 | `codegraph_symbol_search`、`codegraph_find_by_signature`、`codegraph_search_by_pattern` |
| 符号详情/源码 | `codegraph_get_symbol_info`、`codegraph_get_detailed_symbol` |
| 调用关系 | `codegraph_get_call_graph`、`codegraph_get_callers`、`codegraph_get_callees`、`codegraph_find_hot_paths` |
| 依赖/架构 | `codegraph_get_dependency_graph`、`codegraph_find_circular_deps`、`codegraph_get_module_summary`、`codegraph_find_entry_points` |
| 影响面/复杂度 | `codegraph_analyze_impact`、`codegraph_analyze_complexity` |
| 攒上下文 | `codegraph_get_ai_context`、`codegraph_get_edit_context`、`codegraph_get_curated_context` |
| 索引维护 | `codegraph_reindex_workspace`、`codegraph_index_directory`、`codegraph_index_files` |

实测 2026-09-25：`initialize` + `tools/list` 正常，启动即完整索引并 `Persisted 856 nodes, 1456 edges to graph.db`。

用 `--profile core|graph|memory|security|all`（或 env `CODEGRAPH_TOOL_PROFILE`）收窄工具面，减少上下文开销。

## 命令行用法

```bash
codegraph-mcp --help                  # 全部开关
codegraph-mcp --info                  # 构建信息（git hash / 构建时间 / rustc 版本）
codegraph-mcp --mcp -w <仓库根>        # 以 MCP(stdio) 模式手动拉起，排障用
```

常用开关：`-w/--workspace <dir>`（可重复）、`-e/--exclude <dir>`、`--max-files <n>`（默认 5000）、`--profile <name>`。

⚠️ **`--max-files` 默认 5000**：本仓库源码文件数接近上限，启动日志出现 `Indexed 5000 files` 说明可能被截断，必要时显式加大。

## 本仓库注意点

- 图谱只覆盖文本源码；`Build/` 产物、`.rar` 打包文件不在图内。
- 首次启动建索引时会打印 `Storage detached — operating in memory-only mode`；正常跑完会跟一条 `Persisted N nodes, M edges to graph.db`——**以后者为准**，前者不必当故障查。
- `~/.codegraph/` 下的 `graph.db.corrupt` / `graph.db.db.corrupt` 是历史隔离产物（2026-09 多次出现），留着不碍事；若反复报 corrupt，把它们连同 `graph.db.*` 挪走让工具重建。
- 定位符号优先用 codegraph 而非 grep：它理解 C++ 语义，能区分同名重载、派生关系与作用域。
