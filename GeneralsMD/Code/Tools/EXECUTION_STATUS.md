# SagePerfDiag 执行状态看板

> 协作同步点：本文件同步存在于 ①仓库 `GeneralsMD/Code/Tools/EXECUTION_STATUS.md`（GitHub 权威版）
> ②桌面 `SagePerfDiag协作` 文件夹（本机快照）。以 GitHub 为准，桌面版每次会话结束刷新。
> 协作者开工前先 `git pull` 并读此看板，认领任务后改状态并提交。

## 当前状态：P0 核心贯通 ✅（2026-09-14 实测验证），P0 验收余项进行中

| 阶段 | 任务 | 负责人 | 状态 | 验收 | 备注 |
|---|---|---|---|---|---|
| P0 活基线 | T1 FrameProbe 核心框架 | zcode | ✅ | 探针开销<1%待测 | commit 13b90009 |
| P0 | T2 INI 开关（GlobalData） | zcode | ✅ | 编译过 | EnableFrameProbe/FrameProbeIntervalSec |
| P0 | T3 主循环七段插桩 | zcode | ✅ | 落盘验证通过 | 30秒间隔自动产出.spd |
| P0 | T8 spd_analyzer.py 最小版 | zcode | ✅ | 真实数据瀑布报告 | 合成+实测双验证 |
| P0余项 | 探针开/关开销对比<1% | zcode | ⬜ | 硬验收 | 需游戏内同场景跑两遍 |
| P0余项 | 同场景3遍重复性<5% | zcode | ⬜ | 硬验收 | 建议 P1 的 bench_capture 一并做 |
| P1 渲染归因 | T4 Present 拆分+渲染三段 | zcode | ✅ | 真实数据落盘 | commit 8386ae2d |
| P1 | T6 wrapper 状态计数 | zcode | ✅ | draw_calls/state_changes列有数据 | DX8Wrapper现成getter接线 |
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

## 本轮新增修复与工具（2026-09-14 下午，commit 8386ae2d）

- 启动弹窗/崩溃三连修复：①三张 PBR IBL 贴图改 D3DX 加载路线（旧加载器按 TGA 头解析 DDS → Height=0 → width==height 断言）②Set_Render_Target 纯诊断断言软化（自定义 RT 间切换 + 遗留保存，代码本就正确处理）③模型公共骨骼缺失降级为警告（W3X 自动 INI 的损毁变体合理缺 Turret 骨骼）。
- 新工具 `Tools/assert_reader.py`：Win32 WM_GETTEXT 免 OCR 精准读取断言/崩溃弹窗 → 解析"断言+文件:行"自动附源码上下文 → `--watch --auto-ignore` 无人值守穿越弹窗。**弹窗点击流程（实测）**：忽略×2 → "继续将导致不可预测行为"确认点[是] → 若崩溃弹 Technical Difficulties 点[确定]。
- 遗留内容断言（非回归，MOD 数据问题）：BurnedTreeArmor 护甲模板缺失、Tech Center 模板缺失、负电力 Produce=-1200 —— 建议走内容修复轨道。

## 变更日志

- 2026-09-14 zcode：看板建立，P0 T1 开工。
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
