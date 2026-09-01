# 水体系统移植计划：下载版 20260530 逐水域反射 + 20号水 → 项目版

> 日期：2026-09-01
> 来源对比：
> - 下载版：`C:\Users\hjzhhzc\Downloads\GeneralsGameCode_bk20260827\GeneralsMD\Code`（W3DWater.cpp 与独立 `W3DWater (9).cpp` 逐字节一致，MD5=`e94d7064…`）
> - 项目版：`E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code`

---

## 0. 目标（用户需求）

在**保留项目版现有能力**（PBR 水面 shader、D3D9 兼容层、设备丢失/状态泄漏处理、WaterDiag 诊断）的前提下，移植下载版 20260530 的水体特性：

1. **2号水内部升级为双层**（用户决定：只内部升级，不新增 `WATER_TYPE_20` 枚举，map.ini 写 20 无效）：项目版 `WATER_TYPE_2_PVSHADER` = 「半透明基底（原0号水）+ PV 倒影（原2号水）」双层渲染，即下载版 20 号水的行为。
2. **逐水域独立反射 + 按高度兜底**（用户决定 D3）：主用下载版逐水域触发器反射（`ST_ReflectRenderInfo` + `m_ReflectRenderVec` + 相机视锥裁剪）；保留项目版按高度反射（`m_pReflectionTextures[8]`）作为**兜底**（无水域触发器时使用）。
3. **map.ini 水面参数重定义**：`WaterType / OpacityRate / WLOffsetX/Y/Z / PatchSize / PatchUVTiles / PatchScalePU / SkyPlaneExt / WaterSCRate / D3DBlendMode / SubdivCellLV / ShroudCover / SkyVCRate`（顶层 SeaBox）+ `DrawMethod / ForceDraw / WLOffset / Patch… / PMeshRateUV / SubdivCell`（逐水域 MappedImage）。`WaterACRate` **暂不支持**（用户决定 D2）。
4. **倒影缺陷修复**：单面镜 D3D ClipPlane 裁剪、地图边界挡水框、战争迷雾倒影遮罩、`SKYPLANE_HEIGHT` 30→160。
5. **高光外挂（HighLight 系统）不移植**（用户明确）：`g_HLPlug` 全部替换为项目版质量门。
6. **实施前备份相关代码文件**。

---

## 1. 两版本质：平行分支（不是新旧版本）

两版同源于原版 EA `W3DWater.cpp`，但被两个不同的社区各自深度改造，**架构与 D3D 抽象均不兼容**：

| 维度 | 项目版（当前） | 下载版 20260530 |
|---|---|---|
| 反射粒度 | **按水面高度**（`m_pReflectionTextures[8]`，同高度水域共享） | **按水域触发器**（每水域独立 `ST_ReflectRenderInfo` + 视锥裁剪） |
| 水面绘制 | `drawSea` 498 行 **PBR**（`m_waveShaderPBR` ps_2_0） | `drawSea` 8 行存根 → `DrawReflectionOnSeaBox`（ps.1.4 `wave.pso` texbem） |
| D3D 抽象 | **D3D9 兼容**（`d3d8compat.h`：`IDirect3DDevice8`→`IDirect3DDevice9`，shader 为 `IDirect3DPixelShader9*`） | **原生 D3D8**（无 d3d8compat，shader 为 `DWORD` 句柄） |
| 水类型 | `0/1/2/3` | `0/1/2/3/20(TRANS_AND_PV)` |
| 反射贴图 | 高度数组 + `findReflectionTextureForHeight` | 单一 `m_pReflectionTexture` + 逐水域 `ReflectTex` |
| 水类型决策 | `GlobalData::m_waterType`（GameData.ini `WaterType` Int） | **HighLight 外挂**（`g_HLPlug.GetWaterWaveClearLevel()` 档位） + map.ini `WaterType` 覆盖，GameData.ini 不再生效 |
| 依赖子系统 | 无 | `GameEngine\HighLightSystem\`（HighLightPlug/MapHLParse/PixMap/TGA/YUV 处理）**项目版完全没有** |

**关键事实：** `ST_ReflectRenderInfo`、`m_ReflectRenderVec`、`g_HLPlug`、`WATER_TYPE_20` 在项目版整个 GameEngineDevice 中**均不存在**。下载版不是替换单个 .cpp 就能跑的，需要头文件 + 引擎调用点 + 决策子系统协同。

---

## 2. 下载版 20260530 特性清单（对齐目标）

### 2.1 逐水域反射体系（W3DWater.h/.cpp）
- `ST_ReflectRenderInfo`：`ID/Level/Name/SeaBox/Trigger/TriVerts/TriIdxs/ReflectTex/NeedToDraw` + 绘制参数（`DrawMethod/ForceDraw/WLOffset/PatchSize/PatchUVTiles/PatchScale/PatchWidth/PatchUVScale/PMeshRateU/V/D3DSrcBlend/D3DDstBlend/SubdivCellLV`）+ DX 缓冲（`NumVertices/NumIndices/VertexBufferD3D/IndexBufferD3D`）+ 方法（`CreateRenderTarget/PolygonTriggerTo/SeaBoxOffset/Release`）
- 两种绘制方法：`REFDRAW_METHOD_PATCH`（格网 patch 平铺，`DrawIndexedPrimitive` TRIANGLESTRIP）/ `REFDRAW_METHOD_PMSMP`（按水域多边形细分网格，TRIANGLELIST）
- 视锥裁剪：`GetCameraViewPoly`（相机视锥四角世界坐标）→ `CanThisWaterTriggerBeSee` → `IsPolygonIntersect`（多边形相交测试）→ 不可见水域不烘焙
- 资源生命周期：`updateRenderTargetTextures` 惰性建 RT/网格，`ParseAllWaterTrigger` 在 `load()` 重建 `m_ReflectRenderVec`

### 2.2 倒影质量修复
- **单面镜 ClipPlane**：`renderMirror` 内 `D3DXPLANE(0,0,1,-level)` + `SetClipPlane(0)`，水面以下物体不再产生反向倒影
- **地图边界挡水框**：`RenderMapBorderCoverL/R/T/B`，当 `s_SkyDiffuse & 0x00FFFFFF` 非黑时渲染 4 个纯黑矩形挡住天空倒影溢出地图边界
- **战争迷雾倒影遮罩**：`RenderShroudCover`，把战争迷雾贴图渲染到挡水框中央空洞
- **雨雪隐藏**：反射烘焙时临时 `TheSnowManager->setVisible(false)`（项目版已有等价）
- **`SKYPLANE_HEIGHT` 30→160**：解决高山湖（水面高度>38）反射裁剪花屏
- **挡水框高度** = 当前地图最高水面 + 0.2（`s_MapBWLevel`）

### 2.3 20号水（0+2 组合）
- `WATER_TYPE_20_TRANS_AND_PV = 20`：`Render` 中 `drawSea()`（PV 倒影）+ `renderWater()`（0号半透明基底）+ `renderWaterMesh()`
- `m_OpacityRate20`：0号水透明度倍率（缺省 0.52，map.ini `OpacityRate` 可调，建议 0.5~0.75），经 `GetOpacityRate20()` 供 `W3DDisplay::Begin_Render` 与 `BaseHeightMap`（透明水深/迷雾）使用
- `WATER_TYPE_20` 时 `renderWater()` 的透明度降低，避免遮挡倒影

### 2.4 水类型决策（HighLight 外挂）
- 全局缺省 20 号水；**GameData.ini `WaterType` 不再生效**
- `g_HLPlug.GetWaterWaveClearLevel() > 0`（检测 `GraphicSrc\!NoWater01.big/02.big` 存在性，即水面档位未满3档）→ 强制 0 号水，map.ini 无效
- 档位满 3 档后，以 map.ini `MappedImage SeaBox` 的 `WaterType:` 为准
- 与天气系统联动：`HighLightPlug::CreateWaterAddColor` 按 map.ini `Texture = WaterWave/WaterBody/WaterACRate/WaterType` 对水纹/水体贴图做补色（`WaterACRate` 调节亮度，建议 1.5），并把结果写回 `Art\Textures`

### 2.5 map.ini 顶层键（`MappedImage SeaBox` 段）
`WaterType / OpacityRate / WLOffsetX / WLOffsetY / WLOffsetZ / PatchSize / PatchUVTiles / PatchScalePU / SkyPlaneExt / WaterSCRate / D3DBlendMode / SubdivCellLV / ShroudCover / SkyVCRate`

### 2.6 逐水域键（`MappedImage [水域名]` 段）
`DrawMethod / ForceDraw / WLOffsetX / WLOffsetY / WLOffsetZ / PatchSize / PatchUVTiles / PatchScalePU / PMeshRateUV / D3DBlendMode / SubdivCell`

---

## 3. 引擎侧差异（下载版相对项目版的改动）

| 文件 | 差异 | 内容 |
|---|---|---|
| `W3DDisplay.cpp` | ~263 del / 279 ins | `updateRenderTargetTextures` 去掉 `m_waterType==2` 限定（任何水类型都执行）；`Begin_Render` 最小透明度 ×`GetOpacityRate20()`；注：其余大量差异是下载版自身无关改动（replay 判定、include 大小写等），**勿整体拷贝** |
| `W3DTerrainVisual.cpp` | ~322 del / 45 ins | `m_waterRenderObject->load()` **取消注释**（项目版被注释 → 逐水域触发解析的入口必须打开）；其余差异多为下载版无关改动（headless/Granny/BloomBox） |
| `BaseHeightMap.cpp` | ~50 del / 61 ins | 透明水深与迷雾 `m_currentMinWaterOpacity` 全部 ×`GetOpacityRate20()`；水域重载时调 `ParseSeaBoxArgsFromMapINI()` |
| `Water.h/.cpp` | 极小 | `WaterSetting` 新增 `StandingRiverTexture` 键（河流贴图优先用），其余为格式 |
| `W3DWaterTracks.h/.cpp` | 极小 | 无关改动 |
| `W3DDisplay.h` | 极小 | 无关改动 |
| `GameEngine\HighLightSystem\*` | **项目版不存在** | 高光外挂全部（HighLightPlug 66KB + PixMap 62KB + MapHLParse + TGA/YUV 处理） |

---

## 4. 修改计划（分阶段，每阶段可独立验证）

> **规则**：C++ 修改一律用 Edit/Python 工具，不用 sed/PowerShell（记忆约束）。编译必须由用户执行，我不自行构建（记忆约束）。实施前先备份（见 Phase 0）。

### Phase 0 — 备份（先做）
备份以下将改动文件的当前版本到 `backup_20260901_water_port/`：
1. `GameEngineDevice\Include\W3DDevice\GameClient\W3DWater.h`
2. `GameEngineDevice\Source\W3DDevice\GameClient\Water\W3DWater.cpp`
3. `GameEngineDevice\Source\W3DDevice\GameClient\W3DDisplay.cpp`
4. `GameEngineDevice\Source\W3DDevice\GameClient\W3DTerrainVisual.cpp`
5. `GameEngineDevice\Source\W3DDevice\GameClient\BaseHeightMap.cpp`
6. `GameEngine\Source\GameClient\Water.cpp`
7. `GameEngine\Include\GameClient\Water.h`
8. （若做 HighLight 决策）新建 `GameEngine\HighLightSystem\` 相关文件

### Phase 1 — 头文件 `W3DWater.h`（新增数据结构/枚举/方法/成员）
**保留**：项目版 PBR/兼容相关 —— `IDirect3DPixelShader9* m_waveShaderPBR / m_waveShaderNoBump / m_dwWavePixelShader`、`IDirect3DVertexShader9* m_dwWaveVertexShader`、`m_pReflectionTextures[8] / m_reflectionHeights / m_numReflectionHeights / m_reflectionFactor / m_reflectionSize / findReflectionTextureForHeight`、`#include "d3d8compat.h"`。
**新增**（照下载版）：
- `#include <vector>`、`#include "dx8wrapper.h"`（`Create_Render_Target`）、`#include "GameLogic/PolygonTrigger.h"`、`#include "texture.h"`、`SAFE_RELEASE` 宏、`SEA_PV_VEC / SEA_IDX_VEC` typedef
- `enum Enum_ReflectDrawMethod { REFDRAW_METHOD_XX=0, REFDRAW_METHOD_PATCH=1, REFDRAW_METHOD_PMSMP=2 }`
- `struct ST_ReflectRenderInfo`（含构造/`CreateRenderTarget`/`PolygonTriggerTo`/`SeaBoxOffset`/`Release`）
- `WATER_TYPE_20_TRANS_AND_PV = 20`（注意枚举顺序：放在 `WATER_TYPE_3` 之后、`WATER_TYPE_MAX` 之前，值为 20）
- 新方法声明：`ParseAllWaterTrigger / ParseSeaBoxArgsFromMapINI / SetDefaultSeaBoxArgs / ParseTriggerArgsFromMapINI / SetDefaultTriggerArgs / GetWaterType / GetOpacityRate20 / ResetPreMapMane / renderMirror(CameraClass*) / renderMirror(CameraClass*, ST_ReflectRenderInfo&) / DrawReflectionOnSeaBox ×2 / generateIndexBuffer & generateVertexBuffer(ST_ReflectRenderInfo&, …) / GeneratePolyMesh / PointInConvexPoly / PointInAnyPoly / IsPolygonIntersect / GetCameraViewPoly / CanThisWaterTriggerBeSee / RenderMapBorderCoverL/R/T/B / RenderShroudCover`
- 新成员：`m_ReflectRenderVec`、`m_MapINIData`、`m_PreMapName`、`m_OpacityRate20`

**决策 D1（移植时定）**：单一 `m_pReflectionTexture` 与项目版 `m_pReflectionTextures[]` 数组的**共存策略**。建议：保留项目版数组（多高度），另加下载版 `m_ReflectRenderVec` 逐水域体系；`renderMirror(cam)`（无参，用单一贴图）可去掉，只用 `renderMirror(cam, RRInf)` 与 `renderMirror(cam, waterHeight, reflTarget)`。

### Phase 2 — `W3DWater.cpp` 主体移植（分模块，每个模块一次到位）
1. **全局态**：`s_WLOffset* / s_PatchSize / s_PatchUVTiles / s_PatchScale / s_PatchWidth / s_PatchUVScale / s_PMeshRate* / s_SkyPlaneExt* / s_WaterSCRate / s_D3DSrcBlend / s_D3DDstBlend / s_SubdivCellLV / s_MapVSX/Y/B / s_ShroudVSX/Y / s_ShroudCOX/Y / s_SkyVCRate / s_MapBWLevel / s_SkyDiffuse`；`SKYPLANE_HEIGHT` 30→160
2. **`ST_ReflectRenderInfo` 网格生成**：`GeneratePolyMesh / generateVertexBuffer(RRInf,…) / generateIndexBuffer(RRInf,…)`（PATCH 与 PMSMP 两套）
3. **触发器解析**：`ParseAllWaterTrigger / SetDefaultTriggerArgs / ParseTriggerArgsFromMapINI`（照下载版；注意下载版用 `strstr/strncpy` 解析 map.ini 缓冲区，可照搬，但 `CString` 需替换为项目版 `AsciiString` 等价逻辑）
4. **SeaBox 解析**：`SetDefaultSeaBoxArgs / ParseSeaBoxArgsFromMapINI`（含 `m_PreMapName` 缓存、地图名→`Maps\xxx\map.ini` 查找、存档路径重构）
5. **反射烘焙**：`renderMirror(cam)` + `renderMirror(cam, RRInf)`（含 ClipPlane 单面镜、挡水框、迷雾遮罩、雨雪隐藏；**保留项目版**的设备丢失检查与 D3D 状态清理）
6. **挡水框/迷雾**：`RenderMapBorderCoverL/R/T/B / RenderShroudCover`
7. **绘制**：`drawSea`（改为逐触发器循环，**用项目版 PBR shader 选择链** `m_waveShaderPBR ? PBR : noBump : wave.pso`）+ `DrawReflectionOnSeaBox(rinfo)`（逐触发器：PATCH 平铺 / PMSMP 网格 + 迷雾第二 pass）
8. **`Render`**：`WATER_TYPE_2` 分支改为双层（`drawSea + renderWater + renderWaterMesh`，等同下载版 `WATER_TYPE_20` 行为）
9. **`updateRenderTargetTextures`**：改为逐触发器（视锥裁剪 + 惰性建 RT/网格 + `renderMirror(cam, RRInf)`）
10. **`load()`**：由 5 行扩展为 `ParseSeaBoxArgsFromMapINI + ParseAllWaterTrigger + s_SkyDiffuse 计算 + 地图尺寸/挡水框/迷雾遮罩尺寸计算`
11. **`reset()/init()`**：`m_PreMapName.clear()`；`m_OpacityRate20` 按水类型初始化；水类型决策（见 Phase 4）
12. **`ReleaseResources/ReAcquireResources`**：遍历 `m_ReflectRenderVec` 释放/重建

### Phase 3 — 引擎集成（.cpp 外部）
1. `W3DDisplay.cpp`：`updateRenderTargetTextures` 去掉 `m_waterType==2` 限定（改为 `if (TheWaterRenderObj)`）；`Begin_Render` 最小透明度 ×`TheWaterRenderObj->GetOpacityRate20()`
2. `W3DTerrainVisual.cpp`：`m_waterRenderObject->load()` 取消注释
3. `BaseHeightMap.cpp`：透明水深/迷雾 ×`GetOpacityRate20()`；水域重载时调 `ParseSeaBoxArgsFromMapINI()`
4. `Water.h/.cpp`：`WaterSetting` 加 `StandingRiverTexture` 键（可选，河流贴图优化）

### Phase 4 — 水类型决策（**不移植 HighLight**，用项目版质量门）
- `W3DWater.cpp` 移除 `#include "HighLightPlug.h"` 与全部 `g_HLPlug` 引用
- `init()`/`ParseSeaBoxArgsFromMapINI()` 里 `(g_HLPlug.GetWaterWaveClearLevel() > 0) ? 0 : 20` 替换为项目版逻辑（D1）：
  - 质量门 = `TheGlobalData->m_waterType`（GameData.ini）；`map.ini WaterType:` 作覆盖
  - 2 号水即双层（D5）；`m_OpacityRate20` 缺省 0.52
- `WaterACRate`：不解析（D2）
### Phase 5 — 编译与验证（用户执行构建；游戏内验证清单见 §7）

---

## 5. 关键设计决策（需用户拍板）

### D1：HighLight 外挂依赖 —— **已决定：不移植**（用户 2026-09-01 明确）
项目版没有 HighLightSystem，**不移植**该子系统。把下载版 `g_HLPlug.GetWaterWaveClearLevel()` 全部替换为项目版原生质量门：
- **采用（B1）**：保留项目版现有机制 —— 以 `GlobalData::m_waterType`（GameData.ini）作为质量门（取值 `0` 无反射、`≥2` 有反射），map.ini `WaterType:` 作覆盖。语义 = **GameData.ini 决定是否允许反射，map.ini 决定具体类型**。
- 下载版"档位未满3档强制0号水"的项目版等价实现：GameData.ini `WaterType=0` 即无反射。

### D2：`WaterACRate` 亮度调节 —— **已决定：暂不支持**（用户 2026-09-01）
`WaterACRate` 键不解析；水面亮度维持项目版现状。

### D3：`drawSea` 合并策略
- 下载版 `DrawReflectionOnSeaBox` 用 `m_dwWavePixelShader`（texbem ps.1.4）。项目版目标是 **PBR**。
- 采用：逐触发器 surface pass 用项目版 PBR 选择链（`m_waveShaderPBR ? PBR : noBump : wave.pso`）；反射贴图烘焙（renderMirror）取下载版（ClipPlane/挡水框）+ 项目版健壮性。

### D4：反射贴图粒度 —— **已决定：逐水域 + 按高度兜底**（用户 2026-09-01）
- 主路径：逐水域独立反射（`m_ReflectRenderVec`，每可见水域独立 RT + 视锥裁剪）
- 兜底：`m_ReflectRenderVec` 为空（地图无水域触发器）时，退回项目版按高度反射（`m_pReflectionTextures[8]` + `findReflectionTextureForHeight`）
- **共存**：保留项目版 `m_pReflectionTextures[8]/m_reflectionHeights/m_numReflectionHeights/m_reflectionFactor/m_reflectionSize` 全套；`updateRenderTargetTextures` 与 `drawSea` 先查 `m_ReflectRenderVec` 非空走逐水域，否则走高度兜底

### D5：20号水暴露 —— **已决定：只内部升级 2 号**（用户 2026-09-01）
- **不新增** `WATER_TYPE_20` 枚举；`WATER_TYPE_2_PVSHADER` 内部即双层（`renderWater` 基底 + `drawSea` 倒影）
- map.ini `WaterType:` 只认 `0/1/2/3`；写 `20` 无效（解析越界按缺省处理）
- 下载版代码中所有 `WATER_TYPE_20_TRANS_AND_PV` 分支移植时**映射到 `WATER_TYPE_2_PVSHADER`** 分支逻辑
- `m_OpacityRate20` 保留，语义改为「2号水时 0 号基底的透明度倍率」（缺省 0.52），`GetOpacityRate20()` 供引擎侧使用

---

## 6. 风险清单与缓解

| 风险 | 影响 | 缓解 |
|---|---|---|
| D3D8 `DWORD` shader 句柄 vs 项目 D3D9 `IDirect3DPixelShader9*` | 编译失败/黑屏 | 保持项目版指针类型；`SetPixelShader` 调用点天然兼容（D3D9 接口）。`LoadAndCreateD3DShader("wave.pso",…,(void**)&m_dwWavePixelShader)` 项目版已跑通，勿改 |
| 下载版 `CString`/`strstr` map.ini 解析 | 中文路径/存档路径 bug | 移植时改项目版 `AsciiString` 等价（`TheWritableGlobalData->m_mapName`、`TheFileSystem->doesFileExist/openFile`），保留 `strstr` 语法解析逻辑 |
| `drawSea` 498行 PBR 与 8行存根冲突 | 水面绘制错乱 | 以项目版 PBR 为基底，把逐触发器循环并入；不整段覆盖 |
| 项目版 `m_pReflectionTextures[]` 与下载版单一 `m_pReflectionTexture` | 头文件成员冲突 | D4 决策：保留项目版数组（兜底）+ 新增逐水域体系，去掉下载版单一贴图 |
| 水类型枚举 | 越界/不可达 | **已决定不新增 20 枚举**（D5）；map.ini `WaterType:` 值限 `0/1/2/3`，越界按缺省；`W3DDisplay` 门条件去掉类型限定 |
| HighLight 缺失导致编译不过 | 阻断 | **已决定不移植**。全部 `g_HLPlug` 引用改为项目版质量门（`GlobalData::m_waterType`）；`W3DWater.cpp` 顶部 `#include "HighLightPlug.h"` 移除 |
| 逐水域 RT 数量无上限 | 显存/性能 | 复用下载版视锥裁剪 + `ForceDraw`；必要时加最大水域数限制 |
| `m_PreMapName` 生命周期（重开局/读档） | 参数不刷新 | `reset()` 里 `ResetPreMapMane()`；`W3DTerrainVisual::load()` 入口保证 `load()` 被调 |
| 项目版 WaterDiag 大量埋点与新代码 | 日志噪音 | 新移植代码可不加埋点，或按需加少量 |

---

## 7. 验证清单（游戏内）

1. **类型切换**：GameData.ini `WaterType=0`（无反射）与 `=2`（双层）分别验证；map.ini `WaterType:` 覆盖生效
2. **双层观感**：2号水同时有半透明基底 + 水面倒影（物体、天空、建筑倒影清晰且方向正确）
3. **逐水域独立**：两处不同高度的水各有正确倒影（倒影不与另一水域贴图串台）；同一高度两块水域互不影响
4. **单面镜**：水面以下物体（河床、水下建筑）**不再**出现反向倒影
5. **边界溢出**：大水面地图，天空倒影不再溢出地图边界（挡水框生效）；联机时未探开区域倒影为迷雾
6. **高山湖**：高海拔水面无花屏（SKYPLANE_HEIGHT=160）
7. **性能**：逐水域烘焙帧耗可接受（视锥裁剪生效，看不到的水域不烘焙）
8. **雨雪**：反射烘焙时雪花不进入倒影
9. **map.ini 重开局生效**：改 map.ini 后重新开局无需重启游戏
10. **设备丢失**：Alt-Tab 最小化/恢复后反射贴图重建正常（项目版 ReAcquireResources 保留）

---

## 8. 备份清单（Phase 0 执行）

见 §4 Phase 0。备份到 `backup_20260901_water_port/`（放项目根或 `GeneralsMD\Code` 上级，避免进入构建目录）。

---

## 9. 决策汇总（全部已定，2026-09-01）

- [x] **D1：HighLight 外挂 → 不移植**。`g_HLPlug` 用项目版 `GlobalData::m_waterType` 质量门替代
- [x] **D2：`WaterACRate` → 暂不支持**（键不解析，亮度维持现状）
- [x] **D3：`drawSea` 合并 → 项目版 PBR 为基底 + 逐触发器循环**
- [x] **D4：反射贴图粒度 → 逐水域主路径 + 按高度兜底**（保留项目版 `m_pReflectionTextures[8]`）
- [x] **D5：20号水 → 只内部升级 2 号**（不新增 `WATER_TYPE_20` 枚举；map.ini 写 20 无效）
