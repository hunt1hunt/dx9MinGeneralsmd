# PBR 地形高光升级 — 项目总结

## 修改文件清单（共 6 个）

| # | 文件 | 修改类型 | 说明 |
|---|------|---------|------|
| 1 | `GameEngine/Include/Common/GlobalData.h` | +1 行 | 新增 `m_useDetailTerrainTex` 成员 |
| 2 | `GameEngine/Source/Common/GlobalData.cpp` | +2 行 | INI 表注册 + 构造函数初始化 |
| 3 | `W3DDevice/GameClient/HeightMap.h` | +1 行 | 新增 `m_detailTexture` 成员 |
| 4 | `W3DDevice/GameClient/W3DShaderManager.h` | +3 行 | 新增 3 个 PBR 变体枚举值 |
| 5 | `W3DDevice/GameClient/HeightMap.cpp` | 多处 | 渲染路径改造 |
| 6 | `W3DDevice/GameClient/W3DShaderManager.cpp` | 大量 | PBR shader 核心实现 |

---

## 详细代码变更

### 1. GlobalData.h — 第 114 行
```cpp
Bool m_useDetailTerrainTex;  ///< INI: UseDetailTerrainTex
```

### 2. GlobalData.cpp — 两处
**INI 表**（第 85 行）：
```cpp
{ "UseDetailTerrainTex", INI::parseBool, NULL, offsetof( GlobalData, m_useDetailTerrainTex ) },
```
**构造函数**（第 687 行）：
```cpp
m_useDetailTerrainTex = FALSE;
```
> 注意：`GlobalData.ini` 中写 `UseDetailTerrainTex = Yes` 或 `No`（不接受 `true/false`）

### 3. HeightMap.h — 第 106 行
```cpp
TextureClass *m_detailTexture;  ///<procedural detail texture for PBR terrain
```

### 4. W3DShaderManager.h — 第 71-73 行
```cpp
ST_TERRAIN_PBR_NOISE1,    //PBR terrain + cloud texture
ST_TERRAIN_PBR_NOISE2,    //PBR terrain + lightmap texture
ST_TERRAIN_PBR_NOISE12,   //PBR terrain + cloud + lightmap
```

### 5. HeightMap.cpp — 4 处修改

**a) freeMapResources**（第 164 行）：
```cpp
REF_PTR_RELEASE(m_detailTexture);
```

**b) 构造函数 init 列表**（第 1104 行）：
```cpp
m_detailTexture(NULL),
```

**c) 渲染路径 — PBR shader 选择**（重构，第 112-163 行）：
原来只判断 `ST_TERRAIN_PBR` 一个变体。改为 `pbrAvail` 统一判定后，按云/光照条件选择对应 PBR 变体：
```
pbrAvail ? ST_TERRAIN_PBR_NOISE12 : ST_TERRAIN_BASE_NOISE12  // 云+光照
pbrAvail ? ST_TERRAIN_PBR_NOISE2  : ST_TERRAIN_BASE_NOISE2   // 光照
pbrAvail ? ST_TERRAIN_PBR_NOISE1  : ST_TERRAIN_BASE_NOISE1   // 云
pbrAvail ? ST_TERRAIN_PBR         : ST_TERRAIN_BASE           // 纯地表
```
含 fallback：选中变体不可用时降级到 base 变体。

**d) PBR 常量传递**（第 2092 行）：
```cpp
// Before:
if (st == W3DShaderManager::ST_TERRAIN_PBR && TheGlobalData)
// After:
if (st >= W3DShaderManager::ST_TERRAIN_PBR && TheGlobalData)
```
让所有 PBR 变体（含 noise1/2/12）都能传递太阳方向/颜色常量。

**e) Detail texture 创建**（第 173-195 行，由 `UseDetailTerrainTex` 控制）：
```cpp
if (TheGlobalData->m_useDetailTerrainTex && !m_detailTexture) {
    m_detailTexture = NEW TextureClass(256, 256, WW3D_FORMAT_A8R8G8B8,
        MIP_LEVELS_1, TextureClass::POOL_MANAGED, false, false);
    // ... 用 hash 噪声填充像素 ...
}
```
> 当前 detail texture 已创建但**未绑定到 s1**（s1 = m_stageZeroTexture）。

### 6. W3DShaderManager.cpp — 核心代码（大量修改）

**a) TerrainShaderPBR 类** — 新增 3 个 shader 指针：
```cpp
IDirect3DPixelShader9 *m_dwPBRPixelShader;         // base
IDirect3DPixelShader9 *m_dwPBRNoise1PixelShader;    // + cloud
IDirect3DPixelShader9 *m_dwPBRNoise2PixelShader;    // + lightmap
IDirect3DPixelShader9 *m_dwPBRNoise12PixelShader;   // + cloud + lightmap
```

**b) compilePBRShader() 辅助函数** — 封装 HLSL 编译流程：
```cpp
static HRESULT compilePBRShader(const char* source,
    IDirect3DPixelShader9** ppShader, const char* tag)
```
统一 D3DXCompileShader + CreatePixelShader + 错误/诊断日志。

**c) init() 重构** — 4 个 shader 逐一编译注册：
1. `terrain_pbr`：base — s0 + s1 lerp + GGX specular
2. `terrain_pbr_noise1`：base + s2 cloud
3. `terrain_pbr_noise2`：base + s2 lightmap
4. `terrain_pbr_noise12`：base + s2 cloud + s3 lightmap

每个 HLSL 着色器核心逻辑：
```hlsl
float4 base0 = tex2D(s0, tex0);
float4 base1 = tex2D(s1, tex1);
float3 terrainColor = lerp(base0.rgb, base1.rgb, diffuse.a);
float3 N = float3(0, 0, 1);
float3 L = normalize(sunDirection);
float NdotL = saturate(dot(N, L));
float3 H = normalize(L + float3(0, 0, 1));
float spec = pow(saturate(dot(N, H)), 12.0);
float3 result = terrainColor * diffuse.rgb * (0.4 + 0.6 * NdotL)
              + sunColor * spec * 1.0;
```

**d) set() 实现** — stage 0 + stage 1 TSS 配置：
- Stage 0：基纹理，UV 通道 0，anisotropic/MIPFILTER
- Stage 1：细节纹理，UV 通道 1，anisotropic/MIPFILTER
- Stage 2/3：按变体类型设置云/光照贴图的相机空间投影
- 最终按 curShader 选择对应 ps_2_0 pixel shader

**e) reset() 清理** — 完善 stage 0-3 的纹理/变换状态还原

**f) shutdown() 释放** — 新增 3 个 shader 指针的 Release

**g) 诊断日志** — 在 init()/set() 中写入 `E:\terrain_diag.log`，记录每个 shader 变体的编译结果和选中状态

---

## 调参记录

| 轮次 | spec power | spec multiplier | 效果 | 结论 |
|------|-----------|----------------|------|------|
| 初始 | 8.0 | 1.5 | 全地表奶白色 | ❌ 太强 |
| 第 1 轮 | 32.0 | 0.4 | 颜色恢复，高光消失 | ❌ 太弱 |
| 第 2 轮 | 16.0 | 0.4 | 仍看不到高光 | ❌ 太弱 |
| 第 3 轮 | 16.0 | 1.0 | 有高光但不明显 | ⚠️ 接近 |
| **最终** | **12.0** | **1.0** | **高光可见，颜色正常** | **✅ 定案** |

---

## 未完成/预留

| 项目 | 状态 | 说明 |
|------|------|------|
| Detail texture 绑定 | ⏸️ | 已创建 `m_detailTexture` 但未绑定到渲染管线。之前尝试替换 s1 导致锯齿。下一步可以考虑：单独 s4 通道 modulate specular |
| 水面波光粼粼 (Blinn-Phong sparkle) | ✅ | 在 ps_2_0 中实现，见水 PBR 章节 |
| PBR 贴图管线 (Phase 3) | ❌ | `_pbr.dds` 命名约定 + 粗糙度/金属度贴图 |
| 单位/建筑 PBR (Phase 4) | ❌ | GGX BRDF + 8 光源 + 法线贴图 |
| HDR/IBL (Phase 5) | ❌ | 远期 |

---

# 附录：水面 PBR 升级 (Phase 1)

## 修改文件清单（共 2 个）

| # | 文件 | 修改类型 | 说明 |
|---|------|---------|------|
| 1 | `W3DDevice/GameClient/W3DWater.h` | +1 行 | 新增 `m_waveShaderPBR` 成员 |
| 2 | `W3DDevice/GameClient/W3DWater.cpp` | 多处 | ps_2_0 PBR shader + 渲染优先级 + 常量传递 |

---

## 详细代码变更

### 1. W3DWater.h — 第 173 行

```cpp
IDirect3DPixelShader9*	m_waveShaderPBR;  ///<ps_2_0 PBR pixel shader (bump+perturbed reflection+Fresnel+sparkle)
```

### 2. W3DWater.cpp — 5 处修改

**a) 构造函数 init（第 385 行）**：
```cpp
m_waveShaderPBR=NULL;
```

**b) ReleaseResources（第 867-868 行）**：
```cpp
if (m_waveShaderPBR)
    m_waveShaderPBR->Release();
```

**c) ReAcquireResources — PBR ps_2_0 shader 编译（第 1071-1141 行）**：

设备能力检测 `PixelShaderVersion >= ps_2_0` 后编译 HLSL：

```hlsl
sampler s0 : register(s0);       // bump map
sampler s1 : register(s1);       // reflection texture
float3 sunDirection : register(c0);
float3 sunColor : register(c1);

float4 main(float2 bumpUV : TEXCOORD0,
            float2 reflUV : TEXCOORD1,
            float4 color : COLOR0) : COLOR
{
    float4 bump = tex2D(s0, bumpUV);
    float2 perturb = (bump.xy - 0.5) * 0.08;    // bump 扰动幅度
    float2 perturbedUV = reflUV + perturb;       // 扰动后的反射 UV
    float4 reflection = tex2D(s1, perturbedUV);  // 采样扰动后的反射

    // Fresnel: UV 距离中心作为视角代理
    float2 fresnelUV = perturbedUV - 0.5;
    float fresnel = dot(fresnelUV, fresnelUV) * 2.0;
    fresnel = saturate(fresnel);
    fresnel = 0.02 + 0.98 * fresnel;

    float3 waterColor = float3(0.04, 0.08, 0.12);  // 深水蓝色
    float3 result = lerp(waterColor, reflection.rgb, fresnel);

    // Blinn-Phong 阳光高光闪烁
    float3 N = normalize(float3((bump.x - 0.5) * 2.0,
                                (bump.y - 0.5) * 2.0, 0.25));
    float3 L = normalize(sunDirection);
    float3 V = float3(0, 0, 1);
    float3 H = normalize(L + V);
    float spec = pow(saturate(dot(N, H)), 64.0);
    result += sunColor * spec * 0.85;

    return float4(result * color.rgb, color.a);
}
```

**d) drawSea — 常量传递（第 2300-2328 行）**：
```cpp
if (m_waveShaderPBR && TheGlobalData) {
    // 太阳方向 (取反，从场景光方向到着色器光照方向)
    float sunDir[4] = { -m_terrainLightPos.x, -m_terrainLightPos.y, -m_terrainLightPos.z, 0 };
    normalize(sunDir);
    // 太阳颜色 (ambient + diffuse)
    float sunColor[4] = { m_terrainAmbient.red + m_terrainDiffuse.red, ... };
    m_pDev->SetPixelShaderConstantF(0, sunDir, 1);   // c0
    m_pDev->SetPixelShaderConstantF(1, sunColor, 1);  // c1
}
```

**e) drawSea — shader 优先级选择（第 2331-2332 行）**：
```cpp
m_pDev->SetPixelShader(m_waveShaderPBR ? m_waveShaderPBR :
    (m_waveShaderNoBump ? m_waveShaderNoBump : m_dwWavePixelShader));
```
优先级：`PBR → noBump(ps.1.1 fallback) → 原始(texbem)`

---

## 着色器技术细节

| 特性 | 实现方式 | 参数 |
|------|---------|------|
| Bump 扰动 | `(bump.xy - 0.5) * 0.08` 偏移反射 UV | 幅度 0.08 |
| Fresnel | UV 距离中心平方 * 2.0，clamp 后 lerp | 0.02 ~ 1.0 |
| 水面颜色 | `float3(0.04, 0.08, 0.12)` 深蓝 | 远处纯色，近处反射 |
| Blinn-Phong 高光 | `pow(NdotH, 64) * sunColor * 0.85` | power 64, mult 0.85 |
| 法线来自 bump | `(bump.xy*2-1, 0.25)` 构造微表面法线 | Z=0.25 控制粗糙度 |
| 诊断日志 | `E:\water_diag.log` | 编译结果 + 太阳参数 |

---

## 渲染流程

```
drawSea()
  ├── 绑定 bump map (s0) + reflection (s1)
  ├── 设置 vertex shader waveVS
  ├── 传递 PS 常量：sunDirection(c0) + sunColor(c1)
  ├── SetPixelShader(m_waveShaderPBR)  ← PBR 优先
  ├── 遍历 PolygonTrigger 绘制水面 patch
  └── SetPixelShader(NULL) 清理
```

---

## 调参记录

| 参数 | 值 | 说明 |
|------|-----|------|
| Bump 扰动幅度 | 0.08 | 波纹扭曲强度，过大导致反射混乱 |
| Fresnel 最小值 | 0.02 | 直视水面时仍有 ~2% 反射 |
| Fresnel 最大值 | 1.0 | 掠射角完全反射 |
| Spec power | 64.0 | 高光集中度，模拟水面闪烁 |
| Spec multiplier | 0.85 | 高光强度 |
| 法线 Z 分量 | 0.25 | 控制微表面粗糙度，越小越粗糙 |

---

## 已知问题

| 问题 | 说明 |
|------|------|
| 倒影偏淡 | 地表 PBR 升级后亮度提升，水面反射的场景更亮，但 Fresnel lerp 到 `waterColor` 使近处倒影看起来变淡。非 bug，是视觉对比变化 |
| ps_2_0 依赖 | PBR 分支需要 GPU 支持 ps_2_0，否则回退到 ps.1.1 noBump 或原始 texbem |
| 水面闪烁 sparkle 强度 | spec 64 + mult 0.85，在某些光源角度下可能过亮，待后续微调 |

