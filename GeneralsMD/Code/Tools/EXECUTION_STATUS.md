# SagePerfDiag 执行状态看板

> 协作同步点：本文件同步存在于 ①仓库 `GeneralsMD/Code/Tools/EXECUTION_STATUS.md`（GitHub 权威版）
> ②桌面 `SagePerfDiag协作` 文件夹（本机快照）。以 GitHub 为准，桌面版每次会话结束刷新。
> 协作者开工前先 `git pull` 并读此看板，认领任务后改状态并提交。

## 当前状态：P0 施工中（2026-09-14 开工，已获项目所有者批准）

| 阶段 | 任务 | 负责人 | 状态 | 验收 | 备注 |
|---|---|---|---|---|---|
| P0 活基线 | T1 FrameProbe 核心框架 | zcode | 🔄 进行中 | 探针开销<1% | 新建 FrameProbe.cpp/.h |
| P0 | T2 INI 开关（GlobalData） | zcode | ⬜ 待办 | 编译过 | |
| P0 | T3 主循环七段插桩 | zcode | ⬜ 待办 | 掉帧可指认 | GameEngine.cpp:764/:905 |
| P0 | T8 spd_analyzer.py 最小版 | 可并行认领 | ⬜ 待办 | 读 .spd 出瀑布 | Python，无需碰 C++ |
| P1 渲染归因 | T4 Present 拆分+渲染三段 | zcode | ⬜ | | W3DDisplay.cpp:1680 |
| P1 | T6 wrapper 状态计数 | 可并行认领 | ⬜ | | DX8Wrapper |
| P1 | T10 bench_capture.ps1 采集脚本 | 可并行认领 | ⬜ | | 录像回放+定时退出 |
| P2 仿真归因 | T5 逻辑细分+回收 fopen 打点 | zcode | ⬜ | grep fopen=0 | |
| P2 | T7 GPU Query（实验） | 可并行认领 | ⬜ | 默认关 | dgVoodoo 兼容未知 |
| P2 | T9 plan_generator.py | 可并行认领 | ⬜ | 产出 DIAG_PLAN | |
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

## 变更日志

- 2026-09-14 zcode：看板建立，P0 T1 开工。
