---
name: web-access
description: 通过 bb-browser 用本机真实 Chrome 上网取信息。当需要 WebFetch/WebSearch、抓取某个网址、搜索资料、查文档/论文/新闻/股票/视频字幕，而内置的 WebFetch 或 WebSearch 失败或被域名校验拦下时使用。凡是"上网查一下"、"打开这个链接"、"搜索 XX"类需求都走这里。
---

# 上网取信息 (bb-browser)

本环境的 `WebFetch` / `WebSearch` 对多数域名会被安全校验拦下（报 `Unable to verify if domain is safe to fetch`）。**bb-browser 是可靠的替代通道**：它驱动本机真实 Chrome，带你的登录态，直接取数据。

## 本机安装状态（2026-09-21 实测）

| 项 | 值 |
|---|---|
| npm 全局包 | `bb-browser@0.14.2`（最新版；2026-05-29 发布） |
| 命令 | `bb-browser` → `C:\Users\hjzh\AppData\Roaming\npm\bb-browser.cmd` |
| 社区 adapter | 已装 **145 个**（`bb-browser site update` 从 github.com/epiral/bb-sites 拉取） |
| 架构 | `CLI → daemon(127.0.0.1:19824) → CDP → 真实 Edge`（见下节，已从 Chrome 改为 Edge） |

⚠️ **不要走 MCP**。虽然该项目的 README 写了 `{"command":"npx","args":["-y","bb-browser","--mcp"]}` 的 MCP 配置，但实测 **0.14.2 的代码里一处 "mcp" 都没有**（`--mcp` / `mcp` 都只落到帮助页），README 与实现不符。所以它**没有**进 `.mcp.json`，只能走 CLI。

## 用哪个浏览器：Edge（打过补丁）

`bb-browser` 用 `findBrowserExecutable()` 挑浏览器，Windows 下是**固定候选表 + `.find(第一个存在的)`**。原版顺序把 Chrome 排在 Edge 前面，本机装着 Chrome，所以**默认永远启 Chrome**——而且没有任何配置项能改这个顺序（无 `--browser` 参数，无环境变量）。

2026-09-22 按用户要求改写为 **Edge 优先**：

```
文件：C:\Users\hjzh\AppData\Roaming\npm\node_modules\bb-browser\dist\cli.js
改动：findBrowserExecutable() 的 win32 候选表，把两条 msedge.exe 提到 chrome.exe 之前
备份：同目录 cli.js.orig-chrome
```

⚠️ **`npm i -g bb-browser` 升级会覆盖这个补丁**，之后要重新打。判断是否被打回：

```bash
sed -n '123,126p' "$(npm root -g)/bb-browser/dist/cli.js"   # 第一行应为 msedge.exe
```

另有一条不改代码的路子（若不想打补丁）：自己用 `--remote-debugging-port=9222` 起 Edge，然后设 `BB_BROWSER_CDP_URL=http://127.0.0.1:9222`，`discoverCdpPort()` 会优先直连、不再自己启浏览器。

**两个必须知道的点**：

- 它用的是**独立的干净 profile**（`~/.bb-browser/browser/user-data`），**不是你个人的 Edge 配置**——没有书签和扩展，登录态要在那个 profile 里单独登一次（登完会持久保存）。
- 它**从来不会**用 IE（代码里 `iexplore` 一处都没有）。曾经误以为是 IE，实际是 Chrome 的干净 profile 窗口看着陌生。

## 用前先起 daemon

CLI 不自启 daemon，未启动时会报 `Daemon not running`：

```bash
bb-browser daemon start      # 幂等，已在跑就返回 "Daemon started"
bb-browser daemon status     # 查状态
```

`daemon start` 会拉起/接入 Chrome 并建立 CDP 连接，之后 `site` 类命令即可用。

## 取数据：site 命令（首选）

145 个社区 adapter，覆盖 36 个平台。**先用这三个找命令**：

```bash
bb-browser site list              # 列出全部 adapter
bb-browser site recommend         # 按你的浏览习惯推荐
bb-browser site info <name>       # 看某 adapter 的参数/返回值/示例
```

常用示例：

```bash
bb-browser site wikipedia/summary "Python"
bb-browser site baidu/search "关键词"
bb-browser site arxiv/search "transformer"
bb-browser site zhihu/hot
bb-browser site github/search "SAGE engine"
bb-browser site bilibili/search "红警3"
```

全部支持 `--json` 输出和 `--jq <expr>` 内联过滤：

```bash
bb-browser site baidu/search "测试" --jq '.results[] | {title,url}'
```

## 手工操作浏览器（site 覆盖不到时）

```bash
bb-browser open https://example.com      # 新开 tab
bb-browser tab list                      # 看 tab id
bb-browser snap -i --tab <id>            # 可交互元素快照
bb-browser click @3 --tab <id>           # 点击
bb-browser fill @5 "文本" --tab <id>      # 填输入框
bb-browser get text|url|title --tab <id> # 读页面
bb-browser eval "document.title" --tab <id>
bb-browser screenshot [path] --tab <id>  # 截图
bb-browser fetch <URL> --json            # 带登录态直接 fetch
bb-browser network requests --with-body --tab <id>   # 抓包
bb-browser cookies --tab <id>            # 看 cookie
bb-browser console / errors --tab <id>   # 控制台 / JS 错误
```

多数浏览器操作要 `--tab <id>`，先用 `bb-browser tab list` 拿 id。

## 实测记录（2026-09-21）

**可用**：`wikipedia/summary` ✓、`baidu/search` ✓（16 条结果）、`arxiv/search` ✓

**该版本有 bug**：`bing/search`（adapter 内用了相对 URL，`Failed to parse URL`）、`duckduckgo/search`（`Failed to fetch`）

→ **个别 adapter 失败是 adapter 自身的质量问题，不代表工具坏了**。换一个同类 adapter（如 bing 换 baidu）或改用 `open` + `snap` + `get text` 手工走。

## SPA 页面要等渲染（实测教训）

前端渲染的站点（DeepWiki、各类 Next.js/React 站），`open` 返回时**页面还是空的**，别急着下"取不到"的结论。

实测 `https://deepwiki.com/hunt1hunt/dx9MinGeneralsmd`：

| 时刻 | `document.body.innerText` |
|---|---|
| open 后立即 | `Loading...`（仅 10 字符） |
| 等 8 秒 | 标题已出，正文仍空 |
| 等 35 秒 | 全文 6,953 字符才出来 |

正确姿势是**轮询等正文**：

```bash
bb-browser open <url>                                    # 记下返回的 tab id
bb-browser eval "document.body.innerText.length" --tab <id>
sleep 15
bb-browser eval "document.body.innerText.slice(0,3000)" --tab <id>   # 分段取全文
```

取全文用 `eval "document.body.innerText"` 配 `.slice(a,b)` 分段，比 `get text` 可靠（后者要元素 ref）。正文迟迟不出时，用 `bb-browser errors --tab <id>` 和 `bb-browser network requests --tab <id>` 区分"还在加载"和"真挂了"。

同理，**有些站点内置 WebFetch 会被域名安全校验直接拦死**（报 `Unable to verify if domain ... is safe to fetch`），bb-browser 不受此限——这正是本技能存在的理由。

## 注意事项

- 它**驱动你的真实浏览器**、带你的**登录态**：会打开/操作真实标签页，某些命令会消耗你的账号配额或留下操作痕迹。涉及发帖、点赞、购买等**写操作前先问用户**。
- daemon 绑定 `127.0.0.1:19824`（默认仅本机）。
- 抓取失败先看 `bb-browser console` / `bb-browser errors`，adapter 报错信息会显示在那里。
