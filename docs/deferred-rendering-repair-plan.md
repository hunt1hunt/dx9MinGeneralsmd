# 延迟渲染修复方案 — 完整实施计划

> 基于 W3DDeferredRenderer + W3DShaderManager + dx8wrapper 现状分析
> 生成于 2026-07-20 | 三次深度迭代自审

---

## 迭代第1轮：初始设计

### 总体架构目标

```
Frame Render Flow (最终态):
  1. [Forward] 不透明物体 → G-Buffer (RT0/1/2 + DepthRT)
  2. [Compute]  Sun Shadow Map → Shadow Mask (2x2 PCF)
  3. [Compute]  SSAO → AO Mask                        ← 可选
  4. [Lighting] Deferred Sun + Dynamic Lights → HDR RT (A16B16G16R16F)
  5. [Post]     Tone Mapping HDR RT → Back Buffer (LDR + Gamma)
  6. [Forward] 半透明物体特效覆盖
```

### WW3DFormat 扩展方案

在 `ww3dformat.h` 枚举尾部新增：

| 枚举名 | 值(D3DFMT) | 用途 | 精度 |
|-------|-----------|------|------|
| `WW3D_FORMAT_R32F` | 114 | Depth RT3 | 32-bit float |
| `WW3D_FORMAT_G16R16F` | 113 | 法线极致精度 | 16:16 float |
| `WW3D_FORMAT_A16B16G16R16F` | 115 | HDR 合成 RT | 16:16:16:16 float |

同时更新：`formconv.cpp` 的转换数组、`Init_D3D_To_WW3_Conversion`、`ww3dformat.cpp/h` 的 format 名称打印和 bytes-per-pixel 表。

### G-Buffer 新的通道布局方案

```
RT0 (A8R8G8B8):  albedo.rgb[sRGB] + metallic.a
RT1 (A8R8G8B8):  octaNormal.rg + roughness.b + emissiveMask.a
RT2 (A8R8G8B8):  emissive.rgb + specialFlag.a
DepthRT (R32F):  NDC depth (z/w)
HDR_RT (A16B16G16R16F):  lighting compose target
```

G-Buffer MRT 保留 3 个 RT (RT0/1/2)，DepthRT 作为第四个 RT(索引3) 或独立管理。

---

## 迭代第2轮：自审发现的问题与修正

### 自审问题1: R32F 作为 MRT 索引3？

**发现**: D3D9 MRT 要求所有 RT 必须是同一 bit-depth 族。R32F (128-bit) 和 A8R8G8B8 (32-bit) 不兼容，**不能**在同一 MRT set 中混合。

**修正**: DepthRT 必须与 G-Buffer RTs 分开渲染。有两种选择：

- **方案A (推荐)**: 不透明物体先渲染一遍写 RT0/1/2 + DepthStencil，然后单独拷出深度到 R32F 纹理
  - 缺点：多一次拷贝
  - 优点：无需改 MRT 基础设施

- **方案B**: 不透明物体渲染两次。第一次写深度到 R32F 纹理（单 RT），第二次写 RT0/1/2（三 MRT）
  - 缺点：几何体渲染两次，性能下降
  - 优点：代码改动集中

- **方案C (选中的方案)**: 将深度编码到 G-Buffer pass 中已有的 RT 里。不增加独立 R32F RT，而是在 A8R8G8B8 中用 RG 两通道编码 深度为 16-bit 定点数
  - 编码: `float2(depth, depth*depth)` 或 `float2(depth, 1-depth)` 在 RG
  - 解码: 在 lighting PS 中从 RG 重建 深度
  - 优点：无额外 RT、无拷贝、无重复渲染
  - 精度：16-bit 定点数 65536 级，远超 8-bit 的 256 级，接近 24-bit 硬件深度缓冲

**决定**: 采用方案C。编码深度到 RT2 的 RG 通道。
- RT2 布局改为: `depthRG.rg + smoothness.b + specialFlag.a`
- emissive 放到 RT1.b 中（与 roughness 共享 B 通道），用 emissiveMask.a 标记
- 或者 emissive 独立放在 RT2.b

最终通道分配：

```
RT0 (A8R8G8B8):  albedo.rgb[sRGB] + metallic.a
RT1 (A8R8G8B8):  octaNormal.rg + roughness.b + emissiveMask/special.a
RT2 (A8R8G8B8):  depth_Hi.r + depth_Lo.g + emissive.b + flag.a
```

emissive 颜色不要求高精度，放一个通道表示强度即可（单色发光）。

### 自审问题2: HDR 缺少必要的 format 导致 RT 链复杂

**发现**: A16B16G16R16F 的添加需要大量修改。且现有 `TextureClass` 构造函数可能不支持 float 格式纹理。

**核实方案**: 
1. 先扩 formconv，让 `Create_Render_Target` 能传入 float format
2. `TextureClass` 通过 `D3DFMT_A16B16G16R16F` 直接传给 D3D9 创建纹理
3. 如果支持则使用，不支持则回退到 A8R8G8B8（无 HDR）

**决定**: HDR 作为可选阶段（Phase 5），先验证硬件支持再启用。

### 自审问题3: 阴影实现程度

**发现**: 现有代码已有 `W3DProjectedShadowManager` 做逐物体投影阴影（decal-style），但缺乏**太阳方向光阴影贴图** (shadow map)。

**范围收缩**: 初始实现只做简化的全屏 PCF 2x2 软阴影，使用单独的 shadow map RT。不整合现有 per-object 阴影系统，保持独立。

### 自审问题4: 重建世界坐标精度

**发现**: 当前 sunLightPS 用 `invViewProj * clipPos` 重建世界坐标，其中 `depth` 来自 `rt2.a` 的 8-bit 值。用 16-bit 深度编码后精度大幅提升，但 worldPos 重建本身需要 float 精度。

**确认**: 16-bit 定点深度 + invViewProj 重建世界坐标在 32-bit float GPU 上精度足够。

---

## 迭代第3轮：最终优化与风险评估

### 风险1: TextureClass 对 float format 的支持

**缓解**: 在 `createGBufferResources` 中，对 float format 调用 `DX8Wrapper::Create_Render_Target` 前先用 `CheckDeviceFormat` 验证。不支持的硬件回退到 A8R8G8B8。

### 风险2: G-Buffer 写入了 sRGB 颜色但 lighting PS 需要线性

**解决**: albedo 从漫反射贴图采样已经是在 sRGB 空间。在 lighting PS 中：
```hlsl
float3 albedo = tex2D(gbuf0, uv).rgb; // 保持 sRGB
// 在 PBR 计算前不做转换，因为 BRDF 在 sRGB 空间计算误差太大！
// 应该在 G-buffer 写入时就转线性，或者 lighting PS 中先转线性
```

**正确做法**: 由于 D3D9 不支持 sRGB 写入 MRT，我们必须在 G-buffer PS 中手动转：
```hlsl
// G-buffer 写入时: albedo 是 sRGB 纹理采样结果，要转线性存储
albedo = pow(albedo, 2.2);
// Lighting PS 中: 使用线性 albedo 做 PBR
// 最终输出做 gamma: pow(color, 1/2.2)
```

### 风险3: DX9 half-pixel offset 确认为必然 bug

**当前**: XYZRHW quad 用了 -1~1 顶点 + 0~1 UV。这无法正确覆盖屏幕。

**修复**: 全屏 quad 改用实际像素坐标 XYZRHW，加上 -0.5 偏移。

### 风险4: PS 3.0 兼容性

**检查**: D3D9 硬件，PS 3.0 是 Shader Model 3.0 要求。大多数 DX9 显卡（GeForce 6+、Radeon X1K+）支持。但集成显卡（Intel GMA）只到 PS 2.0。

**对策**: 在 `init()` 时检查 `Caps.PixelShaderVersion >= D3DPS_VERSION(3,0)`，不满足则 `m_available = false`。并在 ini 中增加 `RequirePS30 = 1` 开关。

---

## 最终分步实施方案

> 每个步骤都标注了**验证方式**，确保可增量测试

---

### Step 1: WW3DFormat 枚举扩展 (基础层)

**文件**:
- `Libraries/Source/WWVegas/WW3D2/ww3dformat.h` — 枚举新增
- `Libraries/Source/WWVegas/WW3D2/ww3dformat.cpp` — `Get_Bytes_Per_Pixel` 更新
- `Libraries/Source/WWVegas/WW3D2/formconv.cpp` — 转换数组扩展
- `Libraries/Source/WWVegas/WW3D2/formconv.h` — `HIGHEST_SUPPORTED_D3DFORMAT` 更新

**操作**:
1. 在 `WW3D_FORMAT_DXT5` 后追加 `WW3D_FORMAT_R32F`、`WW3D_FORMAT_G16R16F`、`WW3D_FORMAT_A16B16G16R16F`
2. 更新 `WW3D_FORMAT_COUNT` 自动计数
3. 在 `formconv.cpp` 中：
   - `WW3DFormatToD3DFormatConversionArray` 追加 `D3DFMT_R32F`、`D3DFMT_G16R16F`、`D3DFMT_A16B16G16R16F`
   - `Init_D3D_To_WW3_Conversion` 中增加反向映射
   - `Get_Bytes_Per_Pixel` 中增加新格式的 bpp
4. `ww3dformat.cpp` 的 `Get_WW3D_Format_Name` 增加名称

**验证方式**: 编译通过后，在 `W3DDeferredRenderer::init()` 中插入测试创建 A16B16G16R16F RT，看 D3D9 是否返回非空。输出日志确认。

---

### Step 2: 修复 Full-Screen Quad (XYZRHW + Half-Pixel)

**文件**:
- `GameEngineDevice/Source/W3DDevice/GameClient/W3DDeferredRenderer.cpp` — `createFullScreenQuad()`

**操作**:
```cpp
bool W3DDeferredRenderer::createFullScreenQuad()
{
    // 使用 G-buffer 尺寸计算像素坐标
    float w = (float)m_gbufferWidth;
    float h = (float)m_gbufferHeight;
    
    // D3D9 half-pixel offset: 偏移 0.5 像素使纹素对齐像素中心
    // XYZRHW 坐标: 左上角 (0,0) 到右下角 (w,h)
    // 偏移 -0.5 使 pixel center 对齐 texel center
    float o = 0.5f;
    
    struct QuadVertex { float x, y, z, rhw; float u, v; };
    QuadVertex verts[4] = {
        { -o,       h - o, 0.0f, 1.0f, 0.0f, 0.0f },
        {  w - o,   h - o, 0.0f, 1.0f, 1.0f, 0.0f },
        { -o,      -o,     0.0f, 1.0f, 0.0f, 1.0f },
        {  w - o,  -o,     0.0f, 1.0f, 1.0f, 1.0f },
    };
    // ... 创建 VB/IB ...
}
```

**验证方式**: 渲染一帧用 PIX/Debugging 工具验证 quad 覆盖整个视口。或输出一个纯色 quad 到 back buffer 肉眼检查。

---

### Step 3: 升级 Shader Model 到 ps_3_0

**文件**:
- `GameEngineDevice/Source/W3DDevice/GameClient/W3DShaderManager.cpp` — gbuffer_ps 编译参数
- `GameEngineDevice/Source/W3DDevice/GameClient/W3DDeferredRenderer.cpp` — sunLight PS、pointLight PS

**操作**:
1. 三处 PS 编译目标从 `"ps_2_0"` 改为 `"ps_3_0"`
2. `init()` 中增加 PS 3.0 能力检测:
   ```cpp
   const D3DCAPS9 &caps = dx8caps;
   if (caps.PixelShaderVersion < D3DPS_VERSION(3,0)) {
       WWDEBUG_SAY(("...need ps_3_0 for MRT.\n"));
       m_available = false;
       return;
   }
   ```

**验证方式**: 编译运行，检查 `WWDEBUG_SAY` 中 PS 编译成功的日志。如果 PS 3.0 编译失败，回退并报错。

---

### Step 4: 修正 G-Buffer 通道分配 + 八面体法线编码

#### 4a. G-Buffer Vertex Shader 改进

**文件**: `W3DShaderManager.cpp` — gbuffer_vs

当前 VS 输出 `oT2 = (clipZ, clipW)` 用于深度。保留。

#### 4b. G-Buffer Pixel Shader 全面重写

```hlsl
struct PS_OUT {
    float4 color0 : COLOR0;  // RT0: Albedo (sRGB→linear) + Metallic
    float4 color1 : COLOR1;  // RT1: OctaNormal.rg + Roughness.b + EmissiveMask.a
    float4 color2 : COLOR2;  // RT2: DepthRG.rg + Emissive.b + SpecialFlag.a
};

// Octahedral encoding
float2 octEncode(float3 n) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    float2 p = n.xy;
    p = (n.z >= 0) ? p : (1 - abs(p.yx)) * (2 * step(0, p.yx) - 1);
    return p * 0.5 + 0.5;
}

PS_OUT main(PS_IN input) {
    PS_OUT o;
    float4 albedo = tex2D(Diffuse, input.tex0);
    // Convert sRGB to linear for lighting
    float3 albedoLinear = pow(albedo.rgb, 2.2);
    float3 n = normalize(input.worldNormal);
    float metallic = 0;  // TODO: from material/INI
    float roughness = 0.8;  // TODO: from material/INI
    float3 emissive = float3(0,0,0);
    float depth = input.clipDepth.x / input.clipDepth.y;
    
    o.color0 = float4(albedoLinear, metallic);
    o.color1 = float4(octEncode(n), roughness, 0); // emissiveMask=0
    o.color2 = float4(depth, depth*depth, emissive.g, 0); // RG depth encoding
    return o;
}
```

**操作**:
1. 重写 `gbuffer_ps` 字符串内容
2. 粗糙度和金属度来源：当前从 `PBR_GetLegacyPBRParams` 读取。在 VS 中传递 mesh name 或通过 constant table。
   - 简化方案：先用常量默认值 (roughness=0.8, metallic=0.0)
   - 后续从 `PBROverride.ini` 中通过 `LegacyPBRParams` 传入

**验证方式**: 
- 编译通过
- PIX 抓帧查看 RT0/1/2 内容是否正确

#### 4c. SunLight PS 加入八面体解码 + 16-bit 深度解码

```hlsl
float3 octDecode(float2 e) {
    float2 p = e * 2 - 1;
    float3 n = float3(p.x, p.y, 1 - abs(p.x) - abs(p.y));
    float t = max(-n.z, 0);
    n.xy += (n.xy >= 0) ? -t : t;
    return normalize(n);
}

float decodeDepth(float2 rg) {
    // 编码: rg = (depth, depth*depth)
    // 解码: depth = rg.r, 或用 rg.g/rg.r 做校验
    return rg.r;  // 高精度部分在 R 通道
}
```

---

### Step 5: HDR 渲染目标 + Tone Mapping

**文件**:
- `W3DDeferredRenderer.h` — 新增 `m_hdrRT`
- `W3DDeferredRenderer.cpp` — 创建 HDR RT，lighting pass 绘制到 HDR RT

**操作**:
1. `W3DDeferredRenderer` 新增:
   ```cpp
   TextureClass *m_hdrRT;     // A16B16G16R16F 或 A8R8G8B8 fallback
   bool m_hdrAvailable;       // 硬件是否支持 float RT
   ```
2. 在 `sunLightPass` 前: 切换到 HDR RT 作为渲染目标
3. 在所有 lighting pass 完成后: 做 tone mapping
4. Tone mapping PS 用 Reinhard 或 ACES:
   ```hlsl
   float3 hdrColor = tex2D(hdrRT, uv);
   float3 ldrColor = hdrColor / (hdrColor + 1.0); // Reinhard
   ldrColor = pow(ldrColor, 1.0/2.2); // Gamma
   ```
5. Tone mapping 后绘制到 back buffer

**验证方式**: 亮部细节在 HDR 中不截断（过曝区域有高光细节）。

---

### Step 6: Sun Shadow Map (2x2 PCF)

**文件**:
- `W3DDeferredRenderer.h/cpp` — shadow map 相关
- `W3DScene.cpp` — shadow map 渲染时机

**操作**:
1. 创建 shadow map RT (A8R8G8B8 或 R32F 深度纹理, 2048x2048)
2. 在 G-Buffer pass 之前(或之后)：
   - 设置 shadow camera (从太阳方向看向场景中心)
   - 渲染场景深度到 shadow map
3. 在 sunLightPass 中:
   - 绑定 shadow map 作为纹理 s4
   - 计算 shadow UV: `shadowPos = mul(float4(worldPos,1), lightViewProj)`
   - 2x2 PCF: 采样 4 个周围纹素，比较深度
4. shadow 结果乘到 direct 光照上

**验证方式**: 阳光下物体投射正确的硬阴影，PCF 2x2 使边缘略微模糊。

---

### Step 7: SSAO (可选)

**操作**:
1. 从 DepthRT 重建法线 + 深度
2. 对每个像素采样周围 N 个点，计算遮挡因子
3. 模糊 AO 图（双边滤波）
4. AO 作为乘性因子应用到 ambient 项

**验证方式**: 角落和物体接触面变暗。

---

### Step 8: 完整管线集成

**文件**: `W3DScene.cpp` — render 流程

最终渲染循环:
```cpp
// === Deferred Rendering Pipeline ===
if (g_theW3DDeferredRenderer && g_theW3DDeferredRenderer->isAvailable()) {
    // 1. Shadow Map Pass (sun)
    g_theW3DDeferredRenderer->beginShadowMapPass();
    Customized_Render(rinfo); // 只写深度
    Flush(rinfo);
    g_theW3DDeferredRenderer->endShadowMapPass();
    
    // 2. G-Buffer Pass (不透明物体 → MRT)
    g_theW3DDeferredRenderer->beginGBufferPass();
    g_gbufferActive = true;
    setCustomPassMode(SCENE_PASS_GBUFFER);
    updatePlayerColorPasses();
    updateFixedLightEnvironments(rinfo);
    Customized_Render(rinfo);
    Flush(rinfo);
    g_gbufferActive = false;
    setCustomPassMode(SCENE_PASS_DEFAULT);
    g_theW3DDeferredRenderer->endGBufferPass();
    
    // 3. SSAO (可选)
    g_theW3DDeferredRenderer->computeAO();
    
    // 4. Deferred Lighting → HDR RT
    g_theW3DDeferredRenderer->beginHDRPass();
    g_theW3DDeferredRenderer->sunLightPass(sunDir, sunColor, ambient, camPos, invViewProj);
    g_theW3DDeferredRenderer->renderDynamicLights(dev, camPos, invViewProj);
    g_theW3DDeferredRenderer->endHDRPass();
    
    // 5. Tone Mapping → Back Buffer
    g_theW3DDeferredRenderer->toneMapPass();
    
    // 6. Forward Transparent (覆盖)
    Customized_Render(rinfo);
    Flush(rinfo);
} else {
    // Original forward path
    ...
}
```

---

---

## Shader 文件独立策略 — 三阶段过渡方案

> 参考 RA3 (RNA引擎) 的独立 `.fx` shader 文件体系，结合 SAGE 引擎实际约束

### 现状

所有 shader 以 C++ 字符串字面量硬编码在：
- `W3DShaderManager.cpp` — G-Buffer VS/PS, PBR unit shaders
- `W3DDeferredRenderer.cpp` — SunLight PS, PointLight PS

这导致每次改 shader 都需要 **全 C++ 编译**（10min+），无法用 RenderDoc 直接调试，模组也无法介入。

### 三阶段过渡

```
阶段 1 [现在] ─── 内联字符串保持
  理由: Shader 内容还在每周迭代 (G-Buffer 通道分配/八面体编码/PBR参数)
  风险: 提前文件化会增加调试复杂度 (引擎逻辑和shader同时变)
  
阶段 2 [Step 4 完成后] ─── 提取到独立 .fx 文件
  时机: G-Buffer 通道分配稳定、八面体编码验证通过
  改动: D3DXCompileShader → D3DXCompileShaderFromFile + 路径解析
  
阶段 3 [Step 8 之后] ─── 引入 permutation 系统 (按需)
  时机: 需要阴影/SSAO等组合变体时
  方式: -DUSE_SHADOW_MAP / -DUSE_SSAO 编译开关生成变体
```

### 具体文件规划

```
GeneralsMD/
  └── Data/
       └── Shaders/                    ← 独立 shader 目录
            ├── GBufferWrite.fx        ← 顶点 + 像素 shader (G-Buffer写入)
            │     变体: WITHOUT_NORMAL_MAP / WITH_PBR_TEXTURE
            ├── DeferredSun.fx         ← 太阳光 PBR 照明
            │     变体: USE_SHADOW_MAP / USE_SSAO / FULL_QUALITY
            ├── DeferredPoint.fx       ← 点光源 PBR 照明 (additive)
            │     变体: (无, 数据驱动)
            ├── ShadowMap.fx           ← 阴影深度写入
            ├── Tonemap.fx             ← HDR→LDR + Gamma
            ├── SSAO.fx                ← 环境遮挡计算
            ├── SSAOBlur.fx            ← 双边滤波模糊
            └── ForwardPBR.fx          ← 半透明正向 PBR
```

总计约 **8 个 `.fx` 文件**，变体数控制在 **20 个以内**。

### 加载方式

在 `W3DShaderManager::init()` 中增加路径解析：

```cpp
// Shader 文件搜索顺序:
//   1. Data/Shaders/xxx.fx (mod 覆盖)
//   2. GeneralsMD/Data/Shaders/xxx.fx (原始安装)
//   3. 回退到内联字符串 (防止文件缺失崩溃)
bool loadShaderFromFile(const char *filename, const char *entry,
                        const char *profile, ID3DXBuffer **compiled)
{
    // 通过 TheFileSystem / ArchiveFileSystem 搜索
    // D3DXCompileShaderFromFile 直接加载
    // 失败时返回 false，调用者用内联字符串回退
}
```

### 实施时机

| 阶段 | 时机 | 操作 | 风险 |
|------|------|------|------|
| 1. 内联 | **现在** (Step 1-8) | shader 内容在 C++ 中迭代，不做文件化 | 无 |
| 2. 提取 | **Step 4 验证通过后** | 将稳定的 shader 字符串写入 `.fx` 文件，C++ 改调 `CompileFromFile` | 低 — 行为不变 |
| 3. 变体 | **Step 8 完成后** | 按需引入 `#define` 组合，降低 if-branch 开销 | 中 — 需验证每个变体 |

### 不建议做的事情

- ❌ **不要现在做** — shader 还在剧烈迭代，文件化没好处
- ❌ **不要学 RA3 的 256 变体** — 那是多平台 AAA 需求，社区 fork 维护 20 个变体就够了
- ❌ **不要移除内联回退** — `.fx` 文件可能被误删或 mod 破坏，内联回退保证游戏始终可运行

---

## 实施路线图（按优先级排序）

| 优先级 | Step | 工作量 | 风险 | 验证 |
|--------|------|--------|------|------|
| **P0** | Step 1: WW3DFormat 扩展 | 3 files, ~50行 | 低 | 编译通过即可 |
| **P0** | Step 2: Full-screen quad 修复 | 1 file, ~20行 | 低 | 肉眼验证 quad 覆盖 |
| **P0** | Step 3: PS 3.0 升级 | 3 files, ~20行 | 中 | 编译 + 运行日志 |
| **P0** | Step 4: G-Buffer 通道重写 | 2 files, ~150行 | **高** | PIX 抓帧验证 |
| **P1** | Step 5: HDR + Tone Mapping | 2 files, ~200行 | 中 | 高光细节可见 |
| **P1** | Step 6: Sun Shadow Map | 2 files, ~300行 | **高** | 阴影可见 |
| **P2** | Step 7: SSAO | 2 files, ~200行 | 中 | 角落变暗 |
| **P1** | Step 8: 完整集成 + 清理 | 1 file, ~100行 | 中 | 全场景渲染正常 |
| **P3** | Step 9: Shader 提取到 .fx | 3 files + 8 fx, ~80行 | 低 | 行为不变，加载路径切换 |

---

## 关键设计决策记录

| 决策 | 选项 | 选择 | 理由 |
|------|------|------|------|
| Depth RT 格式 | R32F独立 / 16bit编码到现有RT | **16bit编码到RT2.rg** | 避免MRT格式冲突，无需额外pass |
| HDR 回退 | 无/自动回退 | **自动回退A8R8G8B8** | 兼容旧硬件 |
| 阴影类型 | PCF / VSM / 现有decal | **简易PCF 2x2** | 实现简单，效果可接受 |
| AO 必要性 | 必须/可选 | **可选**（默认关闭） | 玩家不一定会注意到缺失 |
| sRGB ↔ Linear | 在GBuffer转/在Lighting转 | **GBuffer写入时转线性** | PBR计算必须在线性空间 |
| Shader 管理方式 | 内联字符串 / 独立.fx文件 | **分三阶段: 内联→提取→变体** | 当前快速迭代，稳定后再文件化 |


## 实施安全指南

1. **每步之间必须验证**：编译通过 + 运行不崩溃 + 能看到渲染结果才继续下一步
2. **保留回退路径**：如果 PS 3.0 编译失败，或 float RT 创建失败，`m_available=false` 自动走原 forward 路径
3. **不要一次改太多文件**：按 Step 顺序，一次只改 1-3 个文件
4. **每个新 RT 的创建都要检查返回值**：`Create_Render_Target` 返回 NULL 时优雅降级
5. **设备重置 (device reset) 必须重建所有 RT**：已通过 `ReleaseResources()/ReAcquireResources()` 实现

---

# 附录A: 实施检查点计划

> 每步实施时的操作清单和验证关卡 (Go/No-Go)。该计划与 Step 1-9 一一对应，在每步开始前和实施中对照执行。
>
> **每步都必须嵌入调试辅助代码**。所有 `WWDEBUG_SAY` 输出在 `WWDEBUG` (Debug) 配置下始终可见。
> 运行时调试模式通过 `PBRDebugMode` INI 变量 (`Int m_pbrDebugMode`) 控制，取值：
>   - 0 = 正常渲染
>   - 1 = 显示 RT0 (albedo+metallic) 可视化
>   - 2 = 显示 RT1 (normal+roughness) 可视化
>   - 3 = 显示 RT2 (depth+emissive) 可视化
>   - 4 = 显示 shadow map / AO mask 等辅助 RT

---

## 全局准备工作 (任何Step开始前)

- [ ] `git status` 确认工作区干净
- [ ] 记录当前 `git rev-parse HEAD` 以便回退
- [ ] 确认 `PBRDebugMode` INI 变量已暴露（用于运行时切换调试可视化模式）
- [ ] 确保最近一次的完整编译产物可用（回退时恢复）
- [ ] 确认测试场景 / 地图可复现渲染结果（同一帧保存截图）
- [ ] **添加全局调试辅助基础设施：** 在 `W3DDeferredRenderer.h` 声明调试输出函数

```cpp
// --- 调试辅助基础设施 (添加到 W3DDeferredRenderer.h) ---
// 运行时通过 INI: PBRDebugMode = 1~4 切换到对应可视化模式
//
// 调试模式输出函数: 在 sunLightPass 结束时判断 g_pbrDebugMode，
// 如果非0，将对应的 G-Buffer 通道直接输出到 back buffer 替代光照结果
static void debugRenderGBufferChannel(int channelIndex);
// 日志打印 G-Buffer 中心 16 个像素的值（每帧调用一次）
static void debugLogGBufferCenterPixels();
```

---

## Step 1: WW3DFormat 枚举扩展

### 前置条件
- [ ] ww3dformat.h 已备份
- [ ] formconv.cpp/.h 已备份
- [ ] 理解 D3DFMT_R32F(114)、D3DFMT_G16R16F(113)、D3DFMT_A16B16G16R16F(115) 的 D3D9 枚举值

### 调试辅助代码（嵌入本步）
在 `W3DDeferredRenderer::init()` 中增加格式自检循环：

```cpp
// ----- 调试输出 START -----
#ifdef WWDEBUG
const WW3DFormat testFormats[] = {
    WW3D_FORMAT_R32F, WW3D_FORMAT_G16R16F, WW3D_FORMAT_A16B16G16R16F
};
const char *testNames[] = { "R32F", "G16R16F", "A16B16G16R16F" };
for (int i = 0; i < 3; i++) {
    // 检举正向转换
    D3DFORMAT d3dFmt = WW3DFormat_To_D3DFormat(testFormats[i]);
    WWDEBUG_SAY(("  Format[%s] → D3DFMT=0x%04x (%s)\n",
        testNames[i], (int)d3dFmt,
        d3dFmt != D3DFMT_UNKNOWN ? "OK" : "FAIL"));
    // 检举反向转换
    WW3DFormat backFmt = D3DFormat_To_WW3DFormat(d3dFmt);
    WWDEBUG_SAY(("  D3DFMT=0x%04x → WW3DFormat=%d (%s)\n",
        (int)d3dFmt, (int)backFmt,
        backFmt == testFormats[i] ? "ROUNDTRIP_OK" : "ROUNDTRIP_FAIL"));
    // 运行时创建测试（仅 A16B16G16R16F 用于 HDR，创建失败不阻止整体初始化）
    TextureClass *testRT = DX8Wrapper::Create_Render_Target(
        64, 64, testFormats[i], true);
    WWDEBUG_SAY(("  Create_Render_Target(%s,64,64) → %s\n",
        testNames[i], testRT ? "SUCCESS" : "NULL (expected on old HW)"));
    REF_PTR_RELEASE(testRT);
}
#endif
// ----- 调试输出 END -----
```

### 实施操作
```
文件 1: Libraries/Source/WWVegas/WW3D2/ww3dformat.h
  操作: 在 WW3D_FORMAT_DXT5 之后追加三个枚举值
  注意: 位置在 WW3D_FORMAT_COUNT 之前，保持计数自动递增

文件 2: Libraries/Source/WWVegas/WW3D2/formconv.cpp
  操作1: WW3DFormatToD3DFormatConversionArray 尾部追加三个 D3DFMT 映射
  操作2: Init_D3D_To_WW3_Conversion() 中追加反向映射
  操作3: 检查 HIGHEST_SUPPORTED_D3DFORMAT 行末注释；如果新格式的 D3DFMT 值 > X8L8V8U8(61)，
         需增大该常量（R32F=114 远超 61，必须增大）

文件 3: Libraries/Source/WWVegas/WW3D2/ww3dformat.h (Get_Bytes_Per_Pixel)
  操作: 在 switch-case 中为新格式添加 bpp 返回值: R32F=4, G16R16F=4, A16B16G16R16F=8

文件 4 (如存在): Libraries/Source/WWVegas/WW3D2/ww3dformat.cpp
  操作: Get_WW3D_Format_Name 中增加名称打印
```

### 检查点 — 调试输出辅助

| # | 检查项 | 调试辅助代码或数据输出 | 通过条件 |
|---|--------|----------------------|---------|
| 1.1 | 编译通过 | VS 编译输出窗口 | 0 error(s) |
| 1.2 | 正向转换正确 | `WWDEBUG_SAY("Format[R32F] → D3DFMT=0x%04x ...", ...)` 输出 | 0x00000072 (114) |
| 1.3 | 反向转换正确 | `WWDEBUG_SAY("... → WW3DFormat=%d ...", ...)` 输出 | R32F= WW3D_FORMAT_R32F 的值 |
| 1.4 | 运行时创建 float RT | `WWDEBUG_SAY("Create_Render_Target(A16B16G16R16F) → SUCCESS")` | 日志显示 SUCCESS 或 "NULL"（可接受） |
| 1.5 | HIGHEST_SUPPORTED 扩大 | 编译时无 `D3DFormatToWW3DFormatConversionArray` 越界 warning | 无 warning |
| 1.6 | BytesPerPixel 正确 | *(间接验证)* 后续 Step 5 创建 HDR RT 时尺寸计算准确 | HDR RT 创建无异常 |

### 回退方案
- 如果 1.4 返回 NULL（硬件不支持 float RT）：记录日志，`g_hdrAvailable = false`，所有代码继续运行
- 如果编译失败：还原 4 个文件的修改，回退到上一个 git commit

---

## Step 2: 修复 Full-Screen Quad (XYZRHW + Half-Pixel)

### 前置条件
- [ ] Step 1 检查点全部通过
- [ ] 理解 D3D9 的 half-pixel 偏移原理：texel center 在 (0.5, 0.5)，XYZRHW 顶点需偏移 -0.5
- [ ] 理解当前的 bug：`createFullScreenQuad()` 用 -1~1 的 XYZRHW 坐标，实际应该用像素坐标

### 调试辅助代码（嵌入本步）
在 `createFullScreenQuad()` 中增加顶点校验日志，在 `sunLightPass` 中增加 UV 调试模式：

```cpp
// ----- createFullScreenQuad() 调试输出 START -----
WWDEBUG_SAY(("W3DDeferredRenderer: FS quad size %dx%d, halfPixelOffset=0.5\n",
    m_gbufferWidth, m_gbufferHeight));
WWDEBUG_SAY(("  vert[0] = (%.1f, %.1f)  uv=(%.3f,%.3f)\n",
    verts[0].x, verts[0].y, verts[0].u, verts[0].v));
WWDEBUG_SAY(("  vert[1] = (%.1f, %.1f)  uv=(%.3f,%.3f)\n",
    verts[1].x, verts[1].y, verts[1].u, verts[1].v));
WWDEBUG_SAY(("  vert[2] = (%.1f, %.1f)  uv=(%.3f,%.3f)\n",
    verts[2].x, verts[2].y, verts[2].u, verts[2].v));
WWDEBUG_SAY(("  vert[3] = (%.1f, %.1f)  uv=(%.3f,%.3f)\n",
    verts[3].x, verts[3].y, verts[3].u, verts[3].v));
// ----- 调试输出 END -----
```

在 `sunLightPass` 的开始处（或创建一份专门的 debug 版 PS），当 `g_pbrDebugMode == 10` 时：

```hlsl
// ----- Quad 覆盖调试 PS (用于替代主光源 PS) -----
// 正常采样 G-Buffer 逻辑不变，只是在最后叠加 UV 网格线
float4 debugQuadOverlay(PS_IN input) {
    // 棋盘格图案显示 UV 范围: 在 UV 的 0.45~0.55 区域画红色十字线
    float2 grid = abs(frac(input.tex0 * 32) - 0.5);
    float line = min(grid.x, grid.y) < 0.02 ? 1.0 : 0.0;
    float3 debug = float3(0, 0, 1);  // 蓝色背景
    if (input.tex0.x < 0.01 || input.tex0.x > 0.99 || 
        input.tex0.y < 0.01 || input.tex0.y > 0.99) {
        debug = float3(1, 0, 0);  // 边缘红色 → UV 未覆盖(0,1)范围
    } else {
        debug = float3(0, 0, 1) + line * float3(1, 1, 0);
    }
    return float4(debug, 1);
}
```

### 实施操作
```
文件: GameEngineDevice/Source/W3DDevice/GameClient/W3DDeferredRenderer.cpp
  函数: createFullScreenQuad()
  操作: 重写顶点数据:
    - 用 m_gbufferWidth / m_gbufferHeight 计算像素范围
    - 顶点位置: (-0.5, -0.5) ~ (w-0.5, h-0.5)
    - rhw=1.0, z=0.0
    - UV: (0,0) ~ (1,1)
    - 注意: 上下翻转？不要翻转，UV 的 V 从上到下递增。
      D3D9 的 back buffer (0,0) 在左上角，(w,h) 在右下角。
```

### 检查点 — 调试输出辅助

| # | 检查项 | 调试辅助代码或数据输出 | 通过条件 |
|---|--------|----------------------|---------|
| 2.1 | 编译通过 | VS 编译 | 无错误 |
| 2.2 | 顶点坐标正确 | `WWDEBUG_SAY("vert[0] = (0.0, 768.0) ...")` 日志显示 | 四个顶点覆盖 [0,w] × [0,h] 范围 |
| 2.3 | UV 全覆盖 | `g_pbrDebugMode=10` 时，画面无红色边缘 | 全屏蓝色+黄色网格，无红色边框 |
| 2.4 | UV 无扭曲 | 网格线在水平和垂直方向均匀等间距 | 32×32 网格线间距一致，不渐变 |
| 2.5 | 不同分辨率正确 | 分别在 1024×768 和 1920×1080 下重复 2.3/2.4 | 结果一致 |
| 2.6 | 回退 | `UseDeferredRendering=0` | 前进渲染正常，无残影 |

### 回退方案
- 恢复 `createFullScreenQuad()` 到旧代码
- 如果检测到 quad 宽度/高度为零，返回 false，`m_available=false`

---

## Step 3: 升级 Shader Model 到 ps_3_0

### 前置条件
- [ ] Step 2 检查点全部通过
- [ ] 确认目标硬件支持 PS 3.0 (GeForce 6+, Radeon X1K+, Intel HD Graphics 2000+)

### 调试辅助代码（嵌入本步）
```cpp
// ----- W3DDeferredRenderer::init() 调试输出 START -----
const D3DCAPS9 &caps = *DX8Wrapper::Get_Current_Caps()->Get_DX8_Caps_Ptr();  // 视具体接口调整
DWORD psVer = caps.PixelShaderVersion;
WWDEBUG_SAY(("W3DDeferredRenderer: PixelShaderVersion=0x%04x (%d.%d)\n",
    psVer, D3DSHADER_VERSION_MAJOR(psVer), D3DSHADER_VERSION_MINOR(psVer)));
if (psVer < D3DPS_VERSION(3,0)) {
    WWDEBUG_SAY(("W3DDeferredRenderer: PS 3.0 required but only %d.%d available. Disabled.\n",
        D3DSHADER_VERSION_MAJOR(psVer), D3DSHADER_VERSION_MINOR(psVer)));
}
// ----- 调试输出 END -----

// 在每个 PS compile 函数中，输出编译参数:
WWDEBUG_SAY(("W3DDeferredRenderer: compiling sunlight PS (ps_3_0)...\n"));
// 成功后输出:
WWDEBUG_SAY(("W3DDeferredRenderer: SunLight PS compiled for ps_3_0.\n"));
```

### 实施操作
```
文件 1: W3DShaderManager.cpp line ~3366 → "ps_2_0" → "ps_3_0"
文件 2: W3DDeferredRenderer.cpp compileSunLightShader() → "ps_2_0" → "ps_3_0"
文件 3: W3DDeferredRenderer.cpp compilePointLightShader() → "ps_2_0" → "ps_3_0"
文件 4: W3DDeferredRenderer.cpp init() 增加 PS 3.0 cap 检测
```

### 检查点 — 调试输出辅助

| # | 检查项 | 调试辅助代码或数据输出 | 通过条件 |
|---|--------|----------------------|---------|
| 3.1 | 编译通过 | VS 编译 | 无错误 |
| 3.2 | 能力检测输出 | `"PixelShaderVersion=0x0300 (3.0)"` 或 `"0x0200 (2.0)"` | 日志明确显示 detected version |
| 3.3 | G-Buffer PS 编译 | `"G-Buffer PS compiled for ps_3_0 (ptr=0x...)"` | 非 NULL 指针 |
| 3.4 | SunLight PS 编译 | `"SunLight PS compiled for ps_3_0."` | 日志输出 |
| 3.5 | PointLight PS 编译 | `"PointLight PS compiled."` | 日志输出 |
| 3.6 | PS 3.0 不可用时回退 | 旧硬件日志含 `"PS 3.0 required but only %d.%d available. Disabled."` | 走原 forward 路径无异常 |
| 3.7 | 渲染结果不变 | `g_pbrDebugMode=10` UV 网格图案与 Step 2 完全一致 | 视觉和日志均无差异 |

### 回退方案
- 如果任何 PS 编译失败：保持 `m_available=false`，将来支持的硬件自动启用
- 如有必要，全部三处恢复 "ps_2_0"

---

## Step 4: G-Buffer 通道分配 + 八面体法线编码

### 前置条件 ⚠️ 最高风险步骤
- [ ] Step 1-3 检查点全部通过
- [ ] 准备 RenderDoc 或 PIX
- [ ] 准备已知渲染结果的测试场景（同一帧的 forward 截图作为参考）

### 调试辅助代码（核心调试手段 — 嵌入本步）

**4a. 添加运行时 G-Buffer 可视化支持**

在 `W3DDeferredRenderer::sunLightPass` 开始时，插入调试模式分支：

```cpp
// ----- G-Buffer 调试可视化 -----
// 当 g_pbrDebugMode == 1~3 时，跳过正常光照，直接输出 G-Buffer 通道
int dbgMode = g_pbrDebugMode;  // 全局 INI 变量
if (dbgMode >= 1 && dbgMode <= 3) {
    // 使用一个专门输出 G-Buffer 通道的 PS
    dev->SetPixelShader(m_debugGBufferPS[dbgMode - 1]);
    // 常量: dbgMode=1 → 显示 RT0, =2 → RT1, =3 → RT2
    float dbgConst[4] = { (float)dbgMode, 0, 0, 0 };
    dev->SetPixelShaderConstantF(0, dbgConst, 1);
    // 绘制 quad 将 G-Buffer 指定通道输出到屏幕
    dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2);
    return;  // 跳过正常光照
}
```

对应的 3 个调试 PS (在 `compileSunLightShader` 旁边编译)：

```hlsl
// debugGBufferPS[0] — 显示 RT0 (Albedo+Metallic)
// 常亮 c0.x = 1
float4 main_rt0(PS_IN input) : COLOR {
    float4 rt0 = tex2D(gbuf0, input.tex0);
    // 左半: albedo, 右半: metallic (灰度)
    float2 uv = input.tex0;
    if (uv.x < 0.5) return float4(pow(rt0.rgb, 1/2.2), 1); // linear→sRGB 显示
    else return float4(rt0.aaa, 1);  // metallic 灰度图
}
// debugGBufferPS[1] — 显示 RT1 (OctaNormal+Roughness)
// 左半: 法线解码显示, 右半: roughness 灰度
// debugGBufferPS[2] — 显示 RT2 (Depth+Emissive)
// 左半: R 通道深度, 右半: G 通道 depth²
```

**4b. 深度编码对称性自检函数（CPU 端）**

```cpp
// ----- 深度编码自检 (CPU 端验证 encode/decode 对称性) -----
#ifdef WWDEBUG
void W3DDeferredRenderer::debugValidateDepthEncoding()
{
    float maxErr = 0;
    for (int i = 0; i < 10000; i++) {
        float depth = (float)i / 9999.0f;  // 0~1 均匀采样
        // 编码 (与 shader 保持一致)
        float enc_r = depth;
        float enc_g = depth * depth;
        // 解码 (与 lighting PS 保持一致)
        float dec_r = enc_r;
        float dec_g = enc_g;
        float decoded = dec_r;  // 主深度来自 R 通道
        float err = fabsf(decoded - depth);
        if (err > maxErr) maxErr = err;
    }
    WWDEBUG_SAY(("G-Buffer depth encoding self-test: max_error=%.8f\n", maxErr));
    WWASSERT(maxErr < 0.0001f);  // 定点数编码误差理论上为 0
}
#endif
```

**4c. 法线编码对称性自检函数**

```cpp
// ----- 八面体编码自检 (CPU 端验证 encode/decode 对称性) -----
#ifdef WWDEBUG
void W3DDeferredRenderer::debugValidateOctEncoding()
{
    float maxAngleErr = 0;
    int sampleCount = 10000;
    for (int i = 0; i < sampleCount; i++) {
        // 生成随机单位向量
        float theta = (float)rand() / RAND_MAX * 3.14159f * 2;
        float phi = acosf(2.0f * (float)rand() / RAND_MAX - 1.0f);
        Vector3 n(sinf(phi)*cosf(theta), sinf(phi)*sinf(theta), cosf(phi));
        // 编码 (与 shader 中 octEncode 一致)
        float nx = fabsf(n.X) + fabsf(n.Y) + fabsf(n.Z);  // L1 normalize
        float2 p = { n.X / nx, n.Y / nx };
        if (n.Z < 0) {
            float2 p_abs = { fabsf(p.x), fabsf(p.y) };
            p.x = (1.0f - p_abs.y) * (p.x >= 0 ? 1.0f : -1.0f);
            p.y = (1.0f - p_abs.x) * (p.y >= 0 ? 1.0f : -1.0f);
        }
        float2 enc = { p.x * 0.5f + 0.5f, p.y * 0.5f + 0.5f };
        // 解码 (与 shader 中 octDecode 一致)
        float2 dd = enc * 2.0f - 1.0f;
        Vector3 dec(dd.x, dd.y, 1.0f - fabsf(dd.x) - fabsf(dd.y));
        if (dec.Z < 0) {
            float t = -dec.Z;
            dec.X += (dec.X >= 0 ? -t : t);
            dec.Y += (dec.Y >= 0 ? -t : t);
        }
        dec.Normalize();
        // 计算角度误差
        float dotProd = n.X*dec.X + n.Y*dec.Y + n.Z*dec.Z;
        float angleErr = acosf(fminf(1.0f, fmaxf(-1.0f, dotProd)));
        if (angleErr > maxAngleErr) maxAngleErr = angleErr;
    }
    WWDEBUG_SAY(("Octahedral encoding self-test: max_angle_error=%.6f rad (%.4f°)\n",
        maxAngleErr, maxAngleErr * 180.0f / 3.14159f));
    // 8-bit 编码的理论最大误差约 0.5°，通过了即 OK
}
#endif
```

**4d. 像素级日志输出**

在 `endGBufferPass()` 中，可选 dump 中心像素值：

```cpp
// ----- G-Buffer 中心像素日志 -----
#ifdef WWDEBUG
if (g_pbrDebugMode > 0 && m_gbufferRT[0]) {
    IDirect3DSurface8 *surf0 = m_gbufferRT[0]->Get_D3D_Surface_Level();
    if (surf0) {
        D3DLOCKED_RECT locked;
        RECT centerRect = { m_gbufferWidth/2-1, m_gbufferHeight/2-1,
                            m_gbufferWidth/2+1, m_gbufferHeight/2+1 };
        if (SUCCEEDED(surf0->LockRect(&locked, &centerRect, D3DLOCK_READONLY))) {
            // 读取中心 2×2 像素
            for (int py = 0; py < 2; py++) {
                for (int px = 0; px < 2; px++) {
                    DWORD pixel = *(DWORD*)((BYTE*)locked.pBits + py*locked.Pitch + px*4);
                    BYTE r = pixel & 0xFF, g = (pixel>>8)&0xFF, b = (pixel>>16)&0xFF, a = (pixel>>24)&0xFF;
                    WWDEBUG_SAY(("  GBUF[%d,%d] RT0 = R=%d G=%d B=%d A=%d\n",
                        m_gbufferWidth/2+px, m_gbufferHeight/2+py, r, g, b, a));
                }
            }
            surf0->UnlockRect();
        }
        surf0->Release();
    }
}
#endif
```

### 实施操作
```
1. 重写 gbuffer_ps 字符串 (octEncode + sRGB→linear + 16bit depth packing)
2. 在 W3DDeferredRenderer 中增加:
   - IDirect3DPixelShader9 *m_debugGBufferPS[3];  // 3 个调试 PS
   - debugValidateDepthEncoding() 和 debugValidateOctEncoding()
3. 在 sunLightPass 开头插入 g_pbrDebugMode 判断分支
4. 在 compileSunLightShader() 旁编译 3 个调试 PS
5. 更新 sunLightPS: octDecode + depth decode + 新通道映射
6. 更新 pointLightPS: 同上
```

### 检查点 — 调试输出辅助

| # | 检查项 | 调试辅助代码或数据输出 | 通过条件 |
|---|--------|----------------------|---------|
| 4.1 | 编译通过 | VS 编译 | 无错误 |
| 4.2 | 八面体编码自检 | `debugValidateOctEncoding()` 输出日志 | `max_angle_error < 0.01 rad (0.57°)` |
| 4.3 | 深度编码自检 | `debugValidateDepthEncoding()` 输出日志 | `max_error < 0.0001` |
| 4.4 | RT0.albedo 线性化 | `g_pbrDebugMode=1` 左半显示 albedo，与 forward 纹理对比 | 颜色略浅（线性→sRGB 再显示会恢复原色） |
| 4.5 | RT0.metallic | `g_pbrDebugMode=1` 右半为灰度 | 非 PBR 物体为纯黑(0)，金属物体为灰色(>0) |
| 4.6 | RT1.octNormal | `g_pbrDebugMode=2` 左半显示解码后法线 | 彩色法线图，颜色过渡平滑 |
| 4.7 | RT1.roughness | `g_pbrDebugMode=2` 右半灰度 | 非 PBR 物体为浅灰(0.8)，金属物体值不同 |
| 4.8 | RT2.depth | `g_pbrDebugMode=3` 左半深度 | 近白远黑，连续渐变，无 8-bit banding |
| 4.9 | RT2.depth² | `g_pbrDebugMode=3` 右半 depth² | 比左半更暗（因 depth² < depth），渐变平滑 |
| 4.10 | 中心像素日志 | `"GBUF[640,360] RT0 = R=128 G=130 B=100 A=0"` | A 通道(metallic) 在非 PBR 物体上为 0 |
| 4.11 | 场景完整性 | `g_pbrDebugMode=1` 时所有不透明物体可见 | G-Buffer 无空洞 |
| 4.12 | 光照输出正常 | `g_pbrDebugMode=0` 正常渲染 | 场景被正确照亮（颜色接近 forward 但可能更好） |

### 回退方案
- 如果编码自检(4.2/4.3)失败：检查 octEncode/octDecode 的 HLSL 实现与 CPU 版本一致性
- 如果 G-Buffer 通道错位(4.4-4.9 异常)：检查 shader 中 color0/color1/color2 的写入顺序
- 如果光照完全异常：恢复 shader 到旧版本字符串，`git diff` 保留修改

---

## Step 5: HDR 渲染目标 + Tone Mapping

### 前置条件
- [ ] Step 4 检查点全部通过
- [ ] Step 1 的自检日志已确认 `A16B16G16R16F` 可用

### 调试辅助代码（嵌入本步）

```cpp
// ----- 创建 HDR RT 时日志输出 -----
WWDEBUG_SAY(("W3DDeferredRenderer: creating HDR RT %dx%d format=A16B16G16R16F\n",
    m_gbufferWidth, m_gbufferHeight));
if (m_hdrRT) {
    // 用 clear color 验证: 用亮黄色(2,2,0) 清空 HDR RT
    // 正常渲染的 clear 值为 0，但测试时用 >1 验证浮点格式
    DX8Wrapper::Clear(true, false, Vector3(2, 2, 0), 0, 1.0f, 0);
    WWDEBUG_SAY(("W3DDeferredRenderer: HDR RT created, cleared to (2,2,0)\n"));
}

// ----- Tone mapping 前后 dump 中心像素值 -----
// 在 toneMapPass 前后的 HDR RT 中心读像素
// 若 HDR 值 > 1.0 且 tone map 后 < 1.0，证明 HDR 有效
#ifdef WWDEBUG
float debugReadHDRAverage()
{
    IDirect3DSurface8 *surf = m_hdrRT->Get_D3D_Surface_Level();
    float avg = 0;
    if (surf) {
        D3DLOCKED_RECT locked;
        RECT r = {0, 0, min(4, m_gbufferWidth), min(4, m_gbufferHeight)};
        if (SUCCEEDED(surf->LockRect(&locked, &r, D3DLOCK_READONLY))) {
            float sum = 0;
            for (int y = 0; y < 4; y++) {
                for (int x = 0; x < 4; x++) {
                    // A16B16G16R16F: 每个像素 8 bytes, 两 half-float
                    WORD *pixel = (WORD*)((BYTE*)locked.pBits + y*locked.Pitch + x*8);
                    // 将 half-float 转 float (简易转换: 取 R 通道)
                    WORD h = pixel[2];  // 在 ARGB 中 G 通道偏移取决于 format
                    // 简易 half→float (省略完整转换，仅验证)
                    sum += (float)h / 1024.0f;  // 近似值
                }
            }
            avg = sum / 16.0f;
            surf->UnlockRect();
        }
        surf->Release();
    }
    return avg;
}
#endif
```

### 实施操作
```
1. W3DDeferredRenderer.h: 增加 m_hdrRT, m_hdrAvailable, begin/endHDRPass, toneMapPass
2. W3DDeferredRenderer.cpp: 实现 createHDRResources 等函数
3. 在 sunLightPass 中: 渲染输出指向 HDR RT (而不是 back buffer)
4. toneMapPass: 全屏 quad, Reinhard: color / (color + 1), 然后 pow(x, 1/2.2)
5. W3DScene.cpp: 调整 pass 顺序
```

### 检查点 — 调试输出辅助

| # | 检查项 | 调试辅助代码或数据输出 | 通过条件 |
|---|--------|----------------------|---------|
| 5.1 | 编译通过 | VS 编译 | 无错误 |
| 5.2 | HDR RT 创建日志 | `"creating HDR RT ... format=A16B16G16R16F"` | 日志无错误 |
| 5.3 | HDR 值 > 1.0 存在 | tone map 前 dump 中心 4×4 像素 | 太阳直射面像素值 > 1.0 |
| 5.4 | Tone map 后值 ≤ 1.0 | tone map 后 back buffer 像素值 | 所有像素 ≤ 1.0 |
| 5.5 | HDR vs 非 HDR 对比 | 截取同一帧带/不带 HDR 的画面 | HDR 版本高光有细节，非 HDR 版高光纯白 |
| 5.6 | 回退路径 | INI `UseHDR=0` 或硬件不支持 | 走 A8R8G8B8 路径，无浮点 RT |

### 回退方案
- HDR RT 创建失败：`m_hdrAvailable=false`，lighting 直接渲染到 back buffer（当前行为）
- Tone mapping 花屏：跳过 tone map pass，直接拷贝 HDR RT（16F→8R8G8B8 自动截断）

---

## Step 6: Sun Shadow Map (2x2 PCF)

### 前置条件
- [ ] Step 4 检查点全部通过
- [ ] 理解场景中太阳方向光与阴影相机的关系

### 调试辅助代码（嵌入本步）

```cpp
// ----- Shadow map 调试输出 -----
// 1. 创建 shadow map 时:
WWDEBUG_SAY(("W3DDeferredRenderer: shadow map %dx%d, format=A8R8G8B8\n",
    SHADOW_MAP_SIZE, SHADOW_MAP_SIZE));

// 2. Shadow map pass 后, 将 shadow map 内容输出到日志:
// (简化: 读取中心像素值确认深度写入)
#ifdef WWDEBUG
void debugDumpShadowMapCenter()
{
    IDirect3DSurface8 *surf = m_shadowDepthRT->Get_D3D_Surface_Level();
    if (surf) {
        D3DLOCKED_RECT locked;
        RECT r = { SHADOW_MAP_SIZE/2-1, SHADOW_MAP_SIZE/2-1,
                   SHADOW_MAP_SIZE/2+1, SHADOW_MAP_SIZE/2+1 };
        if (SUCCEEDED(surf->LockRect(&locked, &r, D3DLOCK_READONLY))) {
            for (int py = 0; py < 2; py++) {
                for (int px = 0; px < 2; px++) {
                    DWORD pixel = *(DWORD*)((BYTE*)locked.pBits + py*locked.Pitch + px*4);
                    WWDEBUG_SAY(("  SHADOW[%d,%d] = 0x%08x (R=%d)\n",
                        px, py, pixel, pixel & 0xFF));
                }
            }
            surf->UnlockRect();
        }
        surf->Release();
    }
}
#endif

// 3. 在 sunLightPS 中增加 shadow 可视化模式:
// 当 g_pbrDebugMode == 4 时, 直接输出 shadow 项值 (纯黑白)
// float shadow = pcfShadow(...);
// return float4(shadow, shadow, shadow, 1);
```

### 实施操作
```
1. W3DDeferredRenderer: 增加 m_shadowDepthRT / m_shadowMapAvailable / begin/endShadowMapPass
2. 在 G-Buffer pass 之前: 用 shadow camera 渲染场景深度到 shadow map
3. sunLightPS: 增加 shadow UV 计算 + PCF 2×2 采样
4. 增加 g_pbrDebugMode=4 的 shadow 可视化模式
```

### 检查点 — 调试输出辅助

| # | 检查项 | 调试辅助代码或数据输出 | 通过条件 |
|---|--------|----------------------|---------|
| 6.1 | 编译通过 | VS 编译 | 无错误 |
| 6.2 | Shadow map 深度写入 | `debugDumpShadowMapCenter()` 输出像素值 | 场景中心物体使深度值 < 255 (非纯白) |
| 6.3 | Shadow 可视化 | `g_pbrDebugMode=4` 时黑白图像 | 物体背面为黑(阴影)，向阳面为白(光照) |
| 6.4 | 阴影方向正确 | 观察方向和太阳方向 | 阴影与 sunDir 方向一致 |
| 6.5 | PCF 软边 | 放大观察阴影边缘 | 2 像素过渡，非硬边 |
| 6.6 | Bias 正确无条纹 | 仔细观察阴影面 | 无 stripe/acne 伪影 |
| 6.7 | 回退 | INI `UseShadowMap=0` | 无阴影，光照正常 |

### 回退方案
- Shadow map 创建失败：`m_shadowMapAvailable=false`
- PCF 算法导致渲染错误：将 PCF 4 次采样改为单个点采样（退化为硬阴影）
- 完全无法工作：在 sunLightPS 中恒置 `shadow = 1.0`

---

## Step 7: SSAO (可选)

### 前置条件
- [ ] Step 4 检查点全部通过
- [ ] 默认 INI `UseSSAO=0`

### 调试辅助代码（嵌入本步）

```cpp
// ----- SSAO 调试输出 -----
// g_pbrDebugMode = 5 时输出 AO mask (纯灰度)
// g_pbrDebugMode = 6 时输出 AO + 光照叠加效果
```

### 检查点 — 调试输出辅助

| # | 检查项 | 调试辅助代码或数据输出 | 通过条件 |
|---|--------|----------------------|---------|
| 7.1 | 编译通过 | VS 编译 | 无错误 |
| 7.2 | AO mask 可视化 | `g_pbrDebugMode=5` 显示 AO 灰度图 | 角落比开放面暗 |
| 7.3 | 双边滤波有效 | AO 图无块状噪声 | 边缘清晰，内部平滑 |
| 7.4 | 开关正常 | INI `UseSSAO=0/1` 切换 | 效果消失/出现 |
| 7.5 | 性能 | 日志输出 AO pass 耗时 | `< 3ms (WWDEBUG_SAY 计时)` |

### 回退方案
- 如果 SSAO shader 编译失败：`m_ssaoAvailable=false`
- 性能不达标：降低采样数（`AOSampleCount=8` 或 4）

---

## Step 8: 完整管线集成

### 前置条件
- [ ] Step 5-7 已完成（如适用）
- [ ] 理解当前 W3DScene::Render() 完整流程

### 调试辅助代码（嵌入本步）

```cpp
// ----- 管线流程日志输出 -----
// 在每个 pass 开始/结束时输出:
#define PASS_LOG(passName) \
    WWDEBUG_SAY(("PIPELINE: === %s ===\n", passName))

PASS_LOG("Shadow Map Pass");
beginShadowMapPass();
// ... render ...
endShadowMapPass();

PASS_LOG("G-Buffer Pass");
beginGBufferPass();
// ... render ...
endGBufferPass();

PASS_LOG("Deferred Lighting Pass");
sunLightPass(...);
renderDynamicLights(...);

PASS_LOG("Tone Mapping Pass");
toneMapPass();

PASS_LOG("Forward Transparent Pass");
// ... render ...

// ----- Pass 耗时统计 (简易帧级 Timer) -----
// 用 QueryPerformanceCounter 测量每个 pass 耗时
// 输出到日志:
// PIPELINE: G-Buffer Pass took 3.24 ms
// PIPELINE: Lighting Pass took 0.87 ms
```

### 实施操作
```
1. 重构 W3DScene::Render() 中的 pass 顺序
2. 每个 pass 前后加入 PASS_LOG + 计时
3. 每个 pass 管理 depth-stencil 的 Clear
```

### 检查点 — 调试输出辅助

| # | 检查项 | 调试辅助代码或数据输出 | 通过条件 |
|---|--------|----------------------|---------|
| 8.1 | 编译通过 | VS 编译 | 无错误 |
| 8.2 | Pass 顺序正确 | `"PIPELINE: === Shadow Map Pass ===" → "=== G-Buffer Pass ===" → "=== Deferred Lighting Pass ===" → "=== Tone Mapping Pass ===" → "=== Forward Transparent Pass ==="` | 顺序准确，无重复/缺失 |
| 8.3 | 各 Pass 耗时 | `"PIPELINE: X Pass took Y.YY ms"` | Y 值合理（GBuffer < 10ms, Lighting < 3ms, Shadow < 3ms） |
| 8.4 | 禁用 shadow 后跳过 | `UseShadowMap=0` → 日志无 `"=== Shadow Map Pass ==="` | 正确跳过 |
| 8.5 | 禁用 HDR 后跳过 | `UseHDR=0` → 日志无 `"=== Tone Mapping Pass ==="` | 正确跳过 |
| 8.6 | 半透明覆盖 | 玻璃/水/粒子在最上层 | 粒子在半透明层，不在 G-Buffer 中 |
| 8.7 | 全部关闭 = forward | 所有 deferred 功能关闭 + `m_available=false` | 原 forward 渲染完整 |

### 回退方案
- 如果管线顺序导致画面异常：临时跳过阴影/AO pass，只做 minimal (GBuffer + SunLight + Forward)
- 如果耗时过高：WS 日志确认哪个 pass 慢，针对性优化

---

## Step 9: Shader 提取到 .fx 文件

### 前置条件
- [ ] Step 4-8 的 shader 内容已经稳定
- [ ] 已建立 `Data/Shaders/` 目录

### 调试辅助代码（嵌入本步）

```cpp
// ----- Shader 加载路径日志 -----
bool W3DShaderManager::loadShaderFromFile(const char *relPath, ...)
{
    WWDEBUG_SAY(("SHADER_LOAD: attempting %s ...\n", relPath));

    // 搜索顺序 1: Data/Mods/<active>/Shaders/
    // 搜索顺序 2: Data/Shaders/
    // 搜索顺序 3: GeneralsMD/Data/Shaders/
    char fullPath[512];
    // ... search logic ...
    
    HRESULT hr = D3DXCompileShaderFromFile(fullPath, defines, NULL,
                                            entry, profile, 0,
                                            compiled, &errors, NULL);
    if (SUCCEEDED(hr)) {
        WWDEBUG_SAY(("SHADER_LOAD: SUCCESS from %s\n", fullPath));
        return true;
    }
    // Fallback
    WWDEBUG_SAY(("SHADER_LOAD: FAILED %s, using internal fallback.\n", fullPath));
    if (errors) {
        WWDEBUG_SAY(("  compile error: %s\n", (const char*)errors->GetBufferPointer()));
        errors->Release();
    }
    return compileFromInternalString(...);
}
```

### 实施操作
```
1. 将稳定的 shader 字符串写入独立的 .fx 文件
2. 增加 loadShaderFromFile 函数
3. 替换原有 D3DXCompileShader 调用
```

### 检查点 — 调试输出辅助

| # | 检查项 | 调试辅助代码或数据输出 | 通过条件 |
|---|--------|----------------------|---------|
| 9.1 | 文件加载路径 | `"SHADER_LOAD: SUCCESS from Data/Shaders/GBufferWrite.fx"` | 日志显示完整的成功加载路径 |
| 9.2 | Fallback | 删除 .fx 文件后，日志显示 `"FAILED %s, using internal fallback."` | 画面仍然正常 |
| 9.3 | 错误信息完整 | 编译错误时输出 `"compile error: ..."` | 错误信息可读，可定位行号 |
| 9.4 | 渲染一致性 | 所有 5 个 INI 开关组合下截图对比 | 文件加载 vs 内联字符串，每组合输出一致 |
| 9.5 | 路径无关性 | 从不同工作目录启动游戏 | shader 始终可找到 |

### 回退方案
- 如果 .fx 文件加载失败，内联字符串 fallback 保证游戏正常运行
- INI 开关 `UseExternalShaders=0` 强制使用内联字符串，跳过文件加载

---

# 附录B: 已实施完成检查计划

> 当某 Step 声称"已完成"后，用此清单逐项验收。该计划独立于实施过程，由 review 视角执行。
> 每项记录: ✅ 通过 / ❌ 失败 / ⚠️ 有瑕疵(记录详情)
>
> **验收依据来源**：附录A 中嵌入的 `WWDEBUG_SAY` 日志输出 + `PBRDebugMode` 运行时可视化 + CPU 端自检函数 + PIX/RenderDoc 截图交叉验证。
> 拒绝纯目测验收 — 每项必须至少对应一条可复现的日志输出或自动化自检结果。

---

## 全局验收 (所有 Step 完成后)

| # | 验收项 | 通过标准 | 日志/工具依据 | 结果 |
|---|--------|---------|-------------|------|
| G.1 | 编译 | Debug + Release 双配置编译通过 | VS 编译输出 | __ |
| G.2 | 调试输出覆盖 | 所有关键步骤有 WWDEBUG_SAY 日志 | `grep "SHADER_LOAD\|PIPELINE:\|W3DDeferredRenderer:"` 日志文件 | __ |
| G.3 | 资源泄漏 | 启动→渲染→退出 后 D3D 资源全部释放 | 无 D3D Debug runtime 警告输出 | __ |
| G.4 | 分辨率切换 | 切换窗口/全屏，RT 重建 | `ReAcquireResources()` 日志显示重建成功 | __ |
| G.5 | 回退路径 | UseDeferredRendering=0 与之前渲染一致 | 画面对比 + 日志无 deferred 相关输出 | __ |
| G.6 | 设备丢失 | Alt+Enter 切换 5 次 | 日志恢复序列完整 | __ |

---

## Step 1 验收 — WW3DFormat 枚举扩展

| # | 验收项 | 通过标准 | 日志/工具依据 | 结果 |
|---|--------|---------|-------------|------|
| 1.1 | 枚举完整性 | `WW3D_FORMAT_R32F` / `G16R16F` / `A16B16G16R16F` 定义 | 编译通过 | __ |
| 1.2 | 正向转换 | `WW3DFormat_To_D3DFormat(R32F) == D3DFMT_R32F` | `"Format[R32F] → D3DFMT=0x00000072 (OK)"` | __ |
| 1.3 | 反向转换 | `D3DFormat_To_WW3DFormat(D3DFMT_R32F) == WW3D_FORMAT_R32F` | `"D3DFMT=0x00000072 → WW3DFormat=%d (ROUNDTRIP_OK)"` | __ |
| 1.4 | BytesPerPixel | 调用 `Get_Bytes_Per_Pixel(WW3D_FORMAT_R32F)` 返回 4 | (间接验证) RT 创建尺寸正确 | __ |
| 1.5 | HIGHEST_SUPPORTED | 数组容量覆盖 D3DFMT_R32F(114) | 编译无越界 warning | __ |
| 1.6 | 运行时创建 | float RT 创建测试 | `"Create_Render_Target(A16B16G16R16F,64,64) → SUCCESS"` | __ |

---

## Step 2 验收 — Full-Screen Quad 修复

| # | 验收项 | 通过标准 | 日志/工具依据 | 结果 |
|---|--------|---------|-------------|------|
| 2.1 | 顶点坐标正确 | 四个顶点覆盖 [0,w] × [0,h] | `"vert[0] = (0.0, 768.0) ..."` 等日志 | __ |
| 2.2 | UV 全覆盖 | 从 (0,0) 到 (1,1) 无红色边缘 | `g_pbrDebugMode=10` 棋盘格无红色边框 | __ |
| 2.3 | UV 无扭曲 | UV 增量稳定 | `g_pbrDebugMode=10` 网格均匀 | __ |
| 2.4 | 多分辨率 | 1024×768 和 1920×1080 均正确 | 日志输出 quad 尺寸与分辨率匹配 | __ |
| 2.5 | 回退路径 | UseDeferredRendering=0 正常 | 画面无残影 | __ |

---

## Step 3 验收 — PS 3.0 升级

| # | 验收项 | 通过标准 | 日志/工具依据 | 结果 |
|---|--------|---------|-------------|------|
| 3.1 | 能力检测 | PS version 正确记录 | `"PixelShaderVersion=0x0300 (3.0)"` | __ |
| 3.2 | G-Buffer PS | ps_3_0 编译成功 | `"G-Buffer PS compiled for ps_3_0 (ptr=0x...)"` | __ |
| 3.3 | SunLight PS | ps_3_0 编译成功 | `"SunLight PS compiled for ps_3_0."` | __ |
| 3.4 | PointLight PS | ps_3_0 编译成功 | `"PointLight PS compiled."` | __ |
| 3.5 | 旧硬件回退 | PS 2.0 硬件走 forward 路径 | `"PS 3.0 required but only %d.%d available. Disabled."` | __ |

---

## Step 4 验收 — G-Buffer 通道分配 + 八面体编码

| # | 验收项 | 通过标准 | 日志/工具依据 | 结果 |
|---|--------|---------|-------------|------|
| 4.1 | 八面体编码自检 | CPU 端对称性验证 | `"Octahedral encoding self-test: max_angle_error=0.000xxx rad (0.xxxx°)"` | __ |
| 4.2 | 深度编码自检 | CPU 端对称性验证 | `"G-Buffer depth encoding self-test: max_error=0.000000xx"` | __ |
| 4.3 | RT0.RGB 线性化 | Albedo sRGB→linear 正确 | `g_pbrDebugMode=1` 左半颜色略浅（线性存储，显示时 pow 回来） | __ |
| 4.4 | RT0.A metallc | 非零值存在 | `g_pbrDebugMode=1` 右半非全黑 | __ |
| 4.5 | RT1.RG 八面体法线 | 法线解码后平滑 | `g_pbrDebugMode=2` 左半彩色法线，无块状 | __ |
| 4.6 | RT1.B roughness | 值在 [0,1] | `g_pbrDebugMode=2` 右半灰度正常 | __ |
| 4.7 | RT2.R 深度 | 连续变化 | `g_pbrDebugMode=3` 左半深色近→浅色远 | __ |
| 4.8 | RT2.G depth² | 灰度比 R 通道更暗 | `g_pbrDebugMode=3` 右半比左半暗 | __ |
| 4.9 | 中心像素日志 | 具体通道值可查阅 | `"GBUF[640,360] RT0 = R=128 G=130 B=100 A=0"` | __ |
| 4.10 | 场景完整 | 无不透明物体缺失 | `g_pbrDebugMode=1` 所有物体可见 | __ |
| 4.11 | 光照输出 | 颜色正常 | `g_pbrDebugMode=0` 场景亮度与 forward 接近 | __ |

---

## Step 5 验收 — HDR + Tone Mapping

| # | 验收项 | 通过标准 | 日志/工具依据 | 结果 |
|---|--------|---------|-------------|------|
| 5.1 | HDR RT 创建 | `m_hdrRT != NULL` | `"creating HDR RT ... format=A16B16G16R16F"` | __ |
| 5.2 | 像素值 > 1.0 | 高光区域 HDR | Tone map 前 `debugReadHDRAverage()` 返回值 > 1.0 | __ |
| 5.3 | Tone map 后 ≤ 1.0 | Back buffer 合法 | Tone map 后 `debugReadHDRAverage()` 返回值 ≤ 1.0 | __ |
| 5.4 | HDR fallback | 不支持 float 时回退 | 日志无 HDR RT 创建记录，走 A8R8G8B8 | __ |
| 5.5 | 高光细节 | 非纯白 | HDR 版高光可见细节，非 HDR 版纯白截断 | __ |

---

## Step 6 验收 — Sun Shadow Map

| # | 验收项 | 通过标准 | 日志/工具依据 | 结果 |
|---|--------|---------|-------------|------|
| 6.1 | Shadow map 创建 | 2048×2048 | `"shadow map 2048x2048, format=A8R8G8B8"` | __ |
| 6.2 | 深度写入 | 非全白 | `"SHADOW[0,0] = 0x........ (R=%d)"` 非全 255 | __ |
| 6.3 | 阴影可视化 | 黑白映射正确 | `g_pbrDebugMode=4` 背光面黑，向阳面白 | __ |
| 6.4 | PCF 软边 | 2 像素过渡 | 边缘放大观察 | __ |
| 6.5 | 无 acne | 无条纹 | 目测 + `g_pbrDebugMode=4` 确认 | __ |
| 6.6 | 回退 | UseShadowMap=0 正常 | 日志无 shadow pass 输出 | __ |

---

## Step 7 验收 — SSAO

| # | 验收项 | 通过标准 | 日志/工具依据 | 结果 |
|---|--------|---------|-------------|------|
| 7.1 | AO mask 创建 | AO RT 灰度图 | `g_pbrDebugMode=5` 显示 AO mask | __ |
| 7.2 | 角落变暗 | 接触面比开放面暗 | `g_pbrDebugMode=5` 对比 | __ |
| 7.3 | 无过暗 | 水平面正常 | AO 图上平面为白(1.0) | __ |
| 7.4 | 双边滤波有效 | 边缘清晰内部平滑 | `g_pbrDebugMode=5` 对比滤波前后 | __ |
| 7.5 | 开关正常 | UseSSAO=0/1 | 效果消失/出现 | __ |

---

## Step 8 验收 — 完整管线集成

| # | 验收项 | 通过标准 | 日志/工具依据 | 结果 |
|---|--------|---------|-------------|------|
| 8.1 | Pass 顺序 | 5 个 pass 按序执行 | `"PIPELINE: === X Pass ==="` 日志 5 个完整 | __ |
| 8.2 | 各 Pass 耗时 | 时间合理 | `"PIPELINE: X Pass took Y.YY ms"` | __ |
| 8.3 | 开关组合 | 8 种 INI 组合 | 每个开关切换对应 pass 出现/消失 | __ |
| 8.4 | 半透明覆盖 | 水、玻璃、粒子正确 | Forward pass 在最上层，G-Buffer 无半透明 | __ |
| 8.5 | 稳定性 | 30 分钟游戏 | 无崩溃 | __ |

**验收方法**: 自动化脚本切换 INI 组合 + 检查日志 PIPELINE 行序 + 截图对比。

---

## Step 9 验收 — Shader 提取到 .fx

| # | 验收项 | 通过标准 | 日志/工具依据 | 结果 |
|---|--------|---------|-------------|------|
| 9.1 | 文件加载 | 启动时从 .fx 加载 | `"SHADER_LOAD: SUCCESS from Data/Shaders/GBufferWrite.fx"` | __ |
| 9.2 | 内联回退 | 删除 .fx 仍运行 | `"SHADER_LOAD: FAILED %s, using internal fallback."` | __ |
| 9.3 | 行为一致 | 文件 vs 内联渲染一致 | 两种模式截图像素对比 < 0.5% 差异 | __ |
| 9.4 | 模组优先 | Mod 路径覆盖 | `"SHADER_LOAD: SUCCESS from Data/Mods/xxx/Shaders/..."` | __ |
| 9.5 | 路径无关 | 任意工作目录 | 日志始终输出文件加载路径 | __ |

**验收方法**: 删除 .fx 文件后游戏仍运行，截图对比。
