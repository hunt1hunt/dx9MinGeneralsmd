---
name: min-generals
description: MinGenerals (C&C Generals / Zero Hour SAGE Engine) 项目深度分析与修改支持。当用户要求分析、修改、完善或理解 Command & Conquer Generals / Zero Hour / MinGenerals / SAGE 引擎代码库时自动触发。触发后第一步必须先检索记忆宫殿（mempalace）恢复既往避坑经验，任务结束把新经验归档回宫殿。
author: CommunityRTS
version: 1.0.0
context: fork
allowed-tools: Read,Bash,Write,Edit,Grep,Glob,WebSearch,WebFetch
argument-hint: "[分析/修改/编译/架构/模块/网络/渲染/调试/地图]"
---

# MinGeneralsmd 项目分析与修改技能

> 本技能合并了 DeepWiki 项目索引（A 文件）与 Claude Code CLI 初始化文档（B 文件）的最佳内容，
> 为 AI 助手提供对 [hunt1hunt/MinGeneralsmd](https://github.com/hunt1hunt/MinGeneralsmd) 项目的完整认知。

---

## 第零步：记忆唤醒（每次任务开始必做）

### 最高思维原则（用户钦定 2026-09-19，优先级高于本文件一切流程惯例）

**对标源码 + 疑问卡点清单 = 防止盲修瞎练的最佳武器。**

1. **对标源码必须系统、认真、细致，每个步骤环节都不能遗漏**：先把要做的事分解成完整环节链（创建→绑定→写入→解绑→采样→回读…），逐环节找到对标对象并记录（对标对象 / 锚点 file:line / 对标结论），不许跳环节、不许抽查了事。
2. **分清哪些必须对标、哪些对标会有问题、为什么不能照套**（对标适用性分层）：
   - **必须对标（权威）**：本仓库引擎源码（dx8wrapper/W3D* 等——引擎自己的机器，照套无歧义）；.fx/.FXH 声明语义层（sampler_state 值编码、寄存器约定——跨栈一致）。
   - **对标会有问题（意图可学，行为不能照套）**：RA3 参考源码——运行平台是真机 D3D9 + 不同渲染器架构，与本栈（dgVoodoo 翻译层）行为面不同；引擎 D3D8 时代遗留路线（ZTextureClass 等）——目标硬件是 NVIDIA 深度纹理特供年代，"创建成功"≠"采样可用"。
   - **规范分层**：D3D9 规范内行为（未绑采样器返白、StretchRect 限制）在 dgVoodoo 上应保持；**规范外行为（厂商扩展、UB）在 dgVoodoo 上无源码可对标**——这是唯一必须实测+问专家的层，禁止拿真机经验当依据。
   - **历史断言**：代码注释/宫殿记忆里的"已验证"必须注明验证年份与栈版本，跨版本引用前必须重验（反例：'D24X8 经 effect 可采'2026-09-05 断言，2026-09-19 同栈复测失效）。
3. **疑问卡点清单**：拿不准的问题显式列成编号清单（问题 + 实验设计 + 判读分支），而不是用猜测填空。打点实验一次只动一个变量，且必须带「已知内容对照组」。
4. **收敛判据**：打点循环超过两轮无收敛 = 方法错了。停下来出清单，去问专家或换路线，不许惯性继续打点。

### 记忆宫殿

本技能触发后，先从记忆宫殿恢复既往经验，再动手：

本机 mempalace 为 npm v2.0.0 加密记忆版（无 wing/room），工具用法：

1. **浏览既往记忆**：调用 `mempalace` MCP 工具（`mcp__mempalace__*`），或 CLI `mempalace list` 列出近期记忆。
2. **定向检索**：用 `mempalace recover <short_id>`（或 MCP 的 recover 工具）恢复与本次任务相关的记忆上下文。
3. **任务收尾归档**：修复完成或发现新根因后，把结论写成 JSON 文件（问题现象、根因、修复方案、避坑要点），用 `mempalace save <json_file>` 或 MCP 的 save 工具存入宫殿，下次会话即可直接检索到。

已知高频教训（详见宫殿记录）：跨 DLL/EXE 函数指针调用会崩溃；新增 shader 变体 ≠ 修改现有签名；Edit 失败时先查 tab/空格与编码，勿直接上 Python 批处理写 C++ 文件。

## 触发条件

当用户要求分析、修改或询问以下内容时自动激活本技能：
- **项目名关键词**："MinGenerals"、"Generals"、"Zero Hour"、"SAGE 引擎"、"C&C"
- **修改请求**：包含"修改代码"、"新增功能"、"修复 bug"、"编译"、"构建"
- **分析请求**：包含"分析架构"、"理解引擎"、"代码解读"、"文件说明"
- **子系统关键词**：GameLogic、GameClient、GameNetwork、W3D、Thing、INI、Xfer

---

## 构建与部署铁律（用户钦定 2026-09-19）

**每次编译构建并部署到游戏目录后，必须立即打 LAA 补丁（幂等，已带则跳过）：**

```
python "E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\Tools\apply_laa.py" "D:\!!!!!!!QWCSB\!!!!!!!QWCSB\RTS.EXE"
```

- 链接器产物不带 LARGE_ADDRESS_AWARE（0x010F），不打补丁的 32 位 exe 只能寻址 2GB。
- 最省事顺序：构建 → **先给 `GeneralsMD\Run\RTS.exe` 打 LAA** → 再拷贝部署（部署件自动带标志）；部署后对目标再跑一次脚本校验亦幂等。
- 游戏目录有两个：`D:\!!!!!!!QWCSB\!!!!!!!QWCSB\` 与 `E:\!!!!!!!QWCSB\`。**2026-09-21 修正：用户日常从 D: 盘启动游戏**（9-19 记的"E: 启动"已过时）——EXE 与资源文件一样双目录部署（覆盖前同名先时间戳备份）。
- 用户偏好真身在 `F:\myDocuments\Command and Conquer Generals Zero Hour Data\options.ini`（`StaticGameLOD = Custom` 时其中 `UseShadowVolumes`/`UseShadowDecals` 为唯一定案值，GameData.ini/GameLOD.ini 无覆盖权）。

**构建链铁律（2026-09-21 血泪新增）：**

- 正确构建链 = ww3d2 → GameEngineDevice → RTS 三工程**各自 /MAKE**（桌面"构建工具"文件夹的 bat 即此流程）；build.sh 往 bash PATH 塞 Windows 分号路径会导致 msdev 找不到。
- **msdev 必须单实例**；被取消的后台构建任务≠杀掉 msdev 子进程——每次构建前 `tasklist` 查残留，杀掉后还要删 `GameEngineDevice\Release\vc60.idb`（锁死即 C1033 风暴源）。
- msdev 退出码不可信，以日志 error 计数为准。
- **HEAD ≠ 已部署**：commit 时间晚于部署 exe 时间且含"已恢复 bak"字样 = HEAD 含未实测代码。从 HEAD 构建前必须甄别（9-21 事故：viz 复活代码从未进游戏却藏在 HEAD 里，三次构建全杀影）。
- **着色器安全基线验证**：新 exe 与已知好 exe 做着色器 fragment 零差异比对（`Tools/cmp_exe_shaders.py` 思路：两 exe 提取含 float/tex2D/lerp/step/register 的字符串段做 diff）。**terrainShadow() 内的采样代码形态碰不得——viz 复活任何形态（休眠 if-return/无分支 lerp）都是结构类杀招，会杀 W3X 贴图阴影。**

**资源文件双目录同步铁律（用户钦定 2026-09-19）：**

- 凡修改过 **INI / 其他游戏资源文件**（GameData.ini、Data\INI\* 等），除布置到 D: 游戏目录外，**必须同步布置到 `E:\!!!!!!!QWCSB\` 对应路径**。
- 部署到 E:（或覆盖任何同名文件）前**先备份**：`<原名>.bak_YYYYMMDD_HHMMSS` 存于同目录，再拷贝 + `cmp` 字节校验。
- 汇报时带一行"已同步 E: + 备份名"。

---

## 核心架构速查

### 引擎引导流程

```
WinMain()                                         [Main/WinMain.cpp]
  └── GameMain()                                  [Common/GameMain.cpp]
        └── Win32GameEngine (CreateGameEngine())
              ├── init()   ← 初始化所有子系统
              ├── execute() ← 主循环(30fps 逻辑帧)
              └── delete   ← 清理
```

### 三巨头架构

| 层 | 单例 | 帧率 | 职责 |
|----|------|------|------|
| **GameLogic** | `TheGameLogic` | 固定 30 FPS | 模拟：对象、AI、武器、脚本、物理 |
| **GameClient** | `TheGameClient` | 可变帧率 | 表现：渲染、GUI、输入、音频 |
| **GameNetwork** | `TheNetwork` | 锁步同步 | 网络：仅传输命令，不传状态 |

### 14 个关键单例

| 单例 | 头文件 | 角色 |
|------|--------|------|
| `TheGameEngine` | `Common/GameEngine.h` | 顶层协调器 |
| `TheGlobalData` / `TheWritableGlobalData` | `Common/GlobalData.h` | 所有可调游戏参数 |
| `TheGameLogic` | `GameLogic/GameLogic.h` | 模拟层 |
| `TheGameClient` | `GameClient/GameClient.h` | 表现层 |
| `TheNetwork` | `GameNetwork/NetworkInterface.h` | 网络 |
| `TheMessageStream` | `Common/MessageStream.h` | 命令/事件分发 |
| `TheTacticalView` | `GameClient/View.h` | 摄像机 |
| `TheTerrainLogic` | `GameLogic/TerrainLogic.h` | 地形模拟 |
| `TheTerrainVisual` | `GameClient/TerrainVisual.h` | 地形渲染 |
| `TheAudio` | `Common/GameAudio.h` | 音频 |
| `TheDisplay` | `GameClient/Display.h` | 渲染表面 |
| `TheInGameUI` | `GameClient/InGameUI.h` | HUD/选择/命令 |
| `TheSubsystemList` | `Common/SubsystemInterface.h` | 子系统生命周期管理 |

### 引擎架构图

```
GameEngine (抽象基类 — Include/Common/GameEngine.h)
  └── GameEngineWin32
        ├── GameLogic     — 确定性模拟（"服务器"）
        ├── GameClient    — 渲染、音频、GUI（"表现层"）
        ├── GameNetwork   — 多人连接
        └── SubsystemInterface — 所有子系统实现的契约
```

---

## 核心编码约束（最高优先级）

### VC6 编译约束

```
1. 编译器：Microsoft Visual C++ 6.0 SP6，仅支持 ANSI C89
2. 禁止：C99/C++11+ 新语法
3. 变量声明：必须在代码块顶部，禁止中途声明
4. 编码：GB2312（避免中文乱码）
5. 依赖：原生 MFC4.2、Win32 API，不引入现代第三方库
```

### 架构约定速查表

| 约定 | 说明 |
|------|------|
| **警告即错误** | `/WX` 标志，整个代码库需在 `/W3` 下无警告编译 |
| **预编译头** | `PreRTS.h` 是所有 GameEngine `.cpp` 文件的第一行 include |
| **内存池** | 许多类继承 `MemoryPoolObject`，使用 `MEMORY_POOL_GLUE` 宏 |
| **侵入式链表** | `MAKE_DLINK_HEAD` / `MAKE_DLINK` 宏 + `DLINK_ITERATOR` 遍历 |
| **引用计数** | `AsciiString` 等使用 `InterlockedIncrement/Decrement` |
| **确定性** | 所有逻辑代码必须确定性执行（锁步网络依赖） |
| **无 RTTI / 无异常** | 使用 `AsObject()`/`AsDrawable()` 自定义类型转换 |
| **Module 注册** | `MAKE_STANDARD_MODULE_MACRO` 等宏提供工厂注册 |

---

## 模块系统（行为组合模式）

项目最核心的设计模式——用组合替代继承。~230 多种模块类型。

### 模块分类

| 接口 | 用途 | 示例 |
|------|------|------|
| `UPDATE` | 每帧行为 | `AIUpdate`, `DozerAIUpdate`, `StealthUpdate`, `HordeUpdate` |
| `BODY` | 生命/死亡状态 | `ActiveBody`, `ImmortalBody`, `StructureBody`, `HighlanderBody` |
| `CONTAIN` | 驻军/运输 | `GarrisonContain`, `TransportContain`, `OverlordContain` |
| `DIE` | 死亡行为 | `CreateObjectDie`, `EjectPilotDie`, `SlowDeathBehavior` |
| `DAMAGE` | 伤害响应 | `DamageModule`, `BoneFXDamage` |
| `SPECIAL_POWER` | 特殊能力 | `OCLSpecialPower`, `FireWeaponPower`, `CashBountyPower` |
| `COLLIDE` | 碰撞响应 | `CrateCollide`, `FireWeaponCollide` |
| `DRAW` | 客户端视觉 | `AnimatedParticleSysBoneClientUpdate`, `SwayClientUpdate` |

### 模块关键类型

```
ModuleData      — INI 解析出的模块配置（静态）
Module          — 运行时模块实例（附加到 Object 或 Drawable 上）
ModuleFactory   — 通过名称字符串到构造函数的映射创建模块
ThingFactory    — 从 ThingTemplate 创建 Thing，实例化所有需要的模块
```

### Thing 继承体系

```
Thing (抽象基类)            [Include/Common/Thing.h]
├── Object (逻辑侧)         — 由 GameLogic 管理，有位置/方向/变换 + ThingTemplate
└── Drawable (客户端侧)     — 由 GameClient 管理，对象的视觉表现
```

---

## 项目依赖关系

### 构建配置

| 配置 | 输出后缀 | 用途 |
|------|---------|------|
| Release | `.lib` / `RTS.exe` | 发布版 |
| Debug | `Debug.lib` / `RTSD.exe` | 调试版 |
| Internal | `Internal.lib` / `RTSI.exe` | 内部测试版 |

### 库依赖链

```
RTS.exe
├── GameEngine.lib          (核心引擎 — Common/GameClient/GameLogic/GameNetwork)
│   └── Compression.lib
├── GameEngineDevice.lib    (平台/设备层)
│   ├── BinkW32.lib
│   ├── Mss32.lib
│   └── Compression.lib
├── ww3d2.lib               (3D 渲染器 — DirectX 8)
├── wwlib.lib               (工具: 文件/加密/线程/内存)
├── wwmath.lib              (数学: 向量/矩阵/几何)
├── wwdebug.lib             (调试/日志/性能分析)
├── wwsaveload.lib          (序列化)
├── WWDownload.lib          (HTTP/FTP 下载)
└── GameSpy*.lib            (在线服务)
```

### 编译标志（Release）

```
/G6  /O2 /Ob2  /MD  /W3 /WX
/Yu"PreRTS.h"
/D "WIN32" /D "_MBCS" /D "_WINDOWS" /D "Z_PREFIX" /D "WINVER=0x400"
/D "_STLP_USE_STATIC_LIB" /D "_STLP_NO_DEFAULT_NAMESPACE"
/D "_STLP_VC6" /D "_STLP_DISABLE_VC6_STL"
```

### Include 路径优先级

```
GameEngine/Include
GameEngine/Include/Precompiled
GameEngineDevice/Include
Libraries/Include
Libraries/Source/WWVegas (及子目录: WWLib/WWMath/WWDebug/WWSaveLoad/WW3D2)
Libraries/Include/Granny
Libraries/Source/Bink
Libraries/Source/Miles
```

---

## 第三方与第一方库

### 第三方依赖

| 库 | 供应商 | 用途 |
|----|--------|------|
| GameSpy | GameSpy/Quazal | 在线多人（匹配、NAT、聊天、统计）|
| Bink Video | RAD Game Tools | 过场视频播放 |
| Miles Sound System | RAD Game Tools | 3D 音频引擎 |
| Granny 3D | RAD Game Tools | 3D 模型/动画运行时 |
| NVASM | NVIDIA | 着色器汇编编译器 |
| STLport | 开源 | MSVC 6.0 的 STL 实现 |
| DirectX 8 | Microsoft | 3D 渲染（经由 WW3D2）|

### 第一方 WWVegas 库

| 库 | 用途 |
|----|------|
| **WW3D2** | DirectX 8 渲染器（网格、纹理、着色器、动画、场景图、LOD、粒子）|
| **WWLib** | 跨平台工具（文件 I/O、加密、线程、内存、调试）|
| **WWMath** | 3D 数学库（向量、矩阵、几何）|
| **WWDownload** | HTTP/FTP 下载 |
| **WWSaveLoad** | 序列化框架 |

---

## 修改工作流程

### 分析阶段

1. **确定范围**：要改哪个子系统？（GameLogic / GameClient / GameNetwork / Common / Device）
2. **查单例**：确认相关单例（`TheGameLogic` 等）和模块类型
3. **查文件**：找到具体文件路径和邻近依赖（见 reference.md 详细索引）
4. **查 DSP**：确认目标文件在 `GameEngine.dsp` 或 `GameEngineDevice.dsp` 中已包含

### 修改阶段

1. 只在指定文件中修改
2. 标注改动位置（注释说明）
3. 保留原有注释风格
4. 确认：
   - 是否需要补充函数/类的声明和定义
   - 是否需要增加 `#include`
   - 未使用 VC6 不支持的现代 C++ 语法

### 修改前检查清单（必须）

在给出最终修改代码前，逐一检查：

1. 有否需要补充相关的函数或类的声明和定义
2. 是否需要增加 `#include` 相关文件
3. 修改代码中有否使用了 vc6.0++ 之外的现代 c++ 代码语句，如果使用了必须进行修改，否则编译出错

### 验证阶段

1. 检查 `#include` 路径是否正确
2. 检查是否违反了确定性规则（特别是 GameLogic 中的修改）
3. 检查是否引入了 RTTI 或异常
4. 检查是否使用了 VC6 不支持的语法
5. 检查内存管理（是否该用 MemoryPoolObject？）

---

## 常见修改场景指引

| 场景 | 关注点 | 受影响的子系统 |
|------|--------|---------------|
| 新增单位/建筑行为 | INI 配置 + Module 注册 + ThingTemplate | GameLogic + Common |
| 修改 AI 逻辑 | `AI.h`（38KB） + `AIDock.h` + `ActionManager.h` | GameLogic |
| 修改渲染效果 | `W3DDevice/` + `DrawModule.h` + `ModelState.h` | GameEngineDevice + GameClient |
| 修改网络协议 | `NetCommandMsg.h` + `NetPacket.h` + `FrameData.h` | GameNetwork |
| 修改保存/加载 | `Xfer.h` + `Snapshot.h` + `Recorder.h` | Common |
| 新增 INI 配置字段 | `INI.h` + `FieldParse` 表 + 对应 ModuleData | Common + 对应模块 |
| 修改音频系统 | `GameAudio.h` + `MilesAudioDevice/` | Common + GameEngineDevice |
| GUI/界面修改 | `GameClient/` 下的窗口和控件文件 | GameClient |
| 地图编辑器修改 | `Tools/WorldBuilder/` | Tools |

---

## 参考

- **详细文件索引（按需加载）：** 参见同目录下 `reference.md`
- **修改场景示例：** 参见同目录下 `examples.md`
- **DeepWiki 文档：** <https://deepwiki.com/hunt1hunt/MinGeneralsmd>
- **GitHub 仓库：** <https://github.com/hunt1hunt/MinGeneralsmd>
- **Discord 社区：** <https://discord.gg/CRZDZEhR5p>
