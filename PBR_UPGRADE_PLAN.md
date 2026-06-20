# PBR 全面升级 — 综合实施计划

> 本计划融合原有 PBR_TERRAIN_SUMMARY.md + DeepWiki 搜索发现的技术方案
> 基于 dx9MinGeneralsmd 代码库现状，严格遵守 VC++ 6.0 约束

> **当前实施阶段：** Phase 5 — ps_3_0 + IBL。详细分步计划见 [`.claude/plans/proud-prancing-lynx.md`](.claude/plans/proud-prancing-lynx.md)

---

## 现状总览

| Phase | 名称 | 状态 | 说明 |
|-------|------|------|------|
| **1** | 水面 PBR | ✅ **已完成** | bump 扰动反射 + Fresnel + Blinn-Phong + sparkle 纹理 |
| **2** | 地形 PBR | ✅ **已完成** | 4 个 ps_2_0 变体 (base/noise1/noise2/noise12) |
| **2.5** | 地形 Detail Texture | ⏸️ 暂停 | `m_detailTexture` 已创建未绑定，阶段 5.4 处理 |
| **3** | **PBR 贴图管线** | ✅ **已完成** | `_pbr.dds` 运行时探测加载 + 4-light GGX 运行时编译 |
| **3.5** | **旧模型兼容 (Legacy PBR)** | ⏸️ 暂停 | 从 W3D 材质推导 PBR 参数（旧模型需要时再做） |
| **4** | **单位/建筑 PBR** | ✅ **已完成** | GGX BRDF + 4 光源 + 能量守恒补偿 + 地形/水面扩展 |
| **5** | **ps_3_0 + IBL** | **▶️ 进行中** | 分阶段升级：5.0 预备 → 5.1 ps_3_0 基础设施 → 5.2 循环版 HLSL → 5.3 Diffuse IBL → 5.4 Specular IBL → 5.5 Debug 可视化 |

---

## VC++ 6.0 核心约束（贯穿所有 Phase）

这是整个升级最关键的约束条件，任何修改必须遵守：

| 约束 | PBR 实现中的对应处理 |
|------|----------------------|
| 无 RTTI | PBR 材质检测用 `m_hasPBRMaterial` 标志位，不用 `dynamic_cast` |
| 无异常 | shader 加载失败返回 `FALSE`，回退到旧 W3D 材质路径 |
| 变量声明必须在块顶部 | 所有临时变量提到函数开头声明 |
| HLSL 不能在 VC6 编译 | `.hlsl` 仅供 `fxc.exe` 离线编译，提交 `.fxo` 二进制 |
| PS 1.1 指令限制 (12条) | PBR 着色器目标 `ps_2_0` (~96 条) / `ps_3_0` (无限) |
| 无 `std::vector` | 用 C 数组替代 |
| DX9 SDK 运行时可用 | `D3DXCompileShader` 运行时编译 HLSL（已用于 Water + Terrain PBR） |

**着色器编译策略（二选一）：**
1. **运行时编译**（已用于 Water/Terrain）：`D3DXCompileShader()` 运行时编译 HLSL 源码 → `CreatePixelShader()`
   - 优点：源码可读，调试方便
   - 缺点：依赖用户安装 DX9 SDK 运行时
2. **离线编译**（推荐用于 Unit PBR）：`fxc.exe /T ps_2_0 /E main /Fo output.fxo input.hlsl`
   - 优点：无运行时依赖，字节码体积小
   - 缺点：需要构建脚本集成 fxc.exe

---

## Phase 3：PBR 贴图管线

### 3.1 命名约定

与现有 W3D 资产并存：

```
unit_body.tga              → 原有 Albedo（保持不变）
unit_body_pbr.dds          → 打包贴图：R=粗糙度, G=金属度, B=AO
unit_body_n.dds            → 法线贴图（切线空间, DXT5nm）
```

### 3.2 W3DAssetManager.cpp — _pbr.dds 探测加载

**文件：** `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DAssetManager.cpp`

在 `Get_Texture()` 返回前（约第 188 行），探测同名 `_pbr.dds`：

```cpp
// 在 return WW3DAssetManager::Get_Texture(...) 之前插入
// === 变量声明在函数顶部（VC6）===
char pbrName[256];
char *pDot;
const char *pSrc;

pSrc = filename;
strncpy(pbrName, filename, 250);
pbrName[250] = 0;
pDot = strrchr(pbrName, '.');
if (pDot) {
    *pDot = 0;  // 截断扩展名
    strcat(pbrName, "_pbr.dds");
    // 用 TheFileSystem 检查文件是否存在
    if (TheFileSystem && TheFileSystem->doesFileExist(pbrName)) {
        // 存入全局哈希：albedo_name → pbr_name
        W3DShaderManager::registerPBRTexture(filename, pbrName);
    }
}
```

同理探测 `_n.dds` 法线贴图。

### 3.3 W3DShaderManager.h — 新枚举 + PBRTextureStages

**文件：** `GeneralsMD/Code/GameEngineDevice/Include/W3DDevice/GameClient/W3DShaderManager.h`

在 `ShaderTypes` 枚举中追加（`ST_MAX` 之前）：

```cpp
ST_PBR_UNIT_OPAQUE,       // 单位/建筑 PBR 不透明 pass
ST_PBR_UNIT_ALPHA,        // 单位/建筑 PBR 半透明 pass
ST_PBR_TERRAIN,           // 地形 PBR（Phase 2 已完成，此处保留供未来 terrainshader 切换用）
```

新增 PBR 纹理槽常量（VC6 兼容，用 `enum` 代替 `const int`）：

```cpp
enum PBRTextureStages {
    PBR_STAGE_ALBEDO     = 0,
    PBR_STAGE_NORMAL     = 1,
    PBR_STAGE_ROUGHMETAL = 2,
    PBR_STAGE_IBL_DIFF   = 3,  // Phase 5 用
    PBR_STAGE_IBL_SPEC   = 4,  // Phase 5 用
};
```

同时新增 `LegacyPBRParams` 结构体和注册/查询静态方法：

```cpp
struct LegacyPBRParams {
    float roughness;   // 0=光滑, 1=粗糙
    float metalness;   // 0=非金属, 1=金属
    float pad[2];      // 对齐到 float4
};
static void setLegacyPBRParams(const char *meshName, float roughness, float metalness);
static Bool getLegacyPBRParams(const char *meshName, LegacyPBRParams *outParams);
static Bool isLegacyPBREnabled(void);
static void registerPBRTexture(const char *albedoName, const char *pbrName);
static Bool hasPBRTexture(const char *albedoName);
```

### 3.4 W3DShaderManager.cpp — W3DPBRShader 类

**文件：** `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DShaderManager.cpp`

新增 `W3DPBRShader` 类（继承 `W3DShaderInterface`），在 `init()` 中调用 `LoadAndCreateD3DShader()` 加载预编译 `.fxo` 文件。

实现细节：
- 加载 `pbr_unit_vs.fxo` 和 `pbr_unit_ps.fxo`
- `set()` 中绑定纹理 stage 0=albedo, 1=normal, 2=rough/metal
- 上传常量寄存器：光照方向/颜色、摄像机位置
- `reset()` 清理 stage 0-2 纹理和 pixel shader

### 3.5 Shaders/ 目录新增 HLSL 源文件

**目录：** `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shaders/`

```
pbr_unit_vs.hlsl → pbr_unit_vs.fxo   VS 2.0：输出切线空间 TBN 矩阵
pbr_unit_ps.hlsl → pbr_unit_ps_30.fxo / pbr_unit_ps_20.fxo  双目标：PS 3.0 完整 GGX / PS 2.0 反射向量优化 4 光源
```

---

## Phase 3.5：旧模型兼容 — Legacy PBR

> 这是 DeepWiki 方案中最重要的新增内容。大量旧模型没有 `_pbr.dds` 资产，
> 需要从现有 W3D 材质自动推导 PBR 参数，零资产改动。

### 3.5.1 核心思路

W3D 引擎的 `VertexMaterialClass` 已经包含高光/光泽度参数。
`meshmdlio.cpp` 中的 `post_process()` 在加载时已将极端值规范化为：
- `Specular` → `(0.80, 0.80, 0.80)`
- `Shininess` → `50.0f`

从这些规范化值推导 PBR 参数（**注意：默认 metalness = 0.0 即 dielectric 假设**）：

```
roughness = 1.0f - (shininess / 128.0f);   // Blinn-Phong 光泽度 → 线性粗糙度
roughness = roughness * roughness;          // 感知空间 → 线性空间
metalness = 0.0f;                           // ⚠️ 默认 dielectric，不擅自设为金属
// 仅当原始（未规范化前）高光值明显偏高时，才考虑非零 metalness：
// metalness = (originalSpecLum > 0.85f) ? 0.7f : 0.0f;  // 保守阈值
```

**为什么默认 0.0：** `post_process()` 已经将所有极端 Specular 规范化为 (0.80, 0.80, 0.80)。
如果这时用 `specLum > 0.5f → metalness=0.8`，**所有经过规范化的材质都会变成金属**。
Generals 的车辆虽然大部分是涂漆金属，但建筑、兵种、树有大量非金属材质。全金属效果是错的。
默认 dielectric，让粗糙度独自控制视觉差异——非金属表面靠粗糙度表现高光宽度，比错误的金属度更自然。

### 3.5.2 meshmdlio.cpp — post_process() 追加推导

**文件：** `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/meshmdlio.cpp`
**位置：** 约第 1898 行，现有规范化块之后

在 `post_process()` 末尾追加 Legacy PBR 参数推导，为每个网格名缓存 roughness/metalness。

### 3.5.3 dx8renderer.cpp — DX8TextureCategoryClass::Render() 注入

**文件：** `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dx8renderer.cpp`
**位置：** 约第 1705 行，`Set_Material()` 之后、`Set_Shader()` 之前

每个网格 draw call 都经过此函数，是最关键的注入点。

```cpp
// === Legacy PBR 注入：所有变量声明在函数顶部（VC6）===
// 1. 检查是否有 _pbr.dds（已在加载时探测并缓存）
// 2. 有 → 绑定到 stage 1/2，激活完整 PBR shader
// 3. 无 → 从 VertexMaterialClass 推导 roughness/metalness
//    → 上传到 PS 常量寄存器 c8
//    → 激活 Legacy PBR pixel shader (ST_LEGACY_PBR_UNIT)
```

### 3.5.4 Legacy PBR Shader

**文件：** `Shaders/legacy_pbr_ps.hlsl` → `legacy_pbr_ps.fxo`

简化 PS 2.0 shader，~30 条指令：
- 输入：s0=albedo, c0=光照方向, c1=光照颜色, c2=视线方向, c8=roughness/metalness
- 使用**顶点法线**（无法线贴图）
- 简化 GGX NDF（去掉高开销的动态分支）
- 输出：GGX specular + 漫反射 + 常量环境光

### 3.5.5 关键设计决策

| 问题 | 解决方案 |
|------|---------|
| 旧模型无法线贴图 | Legacy PBR shader 使用顶点法线（`TEXCOORD1`），跳过法线贴图采样 |
| 推导值不准确 | `post_process()` 已规范化材质，推导结果一致；视觉差异可接受 |
| 性能影响 | ~30 条 PS 2.0 指令，比完整 GGX 轻很多 |
| 回退安全 | `isLegacyPBREnabled()` 检查 PS 2.0 支持，不支持则走原有固定管线 |

---

## PS 2.0 → PS 3.0 升级策略

### 为什么需要 PS 3.0

| 能力 | PS 2.0 | PS 3.0 | PBR 中的必要性 |
|------|--------|--------|---------------|
| 最大指令数 | 96 条 | 无限制 | Phase 4 GGX + 8 光源循环可能撑爆 96 条 |
| 临时寄存器 | 12 | 32 | 多光源需要更多中间变量 |
| 动态分支 | ❌ | ✅ | 跳过距离过远的光源，8 光源循环中关键 |
| 纹理采样器 | 4 | 8+ | Phase 5 需要同时绑定 albedo+normal+rough/metal+IBL_diff+IBL_spec |
| 导数指令 (dsx/dsy) | ❌ | ✅ | 法线贴图自动各向异性过滤 |
| 精度 | fp24 | fp32 | HDR/IBL 色调映射需要更高精度 |
| 纹理内取 (TPROJ) | ❌ | ✅ | 投影阴影贴图 |

### 在计划中的定位

```
Phase 1 (水)   → PS 2.0 ✅ 已实现，无需升级
Phase 2 (地形) → PS 2.0 ✅ 已实现，4 变体工作正常
Phase 3 (贴图) → PS 2.0 ✅ 无需 shader 改动
Phase 3.5 (旧模型兼容) → PS 2.0 ✅ 简化 shader ~30 条指令
Phase 4 (单位) → ⚡ PS 3.0 首次引入点
Phase 5 (HDR)  → PS 3.0 必需
```

**最佳升级时机：Phase 4 Step 1**（写 `pbr_unit_ps.hlsl` 时）。

### 双目标编译策略

不放弃 PS 2.0 兼容，两个目标同时编译：

```bash
:: PS 3.0 主版本 — 功能完整
fxc.exe /T ps_3_0 /E PBR_PS /Fo pbr_unit_ps_30.fxo pbr_unit_ps.hlsl

:: PS 2.0 回退版本 — 可能精简光源循环或简化 GGX
fxc.exe /T ps_2_0 /E PBR_PS /Fo pbr_unit_ps_20.fxo pbr_unit_ps.hlsl
```

运行时检测：

```cpp
if (caps.PixelShaderVersion >= D3DPS_VERSION(3,0)) {
    // PS 3.0 路径 — 完整 GGX + 8 光源动态分支
    LoadAndCreateD3DShader("pbr_unit_ps_30.fxo", ...);
} else if (caps.PixelShaderVersion >= D3DPS_VERSION(2,0)) {
    // PS 2.0 回退 — 可能精简为 4 光源展开 + 简化 GGX
    LoadAndCreateD3DShader("pbr_unit_ps_20.fxo", ...);
} else {
    // PS 1.1 — 用原有 W3D 固定管线
}
```

### PS 3.0 对 Phase 4 的具体帮助

1. **8 光源循环**：PS 2.0 无动态分支，必须完全展开 8 个光源的计算（指令数 x8）；PS 3.0 可用 `for` 循环 + `if (dist < radius)` 跳过，指令数大幅减少
2. **纹理采样器充足**：PS 2.0 只有 4 个，PS 3.0 有 8+ 个，IBL 不需要额外 pass
3. **更高精度**：FP32 避免多光源累计时的 banding  artifacts

### Vertex Shader 对应

| VS | 当前 | Phase 4 升级 |
|----|------|-------------|
| 版本 | VS 1.1 (NVASM) | VS 2.0 / 3.0 |
| 原因 | — | 需要输出切线空间 TBN 矩阵给法线贴图 |
| 编译 | 手写 NVASM | fxc.exe /T vs_2_0 或 /T vs_3_0 |

VS 2.0 已足够（TBN 矩阵 3 条指令即可输出），不必强求 VS 3.0。

### 为什么不直接在 Phase 2 升级

Phase 2 地形 PBR 的 PS 2.0 实现工作正常，4 个变体都控制在 96 条指令内。升级到 PS 3.0 不会带来视觉提升，只会增加不必要的复杂度。PS 2.0 保持不动。

### 已整合到对应 Phase 的步骤中

> 以下变更已直接整合到各 Phase 的实施步骤中，不再以补丁形式列出：
> - Phase 4 Step 4.1：双目标编译 PS 3.0 + PS 2.0 fallback
> - Phase 4 Step 4.0：INI 字段含 m_usePS30

### 硬件覆盖率

| 着色器目标 | 最低显卡 | 发布年份 | 现代机器 |
|-----------|---------|---------|---------|
| PS 1.1 | GeForce 3 / Radeon 8500 | 2001 | 100% |
| PS 2.0 | GeForce FX / Radeon 9500 | 2002-2003 | 100% |
| **PS 3.0** | **GeForce 6 / Radeon X1000** | **2004-2005** | **~99.9%** |
| VS 2.0 | GeForce FX / Radeon 9500 | 2002-2003 | 100% |

结论：PS 3.0 覆盖了所有能在现代 Windows 上运行的硬件，D3D9On12 翻译层也完整支持 PS 3.0。可以放心在 Phase 4 引入，同时保留 PS 2.0 回退。

### 4.0 常量寄存器完整布局（PS + VS）

| 源文件 | 编译目标 | 说明 |
|--------|---------|------|
| `pbr_unit_vs.hlsl` | `pbr_unit_vs.fxo` | VS 2.0：输出切线空间 TBN 矩阵（VS 2.0 已足够，3 条指令即可输出 TBN） |
| `pbr_unit_ps.hlsl` | `pbr_unit_ps_30.fxo` | **PS 3.0 Primary** — 完整 GGX + 8 光源循环 + 动态分支 |
| `pbr_unit_ps.hlsl` | `pbr_unit_ps_20.fxo` | **PS 2.0 Fallback** — 精简 GGX（可能缩减到 4 光源展开）|

运行时按显卡能力选择：

```cpp
if (caps.PixelShaderVersion >= D3DPS_VERSION(3,0)) {
    LoadAndCreateD3DShader("pbr_unit_ps_30.fxo", ...);  // 完整 GGX + 8 光源
} else if (caps.PixelShaderVersion >= D3DPS_VERSION(2,0)) {
    LoadAndCreateD3DShader("pbr_unit_ps_20.fxo", ...);  // 精简版
} else {
    // 回退到原有固定管线
}
```

**PS 常量寄存器布局（独立空间，不与 VS 冲突）：**

| 寄存器 | 内容 |
|--------|------|
| c0 | 主光方向 (xyz) + 强度 (w) |
| c1 | 主光颜色 (RGB) + 环境强度 (w) |
| c2 | 摄像机世界坐标 (xyz) |
| c3 | roughness (x), metalness (y), pad(zw) — Legacy PBR 用 |
| c4-c7 | 4 个光源位置 [xyz=位置, w=半径] （远期扩展到 c4-c11 共 8 个）|
| c12-c15 | 4 个光源颜色 [RGB=颜色, w=强度] （远期扩展到 c12-c19）|
| c20 | PBRDebugMode (x), time (yzw) |

**VS 常量寄存器布局（独立空间）：**

| 寄存器 | 内容 |
|--------|------|
| c0-c3 | WorldViewProj 矩阵 |
| c4-c7 | 世界矩阵（法线变换） |
| c8 | 摄像机世界坐标 (xyz) |

> **注意：** VS 和 PS 的常量寄存器是**独立**的（VS c0 ≠ PS c0），不会相互冲突。
> 但 Tree shader (W3DTreeBuffer.cpp) 使用 VS c4-c9 和 c32-c33，需注意重叠。

**核心 BRDF：** GGX NDF + Smith G1 遮挡项 + Schlick Fresnel，输出线性空间颜色。

### 4.2 顶点格式扩展（切线空间）

**文件：** `W3DModelDraw.cpp` / `W3DModelDraw.h`

W3D 格式不含切线/副切线数据，法线贴图需要切线空间。

- `W3DModelDraw::init()` 中计算切线：遍历三角形，用 UV 差值计算切线/副切线
- 切线存入顶点缓冲区的额外流（DX9 支持多顶点流）
- `W3DModelDraw.h` 新增成员：
  ```cpp
  TextureClass *m_pbrAlbedoTex;     // s0
  TextureClass *m_pbrNormalTex;     // s1
  TextureClass *m_pbrRoughMetalTex; // s2
  Bool m_hasPBRMaterial;
  ```

### 4.3 4 光源常量上传（8 光源远期扩展）

**文件：** `W3DDisplay.cpp`

每帧渲染前，收集场景中最近的 8 个动态光源：

```cpp
float lightPosData[4][4];  // 声明在函数顶部（VC6）
float lightColData[4][4];
// ...填充后：
// PS c4-c7: 4 个光源位置（xyz=位置, w=半径）
DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(4, &lightPosData[0][0], 4);
// PS c12-c15: 4 个光源颜色
DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(12, &lightColData[0][0], 4);
```

### 4.4 W3DShaderManager.cpp — 枚举注册 + set() 实现

- 在枚举中追加 `ST_PBR_UNIT_OPAQUE` / `ST_PBR_UNIT_ALPHA`
- `set()` 中绑定 s0=albedo, s1=normal, s2=rough/metal
- 上传光照常量到寄存器
- `reset()` 清理

### 4.5 LOD 开关集成

**文件：** `GameLOD.h`

新增字段：

```cpp
Bool m_usePBRMaterials;  // 启用 PBR 材质（高/自定义 LOD）
Bool m_useNormalMaps;    // 启用法线贴图（Medium+）
Int m_pbrLightCount;     // 实际使用的光源数（1-8）
```

通过 INI（`GameLOD.ini`）控制不同 LOD 级别的 PBR 功能开关。

---

## Phase 5：HDR 环境贴图 / IBL（远期）

### 5.1 预烘焙 IBL 资产

不在运行时实时卷积（DX9 性能不足），离线预烘焙：

| 资产文件 | 格式 | 说明 |
|---------|------|------|
| `env_default_diff.dds` | DXT1 cubemap, 32×32 | 漫反射辐照度立方体图 |
| `env_default_spec.dds` | DXT1 cubemap, 128×128, 7 mip | 预滤波镜面反射 |
| `brdf_lut.dds` | L8A8, 256×256 | GGX BRDF 积分查找表 |

通过 INI 配置指定环境贴图（利用现有数据驱动系统）。

### 5.2 ScreenTonemapFilter 类

**文件：** `W3DShaderManager.h` / `W3DShaderManager.cpp`

新增 `ScreenTonemapFilter`（继承 `W3DFilterInterface`）：

```cpp
class ScreenTonemapFilter : public W3DFilterInterface {
public:
    virtual Int init(void);
    virtual Int shutdown(void);
    virtual Bool preRender(Bool &skipRender, CustomScenePassMode &scenePassMode);
    virtual Bool postRender(enum FilterModes mode, Coord2D &scrollDelta, Bool &doExtraRender);
protected:
    virtual Int set(enum FilterMode mode);
    virtual void reset(void);
    IDirect3DPixelShader9 *m_tonemapPS;  // Reinhard 或 ACES
};
```

利用已有的 `startRenderToTexture()` / `endRenderToTexture()` 机制：
1. 场景渲染到 HDR 浮点纹理 (`D3DFMT_A16B16G16R16F`)
2. `ScreenTonemapFilter.postRender()` 做 tone mapping
3. 输出到后备缓冲区（LDR）

---

## 日冕（Corona）Mod 兼容引入

### 前提：两个引擎的根本差异

| 方面 | 本项目 (SAGE 1.0) | 日冕 mod (RA3 / SAGE 2.0) |
|------|-------------------|---------------------------|
| 模型格式 | `.w3d`（二进制块） | `.w3x`（XML） |
| 着色器格式 | NVASM 汇编 → `.nvp` / `.pso` | HLSL, Effect 框架 `.fx` → `.fxo` |
| 材质系统 | `VertexMaterialClass`（C++ 对象） | XML 材质定义 + Effect 参数 |
| 着色器加载 | `LoadAndCreateD3DShader()` | `ID3DXEffect::CreateFromFile()` |
| 着色器目标 | 当前 PS 1.1，可升级 PS 2.0/3.0 | PS 3.0 |

### 可直接引入的

1. **PBR 贴图资源 (`.dds`)** — DDS 格式通用，成本最低
   - 需确认通道打包约定：日冕 `_orm.dds` 是 R=AO, G=粗糙度, B=金属度
   - 本项目约定 `_pbr.dds` 为 R=粗糙度, G=金属度, B=AO
   - 如需重打包 → 用通道混合工具

2. **IBL 预烘焙立方体图** — 标准 DDS Cubemap，可直接复制

### 需要移植的

1. **从 `.fx` 提取 HLSL** — 去掉 `technique`/`pass`/`sampler` 声明，保留函数体
2. **常量寄存器重映射** — 日冕用语义绑定，本项目手动映射到 c0-c31
3. **PS 3.0 → PS 2.0 降级**（可选）— 简化 GGX，去掉动态分支
4. **用 fxc.exe 离线编译** → `.fxo`，提交到仓库

### 注意事项

1. **版权** — 日冕的着色器代码和贴图资源有版权，引入前需获得作者许可
2. **Effect 框架不可用** — 不能直接 `#include` 或链接日冕的 `.fx` 文件
3. **W3X → W3D 模型转换** — 最困难的部分，推荐 Blender 插件路径
4. **贴图通道打包必须核实** — R/AO/G/金属/B 粗糙度 vs R/粗糙度/G/金属/B/AO 通道顺序错误会导致视觉效果完全错误

---

## RA3 Custom Shaders 研究成果借鉴

> 来源：https://github.com/NordlichtS/custom-shaders-RedAlert3
>
> 对 RA3 (SAGE 2.0) 自定义着色器仓库的全面分析，提取可移植的技术方案
> 有机融入到本计划各 Phase 中。

### 总体架构对比

| 方面 | RA3 (SAGE 2.0) | 本项目 (SAGE 1.0) | 可借鉴度 |
|------|----------------|-------------------|---------|
| 着色器格式 | .fx Effect 框架 + .fxh include 头文件 | NVASM 汇编 / 直接 HLSL 编译 | *** 架构思路 |
| 编译流程 | COMPILEALL.BAT 一键编译所有 .fx | 手动 fxc.exe 或运行时 D3DXCompileShader | ***** 立即采用 |
| 条件编译 | ~25 个 #define 宏控制功能开关 | 无 | *** 用于调试模式 |
| 光源系统 | 8 点光源 + 反射向量优化 | N/A | ***** 核心优化 |
| 贴图通道 | spm.r=金属度, spm.g=粗糙度, spm.b=阵营, spm.a=发光 | _pbr.dds R=粗糙度, G=金属度, B=AO | ** 参考 |
| 法线贴图 | WorldT/WorldB 顶点格式 + hp_normalmapper | 需新增 | **** 参考实现 |
| IBL 方案 | getSKYBOXcolor (立方体图 LOD) + hp_fake_skybox | 无 | *** Phase 5 轻量方案 |

### 关键可移植技术

#### 1. 反射向量优化（多光源性能关键）
**来源：** head2-functions.FXH (hp_specdist)

**问题：** GGX 每光源计算完整的 NDF + G + F 代价极高（~15-20 条指令/光源）。
4 光源展开 = 80 条，8 光源 = 160 条，PS 2.0 只有 96 条指令预算。

**RA3 方案：** 只计算一次反射向量 R = reflect(V, N)，然后用 R 与每个光源方向 Li 做点积
作为 specular 强度的近似衰减因子：

```hlsl
// 反射向量优化（只计算一次）
float3 R = reflect(-V, N);  // V=视线方向, N=法线
float3 specSum = 0;
for (int i = 0; i < lightCount; i++) {
    float3 L = lightDir[i];
    float specFactor = max(0, dot(R, L));   // 反射向量.光源方向
    specFactor = pow(specFactor, roughness * 8 + 2); // 粗糙度控制锥角
    specSum += lightColor[i] * specFactor;
}
// + 漫反射 + 环境光
```

**优点：**
- 每光源从 ~15 条指令降到 ~3 条（PS 2.0 4 光源 x 3 = 12 条，预算充裕）
- PS 3.0 版本可在此基础上增加完整 GGX（动态分支选择）
- 视觉效果在 RTS 远距离视角下足够好

**纳入 Phase 4：**
- **PS 2.0 fallback** 使用反射向量优化（4 光源 x ~3 指令 = 12 条 + 基础漫反射）
- **PS 3.0 Primary** 使用完整 GGX NDF + Smith G + Schlick Fresnel（4 光源循环 + 动态分支）
- 两者共享相同的常量寄存器布局

#### 2. 双质量等级着色器（Two-Quality Approach）
**来源：** ObjectsStandardPBR.fx 的 PS_H_ARPBR / PS_LOW_ARPBR

RA3 为每个对象 PBR 着色器提供高/低两个入口点，用 #define 控制：
```fx
// ObjectsStandardPBR.fx
#define PBR_QUALITY_HIGH
#include "PBR5-10-objects-ARPBR.FX"  // 编译 PS_H_ARPBR

#undef PBR_QUALITY_HIGH
#define PBR_QUALITY_LOW
#include "PBR5-10-objects-ARPBR.FX"   // 编译 PS_LOW_ARPBR
```

**映射到本计划：**
```
RA3 PS_H_ARPBR   -> 本项目 PS 3.0 Primary（完整 GGX + 4 光源循环 + IBL）
RA3 PS_LOW_ARPBR -> 本项目 PS 2.0 Fallback（反射向量优化 + 2 光源展开）
```

**实施方式：** PS 3.0 和 PS 2.0 用不同入口点写在同一个 .hlsl 文件，fxc 用 /E 指定：

```bash
fxc.exe /T ps_3_0 /E PBR_PS_HIGH /Fo pbr_unit_ps_30.fxo pbr_unit_ps.hlsl
fxc.exe /T ps_2_0 /E PBR_PS_LOW  /Fo pbr_unit_ps_20.fxo pbr_unit_ps.hlsl
```

#### 3. 非二值金属分类阈值
**来源：** PBR5-10-objects-ARPBR.FX

RA3 的金属度判断是**连续阈值**而非二值：
```hlsl
// RA3: 金属 vs 非金属使用连续阈值
if (spm.r > 0.75 || spm.g > 0.25) {  // 非二值判断
    // 金属路径——高光颜色来自 albedo
    float3 F0 = lerp(F0_NONMETAL, albedo.rgb, metalness);
} else {
    // 非金属路径——F0 固定 ~0.04
}
```

**纳入 Phase 3.5 Legacy 推导：**
- Legacy metalness 默认 0.0（已修正）
- 但如果需要非零金属度，用连续阈值而非二值：
  ```
  metalness = clamp((specLum - 0.7f) / 0.25f, 0.0f, 1.0f);  // 0.7->0.0, 0.95->1.0
  ```
- 这避免了"全金属/全非金属"的硬边界

#### 4. 程序化天空盒 / 假 IBL（Fake Skybox）
**来源：** head2-functions.FXH (getSKYBOXcolor, hp_fake_skybox)

RA3 在无 HDR 环境贴图时使用简单的**程序化天空盒**：
```hlsl
float4 getSKYBOXcolor(float3 dir, float roughness) {
    // 从方向计算渐变天空颜色
    float height = dir.y * 0.5 + 0.5;
    float3 skyColor = lerp(skyBottom, skyTop, height);
    // 加入太阳方向高亮
    float sunDot = max(0, dot(dir, sunDir));
    skyColor += sunColor * pow(sunDot, 64);
    return float4(skyColor, 1);
}
```

**纳入 Phase 5：**
- 在 IBL 资产准备完成前，先用程序化天空盒作为环境反射占位
- 基于当前游戏的 sun color + ambient + gradient sky 合成
- 不需要外部资产，纯代码生成

#### 5. Alpha 累积链（透明材质）
**来源：** PBR5-10-objects-ARPBR.FX

RA3 的透明材质处理使用累积链：
```hlsl
float finalAlpha = OpacityOverride * texColor.a * vertexColor.a * damageAlpha * nanoBuildAlpha;
```

**纳入 Phase 4 半透明 Pass：**
- ST_PBR_UNIT_ALPHA 的 alpha 输出应是：ini 透明度 x 贴图 alpha x 顶点 alpha x 损伤 alpha
- 不透明 pass (ST_PBR_UNIT_OPAQUE) 跳过 alpha blend

#### 6. 宏驱动的条件编译（Debug 模式基础）
**来源：** ObjectsStandardPBR.fx 的 #define 体系

RA3 用 ~25 个宏控制编译变体：
```fx
#define USE_BUMP              // 法线贴图
#define USE_SPECULAR          // 高光
#define USE_REFLECTION        // 反射
#define USE_FRESNEL           // 菲涅尔
#define USE_SIMPLE_FOG        // 简单雾
```

**纳入本计划 PBR Debug 模式：**
用 #define 宏控制调试可视化输出，通过不同入口点编译：

```bash
:: 正常 PBR
fxc.exe /T ps_3_0 /E PBR_PS_HIGH /Fo pbr_unit_ps_30.fxo pbr_unit_ps.hlsl
:: Roughness 可视化
fxc.exe /T ps_3_0 /E PBR_PS_DEBUG_ROUGHNESS /Fo pbr_debug_roughness.fxo pbr_unit_ps.hlsl
:: Metalness 可视化
fxc.exe /T ps_3_0 /E PBR_PS_DEBUG_METALNESS /Fo pbr_debug_metalness.fxo pbr_unit_ps.hlsl
```

运行时通过 GlobalData.ini 的 PBRDebugMode 切换（见下文调试模式）。

#### 7. COMPILEALL.BAT 构建集成
**来源：** COMPILEALL.BAT

RA3 的编译脚本模式：
```batch
for %%f in (*.fx) do (
    fxc.exe /T ps_3_0 /Fo "compiled\%%~nf.fxo" "%%f"
)
```

**纳入本计划：** 新增 build_shaders.bat（详见下方"Shader 编译构建集成"章节）。

### 纹理通道打包对照

本项目与 RA3、日冕的通道打包不同，引入外部资产时必须注意：

| 资产 | R | G | B | A |
|------|---|---|---|---|
| 本项目 _pbr.dds | 粗糙度 | 金属度 | AO | -- |
| RA3 *spm.dds | 金属度 | 粗糙度 | 阵营遮罩 | 发光 |
| 日冕 *_orm.dds | AO | 粗糙度 | 金属度 | -- |

> 引入外部 PBR 贴图时，需用通道重排工具转换。

### Fresnel 艺术风格选择

RA3 着色器作者**故意选择非物理 Fresnel** 而不是 Schlick Approx，因为：
- RTS 的俯视视角下，物理 Fresnel 效果太微弱
- 艺术风格需要更大的高光对比度

**对本项目的启示：** Generals 同样是俯视 RTS，可在 Phase 4 调参时：
- 物理 Fresnel (Schlick) 作为默认
- 提供 FresnelBias INI 参数让艺术家微调
- 水面 PBR 已用 Fresnel min=0.25 达到类似效果



---

## 补充完善（全局跨 Phase 设施）

### A. PBR Debug 可视化模式

PBR 调试极其困难，无法肉眼判断"是 roughness 错了还是光照方向错了"。
提供 INI 控制的 Debug 模式，通过宏编译的不同入口点切换：

```ini
; GlobalData.ini
PBRDebugMode = 0  ; 0=正常, 1=Roughness, 2=Metalness, 3=法线强度, 4=光源数
```

**实现方案：** 每种调试模式编译为独立的 .fxo 文件，运行时按 INI 设置切换。
不需要在 shader 内做动态分支（PS 2.0 不支持）。

| 模式 | 值 | 显示内容 | 文件 | 目的 |
|------|-----|---------|------|------|
| 正常 | 0 | 正常 PBR 渲染 | pbr_unit_ps_30.fxo | 默认 |
| Roughness | 1 | 粗糙度灰度图 (R通道) | pbr_debug_rough.fxo | 确认粗糙度贴图加载正确 |
| Metalness | 2 | 金属度二值图 (G通道>0.5=白) | pbr_debug_metal.fxo | 确认金属度通道 |
| 法线强度 | 3 | 法线方向伪彩色 (xy->rgb) | pbr_debug_normal.fxo | 确认法线贴图 |
| 光源数量 | 4 | 光源位置小球 | pbr_debug_lights.fxo | 确认 4 光源系统 |

**文件修改：**
- GlobalData.h: 新增 Int m_pbrDebugMode
- GlobalData.cpp: INI 注册 PBRDebugMode
- W3DShaderManager.cpp: set() 中根据 m_pbrDebugMode 加载不同 shader
- Shaders/: 新增 4 个调试 .hlsl -> .fxo

### B. 标准测试场景清单

每个 Phase 的验证步骤需要具体而非模糊的"游戏内确认"：

| # | 场景 | 测试内容 | 适用 Phase | 通过标准 |
|---|------|---------|-----------|---------|
| 1 | 空地 + 太阳移动 | 地形 PBR 回归 | Phase 2/3 | 路面正常，无闪烁 |
| 2 | 水面 + 反射 | 水面 PBR 回归 | Phase 1 | 倒影可见，波光自然 |
| 3 | 单个单位（坦克）| _pbr.dds + Phase 4 GGX | Phase 4 | 高光颜色正确，金属感自然 |
| 4 | 单个建筑 | Phase 4 PBR | Phase 4 | 法线贴图细节可见 |
| 5 | 单个旧模型（无 _pbr.dds）| Legacy PBR fallback | Phase 3.5 | 不出现全金属效果 |
| 6 | 5 单位 + 5 建筑混战 | 多模型 PBR 压力测试 | Phase 4 | 无花屏/无崩溃 |
| 7 | 大规模战斗（30 单位+）| 性能测试 | Phase 4 | 维持 20+ fps |
| 8 | 雪地/沙漠地图 | 贴图兼容性 | Phase 3/4 | 无花屏/无崩溃 |
| 9 | 树（Tree shader 冲突）| 寄存器冲突检查 | Phase 4 | 树渲染正常 |
| 10 | PBR 开关 A/B 对比 | UsePBRMaterials=No 恢复原始 | Phase 4 | 关闭后与原始渲染一致 |

### C. 性能预算目标

| 场景 | 最低帧率 | 硬件目标 | 着色器 |
|------|---------|---------|--------|
| 空地（无单位）| 60fps | GeForce 9600 GT 等效 | PS 2.0 / 3.0 |
| 小规模战斗（20 单位）| 30fps | 同上 | PS 3.0 |
| 大规模战斗（100 单位）| 20fps（可接受）| 同上 | PS 3.0 |
| PS 2.0 fallback | +10fps 相比 PS 3.0 | 同上 | PS 2.0 |

**PS 2.0 指令预算估算（4 光源反射向量优化）：**

| 步骤 | 指令数 | 说明 |
|------|--------|------|
| 法线解码 + 归一化 | ~6 | V8U8 解码, normalize |
| 反射向量 R = reflect(-V, N) | ~3 | 1 次计算, 4 光源共享 |
| 4 光源 x dot(R, L) + pow | ~4 x 3 = 12 | 反射向量优化核心 |
| 漫反射 NdotL x 4 | ~4 x 2 = 8 | 简单 Lambert |
| Fresnel Schlick | ~5 | 基于 NdotV |
| 最终合成 (spec + diff + amb) | ~5 | 加颜色, saturate |
| **总计** | **~39 条** | ✅ 远低于 96 条上限，有充足余量 |

**PS 3.0 完整 GGX 预算估算（4 光源循环）：**

| 步骤 | 指令数 | 说明 |
|------|--------|------|
| 法线解码 + 归一化 | ~6 | |
| 循环头 + 光源距离检查 | ~3 | for + if 动态分支 |
| 每光源 GGX (NDF+G+F) | 4 x ~18 = ~72 | D、G1、F 三项 |
| 漫反射 NdotL | 4 x ~2 = ~8 | |
| Fresnel Schlick | ~5 | |
| 最终合成 | ~5 | |
| **总计** | **~99 条** |  PS 3.0 无限制，完全可行 |

### D. Shader 编译构建集成

.hlsl -> .fxo 编译不可手动执行，必须集成到构建脚本。

**新增：** GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shaders/build_shaders.bat

```batch
@echo off
REM PBR Shader Build Script
REM Requires DirectX SDK with fxc.exe

set FXC="%DXSDK_DIR%Utilities\bin\x86\fxc.exe"
if not exist %FXC% (
    echo WARNING: fxc.exe not found at %FXC%
    echo Skipping shader compilation.
    exit /b 0
)

set SRC=%~dp0
set OUT=%~dp0

echo === Compiling PBR Unit VS 2.0 ===
%FXC% /T vs_2_0 /E PBR_VS /Fo %OUT%pbr_unit_vs.fxo %SRC%pbr_unit_vs.hlsl

echo === Compiling PBR Unit PS 3.0 ===
%FXC% /T ps_3_0 /E PBR_PS_HIGH /Fo %OUT%pbr_unit_ps_30.fxo %SRC%pbr_unit_ps.hlsl

echo === Compiling PBR Unit PS 2.0 Fallback ===
%FXC% /T ps_2_0 /E PBR_PS_LOW /Fo %OUT%pbr_unit_ps_20.fxo %SRC%pbr_unit_ps.hlsl

echo === Compiling Legacy PBR PS 2.0 ===
%FXC% /T ps_2_0 /E LEGACY_PBR_PS /Fo %OUT%legacy_pbr_ps.fxo %SRC%legacy_pbr_ps.hlsl

echo === Compiling Debug Shaders ===
%FXC% /T ps_2_0 /E PBR_DEBUG_ROUGH /Fo %OUT%pbr_debug_rough.fxo %SRC%pbr_unit_ps.hlsl
%FXC% /T ps_2_0 /E PBR_DEBUG_METAL /Fo %OUT%pbr_debug_metal.fxo %SRC%pbr_unit_ps.hlsl
%FXC% /T ps_2_0 /E PBR_DEBUG_NORMAL /Fo %OUT%pbr_debug_normal.fxo %SRC%pbr_unit_ps.hlsl

echo === Done ===
```

**注意：** .fxo 是平台独立的二进制格式，编译一次后可跨机器使用。
建议 .fxo 提交到 git 仓库，不作为构建依赖（减少 CI 配置复杂度）。
但如果修改了 .hlsl，必须重新编译。

## 完整文件修改清单

### Phase 3 — PBR 贴图管线

```
GeneralsMD/Code/GameEngineDevice/Include/W3DDevice/GameClient/
└── W3DShaderManager.h              ← ST_PBR_UNIT_OPAQUE/ALPHA/TERRAIN 枚举
                                       PBRTextureStages 常量

GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/
├── W3DShaderManager.cpp            ← W3DPBRShader 类实现
├── W3DAssetManager.cpp             ← _pbr.dds / _n.dds 探测加载
└── Shaders/
    ├── pbr_unit_vs.hlsl            ← 新增（fxc 编译源）
    ├── pbr_unit_ps.hlsl            ← 新增（fxc 编译源）
    ├── pbr_unit_vs.fxo             ← 预编译二进制
    └── pbr_unit_ps.fxo             ← 预编译二进制
```

### Phase 3.5 — Legacy PBR 旧模型兼容

```
GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/
├── meshmdlio.cpp                   ← post_process() 追加 PBR 参数推导
└── dx8renderer.cpp                 ← DX8TextureCategoryClass::Render() 注入

GeneralsMD/Code/GameEngineDevice/Include/W3DDevice/GameClient/
└── W3DShaderManager.h              ← LegacyPBRParams 结构体 + 静态方法

GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/
├── W3DShaderManager.cpp            ← 哈希缓存实现 + Legacy PBR shader 加载
└── Shaders/
    ├── legacy_pbr_ps.hlsl          ← 新增（fxc 编译源）
    └── legacy_pbr_ps.fxo           ← 预编译二进制
```

### Phase 4 — 单位/建筑 PBR

```
GeneralsMD/Code/GameEngineDevice/Include/W3DDevice/GameClient/
├── W3DShaderManager.h              ← ST_PBR_UNIT 枚举
├── W3DDisplay.h                    ← 8 光源收集接口
└── Module/W3DModelDraw.h           ← PBR 纹理成员 + 切线数据

GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/
├── W3DShaderManager.cpp            ← PBR shader set()/reset()/init()
├── W3DDisplay.cpp                  ← 每帧上传 8 光源常量
├── Drawable/Draw/W3DModelDraw.cpp  ← 切线计算 + PBR 纹理绑定
└── Shaders/
    ├── pbr_unit_vs.hlsl            ← 扩展切线空间输出
    ├── pbr_unit_ps.hlsl            ← GGX + 8 光源（双目标编译）
    ├── pbr_unit_vs.fxo
    ├── pbr_unit_ps_30.fxo          ← PS 3.0 Primary（完整 GGX + 动态分支）
    └── pbr_unit_ps_20.fxo          ← PS 2.0 Fallback（精简版）

Generals/Code/GameEngine/Include/Common/
└── GameLOD.h                       ← m_usePBRMaterials, m_usePS30 等字段
```

### Phase 5 — HDR/IBL

```
GeneralsMD/Code/GameEngineDevice/Include/W3DDevice/GameClient/
└── W3DShaderManager.h              ← ScreenTonemapFilter 类

GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/
├── W3DShaderManager.cpp            ← tonemap shader 加载 + 色调映射
└── Shaders/
    ├── tonemap_ps.hlsl             ← Reinhard/ACES
    └── tonemap_ps.fxo

Assets/
├── env_default_diff.dds            ← 预烘焙 IBL
├── env_default_spec.dds
└── brdf_lut.dds
```

---

## 实施路线图（分步可验证）

> 设计原则：
> - 每步独立可验证，构建通过 + 游戏内视觉确认后方可进入下一步
> - 每步都有回退机制：改坏了只影响该步骤，回退到 git 或关掉 INI 开关即可恢复
> - Phase 3 → 4 → 3.5 → 5 严格顺序执行（先实现完整 PBR 再 fallback，经验更准确）
> - 所有修改跑在 dx9 分支上，不影响原有 dx8 渲染路径

---

### Phase 3 — PBR 贴图管线

#### Step 3.0：GlobalData.h/.cpp INI 注册（Phase 3 前置条件）

| 项目 | 内容 |
|------|------|
| **文件** | GlobalData.h + GlobalData.cpp |
| **改动量** | ~5 行 |
| **交付物** | GlobalData.h 新增 Bool m_usePBRTextures; + GlobalData.cpp INI 表注册 UsePBRTextures + 构造函数初始化为 FALSE |
| **验证** | ✅ 构建通过 -> ✅ .ini 中 UsePBRTextures = No 可关闭 |
| **回退** | INI 开关 = 全局安全开关 |

#### Step 3.1：W3DAssetManager — _pbr.dds 探测

| 项目 | 内容 |
|------|------|
| **文件** | `W3DAssetManager.cpp` — `Get_Texture()` 中 |
| **改动量** | ~20 行 |
| **交付物** | 加载 `foo.tga` 时自动探测 `foo_pbr.dds` 是否存在，结果缓存到全局哈希 |
| **验证** | ✅ 构建通过 → ✅ 在 Debug 日志输出探测结果 |
| **回退** | 纯探测，不改渲染，不影响任何现有功能 |
| **工作量** | ~1 天 |

#### Step 3.2：W3DShaderManager — 枚举 + W3DPBRShader 框架

| 项目 | 内容 |
|------|------|
| **文件** | `W3DShaderManager.h` (枚举 + PBRTextureStages) + `W3DShaderManager.cpp` (W3DPBRShader 类骨架) |
| **改动量** | ~80 行 |
| **交付物** | `ST_PBR_UNIT_OPAQUE`/`ALPHA` 枚举注册；`W3DPBRShader` 类含 init/set/reset/shutdown 空实现 |
| **验证** | ✅ 构建通过 → ✅ set() 返回 TRUE 但暂不改变渲染（空壳）|
| **回退** | 不注册到 `W3DShaders[]` 数组，完全不影响现有渲染 |
| **工作量** | ~0.5 天 |

#### Step 3.3：基础 GGX pixel shader（仅 albedo + 单光源）

| 项目 | 内容 |
|------|------|
| **文件** | `Shaders/pbr_unit_ps.hlsl` + `.fxo` |
| **改动量** | 1 个新 HLSL 文件 |
| **交付物** | PS 2.0 shader：s0=albedo, c0=光照方向, c1=光照颜色, GGX NDF + Smith G + Schlick Fresnel |
| **验证** | ✅ fxc 编译通过 → ✅ W3DPBRShader::init() 加载成功（日志输出）|
| **回退** | 旧模型仍然走固定管线，只有显式设置了 ST_PBR_UNIT 的 draw call 才触发 |
| **工作量** | ~1 天 |

#### Step 3.4：集成测试 — 用地形测试 PBR 贴图管线

| 项目 | 内容 |
|------|------|
| **文件** | 无代码修改，准备测试资产 |
| **交付物** | 准备一张 `test_pbr.dds`（R=粗糙度, G=金属度, B=AO），临时修改 HeightMap 加载它 |
| **验证** | ✅ 构建通过 → ✅ 游戏内地形出现粗糙度/金属度驱动的 specular 变化 |
| **回退** | 删除测试资产即可 |
| **工作量** | ~0.5 天 |

---

### Phase 3.5 — Legacy PBR 旧模型兼容

#### Step 3.5.0：GlobalData.h/.cpp INI 注册（Phase 3.5 前置条件）

| 项目 | 内容 |
|------|------|
| **文件** | GlobalData.h + GlobalData.cpp |
| **改动量** | ~5 行 |
| **交付物** | GlobalData.h 新增 Bool m_useLegacyPBR; + INI 注册 UseLegacyPBR + 构造函数初始化为 FALSE |
| **验证** | ✅ 构建通过 -> ✅ .ini 中 UseLegacyPBR = No 关闭 Legacy 推导 |
| **回退** | \`UseLegacyPBR = No\` 完全关闭 |
| **工作量** | ~0.5 天 |

#### Step 3.5.1：meshmdlio.cpp — post_process() 追加推导

| 项目 | 内容 |
|------|------|
| **文件** | `meshmdlio.cpp` — 约第 1898 行后 |
| **改动量** | ~30 行 |
| **交付物** | 模型加载时，从 `VertexMaterialClass` 的 shininess/specular 推导 roughness/metalness，缓存在全局哈希 |
| **验证** | ✅ 构建通过 → ✅ Debug 日志输出每个模型的推导参数值 |
| **回退** | 注释掉追加代码即可恢复 |
| **工作量** | ~1 天 |

#### Step 3.5.2：dx8renderer.cpp — Render() 注入

| 项目 | 内容 |
|------|------|
| **文件** | `dx8renderer.cpp` — `DX8TextureCategoryClass::Render()` ~第 1705 行 |
| **改动量** | ~40 行 |
| **交付物** | 渲染每个网格时：检测是否有 `_pbr.dds` → 有则完整 PBR / 无则从缓存取 Legacy 参数 → 上传到 c8 常量寄存器 |
| **验证** | ✅ 构建通过 → ✅ 游戏内旧模型出现 subtle PBR 照明变化（非破坏性）|
| **回退** | 用 `isLegacyPBREnabled()` 返回 FALSE 即可完全关闭 |
| **注意** | ⚠️ 这是所有 Phase 中最关键的注入点，改完必须全量测试各类模型（建筑/单位/树）|
| **热路径优化** | Render() 每帧调用几百到几千次。**不要在 Render() 中做字符串操作！** 改为：纹理加载时缓存结果到全局哈希（hasPBRTexture），Render() 只需查 Bool 标记 |
| **工作量** | ~1 天 |

#### Step 3.5.3：legacy_pbr_ps.hlsl — 简化 PBR shader

| 项目 | 内容 |
|------|------|
| **文件** | `Shaders/legacy_pbr_ps.hlsl` + `.fxo` |
| **改动量** | 1 个新 HLSL 文件 |
| **交付物** | PS 2.0 shader：~30 指令，s0=albedo, 顶点法线, 简化 GGX, 无需法线贴图 |
| **验证** | ✅ fxc 编译通过 → ✅ W3DPBRShader 加载成功 |
| **回退** | 不注册到 ShaderManager 即可 |
| **工作量** | ~1 天 |

---

### Phase 4 — 单位/建筑 PBR

#### Step 4.0：GlobalData.h/.cpp INI 注册（Phase 4 前置条件）

| 项目 | 内容 |
|------|------|
| **文件** | GlobalData.h + GlobalData.cpp + GameLOD.h |
| **改动量** | ~10 行 |
| **交付物** | m_usePBRMaterials, m_useNormalMaps, m_pbrLightCount, m_usePS30 四个字段 + INI 注册 |
| **验证** | ✅ 构建通过 -> ✅ UsePBRMaterials = No 完全关闭单位 PBR |
| **回退** | 全局安全开关——\`UsePBRMaterials = No\` 关闭整个 PBR 管线 |
| **工作量** | ~0.5 天 |

#### Step 4.1：完整 PS/VS shader（GGX + 4~8 光源，双目标编译）

| 项目 | 内容 |
|------|------|
| **文件** | `Shaders/pbr_unit_vs.hlsl` + `pbr_unit_ps.hlsl` + `pbr_unit_ps_30.fxo` (PS 3.0) + `pbr_unit_ps_20.fxo` (PS 2.0) |
| **改动量** | 2 个新 HLSL 文件 |
| **交付物** | VS 2.0 输出切线空间 TBN；PS 3.0 Primary GGX NDF + Smith G + Schlick Fresnel + 4 光源循环（反射向量优化）；PS 2.0 Fallback 2 光源展开 |
| **验证** | ✅ fxc 编译通过 → ✅ 常量寄存器布局与 W3DShaderManager 约定一致 |
| **回退** | 不注册到 ShaderManager 即可 |
| **工作量** | ~3 天 |

#### Step 4.2：W3DModelDraw — 切线计算 + PBR 纹理绑定

| 项目 | 内容 |
|------|------|
| **文件** | `W3DModelDraw.cpp` + `W3DModelDraw.h` |
| **改动量** | ~100 行（核心改动）|
| **交付物** | 模型加载时从 UV/三角形计算切线 → 第二顶点流存储；渲染时绑定 s0=albedo, s1=normal, s2=rough/metal |
| **验证** | ✅ 构建通过 → ✅ 特定测试模型出现法线贴图细节 |
| **回退** | `m_hasPBRMaterial=FALSE` 走原有 W3D 渲染路径 |
| **注意** | ⚠️ 最大改动量文件，需要仔细处理 VC6 变量声明在块顶部的约束 |
| **工作量** | ~2 天 |

#### Step 4.3：4 光源收集 + 常量上传（8 光源远期扩展）

| 项目 | 内容 |
|------|------|
| **文件** | `W3DDisplay.cpp` |
| **改动量** | ~50 行 |
| **交付物** | 初始 4 光源（c16-c17 位置, c24-c25 颜色），预留 c18-c23/c26-c31 为远期 8 光源扩展 |
| **验证** | ✅ 构建通过 → ✅ Debug 日志输出每帧光源数量/位置 |
| **回退** | 不影响——没有光源时默认使用主光方向 |
| **工作量** | ~1 天 |

#### Step 4.4：LOD 集成

| 项目 | 内容 |
|------|------|
| **文件** | `GameLOD.h` + `GameLOD.ini` |
| **改动量** | ~10 行 |
| **交付物** | `m_usePBRMaterials` / `m_useNormalMaps` / `m_pbrLightCount` / `m_usePS30` 四个 INI 控制字段 |
| **验证** | ✅ 构建通过 → ✅ .ini 中 `UsePBRMaterials = No` 关闭 PBR → 恢复原始渲染 |
| **回退** | **这是全局安全开关**——`UsePBRMaterials = No` 即完全关闭整个 PBR 管线 |
| **工作量** | ~0.5 天 |

#### Step 4.5：全量测试 + 调参

| 项目 | 内容 |
|------|------|
| **交付物** | 各类模型（建筑/单位/树/兵）在 PBR 开启/关闭下的对比截图 |
| **验证** | ✅ 所有模型渲染正常，无闪烁/花屏 → ✅ PBR 开关可恢复原始效果 |
| **调参** | GGX roughness 映射曲线、spec multiplier、光源距离衰减 |
| **工作量** | ~2 天 |

---

### Phase 5 — HDR/IBL（远期）

#### Step 5.0：GlobalData.h/.cpp INI 注册（Phase 5 前置条件）

| 项目 | 内容 |
|------|------|
| **文件** | GlobalData.h + GlobalData.cpp |
| **改动量** | ~5 行 |
| **交付物** | GlobalData.h 新增 Bool m_useIBL; + INI 注册 UseIBL + 构造函数初始化为 FALSE |
| **验证** | ✅ 构建通过 -> ✅ .ini 中 UseIBL = No 关闭 IBL |
| **回退** | UseIBL = No 完全关闭 |
| **工作量** | ~0.5 天 |

#### Step 5.1：IBL 资产准备

**环境贴图来源（二选一）：**
1. **代码合成（推荐初始方案）：** 基于当前游戏的 sun color + ambient + gradient sky 用 CPU 生成低分辨率 cubemap。不需要外部资产，适合 Phase 5 初调。
2. **外部烘焙：** 用 CubeMapGen 或类似工具对游戏实际场景截图再烘焙。

推荐先用方案 1 做初始调通，后续再用方案 2 替换高质量贴图。

**BRDF LUT 生成工具：** brdf_lut.dds 不是手绘的，需要一个 GGX BRDF 积分渲染器。

| 项目 | 内容 |
|------|------|
| **交付物** | `env_default_diff.dds`（32x32 DXT1 cubemap）+ `env_default_spec.dds`（128x128, 7 mip）+ `brdf_lut.dds`（256x256 R8G8）|
| **BRDF LUT 方法** | Python + numpy 程序化生成：对 NdotV x roughness 二维表做重要性采样 GGX，累加 F*G*V / (4*NdotV*NdotL) |
| **验证** | ✅ DDS 格式正确 -> ✅ 放入游戏数据目录可被加载 -> ✅ 用 PBR 调试模式确认反射可见 |
| **工作量** | ~1 天 |

#### Step 5.2：ScreenTonemapFilter

| 项目 | 内容 |
|------|------|
| **文件** | `W3DShaderManager.h` + `.cpp` |
| **改动量** | ~80 行 |
| **交付物** | `ScreenTonemapFilter` 类：HDR 浮点纹理渲染 → ACES/Reinhard tonemapping → 输出到后备缓冲 |
| **验证** | ✅ 构建通过 → ✅ 场景渲染先到 `A16B16G16R16F` 纹理 → tonemap 后显示 |
| **回退** | 不注册到 `W3DFilters[]` 即可 |
| **工作量** | ~2 天 |

#### Step 5.3：IBL + Tonemap 集成

| 项目 | 内容 |
|------|------|
| **改动量** | ~30 行 |
| **交付物** | PS 中采样 IBL 漫反射/镜面反射立方体图 + BRDF LUT；最终输出经 tonemapping |
| **验证** | ✅ 水面/金属表面出现环境反射（非纯黑色反射）|
| **回退** | INI 开关控制 `UseIBL = No` |
| **工作量** | ~1 天 |

---

## Phase 完整依赖关系

```
Phase 3 (贴图管线) ─── 独立，可先做
  │
  ▼
Phase 4 (单位/建筑 PBR) ─── 依赖 Phase 3 的 _pbr.dds 探测和 W3DPBRShader 框架
  │                         这是 PS 3.0 首次引入点，双目标编译
  ▼
Phase 3.5 (Legacy 旧模型兼容) ─── 依赖 Phase 3 的贴图基础设施 + Phase 4 的完整
                                    PBR 经验。先实现完整 PBR 再 fallback 更准确
  │
  ▼
Phase 5 (HDR/IBL) ─── 依赖 Phase 4 的 GGX 输出（需要线性空间颜色输入）
                       可独立先做 IBL 资产预烘焙
```

每个 Phase 有 INI 开关控制，可在游戏运行时完全关闭：

```ini
; GlobalData.ini
UsePBRTextures  = Yes   ; Phase 3 — _pbr.dds 贴图管线
UseLegacyPBR    = Yes   ; Phase 3.5 — 旧模型材质推导
UsePBRMaterials = Yes   ; Phase 4 — 单位/建筑 GGX
UseIBL          = No    ; Phase 5 — HDR 环境贴图（默认关闭）
```

---

## 安全网机制

| 层级 | 机制 | 范围 |
|------|------|------|
| **编译期** | 每次修改后增量编译 <60s | VC6 msdev.exe |
| **运行时** | INI 开关逐 Phase 控制 | TheGlobalData |
| **硬件回退** | PS 2.0 不支持 → 走固定管线 | D3DCAPS8 PixelShaderVersion |
| **资产回退** | 无 `_pbr.dds` → Legacy 推导 → 原始 W3D | 按网格粒度 |
| **版本控制** | 每步独立 commit，回退粒度细 | git
