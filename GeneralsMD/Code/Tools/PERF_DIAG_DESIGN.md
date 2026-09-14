# SAGE 运行诊断分析工具（SagePerfDiag）设计文档

> 目标：对《绝命时刻》（Zero Hour，SAGE 引擎）的**运行过程**做系统性诊断，
> 定位效率/流畅度瓶颈，并自动产出**针对性的代码修改计划**。
> 这是一个"通用工具"，服务于所有后续优化迭代，而非一次性补丁。

日期：2026-09-14。依据：deepwiki 9 章分析框架 + 代码导图/记忆宫殿全库分析。

---

## 1. 现状盘点（已存在的设施与缺口）

| 设施 | 位置 | 状态 |
|---|---|---|
| PerfTimer | `GameEngine/Include/Common/PerfTimer.h:33-40` | **编译期被 `NO_PERF_TIMERS` 全灭**，插桩点尚存（如 `W3DDisplay.cpp:1682` 注释掉的 `USE_PERF_TIMER(W3DDisplay_draw)`） |
| StatDump / EXTENDED_STATS | `W3DDisplay.cpp:1703-1733` | 可用，`-stats` 风格触发，粒度粗 |
| StatsCollector | `Common/StatsCollector.cpp`，GameLogic 每帧调用 | 偏游戏统计，非性能 |
| FPS/DebugDisplay | `W3DDisplay.cpp:1540-1565`（drawFPSStats/drawDebugStats） | 屏显，无落盘、无归因 |
| GameLODManager | `W3DDisplay::draw` 内动态 LOD | 有降级逻辑但无决策依据留痕 |
| 本地 diagLogI/terrain_diag.log | `GameEngine.cpp:745-757`、GameLogic.cpp 多处 `fopen` | 临时打点，散乱、每帧开文件、**本身造成卡顿**（commit 809f3845 已拔过一次） |
| W3XShadowDiag | `W3XRenderObj.cpp` 当前未提交改动 | 爆发式采样（24 次/3 秒），诊断完成后应回收 |
| 基准模式 | `GameEngine.cpp:833-856` m_benchmarkTimer | DEBUG/INTERNAL 下定时退出，可复用 |

**缺口**：没有统一的「低开销采样 → 落盘 → 分析 → 产出修改计划」闭环。PerfTimer 复活 + 集中式二进制日志 + 外部分析器是核心方案。

## 2. 架构：三件套

```
┌─ 游戏进程内 ────────────────────────────────┐
│ [A] FrameProbe 采样核心（复用 PerfTimer 插桩点） │
│     - QueryPerformanceCounter 阶段计时          │
│     - 环形缓冲 + 每 N 帧异步落盘（无每帧 fopen）  │
│     - 输出 .spd 二进制/CSV 分帧轨迹             │
└──────────────┬───────────────────────────┘
               │ .spd 文件
┌─ 外部（Python，Tools/）─────────────────────┐
│ [B] spd_analyzer.py  分析器                  │
│     - 帧时间分布/P99、阶段归因（谁吃了帧预算）   │
│     - 卡顿事件聚类（画面阶段×游戏场景×LOD 状态） │
│     - 渲染帧 vs 逻辑帧(30Hz lock-step)错位检测  │
│     - 前后对比（A/B 两份 .spd）               │
│ [C] plan_generator.py 计划生成器              │
│     - 规则库: 症状→嫌疑代码点(带 file:line)    │
│     - 产出 Markdown 修改计划 + 优先级 + 验证法  │
└──────────────────────────────────────────┘
```

## 3. 游戏进程内：FrameProbe（[A]）

### 3.1 插桩点（固定阶段表，全部已实地确认）

| 阶段 ID | 挂点 | 度量 |
|---|---|---|
| FRAME_TOTAL | `GameEngine::update` 入口→出口（GameEngine.cpp:764） | 整帧 |
| RADAR / AUDIO | 同文件各 `UPDATE()` 调用之间 | 各段 |
| CLIENT_UPDATE | `TheGameClient->UPDATE()`（内含 `TheDisplay->draw()`） | 含渲染 |
| RENDER_DRAW | `W3DDisplay::draw`（W3DDisplay.cpp:1680）入口/出口 | 纯渲染 |
| RENDER_VIEWS / 后处理 / 阴影 pass | `W3DDisplay::draw` 内 `primaryW3DView` 循环与 shadow pass | 渲染细分 |
| NET_UPDATE | `TheNetwork->UPDATE()` + `isFrameDataReady()` 等待时间 | **lock-step 空转等待**（多人卡顿常见根因） |
| LOGIC_UPDATE | `TheGameLogic->UPDATE()`（GameLogic.cpp:3947） | 仿真 |
| LOGIC细分 | AI/Pathfind、ScriptEngine、Terrain 各 update（沿用 terrain_diag 打点位置，改为内存写） | 仿真细分 |
| DRAWMODULE 聚合 | W3DModelDraw/W3XModelDraw 计数器（次数+总时长，非逐个计时） | 绘制负载 |
| GC/分配 | 可选：WinMain 消息泵间隙 | 稳态验证 |

### 3.2 关键设计约束（来自记忆宫殿避坑清单）

1. **零每帧文件 IO**：环形缓冲（如 4096 帧槽），满块或 `DiagDump` 热键/定时才 `fwrite` 一次。杜绝 terrain_diag.log 式 `fopen` 卡顿自污染。
2. **只内存写 + QPC**：单次开销目标 <1µs/阶段。DEBUG/INTERNAL 构建默认开，Release 用 INI 开关 `EnableFrameProbe=yes`（挂 GlobalData.cpp:83 一带的现有 INI 框架）。
3. **不动 lock-step 确定性**：只读计时，不改任何逻辑路径；网络诊断只记 `isFrameDataReady` 等待时长。
4. **诊断代码可整体回收**：全部包在 `#ifdef FRAME_PROBE` 或独立 `FrameProbe.cpp`，禁止再散落 `fopen`。
5. 构建验证沿用既有规矩：单管线手动构建、HLSL 标记字符串 grep exe、游戏须 `-win` 窗口化、只用 Internal 构建。

### 3.3 输出格式（.spd → 逐行 CSV，人机皆可读）

```
frame,logicFrame,t_ms_total,t_client,t_render,t_net_wait,t_logic,t_ai,t_script,t_terrain,
draw_calls,w3x_models,shadow_pass_ms,lod_level,fps_avg,objects,particles
```

## 4. 外部分析器（[B] spd_analyzer.py）

1. **帧预算核算**：渲染帧目标（INI `FramesPerSecondLimit`，默认无上限/兜底 120）与逻辑帧 30Hz 的关系；标出 `t_total > 33.3ms`（逻辑帧掉帧）与 `> 预算` 的帧。
2. **阶段归因瀑布**：按中位数/P95 排序各阶段占比，输出"谁吃掉了帧预算"Top-N。
3. **卡顿事件检测**：`t_total` > 滑动均值×3 且 >50ms 的尖峰聚类，关联当时场景（objects/particles/draw_calls 突变、LOD 降级沿、shadow pass 激活）。
4. **锁步等待检测**：`t_net_wait` 占比高 → 网络对齐等待，非本机性能问题（避免误优化）。
5. **场景敏感性**：按 units 数量分桶对比，判断是"负载缩放差"还是"固定开销大"。
6. **A/B 对比模式**：`spd_analyzer.py old.spd new.spd` 输出回归/改善表，防止"优化"反而变慢。
7. 输出诊断报告 Markdown（可直接喂给后续会话/记忆宫殿）。

## 5. 计划生成器（[C] plan_generator.py）

规则库（初版，按本仓库实际热点预置，可持续扩充）：

| 症状 | 嫌疑点（file:line） | 建议动作 |
|---|---|---|
| RENDER_DRAW P95 高 + draw_calls 高 | W3DDrawModule 逐 Drawable 循环；W3DShadow 逐 pass（`Shadow/W3DShadow.cpp`） | 合批/剔除审查；阴影 pass 频率（参照 commit 809f3845 先例：无消费者的 pass 直接关） |
| shadow_pass_ms 突增 | `W3XRenderObj.cpp:1581-1856` _CreateShadowMap / 相机跟随窗口 | 复用阴影图、降低重绘频率 |
| LOGIC_UPDATE 高 + t_ai/t_terrain 高 | GameLogic.cpp:3947 起各 update；AIGuard/Pathfind | AI 节流、寻路缓存 |
| t_net_wait 高 | Network.cpp:807 isFrameDataReady | 非本机问题 → 转网络诊断轨道 |
| particles 突变伴随尖峰 | ParticleSys.cpp | 粒子上限/LOD |
| 每帧固定大开销（与场景无关） | GameEngine.cpp:905-918 自旋 Sleep(0) 帧率限制 | 改 waitable timer，释放 CPU |
| 诊断残留自污染（周期性停顿） | terrain_diag.log / W3XShadowDiag 打点 | 拔除或迁入 FrameProbe（历史上已发生两次） |

产出物：`DIAG_PLAN_<日期>.md`，含：问题描述、证据（.spd 统计摘要）、嫌疑点列表（精确到 file:line）、修改方案、风险、验证方法（跑同一地图录 .spd → A/B 对比）。**这份计划直接就是下一轮代码修改的任务书。**

## 6. 最终实施计划（三轮反思迭代后定稿，2026-09-14）

### 6.0 三轮反思查漏结论（已吸收进下文）

1. Present()/vsync 等待必须从 RENDER_DRAW 拆出独立计时，否则 GPU 等待污染 CPU 渲染归因（最常见归因错误源）。
2. 帧率限制自旋（GameEngine.cpp:905-918）单列为 `t_fps_limit_spin` 阶段——它可能是"什么都不干却吃 CPU"的隐形大户。
3. 增加可选 GPU 侧计时：D3D9 `D3DQUERYTYPE_TIMESTAMP`/`OCCLUSION` Query（**实验性**，dgVoodoo/D3D9On12 兼容层行为未知，失败自动禁用并记日志）。
4. 探针自身开销有硬验收：探针开 vs 关，帧时间差 <1%。
5. VC6 约束：无 lambda、无多行字符串字面量、循环变量作用域显式、`<ctype.h>` 等头文件显式 include（历史提交 090cbb01/942acb50 教训）。
6. 基线可重复性修正：**架构级旧提交跑不了当前 W3X 资产**——跨架构对照用各时代自带地图；同架构 A/B 限邻近提交；可重复采集 = 同一录像回放 + `m_benchmarkTimer` 定时自动退出。
7. 落盘时机：环形缓冲满块自动 + INI 定时（秒级）+ 热键手动三种触发；热键挂现有 DebugDisplay 键位框架，不新增 UI。
8. 集成点避开 W3XRenderObj.cpp（活跃未提交热区）。
9. 构建纪律操作清单（记忆宫殿硬约束）写入 6.4。
10. 新增"第 0 步"：给当前 HEAD 建立活基线；未来涉及性能的提交须附 .spd 摘要防回归。

### 6.1 工作分解（文件级）

| 任务 | 文件（新建/改动） | 内容 |
|---|---|---|
| T1 FrameProbe 核心 | 新建 `GameEngine/Source/Common/System/FrameProbe.cpp` + `Include/Common/System/FrameProbe.h` | QPC 计时 API（`FP_BEGIN(id)/FP_END(id)` 宏，`#ifdef FRAME_PROBE` 包裹，未定义时零开销）；4096 帧环形缓冲；CSV 写出（`GeneralsMD/Data/frameprobe_<时间戳>.spd`）；计数器接口（draw_calls/状态变更/per-pass 可见数） |
| T2 INI 开关 | 改 `Common/GlobalData.cpp/.h`（:83 一带现有框架）+ 对应 INI 解析 | `EnableFrameProbe(bool)`、`FrameProbeIntervalSec(int)`、`FrameProbeGPUQuery(bool, 默认 no)` |
| T3 主循环插桩 | 改 `Common/GameEngine.cpp` update()（:764）各 UPDATE() 之间 + 帧率限制段（:905-918） | FRAME_TOTAL/RADAR/AUDIO/CLIENT/NET/LOGIC/FPS_LIMIT_SPIN 七段 |
| T4 渲染插桩 | 改 `GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp` draw（:1680） | RENDER_DRAW 拆三段：场景绘制 / 后处理 / **Present（单独）**；per-pass 可见 Drawable 计数（主/阴影/反射） |
| T5 逻辑插桩 | 改 `GameLogic.cpp` update（:3947）内各阶段（沿用 terrain_diag 打点位置，全部改内存写） | AI/PATHFIND/SCRIPT/TERRAIN/CREATE/DESTROY 细分；**同时回收现存 fopen 式 terrain_diag 打点** |
| T6 wrapper 计数 | 改 `W3DShaderManager/DX8Wrapper`（仅加计数器，不动推送逻辑） | SetTexture/SetRenderState/SetVertexShader/PixelShader 每帧计数（业界参照第 8 节支柱④） |
| T7 GPU Query（实验） | FrameProbe 内部 + W3DDisplay 一处调用 | TIMESTAMP 前后包夹 Present；能力检测失败自动跳过 |
| T8 分析器 | 新建 `Tools/spd_analyzer.py` | 帧预算/瀑布/P95/卡顿聚类/锁步等待/A-B 对比/CSV+Markdown 报告 |
| T9 计划生成 | 新建 `Tools/plan_generator.py` | 规则库（§5 + §8 新增 4 条）→ `DIAG_PLAN_<日期>.md` |
| T10 基准采集 | 新建 `Tools/bench_capture.ps1` | 拉起游戏 `-win` + 指定录像回放 + benchmarkTimer 定时退出 + 收集 .spd |

**集成点纪律**：T3/T4/T5 每处只加 2-3 行（宏调用），diff 极小便于跨 W3X 活跃开发 rebase；全部诊断实现集中在 FrameProbe.cpp 单文件。

### 6.2 阶段与验收标准

| 阶段 | 内容 | 验收（全部满足才算过） |
|---|---|---|
| **P0 活基线**（半天） | T1-T3 + T8 最小版 | ①探针开/关帧时间差 <1%；②同录像跑 3 遍，各阶段中位数偏差 <5%；③/Internal 构建零告警新增；④掉帧场景（spawn 大军）能在瀑布图上指认阶段 |
| **P1 渲染归因**（1 天） | T4+T6+T10 | ①Present 与场景绘制分离；②状态变更计数/帧可用；③同地图三基线点（若可运行）或当前 HEAD 三场景对照报告产出 |
| **P2 仿真归因+计划**（1 天） | T5+T7+T9 | ①AI/脚本/地形细分；②terrain_diag 遗留 fopen 全部回收（grep 验证为 0）；③plan_generator 对一份真实 .spd 产出可执行 DIAG_PLAN（嫌疑点精确 file:line） |
| **P3 常驻化**（半天） | 代码收编 | ①全部诊断在 `#ifdef FRAME_PROBE` 内（含 W3XShadowDiag 决策：收编或按需保留）；②README 补使用说明；③防回归纪律生效：性能相关提交附 .spd 摘要 |

### 6.3 风险表

| 风险 | 等级 | 缓解 |
|---|---|---|
| 探针扰动测量（尤其诊断代码本身变热点） | 中 | 环形缓冲纯内存写 + P0 开销验收硬指标；禁止任何热路径 fopen |
| 与 W3X 活跃开发冲突 | 中 | 只碰 W3DDisplay.cpp（非 W3XRenderObj）；宏式 2-3 行插入，易 rebase |
| VC6 编译失败 | 中 | 遵守 6.0-#5 约束清单；先在最小 demo 编译验证再插桩 |
| GPU Query 在 dgVoodoo 下崩溃 | 低 | 能力检测 + try 禁用 + 默认关 |
| 旧基线提交跑不起来当前资产 | 低 | 6.0-#6 的对照策略已修正 |
| Release 闪退（未解历史问题） | — | 沿用 Internal 构建路线，不引入新变量 |

### 6.4 构建与验证操作清单（每轮执行）

1. 单管线手动构建（桌面工具包），**禁止并发后台轮询构建**。
2. 构建前请用户退出游戏（PDB 锁）；只用 Internal 构建。
3. 部署验证用 HLSL 标记字符串 grep exe 或运行时编译日志行，不看 exe 大小。
4. 游戏以 `-win` 窗口化启动。
5. 测量前静置 30 秒（避开加载/首帧编译尖峰），benchmark 场景固定地图+固定录像。

## 7. 升级史对照基线（2026-09-14 补充）

源码开放以来的升级已按时间顺序系统梳理为七大升级线，详见姊妹文档 **`Tools/UPGRADE_TIMELINE.md`**（DX9迁移→水面倒影/高光→PBR全线→延迟渲染→红警3模型W3X→软阴影/贴图阴影三波两落→画质/性能/稳定性杂线）。

与 SagePerfDiag 的衔接：
- P1 完成后，优先在 **三个架构级基线提交**（`47d9db16` DX9 / `03f45818` 延迟渲染完成 / `7d610544` 贴图阴影贯通）上跑同地图同回放基准，量化每次升级的帧时间增量——这就是"针对性分析和对照"的量化底座。
- 每轮优化前后各录一份 .spd，A/B 报告归档时同时标注所处的升级线阶段，防止跨阶段误归因。
- 阶段 5/6（W3X/贴图阴影）仍在活跃开发，插桩与基准避开 `W3XRenderObj.cpp` 未提交热区。

## 8. 业界参照系：终末地优化思路 + NVIDIA/MSDN 法则 → SAGE 映射（2026-09-14 补充）

参照《明日方舟：终末地》Unite Seoul 2026 优化演讲（用户翻译版）四大支柱，以及 NVIDIA「Batch Batch Batch」(GDC 2003/2004)、DX9 时代 MSDN 优化指南，逐条映射到 SAGE/DX9 现状。原则：**SAGE 是 2003 年引擎 + DX9 上限（单线程提交、无 instancing、约数千 draw call/帧预算），不照搬架构，只取可落地的诊断维度和优化方向**。

| 终末地支柱 | SAGE 对应现状 | 可吸收的思路 |
|---|---|---|
| ① ECS 缓存友好布局、编译期固化决策、消灭运行时哈希查询 | Object/Drawable 是指针链表式模块架构（GameLogic Object → Update/DrawModule 链），遍历缓存不友好；PBROverride 曾是线性扫描（91001619 已做首字符桶化——同类问题会再现） | 诊断维度：FrameProbe 增加"每帧模块遍历计数+缓存 miss 代理指标（模块链平均长度）"；优化方向：热路径（DrawModule 遍历、shader 参数查找）从哈希/字符串查找改数组索引/编译期绑定 |
| ② 统一多视图剔除（一次剔除多视图共享）、软件遮挡剔除、64 位排序键一次排序完成状态聚合批处理 | 每帧多视图：主视图 + 阴影 pass + 水面反射 pass（逐水域独立反射 79d86d80 后视图数更多），各自独立做可见性；渲染排序在 W3DScene 的 sort 结构（16 位键时代产物） | **这是本项目最大对标缺口**：贴图阴影+水面反射让同一 Drawable 被处理 3+ 次。诊断维度：FrameProbe 记录 per-pass 可见 Drawable 数与 draw_calls；优化方向：共享剔除结果（阴影/反射复用主视图可见集 ± 扩边）、排序键合并 shader state+纹理减少状态切换（NVIDIA 法则 #1：状态排序比省 draw call 更狠） |
| ③ C++ 渲染图（render graph）：类型化资源声明、合并屏障、临时资源内存别名复用降 VRAM 峰值 | SAGE 全 C++ 天然满足；但 pass 间资源依赖隐式散落（G-Buffer/SSAO/阴影图/反射 RT 由 W3DDeferredRenderer、W3XRenderObj、W3DWater 各自管理，重沟通过：skipG-Buffer/水面pass跳过延迟等补丁 2b52cb48/361c7cf6 就是依赖失控证据） | 诊断维度：记录每帧各 RT 的分配/清除次数与 VRAM 峰值；优化方向：给现有 RT 集中建一张轻量"pass-资源表"（哪怕只是注释级文档+FrameProbe 打点），消除无消费者 pass（809f3845 先例）、阴影图按需重绘而非每帧 |
| ④ 底层设备抽象、避免高层 API 的 fallback path | DX8Wrapper 就是设备抽象层，方向正确；但 dgVoodoo/D3D9On12 兼容层导致行为黑箱（4821f748 D24 采样缺陷、矩阵黑箱教训） | 诊断维度：FrameProbe 记录 wrapper 状态变更次数（SetTexture/SetRenderState/SetVertexShader 计数）——DX9 每次状态变更经驱动验证有 CPU 代价（MSDN/StackOverflow 经典结论）；优化方向：wrapper 差量推送已存在（0fb2413a 修过其缺陷），用计数数据定位"每帧全量重推"的热点 |

**DX9 硬约束提醒**（源自 NVIDIA/社区共识）：DX9 单线程提交上限约数千 draw call/帧；状态切换（shader/纹理/混合模式）成本常高于 draw call 本身 → plan_generator 规则库新增：

| 症状 | 新增嫌疑/动作 |
|---|---|
| wrapper 状态变更计数高且同值重复设置 | 差量推送失效点，按 0fb2413a 攻防合一套路修 |
| 阴影/反射 pass 可见对象数 ≈ 主视图 | 剔除未共享 → 先共享可见集（收益大、风险低、不动画质） |
| 每帧 RT 分配/Release 计数非零 | 池化 RT，消灭每帧显存分配抖动 |
| 同 shader 不同纹理反复交替 | 排序键纹理权重前移（NVIDIA Batch Batch Batch 法则） |

**吸收原则**（终末地演讲结语同样适用本项目）："有意放弃一部分灵活性换取性能"——SAGE 的 INI 数据驱动和模块化是灵活性，诊断数据证明是瓶颈时，允许在热路径做编译期固化。

## 9. 与既有体系的衔接

- 记忆宫殿：每次诊断报告与修改结果归档回 MemPalace（MinGenerals/perf-diag 房间），形成"症状→根因→修复"长期记忆。
- 代码导图：plan_generator 输出的嫌疑点经 CodeGraph callers/callees 复核影响面后再动手。
- deepwiki 式知识沉淀：分析结论按其 9 章框架（引擎架构/仿真/表现/网络…）归类写回根目录分析文档。
