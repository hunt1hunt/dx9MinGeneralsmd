# SagePerfDiag 使用说明（FrameProbe 常驻诊断）

> 设计文档: `Tools/PERF_DIAG_DESIGN.md`　|　看板: `Tools/EXECUTION_STATUS.md`

## 1. 是什么

FrameProbe 是 SAGE 引擎内置的分帧性能探针（QPC 计时 + 每帧计数器 + 环形缓冲），
编译开关 `FRAME_PROBE`（Internal/Debug 构建默认自动定义），运行时由 INI 开关控制，
不开启时探针开销为零。CSV 列名与 `spd_analyzer.py` / `plan_generator.py` 配套。

## 2. 开关

`Data/INI/GameData.ini`：

```ini
EnableFrameProbe = yes        ; 运行时总开关
FrameProbeIntervalSec = 30    ; 定时落盘间隔（秒）
FrameProbeGPUQuery = no       ; GPU Query 实验项（默认关）
```

落盘文件：`<游戏目录>/frameprobe_<时间戳>_<序号>.spd`（CSV）。

## 3. 采集流程（基准 A/B）

1. 构建 Internal（桌面工具包 `③增量构建Internal.bat`，单管线手动）。
2. 游戏 `-win` 窗口化启动，静置 30 秒避开加载尖峰。
3. 用 `Tools/bench_capture.ps1` 固定地图 + 固定种子自动跑一轮。
4. 改代码前后各跑一遍，得到 old.spd / new.spd。

## 4. 分析

```bash
python Tools/spd_analyzer.py old.spd new.spd      # A/B 对比 + 阶段瀑布 + 卡顿聚类
python Tools/plan_generator.py new.spd           # 规则库 → DIAG_PLAN_<日期>.md
```

阶段列（P2 起新增逻辑细分）：`t_logic` 之下细分为
`t_logic_script / t_logic_terrain / t_logic_create / t_logic_ai / t_logic_pathfind / t_logic_destroy`。

## 5. 常驻纪律（看板硬规则）

- 性能相关提交必须附 `.spd` 摘要。
- 热路径禁止 `fopen`（诊断一律走 FrameProbe 内存写）。
- 探针改动遵守 VC6 约束；集成点避开 `W3XRenderObj.cpp` 活跃热区。
- 每次构建后对游戏 exe 重打 LAA：`python Tools/apply_laa.py "E:\!!!!!!!QWCSB\RTS.exe"`。
