# SagePerfDiag 执行状态看板

> 协作同步点：本文件同步存在于 ①仓库 `GeneralsMD/Code/Tools/EXECUTION_STATUS.md`（GitHub 权威版）
> ②桌面 `SagePerfDiag协作` 文件夹（本机快照）。以 GitHub 为准，桌面版每次会话结束刷新。
> 协作者开工前先 `git pull` 并读此看板，认领任务后改状态并提交。

## 当前状态：稳定性里程碑达成（2026-09-15），下会话：A/B探针验收 + P1渲染优化开工

**2026-09-15 稳定性验收**：破笔记本从"单家10分钟必崩"到"4v4八家冷酷+作弊四开+加速模式+26万对象长时间稳定"。三板斧：①%ls格式化炸弹(f7663009) ②LAA 4GB ③诊断网常驻。
**性能数据已定向**：加速模式瓶颈大转移（<20万对象渲染占85-99% → 26万对象t_logic独占99%）。P1靶: draw call批处理(45万峰值)；P2靶: 每对象逻辑开销(T5)。
**待办**：探针开销A/B(bench_capture -ProbeOff跑00000000.rep)、重建带OOM快照exe+重打LAA、P1渲染归因开工。

| 阶段 | 任务 | 负责人 | 状态 | 验收 | 备注 |
|---|---|---|---|---|---|
| P0 活基线 | T1 FrameProbe 核心框架 | zcode | ✅ | 探针开销<1%待测 | commit 13b90009 |
| P0 | T2 INI 开关（GlobalData） | zcode | ✅ | 编译过 | EnableFrameProbe/FrameProbeIntervalSec |
| P0 | T3 主循环七段插桩 | zcode | ✅ | 落盘验证通过 | 30秒间隔自动产出.spd |
| P0 | T8 spd_analyzer.py 最小版 | zcode | ✅ | 真实数据瀑布报告 | 合成+实测双验证 |
| P0余项 | 探针开/关开销对比<1% | zcode | ✅ | 0.22%最保守上界 | 微基准验收(28µs/帧@12组QPC+ring写,含Python循环开销); 回放A/B被MOD exe轮换破坏回放CRC校验阻塞 |
| P0余项 | 同场景3遍重复性<5% | zcode | ⬜ | 硬验收 | 建议 P1 的 bench_capture 一并做 |
| P1 渲染归因 | T4 Present 拆分+渲染三段 | zcode | ✅ | 真实数据落盘 | commit 8386ae2d |
| P1 | T6 wrapper 状态计数 | zcode | ✅ | draw_calls/state_changes列有数据 | DX8Wrapper现成getter接线 |
| P1 | T10 bench_capture.ps1 采集脚本 | zcode | ✅ | 脚本就绪待实测 | commit afd4b694; -file回放+-benchmark定时退出(Internal限定) |
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
