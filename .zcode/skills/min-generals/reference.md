# MinGeneralsmd 项目文件参考索引

> 按需加载的详细参考文档。包含完整目录结构、所有头文件分类索引、子系统逐文件说明。
> 主技能指令参见 `SKILL.md`。

---

## 1. 完整仓库目录结构

```
MinGeneralsmd/
├── .gitattributes              # Git LFS/text/binary 属性
├── .github/                    # GitHub Actions CI/CD 工作流
├── .gitignore                  # 过滤 VS/VS Code/JetBrains 产物、构建输出
├── LICENSE.md                  # GPLv3 + Section 7 附加条款
├── README.md                   # 项目概述与状态
│
├── Generals/                   # 原版 C&C Generals 源码
│   └── Code/
│       ├── GameEngine/         # 核心引擎库（DSP 项目）
│       │   ├── GameEngine.dsp  # VC++ 6.0 项目文件
│       │   ├── Include/        # 公开头文件
│       │   └── Source/         # 实现代码
│       ├── GameEngineDevice/   # 渲染/音频设备层（DSP 项目）
│       ├── Libraries/          # 内部帮助库
│       ├── Main/               # WinMain 入口、资源文件
│       ├── Tools/              # 编辑器工具
│       └── RTS.dsp / RTS.dsw   # 根工作区（VC++ 6.0）
│
├── GeneralsMD/                 # 零点行动（资料片）源码
│   └── Code/
│       ├── GameEngine/         # 核心引擎库
│       │   ├── GameEngine.dsp
│       │   ├── Include/
│       │   │   ├── Common/         # 核心引擎头文件（137 个）
│       │   │   ├── GameClient/     # 表现层头文件（111+ 个）
│       │   │   ├── GameLogic/      # 模拟层头文件（56+ 个）
│       │   │   ├── GameNetwork/    # 多人网络头文件（30+ 个）
│       │   │   └── Precompiled/    # 预编译头设置
│       │   └── Source/
│       │       ├── Common/
│       │       ├── GameClient/
│       │       ├── GameLogic/
│       │       ├── GameNetwork/
│       │       └── Precompiled/
│       ├── GameEngineDevice/   # 渲染/音频设备抽象层
│       │   ├── GameEngineDevice.dsp
│       │   ├── Include/
│       │   │   ├── W3DDevice/         # W3D（Westwood 3D）渲染器
│       │   │   ├── MilesAudioDevice/  # Miles Sound System
│       │   │   ├── VideoDevice/       # 视频播放
│       │   │   └── Win32Device/       # Win32 平台层
│       │   └── Source/
│       │       ├── W3DDevice/
│       │       ├── MilesAudioDevice/
│       │       ├── VideoDevice/
│       │       └── Win32Device/
│       ├── Libraries/          # 内部代码库
│       │   ├── Include/
│       │   │   ├── Granny/         # Granny 3D 动画系统
│       │   │   ├── Lib/            # 核心库头文件（Trig.h, BaseType.h）
│       │   │   └── MSS/            # Miles Sound System 头文件
│       │   ├── Lib/                # 预编译静态库
│       │   └── Source/             # 内部库源码
│       ├── Main/               # 应用程序入口
│       │   ├── WinMain.cpp         # 入口点（36KB）
│       │   ├── WinMain.h
│       │   ├── RTS.rc
│       │   ├── buildVersion.h
│       │   ├── generatedVersion.h
│       │   ├── resource.h
│       │   └── Generals.ico
│       ├── Tools/
│       │   ├── NVASM/             # NVIDIA 着色器汇编器
│       │   └── WorldBuilder/      # MFC 地图编辑器
│       └── RTS.dsp / RTS.dsw
│
└── libraries/                  # 第三方依赖
    ├── dxsdk/                  # DirectX SDK（D3D 8/9）
    ├── gamespy/                # GameSpy SDK（在线多人）
    └── stlport/                # STLport（跨平台 STL）
```

---

## 2. Common 核心层（137 个头文件全列表）

位于 `GeneralsMD/Code/GameEngine/Include/Common/`，按功能分类：

### 2.1 引擎核心
- `GameEngine.h` — 核心引擎类，execute() 主循环，子系统生命周期
- `GameCommon.h` — 共享常量（固定 FPS=30，全局定义）
- `SubsystemInterface.h` — 子系统契约（init/reset/update/shutdown）
- `GameState.h` — 游戏状态枚举
- `GameStateMap.h` — 状态映射
- `GameType.h` — 游戏类型定义

### 2.2 类型系统
- `AsciiString.h` — 引用计数、写时复制字符串（最大 32767 字符）
- `UnicodeString.h` — Unicode 字符串
- `BitFlags.h`, `BitFlagsIO.h` — 类型安全标志位
- `STLTypedefs.h` — STL 容器类型重定义
- `List.h` — 自定义列表容器
- `Dict.h` — 字典/键值存储
- `string.h` — 字符串工具
- `KindOf.h` — Kind-of 分类标志系统
- `ObjectStatusTypes.h` — 状态效果类型定义
- `SpecialPowerType.h` — 特殊能力类型枚举
- `SpecialPowerMaskType.h` — 特殊能力掩码

### 2.3 数据驱动与 INI
- `INI.h` — INI 文件解析器（模板、游戏数据、地图设置）
- `INIException.h` — 解析错误处理
- `WellKnownKeys.h` — 预定义 INI 键常量
- `Thing.h` — 所有游戏对象的基类
- `ThingTemplate.h` — 数据驱动的对象模板
- `ThingFactory.h` — 对象工厂模式
- `ThingSort.h` — 空间排序工具
- `Module.h` — 模块接口
- `ModuleFactory.h` — 模块工厂
- `Overridable.h`, `Override.h` — 可覆盖/重写系统
- `StateMachine.h` — 状态机框架
- `DisabledTypes.h` — 禁用状态类型

### 2.4 序列化系统（Xfer）
- `Xfer.h` — 序列化协议基类
- `XferLoad.h` — 加载序列化
- `XferSave.h` — 保存序列化
- `XferCRC.h` — CRC 校验序列化
- `XferDeepCRC.h` — 深度 CRC 校验
- `DataChunk.h` — 分块数据 I/O
- `Snapshot.h` — 状态快照基类（crc()/xfer()/loadPostProcess()）
- `Recorder.h` — 游戏录像/回放

### 2.5 文件系统
- `FileSystem.h` — 虚拟文件系统抽象
- `ArchiveFileSystem.h` — .big 存档文件系统
- `ArchiveFile.h` — 存档文件处理
- `StreamingArchiveFile.h` — 流式存档文件
- `LocalFile.h` — 本地文件
- `LocalFileSystem.h` — 本地文件系统
- `Directory.h` — 目录枚举
- `RAMFile.h` — RAM 文件抽象
- `file.h` — 底层文件操作

### 2.6 调试与错误处理
- `Debug.h` — 调试断言和日志
- `Errors.h` — 错误系统
- `StackDump.h` — 调用栈转储
- `MiniLog.h` — 轻量日志
- `CRCDebug.h` — CRC 调试

### 2.7 性能分析
- `PerfMetrics.h` — 性能指标收集
- `PerfTimer.h` — 高精度计时
- `GameLOD.h` — 动态 LOD
- `UnitTimings.h` — 时间常量

### 2.8 音频系统
- `GameAudio.h` — 音频系统接口
- `AudioEventRTS.h` — 游戏音频事件
- `AudioEventInfo.h` — 音频事件配置
- `AudioSettings.h` — 音频配置
- `AudioAffect.h` — 音频效果（混响等）
- `AudioRandomValue.h` — 随机音频参数
- `AudioRequest.h` — 音频播放请求
- `AudioHandleSpecialValues.h` — 特殊音频句柄常量
- `DynamicAudioEventInfo.h` — 动态音频事件数据
- `MiscAudio.h` — 杂项音频
- `GameMusic.h` — 音乐系统
- `GameSounds.h` — 音效
- `GameSpeech.h` — 语音系统

### 2.9 玩家与经济
- `Player.h` — 玩家状态和数据
- `PlayerList.h` — 多人玩家列表
- `PlayerTemplate.h` — 玩家阵营模板
- `Team.h` — 队伍/分组定义
- `Money.h` — 资源/金钱系统
- `ResourceGatheringManager.h` — 资源采集逻辑
- `Energy.h` — 电力/能量系统
- `ScoreKeeper.h` — 计分系统
- `Handicap.h` — 玩家让步设置

### 2.10 平台抽象
- `CriticalSection.h` — 线程同步
- `ScopedMutex.h` — RAII 互斥锁
- `Registry.h` — Windows 注册表访问
- `SystemInfo.h` — 系统能力检测
- `OSDisplay.h` — OS 显示信息
- `CommandLine.h` — 命令行参数解析
- `MessageStream.h` — 线程间消息传递
- `CopyProtection.h` — 复制保护
- `CDManager.h` — CD 管理

### 2.11 数学与几何
- `Geometry.h` — 3D 数学/几何
- `DiscreteCircle.h` — 离散圆计算
- `BezFwdIterator.h` — 贝塞尔前向迭代器
- `BezierSegment.h` — 贝塞尔曲线段
- `RandomValue.h` — 随机数生成
- `PartitionSolver.h` — 空间分区求解

### 2.12 游戏特定
- `ClientUpdateModule.h` — 客户端更新模块
- `DrawModule.h` — 绘制模块接口
- `DamageFX.h` — 伤害效果定义
- `SpecialPower.h` — 特殊能力定义
- `Science.h` — 科技定义
- `Upgrade.h` — 升级系统
- `ProductionPrerequisite.h` — 建造前提
- `ActionManager.h` — 行动/命令队列管理
- `BuildAssistant.h` — 建造辅助
- `FunctionLexicon.h` — 脚本函数定义
- `QuickTrig.h` — 快速触发系统
- `ModelState.h` — 模型状态管理
- `Terrain.h` — 地形系统
- `TerrainTypes.h` — 地形类型定义
- `MapObject.h` — 地图对象
- `MapReaderWriterInfo.h` — 地图文件元数据
- `Radar.h` — 雷达/小地图逻辑

### 2.13 网络偏好
- `MultiplayerSettings.h` — 多人设置
- `CustomMatchPreferences.h` — 自定义比赛偏好
- `SkirmishPreferences.h` — 遭遇战偏好
- `QuickmatchPreferences.h` — 快速匹配偏好
- `LadderPreferences.h` — 天梯偏好
- `GameSpyMiscPreferences.h` — GameSpy 设置
- `IgnorePreferences.h` — 忽略列表偏好
- `AcademyStats.h` — 学院统计
- `BattleHonors.h` — 战斗荣誉
- `SkirmishBattleHonors.h` — 遭遇战荣誉
- `MissionStats.h` — 任务统计
- `StatsCollector.h` — 统计收集器
- `SparseMatchFinder.h` — 匹配查找算法

### 2.14 其他工具
- `CRC.h` — CRC 校验
- `encrypt.h` — 加密
- `QuotedPrintable.h` — 可打印引用编码
- `NameKeyGenerator.h` — 名称键生成器
- `BorderColors.h` — 边框颜色
- `TunnelTracker.h` — 隧道跟踪
- `Language.h` — 本地化语言
- `simpleplayer.h` — 简单玩家
- `LatchRestore.h` — 锁存恢复
- `version.h` — 版本信息

---

## 3. GameLogic 层文件索引

位于 `Include/GameLogic/`（56+ 个头文件），关键文件：

### AI 系统
| 文件 | 大小 | 说明 |
|------|------|------|
| `AI.h` | 38KB | 主 AI 系统（最大头文件之一）|
| `AIDock.h` | 8KB | AI 驻军/进驻逻辑 |
| `ActionManager.h` | 7KB | 行动/命令队列管理 |
| `BuildAssistant.h` | - | AI 建造辅助 |

### 武器与战斗
| 文件 | 说明 |
|------|------|
| `DamageFX.h` | 伤害效果定义 |
| `SpecialPower.h` | 特殊能力定义 |
| `Science.h` | 科技定义 |
| `Upgrade.h` | 升级系统 |
| `ProductionPrerequisite.h` | 建造前提 |

### 对象与模块
| 文件 | 说明 |
|------|------|
| `Module.h` | 模块基类接口 |
| `ModuleFactory.h` | 模块工厂 |
| `StateMachine.h` | 状态机框架 |
| `DisabledTypes.h` | 禁用状态类型 |

### 地形
| 文件 | 说明 |
|------|------|
| `Terrain.h` | 地形系统 |
| `TerrainTypes.h` | 地形类型定义 |
| `Radar.h` | 雷达/小地图逻辑 |

### 脚本
| 文件 | 说明 |
|------|------|
| `FunctionLexicon.h` | 脚本函数定义 |
| `QuickTrig.h` | 快速触发系统 |

---

## 4. GameClient 层文件索引

位于 `Include/GameClient/`（111+ 个头文件），关键文件：

| 分类 | 关键文件 |
|------|---------|
| **GUI 窗口** | `GameWindow.h`, `GameWindowManager.h`, `ControlBar.h` |
| **动画** | `Anim2D.h`, `AnimateWindowManager.h` |
| **显示** | `Display.h`, `View.h`, `InGameUI.h` |
| **字体/文本** | `Font.h`, `Language.h` |
| **输入** | `Mouse.h`, `Keyboard.h` |
| **绘制** | `Drawable.h`, `DrawModule.h` |
| **地形渲染** | `TerrainVisual.h` |

---

## 5. GameNetwork 层文件索引

位于 `Include/GameNetwork/`（30+ 个头文件）：

### 锁步核心
| 文件 | 大小 | 说明 |
|------|------|------|
| `NetworkInterface.h` | 6KB | 抽象网络传输接口 |
| `FrameData.h` | 2KB | 每帧数据结构 |
| `FrameDataManager.h` | 2KB | 帧数据同步管理 |
| `FrameMetrics.h` | 3KB | 网络性能指标 |

### 命令传输
| 文件 | 大小 | 说明 |
|------|------|------|
| `NetCommandMsg.h` | 16KB | 命令消息类型 |
| `NetCommandList.h` | 4KB | 命令列表管理 |
| `NetCommandRef.h` | 4KB | 命令引用 |
| `NetCommandWrapperList.h` | 2KB | 命令包装列表 |
| `NetPacket.h` | 12KB | 网络数据包格式 |

### 连接管理
| 文件 | 说明 |
|------|------|
| `Connection.h` | 抽象连接 |
| `ConnectionManager.h` | 连接生命周期管理 |
| `DisconnectManager.h` | 断线处理 |
| `Transport.h` | 传输层抽象 |
| `udp.h` | UDP 套接字实现 |
| `IPEnumeration.h` | 网络接口枚举 |
| `FirewallHelper.h` | UPnP / NAT 穿透 |
| `NAT.h` | NAT 协商 |

### GameSpy 在线服务
| 文件 | 说明 |
|------|------|
| `GameSpy/`（目录） | GameSpy SDK 集成 |
| `GameSpyGP.h` | 游戏存在/认证 |
| `GameSpyChat.h` | 聊天/大厅功能 |
| `GameSpyGameInfo.h` | 游戏列表信息 |
| `GameSpyOverlay.h` | 游戏内覆盖层 |
| `GameSpyThread.h` | 异步 GameSpy 操作 |
| `GameInfo.h`（13KB） | 游戏会话信息 |

### LAN 局域网
| 文件 | 大小 | 说明 |
|------|------|------|
| `LANAPI.h` | 19KB | LAN 发现协议 |
| `LANAPICallbacks.h` | 3KB | LAN 回调 |
| `LANGameInfo.h` | 7KB | LAN 游戏信息 |
| `LANPlayer.h` | 3KB | LAN 玩家表示 |

### 排位与匹配
| 文件 | 说明 |
|------|------|
| `RankPointValue.h` | ELO/排位积分 |
| `LadderPreferences.h` | 天梯偏好 |
| `QuickmatchPreferences.h` | 快速匹配设置 |
| `SparseMatchFinder.h` | 匹配算法 |
| `CustomMatchPreferences.h` | 自定义比赛偏好 |
| `MultiplayerSettings.h` | 多人设置 |

### 辅助
| 文件 | 说明 |
|------|------|
| `networkutil.h` | 网络工具函数 |
| `GUIUtil.h` | 网络 GUI 工具 |
| `User.h` | 用户信息 |
| `GameMessageParser.h` | 游戏消息解析 |
| `FileTransfer.h` | 文件传输 |
| `DownloadManager.h` | 下载管理 |

---

## 6. GameEngineDevice 层

| 设备 | Include 路径 | Source 路径 | 说明 |
|------|-------------|-------------|------|
| **W3DDevice** | `Include/W3DDevice/` | `Source/W3DDevice/` | 3D 渲染（DirectX 8/9）|
| **MilesAudioDevice** | `Include/MilesAudioDevice/` | `Source/MilesAudioDevice/` | 音频（Miles Sound System）|
| **VideoDevice** | `Include/VideoDevice/` | `Source/VideoDevice/` | 视频播放 |
| **Win32Device** | `Include/Win32Device/` | `Source/Win32Device/` | 平台抽象（窗口/输入/定时器/注册表）|

---

## 7. 术语表

| 术语 | 含义 |
|------|------|
| **SAGE** | Strategy Action Game Engine |
| **GeneralsMD** | 零点行动内部代号（资料片）|
| **GameLogic** | 模拟层（确定性"服务器"）|
| **GameClient** | 表现层（渲染、音频、GUI）|
| **Thing** | 所有游戏对象的基类 |
| **ThingTemplate** | 数据驱动的对象模板定义 |
| **Xfer** | 保存/加载和网络序列化系统 |
| **W3D** | Westwood 3D 渲染器 |
| **Miles** | Miles Sound System 音频引擎 |
| **Granny** | Granny 3D 动画系统 |
| **NetCommand** | 通过网络传输的单位命令 |
| **Lock-Step** | 锁步同步（所有对等点同步模拟相同帧）|
| **WorldBuilder** | MFC 地图编辑器工具 |
| **Subsystem** | 实现 SubsystemInterface 契约的引擎组件 |
| **INI** | 驱动游戏内容的配置文件格式 |
| **GameSpy** | 在线多人 SDK（存在、聊天、匹配）|
| **Bink** | RAD Game Tools 的视频播放格式 |
| **NVASM** | NVIDIA 顶点和像素着色器汇编器 |

---

## 8. 参考链接

- **DeepWiki 文档：** <https://deepwiki.com/hunt1hunt/MinGeneralsmd>
- **GitHub 仓库：** <https://github.com/hunt1hunt/MinGeneralsmd>
- **Discord 社区：** <https://discord.gg/CRZDZEhR5p>
- **C&C 终极合集（EA App）：** <https://www.ea.com/games/command-and-conquer/command-and-conquer-the-ultimate-collection>
- **C&C 终极合集（Steam）：** <https://store.steampowered.com/bundle/39394/Command__Conquer_The_Ultimate_Collection/>
