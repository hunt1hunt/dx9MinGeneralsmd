# MinGeneralsmd 项目分析与修改支持技能

> 本技能文件基于两份项目文档的对比分析生成：
> - **A 文件**：基于 [DeepWiki](https://deepwiki.com/hunt1hunt/MinGeneralsmd) 生成的项目索引（全面参考型）
> - **B 文件**：Claude Code CLI 对项目初始化时生成的 CLAUDE.md（开发者行动型）
>
> 用途：当对 [hunt1hunt/MinGeneralsmd](https://github.com/hunt1hunt/MinGeneralsmd) 项目进行深度分析、修改、完善时代入本技能上下文，理解代码库全貌和关键约束。

---

## 一、文件 A vs 文件 B 差异总览

| 维度 | A 文件（DeepWiki 型） | B 文件（CLI 初始化型） |
|------|----------------------|----------------------|
| **定位** | 项目参考索引与百科 | 开发者行动指南与约束 |
| **目录结构** | 完整目录树，子模块细化到 .h 文件级别 | 不包含目录树 |
| **构建系统** | 概述性，仅提 VC6 + DSP | **非常详细**：编译标志、依赖顺序、include 路径、三种配置变体 |
| **引擎架构** | 抽象架构图（Subsystem 模式） | 具体引导流程（WinMain→GameMain→execute） |
| **模块系统** | 未涉及 | **核心内容**：7 大模块分类及示例（UPDATE/BODY/CONTAIN/DIE/DAMAGE/SPECIAL_POWER/COLLIDE） |
| **核心类型** | 仅列文件名 | **详细展开**：BaseType.h 类型定义、AsciiString 实现细节 |
| **关键单例** | 未涉及 | **完整表格**：14 个关键单例及其作用 |
| **文件级索引** | **核心价值**：137 个 Common 头文件分类、GameLogic/GameClient/GameNetwork 逐文件说明 | 不包含 |
| **重要约定** | 未涉及 | **核心价值**：WX 编译、PCH、内存池、侵入式链表、确定性约束、无 RTTI/异常 |
| **VC6 约束** | 未涉及 | **核心价值**：C89 标准、GB2312 编码、变量声明位置、MFC4.2 限制 |
| **第三方库** | 简单列表 | **详细展开**：区分第三方与第一方 WWVegas 库 |
| **工具** | WorldBuilder + NVASM | 未涉及 |
| **术语表** | 有 | 无 |
| **许可** | 有 | 无 |

### 差异本质

- **A 文件是「地图」**：告诉你每个文件在哪里、属于哪个子系统、负责什么功能
- **B 文件是「操作手册」**：告诉你编译怎么搭、代码怎么写、约束是什么

**两者互补，缺一不可。**

---

## 二、核心架构速查（合并精华）

### 2.1 引擎引导流程

```
WinMain()                                         [Main/WinMain.cpp]
  └── GameMain()                                  [Common/GameMain.cpp]
        └── Win32GameEngine (CreateGameEngine())
              ├── init()   ← 初始化所有子系统
              ├── execute() ← 主循环(30fps 逻辑帧)
              └── delete   ← 清理
```

### 2.2 三巨头架构

| 层 | 单例 | 帧率 | 职责 |
|----|------|------|------|
| **GameLogic** | `TheGameLogic` | 固定 30 FPS | 模拟：对象、AI、武器、脚本、物理 |
| **GameClient** | `TheGameClient` | 可变帧率 | 表现：渲染、GUI、输入、音频 |
| **GameNetwork** | `TheNetwork` | 锁步同步 | 网络：仅传输命令，不传状态 |

### 2.3 14 个关键单例

| 单例 | 头文件 | 角色 |
|------|--------|------|
| `TheGameEngine` | `Common/GameEngine.h` | 顶层协调器 |
| `TheGlobalData` | `Common/GlobalData.h` | 所有可调游戏参数 |
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
| `TheWritableGlobalData` | `Common/GlobalData.h` | 可写全局数据 |
| `TheSubsystemList` | `Common/SubsystemInterface.h` | 子系统生命周期管理 |

---

## 三、项目依赖关系（编译用）

### 3.1 构建配置

| 配置 | 输出后缀 | 用途 |
|------|---------|------|
| Release | `.lib` / `RTS.exe` | 发布版 |
| Debug | `Debug.lib` / `RTSD.exe` | 调试版 |
| Internal | `Internal.lib` / `RTSI.exe` | 内部测试版 |

### 3.2 库依赖链

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

### 3.3 编译标志（Release 配置）

```
/G6  /O2 /Ob2  /MD  /W3 /WX
/Yu"PreRTS.h"                    # 预编译头
/D "WIN32" /D "_MBCS" /D "_WINDOWS" /D "Z_PREFIX" /D "WINVER=0x400"
# STLport 兼容:
/D "_STLP_USE_STATIC_LIB" /D "_STLP_NO_DEFAULT_NAMESPACE"
/D "_STLP_VC6" /D "_STLP_DISABLE_VC6_STL"
```

### 3.4 Include 路径（按优先级）

```
GameEngine/Include
GameEngine/Include/Precompiled
GameEngineDevice/Include
Libraries/Include
Libraries/Source/WWVegas          (及 WWLib/WWMath/WWDebug/WWSaveLoad/WW3D2)
Libraries/Include/Granny
Libraries/Source/Bink
Libraries/Source/Miles
```

---

## 四、核心编码约定（严格遵守）

### 4.1 VC6 编译约束（最高优先级）

```
1. 编译器：Microsoft Visual C++ 6.0 SP6，仅支持 ANSI C89
2. 禁止：C99/C++11+ 新语法（禁止 // 行注释? 实际可用但注意兼容）
3. 变量声明：必须在代码块顶部，禁止中途声明
4. 编码：GB2312（避免中文乱码）
5. 依赖：原生 MFC4.2、Win32 API，不引入现代第三方库
6. 分析规则：优先理解 .dsw/.dsp 工程结构，识别 MFC 消息映射、类继承关系
7. 修改规范：
   - 只改指定文件
   - 标注改动位置 + VC6 编译验证说明
   - 保留原有注释
```

### 4.2 修改前检查清单

在给出最终修改代码前，必须逐一检查：

1. 是否需要补充相关函数或类的**声明和定义**
2. 是否需要增加 `#include` 相关文件
3. 修改代码中是否使用了 **VC6.0 之外的现代 C++ 代码语句**，如有必须修改，否则编译出错

### 4.3 架构约定

| 约定 | 说明 |
|------|------|
| **警告即错误** | `/WX` 标志，整个代码库需要在 `/W3` 下无警告编译 |
| **预编译头** | `PreRTS.h` 是所有 GameEngine `.cpp` 文件的第一行 include |
| **内存池** | 许多类继承 `MemoryPoolObject`，使用 `MEMORY_POOL_GLUE` 宏自定义分配 |
| **侵入式链表** | `MAKE_DLINK_HEAD` / `MAKE_DLINK` 宏提供双向链表，用 `DLINK_ITERATOR` 遍历 |
| **引用计数** | `AsciiString` 等使用 `InterlockedIncrement/Decrement` 线程安全引用计数 |
| **确定性** | 所有逻辑代码必须确定性执行，因为锁步网络依赖于此 |
| **无 RTTI / 无异常** | 大多数引擎代码禁用 RTTI 和异常，使用 `AsObject()`/`AsDrawable()` 自定义类型转换 |
| **Module 注册** | `MAKE_STANDARD_MODULE_MACRO` 等宏提供工厂注册样板代码 |

---

## 五、模块系统（行为组合模式）

这是项目最核心的设计模式——用组合替代继承。

### 5.1 模块分类

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

### 5.2 模块关键类型

```
ModuleData      — INI 解析出的模块配置（静态）
Module          — 运行时模块实例（附加到 Object 或 Drawable 上）
ModuleFactory   — 通过名称字符串到构造函数的映射创建模块
ThingFactory    — 从 ThingTemplate 创建 Thing，实例化所有需要的模块
```

### 5.3 Thing 继承体系

```
Thing (抽象基类)            [Include/Common/Thing.h]
├── Object (逻辑侧)         — 由 GameLogic 管理，有位置/方向/变换 + ThingTemplate
└── Drawable (客户端侧)     — 由 GameClient 管理，对象的视觉表现
```

---

## 六、文件层级导览（子系统索引）

### 6.1 Common 核心层（137 个头文件）

位于 `Include/Common/`，按功能分类：

| 分类 | 关键文件 |
|------|---------|
| **引擎核心** | `GameEngine.h`, `GameCommon.h`, `SubsystemInterface.h` |
| **类型系统** | `AsciiString.h`, `UnicodeString.h`, `BitFlags.h`, `STLTypedefs.h` |
| **数据驱动** | `INI.h`, `WellKnownKeys.h`, `Thing.h`, `ThingTemplate.h`, `ThingFactory.h` |
| **序列化** | `Xfer.h`, `XferLoad.h`, `XferSave.h`, `XferCRC.h`, `Snapshot.h` |
| **文件系统** | `FileSystem.h`, `ArchiveFileSystem.h`, `LocalFileSystem.h`, `ArchiveFile.h` |
| **调试** | `Debug.h`, `Errors.h`, `StackDump.h`, `MiniLog.h` |
| **性能** | `PerfMetrics.h`, `PerfTimer.h`, `GameLOD.h` |
| **平台** | `CriticalSection.h`, `Registry.h`, `SystemInfo.h`, `OSDisplay.h` |
| **内存** | `GameMemory.h`, `RAMFile.h`, `ScopedMutex.h` |
| **音频** | `GameAudio.h`, `AudioEventRTS.h`, `AudioSettings.h`, `GameMusic.h`, `GameSounds.h`, `GameSpeech.h` |
| **玩家/经济** | `Player.h`, `PlayerTemplate.h`, `Team.h`, `Money.h`, `ResourceGatheringManager.h` |

### 6.2 GameLogic 层

位于 `Include/GameLogic/`：

| 分类 | 关键文件 |
|------|---------|
| **AI 系统** | `AI.h`（38KB，最大头文件之一）, `AIDock.h`, `ActionManager.h` |
| **武器/战斗** | `DamageFX.h`, `SpecialPower.h`, `Science.h`, `Upgrade.h` |
| **对象模块** | `Module.h`, `ModuleFactory.h`, `StateMachine.h`, `DisabledTypes.h` |
| **地形** | `Terrain.h`, `TerrainTypes.h`, `Radar.h` |
| **脚本** | `FunctionLexicon.h`, `QuickTrig.h` |

### 6.3 GameClient 层

位于 `Include/GameClient/`：

| 分类 | 关键文件 |
|------|---------|
| **GUI** | `Anim2D.h`, `AnimateWindowManager.h`（等 111+ 个文件）|
| **显示/字体** | `Language.h`, `GameLOD.h`, `Display.h` |

### 6.4 GameNetwork 层

位于 `Include/GameNetwork/`（30+ 个文件）：

| 分类 | 关键文件 |
|------|---------|
| **锁步核心** | `NetworkInterface.h`, `FrameData.h`, `FrameDataManager.h` |
| **命令传输** | `NetCommandMsg.h`（16KB）, `NetCommandList.h`, `NetPacket.h`（12KB）|
| **连接管理** | `Connection.h`, `ConnectionManager.h`, `Transport.h`, `udp.h` |
| **NAT/防火墙** | `NAT.h`, `FirewallHelper.h`, `IPEnumeration.h` |
| **GameSpy** | `GameSpyGP.h`, `GameSpyChat.h`, `GameSpyGameInfo.h`, `GameSpyOverlay.h` |
| **LAN** | `LANAPI.h`（19KB）, `LANGameInfo.h`, `LANPlayer.h` |
| **排位/匹配** | `RankPointValue.h`, `SparseMatchFinder.h`, `MultiplayerSettings.h` |

### 6.5 GameEngineDevice 层

| 设备 | 路径 | 说明 |
|------|------|------|
| **W3DDevice** | `Include/W3DDevice/` + `Source/W3DDevice/` | 3D 渲染（DirectX 8/9）|
| **MilesAudioDevice** | `Include/MilesAudioDevice/` + `Source/MilesAudioDevice/` | 音频（Miles Sound System）|
| **VideoDevice** | `Include/VideoDevice/` + `Source/VideoDevice/` | 视频播放 |
| **Win32Device** | `Include/Win32Device/` + `Source/Win32Device/` | 平台抽象（窗口/输入/定时器/注册表）|

---

## 七、第三方与第一方库

### 7.1 第三方依赖

| 库 | 供应商 | 用途 |
|----|--------|------|
| GameSpy | GameSpy/Quazal | 在线多人（匹配、NAT、聊天、统计）|
| Bink Video | RAD Game Tools | 过场视频播放 |
| Miles Sound System | RAD Game Tools | 3D 音频引擎 |
| Granny 3D | RAD Game Tools | 3D 模型/动画运行时 |
| NVASM | NVIDIA | 着色器汇编编译器 |
| STLport | 开源 | MSVC 6.0 的 STL 实现 |
| DirectX 8 | Microsoft | 3D 渲染（经由 WW3D2）|

### 7.2 第一方 WWVegas 库

| 库 | 用途 |
|----|------|
| **WW3D2** | DirectX 8 渲染器（网格、纹理、着色器、动画、场景图、LOD、粒子）|
| **WWLib** | 跨平台工具（文件 I/O、加密、线程、内存、调试）|
| **WWMath** | 3D 数学库（向量、矩阵、几何）|
| **WWDownload** | HTTP/FTP 下载 |
| **WWSaveLoad** | 序列化框架 |

---

## 八、修改项目的工作流程

### 8.1 分析阶段

1. **理解范围**：确定你要修改的子系统（GameLogic / GameClient / GameNetwork / Common / Device）
2. **查 B 文件**：确认相关单例（`TheGameLogic` 等）、模块类型、编码约束
3. **查 A 文件**：找到具体文件路径和邻近依赖
4. **查 DSP**：确认目标文件在 `GameEngine.dsp` 或 `GameEngineDevice.dsp` 中已包含

### 8.2 修改阶段

1. 只在指定文件中修改
2. 标注改动位置（注释说明）
3. 在代码中加入 VC6 编译验证说明
4. 保留原有注释风格
5. 确认：
   - 是否需要补充函数/类的声明和定义
   - 是否需要增加 `#include`
   - 未使用 VC6 不支持的现代 C++ 语法

### 8.3 验证阶段

1. 检查 `#include` 路径是否正确（参考 3.4 节 Include 路径）
2. 检查是否违反了确定性规则（特别是 GameLogic 中的修改）
3. 检查是否引入了 RTTI 或异常
4. 检查是否使用了 VC6 不支持的语法
5. 检查内存管理（是否该用 MemoryPoolObject？）

---

## 九、常见修改场景指引

| 场景 | 关注点 | 受影响的子系统 |
|------|--------|---------------|
| 新增单位/建筑行为 | INI 配置 + Module 注册 + ThingTemplate | GameLogic + Common |
| 修改 AI 逻辑 | `AI.h`（38KB） + `AIDock.h` + `ActionManager.h` | GameLogic |
| 修改渲染效果 | `W3DDevice/` + `DrawModule.h` + `ModelState.h` | GameEngineDevice + GameClient |
| 修改网络协议 | `NetCommandMsg.h` + `NetPacket.h` + `FrameData.h` | GameNetwork |
| 修改保存/加载 | `Xfer.h` + `Snapshot.h` + `Recorder.h` | Common |
| 新增 INI 配置字段 | `INI.h` + `FieldParse` 表 + 对应的 ModuleData | Common + 对应模块 |
| 修改音频系统 | `GameAudio.h` + `MilesAudioDevice/` | Common + GameEngineDevice |
| GUI/界面修改 | `GameClient/` 下的窗口和控件文件 | GameClient |
| 地图编辑器修改 | `Tools/WorldBuilder/` | Tools |

---

## 十、关键参考链接

- **DeepWiki 文档：** <https://deepwiki.com/hunt1hunt/MinGeneralsmd>
- **GitHub 仓库：** <https://github.com/hunt1hunt/MinGeneralsmd>
- **Discord 社区：** <https://discord.gg/CRZDZEhR5p>
- **C&C 终极合集（EA App）：** <https://www.ea.com/games/command-and-conquer/command-and-conquer-the-ultimate-collection>
- **C&C 终极合集（Steam）：** <https://store.steampowered.com/bundle/39394/Command__Conquer_The_Ultimate_Collection/>
