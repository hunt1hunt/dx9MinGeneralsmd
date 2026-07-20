# 延迟渲染改造计划 — GeneralsMD (SAGE Engine)

> 建立日期: 2026-07-14
> 状态: 计划阶段 — 准备实施
> 前置条件: DX8→DX9 升级完成，DX9c SM3.0 就绪，PBR 前向光照渲染完成
> 参考资料: RTR 3rd §7、《绝命时刻 WW3D DX9 SM3.0 正向 PBR 改延迟渲染》完整方案、毛星云知乎文章

---

## 1. 概述

将现有的前向 PBR 渲染管线扩展为**混合延迟+前向管线**。核心思路来自经典的 Deferred Shading 两 Pass 架构：

```
前向: n 个物体 × m 个光源 = O(n*m)
延迟: n 个物体写入 G-Buffer + m 个光源在屏幕空间着色 = O(n+m)

RTS 场景收益: ~500 物体 × 4 光源 = 2000 次 → ~504 次操作
数十动态光源(爆炸+车灯+炮塔)时，帧率提升 40%~70%
```

不透明物体通过 G-Buffer MRTs 渲染，然后进行屏幕空间 PBR 光照合成。透明物体、粒子、UI、半透明装甲玻璃保持前向渲染路径——延迟渲染无法处理半透明混合。

### 1.1 引擎级硬限制（DX9c SM3.0 决定改造工作量）

| 限制 | 影响 | 对策 |
|------|------|------|
| MRT 上限 4 个 RT | G-Buffer 最多 4 张纹理 | 使用 3 RT 方案平衡带宽与质量 |
| 无浮点 RT 原生支持 | 精度损耗 | 8bit 定点 RT + 伽马校正线性空间处理 |
| 不支持硬件 MSAA | 边缘锯齿 | 后期 FXAA |
| 无 UAV | 光照只能全屏 Quad 叠加 | Stencil Volume 裁剪灯光范围 |
| 透明无法走延迟 | 必须保留正向 | 渲染队列分层 + 正向回退 Pass |
| VC6.0 编译器 | 复杂模板/常量易报错 | 拆分代码，避免复杂模板 |

### 1.2 G-Buffer 布局设计（核心决策）

**最终方案：3 RT + 深度重建**（比 4 RT 省 25% 带宽，减轻显存压力）

```
RT0 (A8R8G8B8): Albedo(RGB) + Metallic(A)
RT1 (A8R8G8B8): WorldNormal(RGB*0.5+0.5) + Roughness(A)
RT2 (A8R8G8B8): Emissive(RGB) + LinearDepth(A)

总带宽: 3 × 4 字节 × 分辨率 = 12 字节/像素
(1920x1080 = 24.9MB/帧, 显存占用 ~75MB 含 Depth/Backbuffer)

位置重建: screenUV + LinearDepth + inverse(ViewProj) → worldPosition
```

**为什么不存 AO：** AO 交由后续 SSAO Pass 处理。这是延迟渲染标准做法（SSAO 基于 G-Buffer 法线/深度实现，质量更高）。

**为什么不存世界坐标（4 RT）：** 
- RTS 大地图场景下，8bit 定点存储世界坐标精度严重不足（远处闪烁）
- 深度重建在大场景中精度更好（深度是 logarithmic 分布）
- 省一个 RT = 省 8.3MB/帧 带宽

**备选 4 RT 方案（如需未来扩展）：**
```
RT0: Albedo(RGB) + MaterialID(A)
RT1: WorldNormal(RGB) + Reserved(A)
RT2: SPM(R=Spec, G=Metallic, B=Roughness, A=AO)
RT3: WorldPosition(XYZ) + LinearDepth(A)
// + DXT5 压缩降低带宽，+ 动态分辨率降级
```

---

## 2. 渲染管线数据流（完整）

```
帧开始
  │
  ├── W3DDisplay::draw()
  │     ├── [1] updateRenderTargetTextures()    // 水反射(不变)
  │     ├── [2] W3DDraw() —— 主渲染循环
  │     │     │
  │     │     ├── 判定: 使用延迟路径?
  │     │     │     条件: HW 支持 MRT(3+) && INI 启用 && 非滤镜模式 && 非线框
  │     │     │
  │     │     ├── YES ──→ [延迟渲染路径]
  │     │     │     │
  │     │     │     ├── Step A: G-Buffer Pass (仅不透明物体)
  │     │     │     │     SetRenderTarget(0..2) = 3 MRT
  │     │     │     │     Clear(3 RTs + DepthStencil)
  │     │     │     │     RTS3DScene::doRender(SCENE_PASS_GBUFFER)
  │     │     │     │       → 所有不透明物体 → G-Buffer PS → MRT
  │     │     │     │       → 透明物体: continue (跳过)
  │     │     │     │       → 天空盒: continue (最后单独绘制)
  │     │     │     │     SetRenderTarget(1..2, NULL)  // 恢复单 RT
  │     │     │     │
  │     │     │     ├── Step B: Lighting Pass — 方向光 (全屏 Quad)
  │     │     │     │     SetRenderTarget(0, BackBuffer)
  │     │     │     │     SetDepthStencilSurface(DepthStencil)
  │     │     │     │     绑定 G-Buffer 纹理 s0/s1/s2
  │     │     │     │     设置 PBR 常量 c0-c10 (含 invViewProj)
  │     │     │     │     DrawPrimitive(全屏四边形) — 太阳 PBR 光照
  │     │     │     │
  │     │     │     ├── Step C: Lighting Pass — 动态光 (Stencil Volume)
  │     │     │     │     对每个动态点光/聚光:
  │     │     │     │       SetStencil(仅渲染光体积内像素)
  │     │     │     │       SetPixelShader(点光/聚光 PBR)
  │     │     │     │       Draw(光体积包围体网格)
  │     │     │     │
  │     │     │     ├── Step D: SSAO Pass (可选)
  │     │     │     │     基于 G-Buffer 法线+深度做屏幕空间 AO
  │     │     │     │
  │     │     │     ├── Step E: 自发光合并
  │     │     │     │     读取 RT2 Emissive 叠加到 BackBuffer
  │     │     │     │
  │     │     │     ├── Step F: Forward Transparent Pass
  │     │     │     │     RTS3DScene::doRender(SCENE_PASS_FORWARD_TRANSPARENT)
  │     │     │     │       → 玻璃/烟雾/粒子/激光/爆炸特效 (正向 PBR)
  │     │     │     │       → 天空盒绘制 (最后叠加)
  │     │     │     │
  │     │     │     ├── Step G: 后处理
  │     │     │     │     Bloom、色调映射、Gamma 校正、FXAA
  │     │     │     │
  │     │     │     └── Step H: UI + 2D (不变)
  │     │     │           RTS2DScene::doRender()
  │     │     │
  │     │     └── NO ───→ [原有前向渲染路径 — 完全保留]
  │     │           RTS3DScene::doRender(SCENE_PASS_DEFAULT)
  │     │           → 所有物体: 原有 PBR 前向着色器
  │     │           → RTS2DScene::doRender() (UI)
  │     │
  │     └── [3] Present()
```

### 2.1 渲染队列分层（关键：透明/不透明分离）

```
渲染顺序 (严格):
  1. G-Buffer Pass: 不透明物体 (建筑、坦克、步兵、地形、植被)
     ↓
  2. 延迟光照合成 (方向光 + 动态光)
     ↓
  3. 正向半透明层 (玻璃、烟雾、粒子、激光、爆炸特效、水)
     ↓
  4. 天空盒 (最后叠加)
     ↓
  5. UI、血条、选中框 2D 精灵
```

---

## 3. 六大阶段实施计划（整合两篇参考）

### 阶段 1：底层 DX9 渲染框架改造

**工期:** 3~5 天 | **代码量:** ~1200 行 C++
**目的:** 新增 MRT 渲染目标管理、G-Buffer 资源池、帧渲染流程重构

#### 1a. 新增 W3DDeferredRenderer 类

**新文件：** `W3DDeferredRenderer.h` / `.cpp`

类职责：
- G-Buffer 3 RT 创建/销毁/重建 (`init` / `shutdown` / `reAcquireResources` / `releaseResources`)
- MRT 设置 (`beginGBufferPass`) → `SetRenderTarget(0..2)`
- 全屏 Quad 工具方法
- 光照 Pass 编排
- 常量缓冲区管理

```cpp
class W3DDeferredRenderer
{
public:
    // 生命周期
    bool init(int width, int height);
    void shutdown();
    bool reAcquireResources(int w, int h);
    void releaseResources();

    // G-Buffer Pass
    bool beginGBufferPass();          // Set 3 MRTs + Clear
    void endGBufferPass();            // SetRenderTarget(1,2)=NULL

    // Lighting Passes
    void sunLightPass(CameraClass* cam, RTS3DScene* scene);     // 方向光全屏 Quad
    void pointLightPass(W3DDynamicLight* light, CameraClass* cam); // 点光 Stencil Volume
    void spotLightPass(...);                                      // 聚光 Stencil Volume
    void emissiveMergePass();           // 自发光合并
    void ssaoPass();                    // SSAO (可选后期)

    // 正向透明通道
    void beginForwardPass();           // 恢复 RT0 + Depth

    // 状态
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool e) { m_enabled = e; }

private:
    // G-Buffer textures (3 RTs)
    TextureClass *m_rtAlbedo, *m_rtNormal, *m_rtEmissive;
    IDirect3DSurface9 *m_surfAlbedo, *m_surfNormal, *m_surfEmissive;

    // Saved surfaces
    IDirect3DSurface9 *m_savedRT, *m_savedDepth;

    // Shaders
    IDirect3DVertexShader9 *m_gbufferVS, *m_lightingVS;
    IDirect3DPixelShader9 *m_gbufferPS;
    IDirect3DPixelShader9 *m_sunLightPS;
    IDirect3DPixelShader9 *m_pointLightPS;    // Stencil Volume variant
    IDirect3DPixelShader9 *m_spotLightPS;
    IDirect3DPixelShader9 *m_emissiveMergePS;
    IDirect3DPixelShader9 *m_ssaoPS;
    IDirect3DPixelShader9 *m_fxaaPS;

    // Fullscreen quad VB
    IDirect3DVertexBuffer9 *m_quadVB;

    // Light volume VB/IB (sphere + cone for stencil)
    IDirect3DVertexBuffer9 *m_sphereVB;
    IDirect3DIndexBuffer9  *m_sphereIB;
    // ...

    bool m_enabled;
    int m_width, m_height;
};

extern W3DDeferredRenderer* TheW3DDeferredRenderer;
```

#### 1b. CustomScenePassModes 枚举扩展

**文件：** `W3DCustomScene.h`

```cpp
enum CustomScenePassModes
{
    SCENE_PASS_DEFAULT,
    SCENE_PASS_ALPHA_MASK,
    SCENE_PASS_GBUFFER,              // 新增
    SCENE_PASS_FORWARD_TRANSPARENT   // 新增
};
```

#### 1c. RTS3DScene 成员 + 初始化

**文件：** `W3DScene.h` / `W3DScene.cpp`

```cpp
// W3DScene.h — 新增成员
MaterialPassClass *m_gbufferMaterialPass;   // G-Buffer 材质覆盖(禁光照)

// W3DScene.cpp — 构造函数中初始化
m_gbufferMaterialPass = NEW_REF(MaterialPassClass,());
VertexMaterialClass *gbufMtl = NEW_REF(VertexMaterialClass,());
gbufMtl->Set_Lighting(true);
gbufMtl->Set_Ambient(0,0,0);
gbufMtl->Set_Diffuse(0,0,0);
gbufMtl->Set_Emissive(0,0,0);
m_gbufferMaterialPass->Set_Material(gbufMtl);
ShaderClass gbufShader = ShaderClass::_PresetOpaqueShader;
m_gbufferMaterialPass->Set_Shader(gbufShader);
gbufMtl->Release_Ref();

// 析构函数释放
REF_PTR_RELEASE(m_gbufferMaterialPass);
```

#### 1d. W3DDisplay::init() 集成

**文件：** `W3DDisplay.cpp` — 第 827 行附近

```cpp
// 在 m_initialized = true; 之前初始化延迟渲染器
TheW3DDeferredRenderer = NEW W3DDeferredRenderer;
if (!TheW3DDeferredRenderer->init(getWidth(), getHeight())) {
    DEBUG_LOG(("DeferredRenderer init FAILED — fallback to forward\n"));
    // 非致命，继续使用前向渲染
}
```

#### 1e. 设备重置 + 分辨率变化

**文件：** `W3DDisplay.cpp`

```cpp
// 设备丢失/重置处理 (参考 W3DProjectedShadowManager 模式)
// 释放:
if (TheW3DDeferredRenderer) TheW3DDeferredRenderer->releaseResources();
// 重建:
if (TheW3DDeferredRenderer) TheW3DDeferredRenderer->reAcquireResources(w, h);

// setDisplayMode 分辨率变化:
if (TheW3DDeferredRenderer) {
    TheW3DDeferredRenderer->releaseResources();
    TheW3DDeferredRenderer->reAcquireResources(xres, yres);
}
```

#### 1f. INI 开关

**文件：** `GlobalData.h` + `GameData.cpp`

```cpp
// GlobalData.h
Bool m_useDeferredRendering;   // 默认 true
Bool m_useFXAA;                // 默认 true
Int m_gbufferScale;            // 100=全分辨率, 75=75%, 50=50%

// GameData.cpp INI 解析
fieldBegin(m_useDeferredRendering, "UseDeferredRendering");
fieldBegin(m_useFXAA, "UseFXAA");
fieldBegin(m_gbufferScale, "GBufferScale");
```

---

### 阶段 2：PBR 着色器大规模重构

**工期:** 4~7 天（最大模块） | **代码量:** ~600 行 HLSL (G-Buffer) + ~900 行 (光照)
**目的:** 拆分原有 Forward PBR 着色器为 G-Buffer 写入 + 延迟光照两套

#### 2a. G-Buffer 写入着色器（新建）

**G-Buffer 顶点着色器** (复用现有矩阵变换):

```
vs_2_0 / vs_3_0
常量: c0-c3 WorldViewProj, c4-c7 World, c8-c11 WorldIT
输入: POSITION, NORMAL, TEXCOORD0
输出: oPos (clip), oUv, oWorldNorm, oWorldPos
```

**G-Buffer 像素着色器** (ps_2_0/ps_3_0 兼容):

```
ps_2_0
移除: GGX BRDF, 光照计算, 阴影采样
保留: 纹理采样, 法线贴图解码, 世界法线变换
输出:
  COLOR0: Albedo.rgb + Metallic.a    [RT0]
  COLOR1: WorldNormal*0.5+0.5 + Roughness.a  [RT1]
  COLOR2: Emissive.rgb + clipPos.z   [RT2]

// 伪代码:
sampler s0 : diffuse map
sampler s1 : normal map
sampler s2 : PBR params map (R=Metallic, G=Roughness, B=AO)

float4 main(VS_OUT v) : COLOR {
    float4 albedo = tex2D(s0, v.uv);
    float3 normal = decode_normal(tex2D(s1, v.uv), v.worldNorm);
    float4 pbr    = tex2D(s2, v.uv);

    float4 rt0 = float4(albedo.rgb, pbr.r);           // Albedo + Metallic
    float4 rt1 = float4(normal*0.5+0.5, pbr.g);       // Normal + Roughness
    float4 rt2 = float4(0, 0, 0, v.pos.z/v.pos.w);    // Emissive(0) + Depth

    return rt0;  // MRT 输出 COLOR0/1/2
}
```

**关键要求：**
- 所有不透明材质新增 G-Buffer Pass（建筑、坦克、步兵、地形、植被）
- 透明材质（玻璃、烟雾、激光）**保留**前向着色器，不写入 G-Buffer
- 多层叠加材质：多层结果合并写入 G-Buffer（分层逻辑保留在几何 Pass）
- NT（Non-Texture）路径：使用常量替代纹理采样

#### 2b. 延迟光照着色器（新建）

**方向光（太阳光）全屏 Quad 着色器:**

```hlsl
// SunLight PS — 读取 G-Buffer, 计算 PBR Cook-Torrance
// 采样器:
//   s0: G-Buffer RT0 (Albedo + Metallic)
//   s1: G-Buffer RT1 (Normal + Roughness)
//   s2: G-Buffer RT2 (Emissive + Depth)
//
// 常量寄存器 (必须与 renderOneObject 一致):
//   c0: 太阳方向 (normalized world space)
//   c1: 太阳颜色 (diffuse * intensity)
//   c2: 相机位置 (world space)
//   c3-c6: inverse(ViewProj) 矩阵 (4x4)
//   c7-c9: 额外全局光方向/颜色 (最多 3 对)
//   c10: 环境光颜色
//   c11: 1/width, 1/height, 0, 0  (屏幕参数)

ps_3_0  // 使用 ps_3_0 以获取更多指令和更好的精度

// 1. 从 G-Buffer 采样
float4 albedoMetal = tex2D(s0, v.uv);
float4 normalRough = tex2D(s1, v.uv);
float4 emissiveDepth = tex2D(s2, v.uv);

// 2. 解码 G-Buffer
float3 albedo = albedoMetal.rgb;
float metallic = albedoMetal.a;
float3 N = normalize(normalRough.rgb * 2 - 1);
float roughness = normalRough.a;
float depth = emissiveDepth.a;

// 3. 重建世界坐标
float2 screenPos = v.uv * 2 - 1;
float4 clipPos = float4(screenPos, depth, 1);
float4 worldPos = mul(clipPos, c3);  // invViewProj
worldPos /= worldPos.w;

// 4. Cook-Torrance BRDF (与现有前向着色器一致)
float3 V = normalize(c2 - worldPos.xyz);
float3 L = normalize(-c0.xyz);
float3 H = normalize(V + L);
float NdotL = max(dot(N, L), 0);
float NdotV = max(dot(N, V), 0);
float NdotH = max(dot(N, H), 0);
float HdotV = max(dot(H, V), 0);

// GGX NDF
float a = roughness * roughness;
float a2 = a * a;
float d = NdotH * NdotH * (a2 - 1) + 1;
float D = a2 / (PI * d * d);

// Schlick-GGX Geometry
float k = (roughness + 1) * (roughness + 1) / 8;
float G = NdotL / (NdotL * (1 - k) + k) * NdotV / (NdotV * (1 - k) + k);

// Schlick Fresnel
float3 F0 = lerp(0.04, albedo, metallic);
float3 F = F0 + (1 - F0) * pow(1 - HdotV, 5);

// Specular + Diffuse
float3 specular = D * G * F / (4 * NdotL * NdotV + 0.0001);
float3 kD = (1 - F) * (1 - metallic);
float3 diffuse = kD * albedo / PI;

// Lo = (diffuse + specular) * lightColor * NdotL
float3 Lo = (diffuse + specular) * c1.rgb * NdotL;

// 环境光
float3 ambient = c10.rgb * albedo;

// Final
return float4(ambient + Lo, 1);
```

**点光/聚光 Stencil Volume 着色器：**（见阶段 3b）

#### 2c. 自发光合并 Pass

```hlsl
// Emissive Merge PS
// 从 RT2 读取自发光, 叠加到当前 BackBuffer
float4 emissive = tex2D(s_emissive, v.uv);
return float4(emissive.rgb, 0);  // additive blend
```

---

### 阶段 3：灯光系统改造 + Stencil Volume

**工期:** 2~3 天 | **代码量:** ~500 行 C++ + ~300 行 HLSL
**目的:** CPU 灯光管理批处理 + Stencil Volume 光照裁剪

#### 3a. CPU 灯光管理

**每帧处理：**
```
1. 收集场景所有有效光源:
   - 太阳光 (TheGlobalData->m_terrainLightPos[0])
   - 额外全局光 (最多 3 个)
   - 动态点光 W3DDynamicLight (爆炸、车灯)
   - 动态聚光 (炮塔探照灯)
2. 光源剔除:
   - 剔除屏幕外光源 (视锥体裁剪)
   - 剔除距离过远光源 (距离衰减 ≤ 0)
   - 最多保留 N 个动态光源 (按强度排序)
3. 上传光源数据到 Constant Buffers
```

**文件集成：** `RTS3DScene` 中的 `m_dynamicLightList` 已经维护了动态光列表。只需新增光源剔除函数。

#### 3b. Stencil Volume 光照（DX9 SM3.0 性能关键）

**原理：** 利用 Stencil 缓冲标记灯光覆盖的像素，避免全屏 Quad 遍历所有灯光。

```
对每个点光:
  Pass 1: 渲染光包围球体 → 深度测试
          设置 Stencil Ref=1, 仅在球体覆盖区域标记
  Pass 2: 渲染全屏 Quad → 仅在 Stencil==1 区域执行 PBR 光照
          相加混合到 BackBuffer

对每个聚光:
  类似但使用圆锥包围体 + 聚光衰减纹理
```

**实现要点：**
- 预生成球体 VB/IB（16 段经纬度，~512 三角形）
- 预生成圆锥 VB/IB（聚光范围）
- Stencil 操作：`SetRenderState(D3DRS_STENCILENABLE, TRUE)`
- 光源 Pass 在 G-Buffer 深度已有非透明几何体深度，可直接深度测试裁剪

**着色器实现（点光 PBR，区别于方向光之处）：**
```hlsl
// PointLight PS — 与方向光类似但使用:
// c0: 点光位置 (world space)
// c1: 点光颜色
// c2: 点光衰减参数 (constant, linear, quadratic)
// c3: 点光范围半径

float3 L = c0.xyz - worldPos.xyz;
float dist = length(L);
L /= dist;
float attenuation = 1 / (c2.x + c2.y*dist + c2.z*dist*dist);
attenuation = saturate(1 - dist*dist/(c3*c3));  // 平滑衰减

// ... 剩余 BRDF 相同 ...
```

---

### 阶段 4：透明物体、粒子、UI 兼容改造

**工期:** 2~4 天 | **代码量:** ~400 行 C++
**目的:** 解决延迟渲染无法处理半透明的天生缺陷

#### 4a. RTS3DScene::Customized_Render() 分支

**文件：** `W3DScene.cpp` 第 1153-1270 行

```cpp
void RTS3DScene::Customized_Render(RenderInfoClass &rinfo)
{
    // ... [前置代码: Visibility_Check, On_Frame_Update] ...

    // === 地形 (所有通道都需要) ===
    if (terrainObject) {
        if (m_customPassMode == SCENE_PASS_GBUFFER) {
            robj->Render(rinfo);  // G-Buffer 通道: 无遮蔽
        }
        else if (m_customPassMode == SCENE_PASS_DEFAULT && m_shroudMaterialPass) {
            rinfo.Push_Material_Pass(m_shroudMaterialPass);
            robj->Render(rinfo);
            rinfo.Pop_Material_Pass();
        }
        else if (m_customPassMode == SCENE_PASS_FORWARD_TRANSPARENT) {
            // 在地形渲染的情况下透明通道可以跳过地形
        }
        else {
            robj->Render(rinfo);
        }
    }

    if (m_drawTerrainOnly) return;

    // === 对象遍历 ===
    for (it.First(&RenderList); !it.Is_Done();) {
        robj = it.Peek_Obj(); it.Next();
        if (robj->Class_ID() == CLASSID_TILEMAP) continue;
        if (!robj->Is_Really_Visible()) continue;

        DrawableInfo *di = (DrawableInfo*)robj->Get_User_Data();
        Drawable *draw = di ? di->m_drawable : NULL;

        if (m_customPassMode == SCENE_PASS_GBUFFER) {
            // G-Buffer: 仅不透明物体
            if (draw && draw->isTranslucent()) continue;  // 跳过透明
            if (draw && draw->isSkybox()) continue;        // 跳过天空盒
            rinfo.Push_Material_Pass(m_gbufferMaterialPass);
            renderOneObject(rinfo, robj, localPlayerIndex);
            rinfo.Pop_Material_Pass();
            continue;
        }

        if (m_customPassMode == SCENE_PASS_FORWARD_TRANSPARENT) {
            // 正向透明: 仅透明 + 天空盒
            if (!draw || !draw->isTranslucent()) continue;
            // 使用原有前向着色器 (renderOneObject 已有 PBR 光照)
        }

        // SCENE_PASS_DEFAULT: 原有逻辑保持不变
        // ...
    }

    // === Granny/阴影/粒子 (仅在非 GBUFFER 通道处理) ===
    if (m_customPassMode != SCENE_PASS_GBUFFER) {
        // Granny Flush
        // 阴影排队
        // 粒子排队 (粒子在透明通道中正常渲染)
    }
}
```

#### 4b. 透明物体正向 Pass 注意事项

- **深度缓冲共享：** G-Buffer Pass 写入的 Z-Buffer 中有不透明物体深度。透明 Pass 使用 `ZTest=LEQUAL, ZWrite=OFF`，确保正确排序在后。
- **粒子系统：** `TheParticleSystemManager->queueParticleRender()` 延迟到正向透明通道执行。
- **水面渲染：** 水的 `updateRenderTargetTextures()` 在主渲染前已执行完毕（反射纹理渲染），不受影响。水面绘制在正向透明通道。
- **天空盒：** G-Buffer Pass 跳过，正向透明通道中最后绘制（设置 `ZTest=LEQUAL, ZWrite=OFF`，使用深度缓冲的不透明物体深度）。

---

### 阶段 5：后处理管线 + 画面一致性

**工期:** 4~8 天 | **代码量:** ~500 行 C++ + ~300 行 HLSL
**目的:** 迁移后处理、对齐正反向画面、性能优化

#### 5a. 后处理管线

```
光照合成输出 → (可选) SSAO → Bloom → 色调映射 → Gamma 校正 → FXAA → Present
```

- **SSAO：** 基于 G-Buffer 法线 + 深度做屏幕空间环境光遮蔽（比原前向 AO 更精确）
- **Bloom：** 读取光照累积纹理，提取高亮区域做高斯模糊叠加
- **FXAA：** 替代 MSAA（MSAA 与 MRT 不兼容），做边缘抗锯齿

#### 5b. 画面一致性（正向/延迟画面对齐）

| 调整项 | 方法 |
|--------|------|
| 8bit G-Buffer 精度丢失 | 伽马校正 + 线性空间统一处理 |
| PBR 参数数值匹配 | 使用与正向完全相同的 GGX/Schlick 公式 |
| 光照强度对齐 | 逐像素对比正向/延迟渲染差异，微调参数 |
| 环境光/阴影一致 | 使用相同环境光颜色和阴影贴图 |
| 透明粒子光照氛围 | 确保正向透明通道使用相同的光照常量（c0-c10） |

#### 5c. DX9 平台专项优化

| 优化 | 方法 | 收益 |
|------|------|------|
| G-Buffer 纹理压缩 | DXT5 压缩所有 RT（ps_3_0 支持直接采样 DXT） | 降低 4x 显存 |
| 动态分辨率降级 | `GBufferScale=75` 时 G-Buffer 按 0.75x 渲染后放大 | 帧率提升 30%+ |
| 关闭不必要 Mipmap | G-Buffer 使用 MIP_LEVELS_1 | 省显存 |
| 合并清除操作 | 一次 Clear 清除 3 RTs + Depth | 减少 API 调用 |
| VC6.0 规避 | 拆分长函数，避免复杂模板 | 避免编译报错 |

#### 5d. 全场景测试

| 测试项 | 方法 | 验收标准 |
|--------|------|----------|
| 大规模对战 | 8 玩家满人口对战 | 帧率稳定 > 30fps |
| 多动态光源 | 同时 20+ 爆炸/车灯 | 无掉帧 |
| Alt-Tab 切换 | 重复切换 10 次 | 无崩溃/黑屏 |
| 分辨率切换 | 窗口↔全屏 800x600↔1920x1080 | G-Buffer 重建正确 |
| 前向↔延迟对比 | 逐帧截图比较 | RGB 差异 < 5% |

---

## 4. 文件更改完整清单

| # | 文件 | 操作 | 预估代码量 |
|---|------|------|-----------|
| 1 | `Include/W3DDevice/GameClient/W3DCustomScene.h` | 修改枚举 | +2 行 |
| 2 | `Include/W3DDevice/GameClient/W3DScene.h` | 新增成员 | +3 行 |
| 3 | `Source/W3DDevice/GameClient/W3DScene.cpp` | 构造函数 + Render 分支 | +80 行 |
| 4 | `Source/W3DDevice/GameClient/W3DView.cpp` | draw() 编排 | +60 行 |
| 5 | `Source/W3DDevice/GameClient/W3DDisplay.cpp` | init + 设备重置 | +40 行 |
| 6 | `Include/W3DDevice/GameClient/W3DDeferredRenderer.h` | **新建** | +150 行 |
| 7 | `Source/W3DDevice/GameClient/W3DDeferredRenderer.cpp` | **新建** | +1200 行 |
| 8 | `Include/Common/GlobalData.h` | INI 配置 | +5 行 |
| 9 | `Source/Common/GameData.cpp` | INI 解析 | +10 行 |
| 10 | `RTS.dsp` / `RTS.dsw` | 加入新文件 | +2 行 |
| | **总计** | | **~1550 行 C++** |
| | HLSL 着色器字符串 (内联在 .cpp 中) | **新建** | **~1500 行 HLSL** |
| | **代码总量** | | **~3050 行** |

---

## 5. 工期估算

| 阶段 | 单人 | 双人（程序+Shader） |
|------|------|-------------------|
| 阶段1: 底层框架 | 3~5 天 | 2~3 天 |
| 阶段2: Shader 重构 | 4~7 天 | 2~4 天 (Shader 工程师) |
| 阶段3: 灯光系统 | 2~3 天 | 1~2 天 |
| 阶段4: 透明兼容 | 2~4 天 | 1~2 天 |
| 阶段5: 后处理+调参 | 4~8 天 | 3~5 天 |
| **合计** | **18~27 工作日** | **9~14 工作日** |

---

## 6. 实施顺序建议（最简落地）

```
第 1 步: 阶段1 — 枚举 + W3DDeferredRenderer 类框架 (编译验证)
第 2 步: 阶段1 — W3DDisplay init/设备重置集成 (编译验证)
第 3 步: 阶段2 — G-Buffer 写入着色器 (D3DXCompileShader 验证)
第 4 步: 阶段2 — 太阳光全屏 Quad 着色器 (RenderDoc 截帧验证)
第 5 步: 阶段1 — Customized_Render + W3DView::draw 编排 (画面显示验证)
第 6 步: 阶段3 — 动态光 Stencil Volume (动态光源验证)
第 7 步: 阶段4 — 透明/粒子/天空盒回退 (视觉效果验证)
第 8 步: 阶段5 — 后处理迁移 (画面质量验证)
第 9 步: 阶段5 — 画面一致性调参 (正向/延迟 A/B 对比)
第 10 步: 阶段5 — 性能优化 + 全场景测试 (帧率/稳定性验证)
```

---

## 7. 核心风险与应对

| 风险 | 概率 | 影响 | 应对 |
|------|------|------|------|
| **DX9 MRT 兼容 BUG** | 中 | 输出通道错乱/黑屏 | 编写降级分支 + 2~3 天调试 |
| **多层叠加材质画面色差** | 高 | 材质视觉效果偏离 | 保留原前向着色器做双路对比 + 2 天调参 |
| **显存暴涨导致低配设备闪退** | 中 | 4x RT + Depth 显存翻倍 | 动态分辨率降级 + DXT5 压缩 + 1~2 天 |
| **透明粒子光照不一致** | 中 | 透明/不透明物体光照氛围不同 | 统一光照常量 + 1~2 天视觉对齐 |
| **VC6.0 编译器限制** | 低 | 复杂模板/常量编译报错 | 拆分代码 + 1 天额外工时 |
| **Granny 骨骼动画状态错乱** | 中 | 多 Pass 中动画更新冲突 | 仅正向透明通道执行 queueUpdate |

---

## 8. 参考资料

### 学术/行业文献

- [RTR 3rd §7.9.2] *Real-Time Rendering, 3rd Edition*, Section 7.9.2 "Deferred Shading"
- [Lauritzen 2010] Andrew Lauritzen, "Deferred Rendering for Current and Future Rendering Pipelines", SIGGRAPH 2010
- [GPU Gems 3 Ch.19] *GPU Gems 3*, Chapter 19 "Deferred Shading in Tabula Rasa"
- [GDC 2004] 原始 Deferred Shading 提出: http://www.tenacioussoftware.com/gdc_2004_deferred_shading.ppt
- [Wolfgang Engel 2008] Light Pre-Pass / Deferred Lighting
- [Coffin 2011] SPU-based deferred shading for Battlefield 3 on Playstation 3, GDC 2011

### 项目关键文件索引

| 文件 | 路径 |
|------|------|
| W3DCustomScene.h | `GeneralsMD/Code/GameEngineDevice/Include/W3DDevice/GameClient/` |
| W3DScene.h | `GeneralsMD/Code/GameEngineDevice/Include/W3DDevice/GameClient/` |
| W3DScene.cpp | `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/` |
| W3DView.cpp | `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/` |
| W3DDisplay.cpp | `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/` |
| dx8wrapper.h | `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/` |
| dx8wrapper.cpp | `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/` |
| W3DShaderManager.cpp | `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/` |
| W3DShaderManager.h | `GeneralsMD/Code/GameEngineDevice/Include/W3DDevice/GameClient/` |
| W3DDeferredRenderer.h | **(新建)** `GeneralsMD/Code/GameEngineDevice/Include/W3DDevice/GameClient/` |
| W3DDeferredRenderer.cpp | **(新建)** `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/` |
