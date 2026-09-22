---
name: code-graph
description: 使用 CodeGraph(代码导图)查询本仓库(SAGE 引擎 C++ 代码库)的结构化代码图谱。当用户要求"画/看代码导图"、"代码结构"、"调用关系"、"谁引用了这个函数/类"、"符号在哪里定义"、"依赖分析"、"影响面分析",或在大型 C++ 代码库中定位符号、追踪调用链时使用。凡是需要在 GeneralsMD/Code 里找代码的场合,优先走 codegraph 而不是全文 grep。
---

# 代码导图 (CodeGraph)

本仓库已建有 CodeGraph 索引，通过 `codegraph` MCP 与同名 CLI 使用。

## 本机安装状态（2026-09-21 实测）

| 项 | 值 |
|---|---|
| npm 全局包 | `@colbymchenry/codegraph@1.6.0`（自包含运行时；Windows 平台包 `@colbymchenry/codegraph-win32-x64`） |
| 命令 | `codegraph` → `C:\Users\hjzh\AppData\Roaming\npm\codegraph.cmd` |
| 索引库 | `<仓库根>/.codegraph/codegraph.db` |
| 规模 | 4,260 文件 / 114,435 节点 / 379,894 边（cpp 3,883、c 343） |
| MCP 配置 | 仓库根 `.mcp.json` 的 `codegraph` 项 |

**索引不随 git 走**：`.codegraph/` 已被 `.gitignore` 忽略，每台机器各建一份。换机器后若 `codegraph status` 报无索引，需重新 `codegraph init`。

## MCP 用法（首选）

`.mcp.json` 中（**必须放在 git 仓库根**，Claude Code 不读子目录里的 `.mcp.json`）：

```json
"codegraph": {
  "type": "stdio",
  "command": "codegraph",
  "args": ["serve", "--mcp", "-p", "E:\\Source\\repos\\MinGeneralsfreebuild2ok"]
}
```

`-p` 是**必需的本地调整**：本会话 cwd 通常是 `GeneralsMD\Code`，而索引在仓库根；不给 `-p` 时服务会按客户端上报的根路径去找 `.codegraph/`，找不到。

**v1.6.0 只暴露 1 个 MCP 工具**：`codegraph_explore`（参数 `query`、可选 `maxFiles`、`projectPath`）。
网上教程里常见的 `codegraph_search` / `codegraph_node` / `codegraph_callers` / `codegraph_files` **在 1.6.0 里不存在**——它们只是 CLI 子命令，别去调这些 MCP 工具名。

优先用 `codegraph_explore`：一次调用返回相关符号的**逐字源码**（按文件分组，等同于已经 Read 过）+ 它们之间的调用路径，通常一次就够。

## CLI 用法（覆盖面比 MCP 大）

```bash
cd /e/Source/repos/MinGeneralsfreebuild2ok      # 或在任意子目录，用 -p 指定

codegraph explore "W3DWater reflection"         # 等价于 MCP 的 codegraph_explore
codegraph query IShaderManager                  # 按名字搜符号
codegraph node WaterRenderObjClass::render      # 单个符号的源码 + caller/callee 轨迹
codegraph callers update                        # 谁调用了它
codegraph callees render                        # 它调用了谁
codegraph impact Thing::setPosition             # 改动影响面（-d 控制深度）
codegraph files --filter W3DDevice              # 从索引看目录结构
codegraph context "修水面倒影"                   # 按任务攒上下文
codegraph status                                # 索引统计
codegraph sync                                  # 手动增量同步
```

**路径参数有两种写法，别搞混**：
- `query` / `explore` / `node` / `callers` / `callees` / `impact` / `files` → 用 **`-p <path>`**
- `status` / `sync` / `index` / `init` → 用**位置参数** `codegraph status <path>`

多数子命令支持 `-j/--json`，便于脚本消费。不确定时 `codegraph <子命令> --help`。

## 本仓库注意点

- 图谱只覆盖文本源码；`Build/` 产物、`.rar` 打包文件不在图内。
- 文件监听对被占用的 `.exe`（如 `test_vc6.exe`、`test_d3d9.exe`）会报 `EBUSY` —— **正常噪音，不影响索引**，不必当故障查。
- 守护进程带文件监听，改动后约 1 秒增量同步；刚改完源码查不到新符号时，稍等或手动 `codegraph sync`。
- 定位符号优先用 codegraph 而非 grep：它理解 C++ 语义，能区分同名重载、派生关系与作用域。
