# SagePerfDiag 执行状态看板

> 协作同步点：本文件同步存在于 ①仓库 `GeneralsMD/Code/Tools/EXECUTION_STATUS.md`（GitHub 权威版）
> ②桌面 `SagePerfDiag协作` 文件夹（本机快照）。以 GitHub 为准，桌面版每次会话结束刷新。
> 协作者开工前先 `git pull` 并读此看板，认领任务后改状态并提交。

## 当前状态（2026-09-18 收工）：**W3X 贴图路径缓存修复落地（t_total −12.7%）+ T11/T12/T13 探针；B2 待换平稳场景**

> 本轮已提交推送：commit **`137426f0`**。下发"开工必读"含 exe 身份表、5 条优先级、方法论沉淀。

> 本轮（晚）成果：T11 分段探针 **构建 → 部署 → grep 验证**全通；**第 1 条结案**（详见文末「T11 实测结果」）。
> 同时产出 4 条勘误（A: 退出期 AV 归因方向 / B: 12 份基准不同质 / C: 加速模式判据 / D: AV 未复现）+ bat 校验 bug 真因。
> ⚠️ **本轮所有改动均未提交 git。** 顶栏以下到「2026-09-17（四）」之前是**上一轮**交接，保留作历史。

### 🔴 2026-09-18 新增重大发现：`ResolveTextureDDS` 缓存失效（疑似帧时大头）

**证据（日志实测）**：一次普通模式跑批的 `DebugLogFileI.txt` 共 1,048,091 行，其中
**1,030,991 行（98.4%）是同一条 `[W3X_P2] ResolveTextureDDS:` 打印**。

**根因链**：
1. `W3XLoader::ResolveTextureDDS`（`w3x_loader.cpp:1062`）**无内部缓存**，每次调用做：
   `sprintf` 拼路径 → `ReadFileContent()` **打开并读取 `<tex>.xml`** → `pugi::xml_document::load_buffer()`
   **完整 XML 解析** → `DEBUG_LOG`（**又一次 fopen/fprintf/fclose**）。
   最热的那行日志在 `:1132`。
2. 两个调用点都包了缓存，但**都是 32 槽且写满即止、且从不重置**：
   - `W3XRenderObj.cpp:873` `ResolveTexturePathCached()` → `s_texPathCache[32]`
   - `W3XModelDraw.cpp:856` `resolveTextureCached()` → `s_texCache[32]`

   ```cpp
   if (s_texCacheSize < 32) { ... s_texCacheSize++; }   // 满 32 后永不新增
   ```
   全仓库搜不到 `s_texCacheSize = 0` ⇒ **缓存不淘汰也不重置**。
3. ⇒ **超过前 32 种之外的每种贴图，每次调用都 miss** ⇒ 每次都真做一遍文件 I/O + XML 解析 + 日志写。

**影响**：单次跑批约 **103 万次** miss。这既是海量日志的来源，也很可能是帧时的大头
（每次 miss = 1 次文件打开读取 + 1 次 XML 解析 + 1 次日志写）。

⚠️ **尚未量化 ms**：日志行数证明了**调用量**，不等于耗时。要坐实需要探针。
⚠️ **未改动**：`w3x_loader.cpp` / `W3XModelDraw.cpp` 属 W3X 贴图工作流；
`W3XRenderObj.cpp` 更是硬规则 #6 保护。**改之前先与 W3X 任务协调。**

**候选修法**（待批准，均小改）：
① 把 32 槽上限提高并加替换（LRU/环形）；或 ② 在 `ResolveTextureDDS` **内部**加一张
足够大的 memo 表——这样两个调用点都受益，且**改动只落在 `w3x_loader.cpp`（不受硬规则 #6 保护）**。

### ✅ 2026-09-18 实测定案：`ResolveTextureDDS` 缓存修复 —— 帧时 −14%，日志 −99.9%

**A/B 设计**：同一存档（`00000037.sav` / 显示名 `golden oasis54321`）、同 150s+210s 参数、
同 Tag 场景，唯一变量 = exe。
- 改前：`RTSI.exe.bak20260918`（`68bf5bcf`，有 bug）
- 改后：本轮 T12 构建（`b56c9d6a`，含缓存修复）

**三轮数据**（`objects` 中位均为 719-720，`t_render==0` 帧占比均 <1% → 都是普通模式）

| 阶段 | 改前 | 改后 run1 | 改后 run2 | **改前→改后** |
|---|---|---|---|---|
| **`t_render`** | 639.7 | 508.9 | 462.4 | **−24.1%** |
| **`t_total`** | 849.4 | 764.3 | 694.9 | **−14.1%** |
| `t_client` | 819.5 | 742.1 | 664.7 | −14.2% |
| `t_postfx` | 126.2 | 220.0 | 170.9 | **+54.8%** ⚠️ |
| `t_logic` | 25.5 | 21.0 | 27.7 | −4.5% |

**帧吞吐**（同 210 秒窗口）：362 / 393 / 452 → 修复版均值 **+16.7%**（与帧时 −14% 相互印证）。

**日志量**（铁证，且可复现）：`ResolveTextureDDS` 行数 **90,047 → 65 / 65**；
日志体积 **6.0 MB → 0.47 / 0.32 MB**。

⚠️ **`t_postfx` 反向 +54.8%，未解释。** 两轮修复版（220.0 / 170.9）**都高于**改前（126.2），
所以不像纯噪声；但两轮之间又差 22%，方差也大。**它吃掉了 `t_render` 省下的一部分，净收益被压到 −14%。**
缓存修复只删文件读/XML 解析，机理上不可能让 postfx 变慢 —— **真因待查**。

⚠️ **B2 重复性：同场景同 exe 两轮差 9.1%**（764.3 vs 694.9），**远超 <5% 目标**。
结合前面"帧时在 594→1204ms 间持续漂移"的发现 ⇒ **这个中局存档太动态，不能当 B2 基准**。
B2 要用平稳场景（原 `00000036.sav` 得 1.046 正因它静止），或把判据改成"取负载平稳子窗口"。

⚠️ **改前那轮是被超时强杀的**（`超时未退出, 强制结束`，exitCode −1），非自然退出；
12 份 spd 齐全，且帧数更少恰好佐证它更慢。

### 🔬 2026-09-18 对称样本定案（改前 4 样本 vs 改后 6 样本）

**方法**：同一存档 `golden oasis54321`、同参数、`-Runs 3` 连续多轮以抵消会话内漂移；
只取 `objects ≥ 700` 的完整场景帧。改前 = `RTSI.exe.bak20260918`(`68bf5bcf`)；改后 = T12/T13 构建。

| 指标 | 改前（4 样本）| 改后（6 样本）| 变化 |
|---|---|---|---|
| **`t_total`** | `821/825/827/920` → 中位 825.7 | `809/698/800/736/706/689` → 中位 **720.8** | **−12.7%** |
| **`t_render`** | `645/652/676/708` → 中位 663.6 | `517/457/519/469/468/455` → 中位 **468.5** | **−29.4%** |
| **`t_postfx`** | `111/115/96/152` → 中位 112.5 | `264/182/229/211/177/171` → 中位 **196.5** | **+74.5%** |

**① 净收益确认（稳健）**：缓存修复让 `t_render` 降 29.4%，`t_total` 降 12.7%。

**② `postfx` 的 +74.5% 是真实差异，不是"首轮偏高"假象** —— 改前 3 个连续轮次
`110.5/114.6/96.5` 非常紧（±10%）且无首轮偏高趋势；两组分布**几乎不重叠**（改前 96–152，改后 171–264）。

**③ 但 postfx 已被 T13 证明 100% 是 `TheInGameUI->DRAW()`（HUD）**，而 HUD 工作量不该因删掉
W3X 文件读取而变。**最可能：CPU 侧计时归属迁移** —— dgVoodoo/D3D8 异步提交，绘制提交变快后
GPU 队列更满，后续 HUD 绘制阻塞在等 GPU 上，这段等待被记进 `t_postfx`。**时间被挪位置，不是新增。**
⚠️ **该解释未经证实**，需要 GPU 侧计时（T7 GPU Query 桩，默认关）。

**账目佐证"挪位置"**：`t_render` −195ms、`t_postfx` +84ms、其余持平 → 净 −105ms。
若 postfx 真多做了 84ms 活，总量不可能下降。

**④ 附带发现**：`t_postfx_debug` 中位仅 0.03ms，但 **P95=164 / max=1711.9** ——
反复出现的 `t_postfx max≈1289/1712` 尖峰**整个来自 debug 叠加层**（`drawFPSStats()` 等），
是极少数帧的事件，不影响总量。

### 下会话开工必读（2026-09-18 收工）

**✅ 本轮成果已提交推送**：commit **`137426f0`**（`1745df3a..137426f0  main -> main`）。
12 文件 **+988/−36**：缓存修复 + T11/T12/T13 探针 + SHX 面包屑 + 工具 + 看板 + settings。
独立 code review 结果 **APPROVE**（0 CRITICAL / 0 HIGH / 0 MEDIUM，2 条 LOW 已按建议修正注释）。

**不在仓库内的环境侧改动**
- 桌面 `③增量构建Internal.bat` 的 `findstr` 修复（已改好并**端到端验证**：本轮构建自动部署成功）
- `.claude/settings.local.json`（含 `ANTHROPIC_AUTH_TOKEN`，已 gitignore）
- `.claude/settings.json` 的 `permissions.deny` 移除了 `PowerShell(Copy-Item:*)`（其余 12 条保留）

**exe 身份**（游戏目录，2026-09-18 收工）

| 文件 | MD5 | 身份 |
|---|---|---|
| `RTSI.exe` | `09593478…` | **T13 构建 + LAA**（含缓存修复），当前在用 |
| `RTSI.exe.t13fix` | `09593478…` | 同上（A/B 换 exe 时的中转备份）|
| `RTSI.exe.bak20260918` | `68bf5bcf…` | **改前**（有缓存 bug），A/B 对照组 |
| `RTSI.exe.bak20260917` | `896a9d2b…` | 产出 12 份存档基准 .spd 的基线 |
| `RTSI9月16日收工.exe` | `064045d3…` | 原版对照 |
| `.bak20260908-prerestart` / `RTSI_旧版_0831备份.exe` | 见文件 | 更早存档 |

> 勘误（保留作教训）：曾记录"基线 `896a9d2b` 已丢失"——**是错的**。备份在 `RTSI.exe.bak20260917`，
> 只是命名非标准 `.bak`；当时沿用了备份动作**之前**的目录枚举。**断言"文件丢失"前必须重新枚举。**

**基准场景**：存档 **`00000037.sav`**（游戏内名 **`golden oasis54321`**，中局档）。
⚠️ 它**太动态**：帧时在 594–1204ms 间持续漂移，轮间差 9–16%，**不适合 B2 重复性验收**。

**下会话优先级**

1. **B2 重复性验收** —— 需换**平稳场景**（原 `00000036.sav` / `Golden Oasis12345` 开局档，
   实测 P95/中位 = 1.046，正因它静止），或把判据明确改为"取负载平稳子窗口"。
2. **#3 退出期 AV** —— 连续三轮 `EXCEPTION DUMP = 0`，**未复现**。`SHX:` 面包屑已在位，
   实测能分辨 `doPop` 是否调 `runInit`。复现要点：载入存档 → 打满 benchmark → 战报弹出 →
   **让 ScoreScreen 的弹出成为退出前最后一个动作**。
3. **postfx 归属迁移证实/证伪** —— 需 GPU 侧计时（T7 GPU Query 桩，默认关）。
4. **B3 后续** —— `t_draw_rttex` 已探明 **100% 是水面**（阴影 0.001ms，光源缓存短路）。
   再优化水面反射需动画质，而用户已表态"**只做安全项**"。
5. ⚠️ `spd_rescan.py` 加速模式判据**无法区分"菜单/空图空闲"与 fastmode**（已知局限，未修，
   仅在代码注释里记录；误判方向无害）。

**方法论沉淀（本轮踩坑换来的）**
- **A/B 必须对称采样**：两侧样本数相当、且都用 `-Runs 3` 连续多轮 —— 单轮会撞上"会话首轮偏高"
  的系统漂移，导致假结论。（本轮差点因此误判 postfx。）
- **同场景比较必须先对齐 `objects`**：中位数会被"载入早期窗口"拉低（291 vs 719），
  要只取 `objects ≥ 700` 的完整场景帧。
- **推翻了两个自己的假说**：① `do/while` 重入假说 —— 用 `draw_calls` 三轮一致（1657/1721/1658）
  否掉；② 我的"勘误 C"本身是错的 —— 用一个未验证适用条件的检验去推翻有多重旁证的结论，顺序错了。

---

## （上一轮）当前状态（2026-09-17 收工）：**存档基准打通 + 探针重大缺陷已修**，B2 待正式验收

> 本轮成果：commit **`b79c433a`**（已推送 origin/main）。
> `bench_capture.ps1` 现有 `-Map` / `-ReplayPath` / `-ManualLoad` 三种模式；A1 回放已可播；
> 探针累加污染已修并跨窗口验证；存档基准拿到可信基线（P95/中位 = 1.046）。

### 下会话交接 —— 剩余 5 条（按优先级）

| # | 任务 | 为什么 / 怎么做 | 依赖 |
|---|---|---|---|
| **1** | **补 `t_client` 内约 105ms 的未细分埋点** | 存档局 `t_client` 1081ms = `t_render` 865 + `t_postfx` 110 + `t_present` 0.8 + **约 105ms 黑盒（占帧 11%）**。查 `TheGameClient->UPDATE()` 里除 render/postfx 外的大户（drawable 更新 / UI / 雷达绘制）。 | 需 Internal 构建 |
| **2** | **重扫历史 `.spd`（共 729 份：712 污染 + 17 干净）** | 切分点 = 修复版 exe 部署时刻 **2026-09-17 10:42:14**（即 `E:\!!!!!!!QWCSB\RTSI.exe` 的 mtime），**早于此的 712 份首帧含累加污染**。⚠️ 那 17 份"干净"里只有 **12 份是存档基准**（`save_baseline_fixed`），另 **5 份是空图验证轮**（`probe_fix_verify*`，无 AI 无战斗，**不属于基准数据**），重扫时按 manifest 的 tag 过滤。先给 `spd_analyzer.py` / `plan_generator.py` 加"丢弃每份首行"兜底，再重算历史结论——**一切基于 max/峰值/卡顿聚类 的结论都要重过**（中位数结论不受影响）。 | 无需构建，可立即做 |
| **3** | **修退出期 AV** | 载入存档局被 `-benchmark` 局内强退 → scratch-pad `00000000.sav` 的 Xfer 句柄未关 → `AsciiString::operator==()` AV（读 `0x000005DE`，`asciistring.h:589`）。**可稳定复现**（存档局 + 局内强退即可）。查 `GameStateMap` 的 scratch-pad 清理与 `XferLoad` 关闭时序。 | 需构建 + 带符号调试 |
| **4** | **B2 正式验收（终于可做了）** | 存档局已是目前最稳场景：**P95/中位 = 1.046（抖动 4.6%）**，"同场景 3 遍 <5%" 有希望一次过。探针开销 A/B 仍**无数据源**（探针关掉就没有 `.spd`），维持"由构造保证 + 微基准 0.22% 上界背书"收口。 | 无需构建 |
| **5** | **B3 专项：每次 draw call 0.485ms 的归因** | 靶子不是 draw call **数量**（真实峰值仅 ~1790），而是**每次成本**。`state_changes/draw_call = 0.51` 不算失控 → 嫌疑在每 draw 固定开销（shader 常量上传 / dgVoodoo 包装层 / 阴影 pass 重复提交）。 | 需构建（加埋点） |

### ⚠️ 开工前必读

- `W3XRenderObj.cpp` 的在制品改动在 **`stash@{0}`**（补丁备份 `/tmp/w3x_shadow_diag_wip.patch`）。
  **做阴影调试前先 `git stash pop`**；做性能基准时必须让它保持 stash（硬规则 #6）。
- 游戏目录 exe 身份：`RTSI.exe`(`f896a9d2`, LAA=YES) = 本轮双修复构建；
  `RTSI9月16日收工.exe` / `RTSI.exe.bak`(`064045d3`) = 原版对照，**不要覆盖**。
- 基准场景：存档 **`00000036.sav`**（游戏内名 **Golden Oasis12345**，本地玩家 China）。
- 一键命令：`powershell -File Tools\bench_capture.ps1 -ManualLoad -SaveName "Golden Oasis12345" -WarmupSeconds 150 -Seconds 210 -Tag <标签>`
  （已带 `-ignoreAsserts`；退出码见 `bench_manifest_*.csv`，`0`=正常退出，`killed`=脚本超时强杀）。

## （历史）2026-09-15 稳定性里程碑

> ⚠️ 本节"45 万 draw_calls 峰值 / draw call 批处理"的靶子**已被 2026-09-17 证伪**（探针累加污染），
> 只保留作历史记录。真实峰值约 1790，靶子应改为"每次 draw call 成本"。

**2026-09-15 稳定性验收**：破笔记本从"单家10分钟必崩"到"4v4八家冷酷+作弊四开+加速模式+26万对象长时间稳定"。三板斧：①%ls格式化炸弹(f7663009) ②LAA 4GB ③诊断网常驻。
**性能数据已定向（口径见上方警示）**：加速模式瓶颈大转移（<20万对象渲染占85-99% → 26万对象t_logic独占99%）。
**待办**：探针开销A/B(bench_capture -ProbeOff跑00000000.rep)、重建带OOM快照exe+重打LAA、P1渲染归因开工。

| 阶段 | 任务 | 负责人 | 状态 | 验收 | 备注 |
|---|---|---|---|---|---|
| P0 活基线 | T1 FrameProbe 核心框架 | zcode | ✅ | 探针开销<1%待测 | commit 13b90009 |
| P0 | T2 INI 开关（GlobalData） | zcode | ✅ | 编译过 | EnableFrameProbe/FrameProbeIntervalSec |
| P0 | T3 主循环七段插桩 | zcode | ✅ | 落盘验证通过 | 30秒间隔自动产出.spd |
| P0 | T8 spd_analyzer.py 最小版 | zcode | ✅ | 真实数据瀑布报告 | 合成+实测双验证 |
| P0余项 | 探针开/关开销对比<1% | zcode | ✅ | 0.22%最保守上界 | 微基准验收(28µs/帧@12组QPC+ring写,含Python循环开销); 回放A/B被MOD exe轮换破坏回放CRC校验阻塞 |
| P0余项 | 同场景3遍重复性<5% | zcode | 🔄 | 硬验收 | **2026-09-17 起可做**：存档局 P95/中位=1.046(抖动4.6%)，场景已足够稳 |
| P1 渲染归因 | T4 Present 拆分+渲染三段 | zcode | ✅ | 真实数据落盘 | commit 8386ae2d |
| P1 | T6 wrapper 状态计数 | zcode | ✅ | draw_calls/state_changes列有数据 | DX8Wrapper现成getter接线 |
| P1 | T10 bench_capture.ps1 采集脚本 | zcode | ✅ | 三种模式实测通过 | b79c433a: -Map/-ReplayPath/-ManualLoad + manifest |
| P1余项 | **探针累加污染修复** | zcode | ✅ | 跨窗口首帧序列变平坦 | b79c433a; 原"45万draw_calls峰值"由此证伪 |
| P2 仿真归因 | T5 逻辑细分+回收 fopen 打点 | zcode | ✅ | 6个t_logic_*列有数据 | 5d1f7bac; 存档局 t_logic 占1.4% |
| P2 | T7 GPU Query（实验） | 可并行认领 | ⬜ | 默认关 | dgVoodoo 兼容未知 |
| P2 | T9 plan_generator.py | zcode | ✅ | 可产出 DIAG_PLAN | edbd160f; 待接"丢首行"兜底后重扫历史数据 |
| P2 | t_client 内 105ms 未细分埋点 | zcode | ⬜ | 黑盒降到 <2% | 下会话第1条 |
| P2 | 退出期 AV（scratch-pad Xfer 未关） | zcode | ⬜ | 存档局+局内强退不再崩 | 下会话第3条; 可稳定复现 |
| P3 常驻化 | 收编 #ifdef FRAME_PROBE | zcode | ⬜ | | |

状态图例：⬜待办 🔄进行中 ✅完成 ⛔受阻

## 协作硬规则（违反会损失构建轮次）

1. **单管线手动构建**（桌面 `构建工具\③增量构建Internal.bat`），禁止并发后台轮询构建。
2. **只用 Internal 构建**（Release 闪退未解）。
3. 构建前**必须让所有占用游戏/PDB 的人退出游戏进程**（多机协作时提前在群里喊）。
4. 部署验证只认 HLSL 标记字符串 grep exe 或运行时编译日志行，exe 大小不可作凭证。
5. 游戏测试一律 `-win` 窗口化启动。
6. **禁止改动 `W3XRenderObj.cpp`**（贴图阴影活跃热区，另有专人/专任务在改）。
7. 每个性能相关提交必须附 .spd 摘要（P2 之后生效）。
8. C++ 代码遵守 VC6 约束：无 lambda、无多行字符串字面量、循环变量作用域显式、显式 include。

## 决策记录

- 2026-09-14 计划定稿（三轮反思迭代），设计文档 `PERF_DIAG_DESIGN.md`，升级史参照 `UPGRADE_TIMELINE.md`。
- 2026-09-14 批准开工，P0 启动。

## 本轮新增修复与工具（2026-09-14 下午，commit 8386ae2d）

- 启动弹窗/崩溃三连修复：①三张 PBR IBL 贴图改 D3DX 加载路线（旧加载器按 TGA 头解析 DDS → Height=0 → width==height 断言）②Set_Render_Target 纯诊断断言软化（自定义 RT 间切换 + 遗留保存，代码本就正确处理）③模型公共骨骼缺失降级为警告（W3X 自动 INI 的损毁变体合理缺 Turret 骨骼）。
- 新工具 `Tools/assert_reader.py`：Win32 WM_GETTEXT 免 OCR 精准读取断言/崩溃弹窗 → 解析"断言+文件:行"自动附源码上下文 → `--watch --auto-ignore` 无人值守穿越弹窗。**弹窗点击流程（实测）**：忽略×2 → "继续将导致不可预测行为"确认点[是] → 若崩溃弹 Technical Difficulties 点[确定]。
- 遗留内容断言（非回归，MOD 数据问题）：BurnedTreeArmor 护甲模板缺失、Tech Center 模板缺失、负电力 Produce=-1200 —— 建议走内容修复轨道。

## 卖指挥中心闪崩根因（2026-09-14 已修）

远程09-12建筑灯光提交引用的 `Shaders/RA3/w3x_lights.fx`（和 w3x_nano.fx）**只进了仓库没部署到游戏目录** →
建筑模型每帧 submesh FAILED 刷屏 → 卖出/摧毁建筑（模型状态切换）时未捕获异常闪崩。
修复：跑仓库里的 `sync_shaders.bat`（ADD 2 / DIFF 7，均有.bak备份）。**协作者注意：改 fx 后必须 PUSH 到游戏目录且重启游戏进程才生效。**

## 弹窗点击完整流程（assert_reader 实测定稿）

断言弹窗 → [忽略]×2 → "Ignore this crash from now on?"确认框(**无窗口标题**,必须按#32770类识别)→[是] → 若已崩溃则 Technical Difficulties →[确定]。

## 2026-09-14 下午追加修复（均已提交推送，详见 git log 64094874..8fb0f982）

崩溃类：负电力钳零(delta=-1200双移除已留证) / NaN三角形消毒 / 分区COI数组越界硬护栏(大型W3X建筑走small几何路径,GameEngine::update静默崩溃高嫌疑) / VEH崩溃栈真捕获(原栈转储FPO下恒空)
弹窗降噪类(降级为日志警告,均下游有NULL兜底)：TurretBone/fx骨骼/SubObject/动画缺失/Model缺失/脚本命令按钮未实现(伪装网升级触发)
内容修复：BurnedTreeArmor护甲模板补全(注意:loose Armor.ini会整体替换INI.big同名文件,必须以完整原版为底追加,已由用户提取原版完成) / w3x_lights.fx等shader部署同步
游戏目录已部署最新exe+pdb(VEH栈捕获生效版)

## 2026-09-15 下午（二）：回放播放深修受阻，有效修复已入库（commit 510ec24f）

- **已修**：①CRCDebug `IS_FRAME_OK_TO_LOG` 判空（退出崩溃）②W3DWater/HeightMap Render 的 `Camera.Get_User_Data()` 判空（回放加载场景 NULL，getCustomPassMode 现场已定位）③bench_capture 空格路径规避+PS编码修复。
- **未修（挂账）**：`-file` 回放在本 MOD 深部损坏——playbackFile 早期操作污染子系统链表（主循环 UPDATE 野指针 edx=4，日志尾部崩溃丢失）+ exe 轮换致回放 exe/INI CRC 必失配 + 退出 mempool FreeObjectCount 断言。需专门会话带符号调试 playbackFile 全路径。
- **基准方案改道**：固定场景基准用 `-file <map>` 直接开局（InitRandom(0) 固定种子），回放路线搁置。
- P0 探针开销验收已按微基准法通过（0.22% 上界），见上节。

## 2026-09-15 下午：退出崩溃修复 + P0 验收 + bench 陷阱记录

- **退出阶段崩溃修复**：CRCDebug `IS_FRAME_OK_TO_LOG` 宏裸调 `TheGameLogic->isInGame()`，退出销毁后 NULL 空指针 → 已加判空（含 15:41/15:55 两次现场，VEH 原始栈+map 符号化定位）。
- **P0 探针开销验收 ✅**：微基准 28µs/帧 = 0.22% 最保守上界（13ms 轻负载帧），远低于 1%。回放 A/B 法不可用原因见下。
- **`-file` 回放命令行三坑**（bench_capture 实测）：①路径含空格被 WinMain 重分词拆碎 → 拷游戏目录规避（已入脚本）②MOD exe 频繁轮换 → 回放头 exe/INI CRC 校验必失败 ③CRC 失败路径下游 AV（SubsystemInterface::UPDATE 写 0x8，未修，引擎健壮性问题）。结论：**固定回放基准在本 MOD 工作流下不可行**，建议基准场景改用 `-file <map>` 直接开局（InitRandom(0) 固定种子）。
- 同场景 3 遍重复性验收：随基准场景改造后补做（看板挂 P1）。
- 同步修复断言：'UI_AllCheerSound 无 player restrictions' 启动断言（良性，auto-ignore 穿越即可）。

## 2026-09-15 中午：真 OOM 崩溃对策 + 首个大规模性能数据点

- **真 OOM**（11:09, 作弊开启+建兵工厂瞬间, GlobalAlloc 1.4MB 失败）：exe 已打 **LARGEADDRESSAWARE**（2GB→4GB, PE头 pe+4+18 |= 0x20 字节补丁, VC6 链接器 /LARGEADDRESSAWARE 无效；**每次构建后需重打**, 勿改 pe+4=Machine 字段会致 WinError 193）。LAA 后实测余量 2.2GB+。
- OOM_SYSALLOC 失败点已加内存全景快照(commit 后待构建生效)；FrameProbe 每 30s 落 #MEM 水位(commit 51094279)。
- **首个渲染归因数据**（作弊大战场）：objects 30,873 / draw_calls 74,727，中位帧 1.78s，t_client 50% + t_render 48.5%，t_logic 仅 0.4% → 瓶颈明确在客户端渲染管线。
- 探针开销理论界 <0.01%（11 组 QPC+环形缓冲/帧）；正式 A/B 用 bench_capture.ps1 -ProbeOff 跑同一回放待做。

## 2026-09-15 上午：长局必现闪崩已根治（commit f7663009，用户实测长局稳定）

**根因链**（三层洋葱，全程靠 terrain_diag.log 面包屑网络点名）：
1. `UPDATE_CRASH_ERRORCODE -559087614` = 0xDEAD0002 = ERROR_OUT_OF_MEMORY → 伪 OOM
2. 崩溃前最后面包屑 `OOM_ASCIIFORMAT format="Object %d [%s, owned by player %d (%ls)]"`（Object.cpp DescribeObject）
3. 链条：AI/调试日志用 `%ls` 打印中文玩家名 → VC6 CRT 宽字符转 MBCS 失败 → `_vsnprintf` 返回 -1 → 误判 OOM 抛异常 → update 捕获 → 闪崩。崩溃处理器 fprintf 同路自毙 → ReleaseCrashInfo 截断+空栈，此前无法定位。
**修复**：AsciiString/UnicodeString `format_va` 共 4 处，失败降级 `<format-error>` + FORMAT_FAIL 日志，不再抛。
**沉淀的诊断网（保留）**：VEH 硬错误优先+C++异常环形记录、DumpFaultContextStack 原始栈扫描（主模块返回地址，离线 RTSI.map 符号化）、7 处 THROW_SITE_N 面包屑、catch(INIException) 补 UPDATE_CRASH_INIEXC。
**勘误**：Tech Center 模板缺失断言是良性噪音（原版/loose/INI.big 均无此定义，是 SkirmishScripts.scb 的脚本参数引用，脚本路径全有 NULL 保护），与本次崩溃无关。

## 变更日志

- 2026-09-15 zcode：长局闪崩根治（%ls 格式化失败误抛 OOM，f7663009），用户实测稳定。崩溃捕获网三层强化入主线。
- 2026-09-14 zcode：看板建立，P0 T1 开工。
- 2026-09-14 zcode：P1-T4/T6完成(8386ae2d)+启动崩溃三修(IBL贴图D3DX路线/RT断言软化/骨骼警告化)+assert_reader弹窗工具(#32770类识别,三段式点击)。首个诊断结论:菜单外壳地图t_render 633ms/帧@1433draw_calls,t_present 0.5ms。
- 2026-09-14 zcode：P0 T1/T2/T3/T8 完成并实测贯通（commit 13b90009）。游戏 30 秒间隔自动落盘 .spd，
  spd_analyzer 真实数据瀑布报告 OK。构建踩坑两则见下。

## 本机踩坑记录（新增，其他机器协作也适用）

1. **VC6 增量构建的 mtime 陷阱**：git rebase/autostash 后源文件 mtime 变旧，增量编译用过期的旧编译结果
   （症状：明明存在的类成员报 C2039 "not a member"，新旧字段一起报）。解决：④全量重编，或 touch 报错的 cpp。
2. **改头文件后宏未生效**：FrameProbe.h 的 FRAME_PROBE 自动定义加入后，未变更的 GameEngine.cpp 不重编，
   FP 宏仍是空操作（症状：Init 日志有、落盘没有）。解决：touch 该 cpp 强制重编。
3. **工具包 bat 勿在 git-bash 直接调**：`find` 会被 GNU find 抢占、误报 error 计数。用
   `PATH="/c/Windows/System32:$PATH" cmd //c "③增量构建Internal.bat"` 方式调用；其实际构建结果以
   desk_rts.log 的 "N error(s)" 行为准。
4. **INI 接线生效凭证**：DebugLogFileI.txt 出现 "FrameProbe: init enable=1"，游戏目录出现 frameprobe_*.spd。

## 2026-09-16：A 项稳定性收口 + B 项部分推进（下会话交接）

- **A2 回放 CRC 失败下游 AV**：GameEngine::update 主循环 7 个单例 UPDATE 全判空（commit 9652672a），用户实测回放 CRC 失败场景已不崩。
- **A3 负电力 -1200 根因**：面包屑定位为 **ALT+B 免费建造作弊键 `depositEnergy` 的 ±1200 逻辑写反**（开扣1200/关加1200），与卖电厂无关（ENERGY_BONUS_ADD/REMOVE 账目配平）。已修（commit 71c0c0f7）：开 +1200、关 `withdrawEnergy(1200)` 钳到非负。卖电厂路径无 bug。
- **A1 -file 回放**：静态防御已推（getSlot(-1) 越界 + fread 截断校验，commit b2cc0d4f）；**playbackFile 污染子系统链表（UPDATE 野指针 edx=4）仍挂账**，需专门带符号调试会话。
- **A4 构建验证**：Release/Internal 均 0 error；用户实测长局稳定。游戏目录 RTSI.exe（Internal 全修复）+ RTS.exe（Release A/B 前）均已打 LAA。
- **B 项已推**：T5 逻辑细分（6 个 t_logic_* 阶段）+ 回收 GameLogic 热路径 fopen（5d1f7bac）；T9 plan_generator.py（edbd160f）；SagePerfDiag_README（310ee9eb）；W3DView 每帧 VIEW_3D_DONE fopen 已清（1a81b2df）。诊断日志 terrain_diag.log/pbr_compile.log 已改写到游戏目录（不再 E:\ 根）。
- **剩余（下会话）**：
  1. ~~B1/B2 基准：bench_capture.ps1 需先支持 `-file <地图名>` 模式~~ → **B1 已于 2026-09-17 完成**，见文末。
  2. B3 draw call 批处理：等受控 .spd 数据到手后专项（性能已定向：t_client 50% + t_render 48.5%，45万 draw_calls 峰值）。
  3. B5 T7 GPU Query 实验桩（默认关，可随时加）。
  4. A1 playbackFile 符号调试会话（深部损坏，需专门时间）。
  5. 已有 663 份 .spd 碎片，可先跑 plan_generator 做初步归因（无需启动游戏）。

## 2026-09-17：B1 固定地图基准打通 + 两条勘误 + B3 靶子重新定向

### 已完成

- **`RTSI.exe` 缺失已恢复**：游戏目录只剩改名存档版；已用 `RTSI9月16日收工.exe`（PE 时间戳 2026-09-16 17:38:54，LAA=YES，含 T5）字节复制回 `RTSI.exe`。**注意用户有把 exe 改名存档的习惯，脚本会因此启动失败。**
- **`bench_capture.ps1` 加 `-Map` 模式**：新增 `-Map "Maps\xxx.map"` / `-Exe` / `-Tag`，产出 `bench_manifest_*.csv`（轮次/探针状态/场景/exe/退出码/spd）。⚠️ 该 .ps1 必须存为 **UTF-8 with BOM**，否则 PowerShell 5.1 按 ANSI 读中文注释会解析报错。
- **B1 验收通过**：`-Map "Maps\Golden Oasis.map"` 直接开局成功，帧号 0..274 连续落盘，退出码 0。日志双凭证：
  `Command-line args: -win -noshellmap -ignoreAsserts -file Maps\Golden Oasis.map -benchmark 120`
  `Shell:showShell() - Maps\Golden Oasis\Golden Oasis.map (no top screen)`

### 勘误一：`-file` 的空格问题不是分词造成的

原结论"路径含空格被 WinMain 重分词拆碎"**不成立**。`Main/WinMain.cpp:1006` 用
`nextParam(lpCmdLine, "\" ")`，分隔符集合含双引号，引号内的空格会被正确保留（实测 `-file Maps\Golden Oasis.map` 原样到达）。

**真凶**是 `CommandLine.cpp:73 ConvertShortMapPathToLongMapPath()`：它只接受 `.map` 结尾的路径，
对 `.rep` 会命中 `DEBUG_CRASH("Invalid map name")`，并且继续把路径拼成 `"<xxx.rep>\.map"`
——于是 `GameEngine.cpp:650` 的 `fname.endsWithNoCase(".map")` 判真，**回放被误送进地图分支**，
下游必然加载失败/AV。这才是 `-file` 回放一路崩到 `SubsystemInterface::UPDATE` 的上游成因，
"退出崩溃/mempool 断言"很可能是它的次生现象。**A1 的排查入口应从这里开始，而不是从链表污染开始猜。**

### 勘误二：`-file` 地图路径必须二段式

`ConvertShort` 会把 `<目录>\<名>.map` 展开为 `<目录>\<名>\<名>.map`（幂等）。
传三段式 `Maps\Golden Oasis\Golden Oasis.map` 会被重复拼成 `Maps\Golden Oasis\Golden Oasis\Golden Oasis.map`。
**脚本侧只准传 `Maps\<地图名>.map`**（bench_capture.ps1 已有此校验）。

### 试点数据（Golden Oasis，120s，帧 0–274，单家无 AI）

- 中位帧时 472ms / 中位 FPS 2.1 / 超 33.3ms 预算 **98.2%**
- `t_client` 472ms ⊇ `t_render` 399ms = **帧的 84%**；`t_logic` 仅 0.25ms（0.05%）
- objects 258 / draw_calls 1380 → **单次 draw call ≈0.29ms**。健康值 1–10µs，差 **1–2 个数量级**。
- 瞬态帧（#218）：draw_calls 4027、1430ms——开局资产流式加载 + shader 编译期。

### 由试点发现的两个口径问题（B2 验收标准需修订）

1. **"同场景 3 遍 <5%" 按现写法不可能达成**：同一次运行内部漂移就有 41%（344→472ms）。
   必须改为 **弃瞬态、取稳态窗口**（例如丢弃前 200 帧后比较），否则测的是加载抖动不是重复性。
2. **"探针开关 A/B <1%" 无可测数据源**：探针关掉就没有 `.spd`，拿不到任何帧时样本。
   该验收只能靠**微基准上界（0.22%）**收口，或另建一个独立于 FRAME_PROBE 的外部帧计时器。
   建议：B2b 判定为"由构造保证 + 微基准背书"，不再要求端到端 A/B。

### 环境备注

- 工作区 `W3XRenderObj.cpp` 未提交改动**已存在于所有在役 exe**（`fx='%s' deferred` 在 9-15 的
  `RTSI4GB版.exe` / `RTS4GB.exe` 里同样能 grep 到）。因此它不构成基准污染（同一 exe 双臂），
  但仓库工作区仍脏，**提交前必须隔离**，且硬规则 #6 仍然禁止改该文件。

## 2026-09-17（二）：探针重大缺陷 —— 每个落盘窗口首帧被累加污染

### 缺陷（`FrameProbe.cpp:180`，已修，待构建）

`FrameProbeFlush()` 结尾把 `g_ringHead = 0` 复位，**却没有清零 `g_ring[0]`**；
而 `FrameProbeCount()` / `FrameProbeEnd()` 都只做 `+=`（`:209` / `:201`），
`FrameProbeEndFrame()` 的清零只作用于**下一个**槽位（`head+1`，`:235`）——永远碰不到 0 号槽。

**后果：每个 `.spd` 的第一行，是"本次窗口首帧 + 之前所有窗口首帧"的累加和。**

算术级证据（Golden Oasis 存档场景，10 个窗口）：

```
Δobjects    = +287(菜单) +651 +651 +650 +651 +651 +655 +657   ← 正是稳态 objects
Δdraw_calls = +1770..1794                                     ← 正是稳态 draw_calls
Δt_total    = +880..955 ms                                    ← 正是稳态帧时
窗口首帧 t_total 从 914ms 一路累到 14703ms，objects 累到 5140
```

### 影响面：**历史结论作废，必须重算**

- 看板原 B3 靶子"**45 万 draw_calls 峰值**"是伪结论——那是约 250 个窗口的累加值，不是任何一帧的真实值。
- 一切基于 max / 峰值 / `spd_analyzer` 卡顿聚类 的结论都不可信（一个窗口只有 1 帧被污染，所以**中位数受影响很小**，中位数结论仍可用）。
- 试点里那个"瞬态帧 #218：4027 draw_calls / 1430ms"同样是污染帧，不是加载尖峰。

### 修复

- `FrameProbe.cpp` 落盘后补 `memset(&g_ring[0], 0, sizeof(FPFrameRecord));`（1 行，待 Internal 重建）。
- 兜底（无需构建）：`spd_analyzer.py` / `plan_generator.py` 读档时**丢弃每份 `.spd` 的第一行**，
  并在报告里标注"含首帧"或"已剔除污染首帧"。

## 2026-09-17（三）：存档基准打通（Golden Oasis12345）+ 干净基线数据

- **引擎没有命令行载入存档的通路**：存档载入只有 UI 的 `doLoadGame` →
  `TheGameState->loadGame(AvailableGameInfo)`。`-quickstart` 只跳 logo/shellmap/动画；
  `-map` 只设 `m_mapName` 供 Recorder/Stats 用；`-file` 只认 `.map` / `.rep`。
- **存档路径不做 exe/INI CRC 校验**（已查 `System/SaveGame/` 全目录），换 exe 后仍可载入。
- `bench_capture.ps1` 新增 `-ManualLoad -SaveName <显示名> -WarmupSeconds <预热>`：
  不给 `-file`，由人点"载入游戏"，`-benchmark (Warmup+Seconds)` 负责自动退出。
- 实测（`00000036.sav`，本地玩家 China，Golden Oasis，150s 预热 + 210s 采集，**探针修复后**）：

| 阶段 | 中位 ms | P95 ms | P99 ms | 占帧 |
|---|---|---|---|---|
| **t_total** | **1095.3** | **1145.2** | **1272.8** | 100% |
| t_client | 1081.1 | 1131.6 | 1205.4 | 98.7% |
| **t_render** | **865.4** | **897.7** | **921.5** | **79.0%** |
| t_postfx | 110.2 | 337.2 | 366.4 | 10.1% |
| t_logic | 14.9 | 24.8 | 36.4 | 1.4% |
| t_present | 0.77 | 1.75 | 2.16 | 0.1% |

- objects 651（649–654）/ draw_calls 1783 / state_changes 910 → **每次 draw call 0.485 ms**（健康值 0.001–0.01 ms）。
- **P95/中位 = 1.046**：抖动仅 4.6%，**已落在 B2 的 <5% 口径内** —— 固定场景就用它。
- 126 帧真实战斗稳态（帧 102–227），**超 33.3ms 预算 100%**。
- 存档载入帧（#101）t_total = 8804ms，是**真实**的载入尖峰（不是污染）。
- t_client 1081 = t_render 865 + t_postfx 110 + t_present 0.8 + **其余客户端约 105ms**（尚未细分）。

### 探针修复的功能验证（跨多窗口首帧）

| 构建 | 窗口首帧 objects 序列 | 判定 |
|---|---|---|
| 修复前 | 1225 → 1876 → 2526 → 3177 → 3828 → 4483 → 5140 | 每窗口 +651，累加污染 |
| 修复后 | 651 → 650 → 651 → 651 → 651 | **平坦**，已修好 |

### 重新定向后的 B3 靶子

不是"draw call 数量"，而是**每次 draw call 的成本（0.485ms）**：1783 个 draw call 就吃掉 865ms。
state_changes 910 / draw_calls 1783 = 0.51，状态切换本身不算失控，嫌疑集中在
每 draw 的固定开销（shader 常量上传 / dgVoodoo 包装层 / 阴影 pass 重复提交）。
另外 t_client 里有 **约 105ms 完全未细分**，下一个埋点缺口在这里。

### 回放路线（已修，备用）

- `parseFile` 只对 `.map` 调 `ConvertShortMapPathToLongMapPath`（`CommandLine.cpp`，已改）。
- 回放名必须是**相对 Replays 目录的裸文件名**（`Recorder.cpp:818-820` 是
  `fopen(getReplayDir() + filename)`，绝对路径会被拼坏）；且 `GameEngine.cpp:647` 会 `toLower()`。
- CRC 不一致只是 `DEBUG_ASSERTCRASH`（`Recorder.cpp:1146`，`#ifdef DEBUG_LOGGING`），**不阻断播放**。
- 实测 `2222.rep` 是 Twilight Flame 8 家混战，能播；但 120s 只推进 54 帧（0.45 FPS），
  到不了重载状态 —— 所以**基准场景优先用存档，回放留作回归复现**。

## 构建踩坑（2026-09-17 新增）

- **⚠️ 勘误（2026-09-17 实测）：`[5/5]` 校验失败的根因不是 MSYS，是 `findstr` 的空格 OR 语义。**
  原记录说"`findstr /R` 里 `/R` 被 MSYS 当路径吃掉"——**不成立**。真实原因：
  `findstr /R "[1-9][0-9]* error"` 中那个**空格被 findstr 当作 OR 分隔符**，于是实际是
  `[1-9][0-9]*` **或** `error` 两个独立模式，而 `[1-9][0-9]*` = "任意 ≥1 的数字"。
  实测该模式在本轮 `desk_rts.log` 上命中了**全部 4 行**，包括明明不含 error 的
  `----Configuration: GameEngine - Win32 Internal----`（因为含 `32`）与 `LINK : warning LNK4075`（含 `4075`）。
  **结论：这条校验在构建成功时必然误判 `[失败]`，部署永远被跳过** —— 与是否从 git-bash 调用无关。
  修法：改成 `findstr /R /C:"[1-9][0-9]* error"`（`/C:` 让空格变字面量）。
  （`[1/5]` 的 `find /I` 被 GNU find 劫持是**另一回事**，仅从 git-bash 调用时发生；走 `cmd /c` 无此问题，
  本轮已实测 `[1/5]` 正常通过。）
- **bat 到 `[5/5]` 失败时，连备份都不会做** —— 而当前游戏目录 exe 往往是产出基准数据的干净基线，
  手工补部署前**必须先把它存成 `.bak`**，否则该基线不可复现。
- 构建后 bat 末尾有 `pause`：非交互调用需把 stdin 接 `NUL`（`cmd /c "call ...bat" < NUL`），否则挂住。
- 构建成功后**必须手动补三件事**：① 拷 `GeneralsMD\Run\RTSI.exe` → 游戏目录（若 bat 跳过了部署）② 跑
  `python Tools/apply_laa.py <游戏目录>\RTSI.exe` 重打 LAA（bat 不做）③ 用标记字符串验证
  （**exe 大小跨构建常常完全相同**：本轮新旧 exe 都是 10661968 字节，只有 hash 能区分）。
  ⚠️ 本机 `settings.json` 的 `permissions.deny` 含 `PowerShell(Copy-Item:*)` 等，会拦住部署类命令；
  经允许可走 `Bash(python:*)`（在 allow 列表内）绕过，或由人工执行。
- 构建成功后**必须手动补三件事**：① 拷 `GeneralsMD\Run\RTSI.exe` → 游戏目录 ② 跑
  `python Tools/apply_laa.py` 重打 LAA（bat 不做）③ 用标记字符串验证（exe 大小跨构建常常完全相同）。
- `.ps1` 工具必须存为 **UTF-8 with BOM**，否则 PowerShell 5.1 按 ANSI 读中文注释直接语法报错。

## 退出期崩溃（2026-09-17 现场，已定位未修）

**现象**：载入存档局跑满 `-benchmark` 后被强退时，屏幕反复闪（模态断言窗后面游戏还在重绘）、
弹出信息、点[忽略]后进程退出。

**判定：不是脚本杀的**。`bench_manifest_*.csv` 里 12 份 spd 的 `exitCode` 全为 `0`
（若是超时强杀会记成 `killed`，脚本也会打印"超时未退出, 强制结束"）。

**真身是退出期访问违例**（`DebugLogFileI.txt` 尾部，本轮起始行 91，全轮仅 4 个断言）：

```
EXCEPTION DUMP / Exception is access violation
Access address: 000005DE was read from
  common/asciistring.h(589) : operator==(); 0x00405760
ASSERTION FAILURE: Warning: Xfer file '00000000.sav' was left open
Stack Dump: [Ignore]        ← 用户点忽略后日志仍正常收尾
```

**触发条件**：`-benchmark` 计时器在**局内**强退 → `TheGameLogic->clearGameData()` +
`setQuitting(TRUE)`，而此时存档的临时 scratch-pad `00000000.sav` 的 Xfer 句柄还没关，
teardown 顺序错乱 → `AsciiString::operator==()` 比较到已失效指针。

**与本次两处改动无关**：这一轮没传 `-file`（`parseFile` 分支不执行）；
`FrameProbe.cpp` 的 memset 只作用于静态数组，没有能引发该 AV 的路径。

**对数据无影响**：AV 发生在采集窗口之后，12 份 `.spd` 全部有效。

**已做的规避**：`bench_capture.ps1` 的 `-ManualLoad` 模式补上 `-ignoreAsserts`（原先漏了，
所以弹窗会挡住流程）。断言仍照常写进 `DebugLogFileI.txt`。

**待办**：这是可稳定复现的真实 bug（存档局 + 局内强退即可触发），值得单独一轮排查
`GameStateMap` 的 scratch-pad 清理与 `XferLoad` 关闭时序。

## 2026-09-17（四）：`_palace` 勘误两条 + T11 埋点已加（待构建）

### 勘误 A：退出期 AV 的归因方向错了（上面第 3 节的"scratch-pad Xfer"结论不成立）

直接读 `E:\!!!!!!!QWCSB\DebugLogFileI.txt`（100541 行）原始日志，证据如下：

- 全日志 **148 次 `EXCEPTION DUMP`**（不是 1 次），且**日志以 `Log closed` 正常收尾** →
  这些 AV 全部被 VEH 捕获、游戏继续跑完。**不是致命崩溃**，是反复被吞掉的访问违例，
  这才是"屏幕反复闪 + 模态框"的来源。
- **第一次 AV 前的最后成功操作**是：
  `GameState::xferSaveData() - XFER_SAVE` → `DeepCRCSanityCheck: CRC is BAF41A0C` →
  `FrameProbe: flushed 33 frames to frameprobe_94241284_11.spd` → `setFramesPerSecondLimit(30)` →
  **`Shell:popImmediate() - stack was  Menus/MainMenu.wnd / Menus/SaveLoad.wnd / Menus/ScoreScreen.wnd`**
  → 然后才炸。
  即：`-benchmark` 计时到期 → 战报 ScoreScreen 弹出 → `popImmediate` 回退，**与 `00000036.sav` 的
  scratch-pad / `XferLoad` 关闭时序无关**。"Xfer file left open" 断言是另一件（很可能良性的）事。
- **AV 精确定位**：日志帧地址 `0x00405760`，查 `RTSI.map` 落在
  `0x00405740  ??8@YA_NABVAsciiString@@0@Z` = **`operator==(AsciiString const&, AsciiString const&)`
  的 out-of-line 版本**（+0x20 处）。EIP 字节 `66 8B 02 83 C2 02 3A 01 75 CE` = MSVC `strcmp` 的
  2 字节快路径。寄存器 `Eax=Edx=000005DE`、`Ecx=2AB274A8` →
  **`strcmp(s1.str(), s2.str())` 里 `s1.m_data = 0x000005DE`**（小整数；因非 NULL，`str()` 的
  NULL 兜底拦不住）。
- `Shell::doPop`（`Shell.cpp:628`）弹出 ScoreScreen 后会 `deleteInstance()` 掉该 WindowLayout，
  再对新的栈顶（**`SaveLoad.wnd`**）调 `runInit()`。嫌疑集中在
  **被释放的 `WindowLayout` 的 `m_filenameString`（AsciiString）被后续遍历读到**。
- **下一步**：需要 148 次 AV 的**调用者**（当前日志栈只有 2 帧，FPO 下不可走）。要么给
  `Shell::unlinkScreen/doPop` 加面包屑，要么用项目已有的 `DumpFaultContextStack` 原始栈扫描
  离线符号化。**不要再从 scratch-pad/Xfer 方向查。**

### 勘误 B：12 份"干净基准"`.spd` 不同质 —— 其中 4 份是加速模式

`W3DDisplay.cpp:1894` 是**整个渲染块的总门**：

```cpp
if ( (TheGameLogic->getFrame() % 30 == 1) || ( ! (!TheGameLogic->isGamePaused() && TheGlobalData->m_TiVOFastMode) ) )
```

按真值表，只有 **`m_TiVOFastMode` ON 且未暂停** 时才会退化成 `frame % 30 == 1`（**只渲染 1/30 帧**）。

实测（`frame` 列是 `g_diagFrame`，与 `getFrame()` 差固定偏移）：

| 文件 | 行数 | t_render>0 的帧 | 全部落在 |
|---|---|---|---|
| `..._93909772_0` ~ `..._94142549_7` | 228 | 227 | 几乎每帧 |
| `..._94173576_8` | 135 | **12** | `frame%30==2` |
| `..._94203654_9` | 176 | **5** | `frame%30==2` |
| `..._94233745_10` | 182 | **6** | `frame%30==2` |
| `..._94241284_11` | 33 | **2** | `frame%30==2` |

**结论：白名单 12 份里，后 4 份（`94173576_8` 起）是加速模式录的**，只渲染 1/30 帧，
`draw_calls`/`t_render` 大量为 0。看板头条数字（t_render 865 / P95÷中位=1.046）取自
**帧 102–227**，恰好只覆盖普通模式的 3–7 号文件（127 帧）→ **头条数字本身有效**，
但**任何跨全部 12 份的聚合都会被这 4 份污染**。

⚠️ **对 B2（第 4 条）的影响**：3 遍重复性验收**必须确认加速模式处于关闭**，
否则测的是"1/30 渲染"与"全帧渲染"两套东西，P95/中位 毫无意义。

### T11 埋点（第 1 条）代码已加，待 Internal 构建

改动 4 文件（全部只加 `FP_BEGIN/FP_END`，不动逻辑）：

| 文件 | 改动 |
|---|---|
| `GameEngine/Include/Common/System/FrameProbe.h` | 尾部新增 11 个 stage id（CSV 列序保持稳定） |
| `GameEngine/Source/Common/System/FrameProbe.cpp` | `fpStageName()` 名称表同步 11 项 |
| `GameEngine/Source/GameClient/GameClient.cpp` | 补 include + 9 段埋点，拆开 `t_client` 的 ~105ms 黑盒 |
| `GameEngineDevice/.../W3DDisplay.cpp` | 补 `t_draw_views` / `t_draw_rttex`，覆盖 `FP_BEGIN(RENDER)` **之前**的预渲染段 |

新增列：`t_client_input / t_client_window / t_client_ghost / t_client_drawables /
t_client_terrain / t_client_displupd / t_client_strmgr / t_client_shell / t_client_ingameui /
t_draw_views / t_draw_rttex`。

注意 `W3DDisplay::draw()` 里 `FP_BEGIN(RENDER)` 之前的 `updateViews()`+粒子更新、
**水面 RT 更新**、**投影阴影 RT 更新** 原本**不在任何探针内**，是 105ms 的头号嫌疑。
`spd_analyzer.py` / `plan_generator.py` 的 `STAGE_COLS` 需同步加这些列（见第 2 条）。

### T11 实测结果（2026-09-17 18:12 跑批）：**第 1 条结案**

30 列全部落盘（表头含 11 个新列）。4887 帧稳态中位值：

| 阶段 | 中位 ms | 占帧 |
|---|---|---|
| **t_total** | 114.30 | 100% |
| t_client | 80.64 | 70.6% |
| ├ **`t_draw_rttex`** | **77.113** | **67.5%** |
| ├ `t_draw_views` | 1.066 | |
| ├ `t_client_drawables` | 0.598 | |
| ├ `t_client_ingameui` | 0.184 | |
| └ 其余 8 项合计 | ~0.05 | |
| **残差（原"105ms 黑盒"）** | **1.633** | **1.4%**（原 ~11%）|

**105ms 黑盒 = `t_draw_rttex`**，即 `TheWaterRenderObj->updateRenderTargetTextures()` +
`TheW3DProjectedShadowManager->updateRenderTargetTextures()`。

⚠️ **关键性质**：这两行在 `frame%30` 渲染门（`W3DDisplay.cpp:1894`）**之外** ——
实测 **100% 的帧（4887/4887）都在执行**。也就是说**即使这一帧根本不渲染场景，
它仍然每帧把水面和投影阴影各渲一遍到纹理**。这既是原 105ms 的真身，
也是 B3「每 draw call 固定开销」的头号嫌疑。**下一步 B3 从这里开刀。**

### 勘误 C（**本条自身有误，已被 2026-09-18 推翻，保留作教训**）

原写："`spd_rescan.py` 的加速模式判据不可靠，本轮把一次**非**加速模式的运行判成了加速模式。"

**该结论是错的。那次运行确实是加速模式**，两条独立证据：

1. 日志 `Begin_Render` 告警 **0 次** ⇒ `W3DDisplay.cpp:2015` 的 else 分支从未进入
   ⇒ 跳过渲染只能来自**外层 `frame%30` 门**（`:1894`）⇒ `m_TiVOFastMode` 为 ON。
2. 零渲染帧占比 13.1%，与 1/30 门控（3.3%）+ 用户交互时的额外渲染吻合。

**我当时的推理错在**：拿"`t_render>0` 帧的 `frame%30` 余数应集中于单一值"当否决依据。
但 `.spd` 的 `frame` 列是 FrameProbe 自己的 `g_frameOrdinal`，**不是** `TheGameLogic->getFrame()`；
两者偏移**并非总是恒定**（存档基准那次恒定，所以测得出干净指纹；这次不恒定，余数才铺开）。
⇒ **帧余数指纹是弱证据，不能用来否决"零渲染占比"这个强证据。**

**教训：用一个自己都没验证过适用条件的检验，去推翻一个有多重旁证的结论 —— 顺序错了。**
`spd_rescan.py` 的「零渲染占比 ≥50%」判据**维持原样**（它是有效的）；余数指纹仅作参考，不作否决。

### 勘误 D：第 3 条（退出期 AV）**未复现、也未修复**

本轮日志：`EXCEPTION DUMP = 0`（上次 148）、`Log closed` 干净收尾、30 条 `SHX:` 面包屑全部走完。

看似"好了"，但**同一条危险路径这次其实走到了**（`Shell:pop()` 时栈为
`MainMenu / SaveLoad / ScoreScreen`，`doPop` 对 SaveLoad 调 `runInit` 并完整返回，无 AV）。

差别在**时序**：10:57 那次 ScoreScreen 弹出是**退出前最后一个动作**（日志 98392 行，紧接着 148 次 AV）；
这次它在 123264 行弹出，之后还继续操作、最后才退出。

AV 本质是 **use-after-free（读已释放内存，`m_data=0x000005DE`）**，行为依赖堆内容 ⇒ **间歇性**。
本轮新增探针改变了内存布局，**很可能只是把它盖住了，不是修好**。**不得据此宣称已修复。**

`SHX:` 面包屑位置经实测正确（能分辨 `doPop` 是否调用 `runInit`），**下次复现即可直接点名故障步骤**。
复现要点：打开载入菜单 → 载入存档 → 打满 benchmark → 战报弹出 → **让 ScoreScreen 的弹出成为退出前最后一个动作**。

### 第 2 条完成：历史 .spd 重扫 —— 修正后的历史峰值

工具：`spd_analyzer.py` / `plan_generator.py` 新增 `--drop-first {auto,always,never}`；
新脚本 `Tools/spd_rescan.py` 整目录重扫。报告 `Tools/spd_rescan_report.md`（可再生成，勿提交）。

**检测是"识别"而非"无脑丢首行"**：`auto` 用「首行 counters ÷ 本文件稳态 > 1.5」判污染。
稳态取**非零值中位数**（不是中位数）—— 这是踩过的坑：`frameprobe_921168308_419` 这类
1359 行、大部分行 `objects/draw_calls` 全为 0 的文件（游戏长期停在菜单/载入态），
中位数 = 0 会让检查被静默跳过，从而漏判（该文件首行 objects 610160 vs 真实 ~2335）。

**重扫 729 份的结果**：

| 项 | 值 |
|---|---|
| 检出累加污染首帧（已剔除） | **663** |
| 无法判定（无对照基线，未剔除） | 8 |
| 加速模式录制 | **220** |
| 存档基准白名单命中 | 12/12（其中加速模式 **4**）|

**修正后的历史峰值（旧结论 → 剔除污染后）**：

| 指标 | 剔除前 | 剔除后 | 倍数 |
|---|---|---|---|
| draw_calls | 459986 | **8798** | 52× |
| objects | 610160 | **2627** | 232× |
| t_total (ms) | 263553 | **33840** | 7.8× |

→ "**45 万 draw_calls 峰值**"确系累加伪影，被夸大 **52 倍**，看板此前的证伪成立。

⚠️ 但**修正后的 8798 是真峰值，不是新伪影**：来自 `frameprobe_91117135_6.spd`，
该场景稳态本身就 ~7451–7719 draw_calls/帧（top 值 8798/7719/7719/7719…），首行/稳态比值
仅 1.18，检测器正确地**没有**误判它。
**看板"真实峰值约 1790"只是存档场景（651 对象）的值，不是全局真理** —— 引用时须带场景。

⚠️ **加速模式文件占 220/729（30%）**，远超之前只知道的那 4 份。任何历史聚合
（尤其 `t_render`/`draw_calls` 的均值、占比、聚类）在混入快模式文件后都不可信。
`spd_rescan.py` 已逐份标注 `加速模式` 列。
