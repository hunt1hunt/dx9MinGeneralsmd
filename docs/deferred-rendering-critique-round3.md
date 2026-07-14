# 延迟渲染计划 — 第3轮检讨：正式实施计划（修正版）

> 基于第1、2轮共 40+ 项差距分析 + 完整代码走读
> 日期: 2026-07-14
> 状态: 最终审定稿

---

## 一、核心架构决策变更清单

### 变更1：❌ 3-RT MRT 方案 → ✅ 单 Pass G-Buffer + Z-Buffer 重建

**原因：** DX8Wrapper 不支持 MRT，所有 RT 操作只使用 index 0。若要添加 MRT 需修改整个 DX8Wrapper 层，风险高、工作量大。

**新方案：**
```
G-Buffer: 单张 A8R8G8B8 渲染目标
  RGB = Albedo (线性空间)
  A   = PackedPBR: Metallic[7..4] | Roughness[3..0]
  Z-Buffer = 24-bit 原生深度 (直接使用现有 DepthStencil)

光照 Pass:
  法线重建: ddx/ddy(线性化深度) → 世界法线 (ps_3_0 only)
  位置重建: screenUV + depth + inverse(ViewProj) → 世界坐标
  PBR 光照: Cook-Torrance BRDF (与现有前向着色器公式一致)
```

**优点：**
- 完全不需要 MRT，不修改 DX8Wrapper
- 单几何 Pass，不增加绘制调用
- 24-bit Z-Buffer 精度远优于 8-bit
- 显存占用最小（1 RT vs 原计划 3 RTs）

**缺点：**
- 法线重建损失法线贴图细节（无微表面法线）
- 需要 ps_3_0（2004+ 硬件，合理要求）
- PackedPBR 的 4-bit 精度（16 级 metallic/roughness）

### 变更2：❌ 在 Customized_Render 内部完成全部渲染 → ✅ 在 W3DDisplay::draw() 层编排多阶段渲染

**原因：** 现有 Customized_Render 同时管理不透明/透明/阴影/粒子。延迟渲染需要在场景渲染前完成 G-Buffer、在场景渲染中执行光照合成。

**新编排位置：** W3DDisplay::draw() 的 render loop 中（见下文详细编排）

### 变更3：❌ 忽略迷雾 → ✅ 屏幕空间 Shroud Pass

**原因：** 迷雾系统基于材质 Pass 叠加，在 G-Buffer 中无法直接复用。

**方案：** 在光照合成后、透明 Pass 前，添加一个全屏 Quad 读取 shroud 纹理 + G-Buffer 深度重建世界坐标 → 对光照结果进行迷雾遮罩。

### 变更4：❌ 忽略遮挡系统 → ✅ G-Buffer 中保留遮挡分类

**原因：** 遮挡系统在 Customized_Render 中分类 object flags。G-Buffer Pass 中仍需要按遮挡分类来过滤渲染对象。

**方案：** 在 G-Buffer Pass 中保留 `ERF_DELAYED_RENDER` / `ERF_POTENTIAL_OCCLUDER` 等标志的处理逻辑。

---

## 二、完整渲染管线数据流（修正版）

```
W3DDisplay::draw()
  │
  ├── [前置 RT Passes] (不变)
  │     ├── 水反射纹理更新 (updateRenderTargetTextures)
  │     └── 阴影纹理更新 (updateRenderTargetTextures)
  │
  ├── [判定: 启用延迟渲染?]
  │     条件: SM3.0 支持 && INI 启用 && 非滤镜模式 && 非线框
  │
  ├── YES ──→ [延迟渲染路径]
  │     │
  │     ├── Step A: G-Buffer Pass (单 RT0 + Depth)
  │     │     TheW3DDeferredRenderer->beginGBufferPass()
  │     │       → Create/Get RT0 surface
  │     │       → DX8Wrapper::Set_Render_Target(rt0Surf, depthSurf)
  │     │       → Clear(rt0, depth, stencil)
  │     │
  │     │     W3DDisplay::m_3DScene->setCustomPassMode(SCENE_PASS_GBUFFER)
  │     │     W3DDisplay::m_3DScene->doRender(gbufferCamera)
  │     │       → Customized_Render 中:
  │     │          地形 → 使用地形 G-Buffer 着色器写入 Albedo+PackedPBR
  │     │          不透明物体 → m_gbufferMaterialPass + G-Buffer PS
  │     │          透明/天空盒 → continue (跳过)
  │     │          遮挡分类: 保留 ERF_DELAYED_RENDER/ERF_POTENTIAL_OCCLUDER
  │     │        渲染后 → Flush() 中跳过阴影/粒子/透明
  │     │
  │     │     TheW3DDeferredRenderer->endGBufferPass()
  │     │       → Set_Render_Target(backbuffer, depthSurf)
  │     │
  │     ├── Step B: 光照合成 (全屏 Quad)
  │     │     TheW3DDeferredRenderer->sunLightPass()
  │     │       → 绑定 RT0 作为纹理 s0
  │     │       → 绑定 Depth 作为纹理 s1 (或使用 Z-Buffer 重建)
  │     │       → 设置 PBR 常量 (太阳方向/颜色, 相机位置, invViewProj, 环境光)
  │     │       → 绘制全屏 Quad
  │     │       → 输出到 backbuffer
  │     │
  │     ├── Step C: 动态光 (Stencil Volume)
  │     │     对每个有效动态光:
  │     │       渲染包围体标记 Stencil
  │     │       在 Stencil 区域内叠加点光/聚光 PBR
  │     │
  │     ├── Step D: 屏幕空间 Shroud
  │     │     全屏 Quad 读取 shroud 纹理
  │     │     对光照结果应用迷雾遮罩
  │     │
  │     ├── Step E: 自发光合并
  │     │     不依赖于 RT2 (因无 MRT)
  │     │     替代: 在前向透明 Pass 中带 emissive 叠加
  │     │
  │     ├── Step F: 正向透明 Pass
  │     │     W3DDisplay::m_3DScene->setCustomPassMode(SCENE_PASS_FORWARD_TRANSPARENT)
  │     │     W3DDisplay::m_3DScene->doRender(forwardCamera)
  │     │       → 玻璃/烟雾/粒子/激光/爆炸 (前向 PBR)
  │     │       → 天空盒 (最后)
  │     │
  │     ├── Step G: 阴影叠加
  │     │     在光照合成时已完成 (或另做全屏 Quad 叠加)
  │     │
  │     ├── Step H: 后处理 (可选)
  │     │     Tone mapping, Gamma correction
  │     │     Bloom, FXAA (后续阶段)
  │     │
  │     └── Step I: UI + 2D (不变)
  │           W3DDisplay::m_2DScene->doRender()
  │
  └── NO ───→ [原有前向路径 — 完全保留]
        正常调用 RTS3DScene::doRender()
        → Customized_Render + Flush (现有逻辑不变)
```

---

## 三、G-Buffer 着色器设计（修正版）

### 3.1 G-Buffer 顶点着色器 (vs_3_0)

```
常量:
  c0-c3: WorldViewProj 矩阵
  c4-c7: World 矩阵
  c8-c11: WorldIT 矩阵 (法线变换)

输入: POSITION, NORMAL, TEXCOORD0
输出: oPos (clip space), oUv, oWorldNormal, oWorldPos
```

### 3.2 G-Buffer 像素着色器 (ps_3_0)

```hlsl
// 输入: v.uv, v.worldNormal, v.worldPos
// 采样器: s0 = 漫反射贴图, s1 = 法线贴图, s2 = PBR 参数贴图
// 输出: COLOR0 = float4(Albedo.rgb, PackedPBR)

sampler s0 : diffuse map;       // t0
sampler s1 : normal map;        // t1  
sampler s2 : PBR params map;     // t2 (R=Metallic, G=Roughness, B=AO)

struct VS_OUT {
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float3 worldPos : TEXCOORD2;
};

float4 main(VS_OUT v) : COLOR0 {
    float4 albedo = tex2D(s0, v.uv);
    
    // 法线贴图解码
    float3 normalTS = tex2D(s1, v.uv).xyz * 2 - 1;
    float3 worldNormal = normalize(v.worldNormal);
    // 简化: 不进行 tangent-bitangent 变换 (因重建法线不使用)
    // 实际上，G-Buffer 中写入世界法线用于光照
    // 但因为我们用法线重建，这里不需要精确法线
    // 所以写入简化的几何法线 (从 Normal 顶点数据计算)
    float3 N = normalize(v.worldNormal);
    N = N * 0.5 + 0.5;  // 编码到 [0,1]
    
    // PBR 参数
    float metallic = tex2D(s2, v.uv).r;
    float roughness = tex2D(s2, v.uv).g;
    
    // 打包: Metallic[7..4], Roughness[3..0]
    float packedPBR = (floor(metallic * 15 + 0.5) * 16 + 
                      floor(roughness * 15 + 0.5)) / 255.0;
    
    return float4(albedo.rgb, packedPBR);
}
```

**注意：** 上面是完整法线贴图解码版本。如果为性能选择无法线贴图版本，可以简化为：

```hlsl
// 轻量版 G-Buffer PS — 无法线贴图解码
float4 main(VS_OUT v) : COLOR0 {
    float4 albedo = tex2D(s0, v.uv);
    // 使用几何法线 (直接从顶点传递，在光照 Pass 中用 ddx/ddy 重建)
    // 所以这里不需要写法线
    
    float metallic = tex2D(s2, v.uv).r;
    float roughness = tex2D(s2, v.uv).g;
    float packedPBR = (floor(metallic * 15 + 0.5) * 16 + 
                      floor(roughness * 15 + 0.5)) / 255.0;
    
    return float4(albedo.rgb, packedPBR);
}
```

### 3.3 光照合成像素着色器 (ps_3_0)

```hlsl
// 全屏 Quad 光照合成 PS
// s0: G-Buffer RT0 (Albedo + PackedPBR)
// s1: Depth 纹理 (或使用 Z-Buffer 重建)
// s2: 环境/IBL 纹理 (可选)

// 常量:
//   c0: 太阳方向 (normalized world)
//   c1: 太阳颜色 (diffuse * intensity)
//   c2: 相机位置 (world)
//   c3-c6: inverse(ViewProj)
//   c7: 环境光颜色
//   c8: 屏幕参数 (1/w, 1/h, 0, 0)
//   c9: 阴影参数

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    // 1. 采样 G-Buffer
    float4 gbuffer = tex2D(s0, uv);
    float3 albedo = gbuffer.rgb;
    
    // 2. 解包 PBR
    float packedPBR = gbuffer.a * 255.0;
    float metallic = floor(packedPBR / 16) / 15.0;
    float roughness = (packedPBR - floor(packedPBR / 16) * 16) / 15.0;
    
    // 3. 重建深度和位置
    float depth = tex2D(s1, uv).r;  // 或使用 Z-Buffer 采样
    float2 screenPos = uv * 2 - 1;
    float4 clipPos = float4(screenPos, depth, 1);
    float4 worldPos = mul(clipPos, c3);  // invViewProj
    worldPos /= worldPos.w;
    
    // 4. 法线重建 (ddx/ddy 偏导法)
    float3 normal;
    float3 dx = ddx(worldPos.xyz);
    float3 dy = ddy(worldPos.xyz);
    normal = normalize(cross(dx, dy));
    
    // 5. 标准 PBR Cook-Torrance (同现有前向着色器)
    float3 V = normalize(c2 - worldPos.xyz);
    float3 L = normalize(-c0.xyz);
    float3 H = normalize(V + L);
    
    float NdotL = max(dot(normal, L), 0);
    float NdotV = max(dot(normal, V), 0);
    float NdotH = max(dot(normal, H), 0);
    float HdotV = max(dot(H, V), 0);
    
    // GGX NDF
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1) + 1;
    float D = a2 / (3.14159 * d * d);
    
    // Schlick-GGX Geometry
    float k = (roughness + 1) * (roughness + 1) / 8;
    float G = NdotL / (NdotL * (1 - k) + k) * 
              NdotV / (NdotV * (1 - k) + k);
    
    // Schlick Fresnel
    float3 F0 = lerp(0.04, albedo, metallic);
    float3 F = F0 + (1 - F0) * pow(1 - HdotV, 5);
    
    // Specular + Diffuse
    float3 specular = D * G * F / (4 * NdotL * NdotV + 0.0001);
    float3 kD = (1 - F) * (1 - metallic);
    float3 diffuse = kD * albedo / 3.14159;
    
    // 直接光照
    float3 Lo = (diffuse + specular) * c1.rgb * NdotL;
    
    // 环境光
    float3 ambient = c7.rgb * albedo * (1 - metallic);
    
    return float4(ambient + Lo, 1);
}
```

---

## 四、实施阶段（修正版 — 10 个可测试增量）

### Phase 0: 基础枚举 + W3DDeferredRenderer 框架
**文件：** W3DCustomScene.h, W3DScene.h, W3DScene.cpp, W3DDeferredRenderer.h, W3DDeferredRenderer.cpp

```
Δ 代码量: ~180 行
验证方法: 编译通过，前向渲染不变
依赖: 无
```

1. `W3DCustomScene.h` — 枚举 +2 值
2. `W3DScene.h` — 新增 `m_gbufferMaterialPass` 成员
3. `W3DScene.cpp` — 构造函数创建禁光材质、析构函数释放
4. `W3DDeferredRenderer.h/.cpp` — 空壳类（init/shutdown/bool m_enabled）
5. `W3DDisplay.cpp` — 初始化 `TheW3DDeferredRenderer`
6. `GlobalData.h` + `GameData.cpp` — INI 开关
7. `RTS.dsp` — 注册新文件

### Phase 1: G-Buffer RT 资源管理
**文件：** W3DDeferredRenderer.cpp, W3DDisplay.cpp

```
Δ 代码量: ~200 行
验证方法: INI 启用后画面变黑（RT Clear），关闭后恢复前向
依赖: Phase 0
```

1. `init()` 创建 RT0 + Depth（跟 DX8Wrapper::Create_Render_Target 模式）
2. `releaseResources()` / `reAcquireResources()` — 设备重置
3. `beginGBufferPass()` — Set_Render_Target(rt0, depth) + Clear
4. `endGBufferPass()` — Set_Render_Target(backbuffer, depth)
5. `W3DDisplay::draw()` 中插入 clear RT 的测试代码（画面全黑 = RT 在 Clear）

### Phase 2: G-Buffer 写入（不透明物体几何 Pass）
**文件：** W3DScene.cpp (Customized_Render), W3DShaderManager.cpp, W3DDeferredRenderer

```
Δ 代码量: ~300 行
验证方法: 通过 Debug View 观察 RT0 内容（肉眼确认 Albedo 数据正确）
依赖: Phase 1
```

1. `Customized_Render` 中 `SCENE_PASS_GBUFFER` 分支
2. 地形渲染使用 G-Buffer 着色器
3. 不透明物体用 `m_gbufferMaterialPass` + G-Buffer PS
4. 透明/天空盒跳过
5. 保留 `ERF_DELAYED_RENDER` 等遮挡标志
6. G-Buffer 像素着色器（HLSL 内联字符串 → D3DXCompileShader）
7. `Flush()` 中跳过阴影/粒子（仅 flush 不透明网格）

### Phase 3: 太阳光光照 Pass（首个可见画面）
**文件：** W3DDeferredRenderer.cpp, W3DView.cpp

```
Δ 代码量: ~250 行
验证方法: 画面出现（可能有瑕疵但可辨识），前向/延迟 A/B 对比
依赖: Phase 2
```

1. 全屏 Quad 工具方法（VB + DrawPrimitive）
2. 太阳光 PS（法线重建 + PBR Cook-Torrance）
3. `W3DDisplay::draw()` 中的编排：G-Buffer Pass → 光照 Pass
4. 环境光处理
5. `PBR_RenderSunGlow()` 保持启用

### Phase 4: 透明正向回退
**文件：** W3DScene.cpp, W3DDeferredRenderer.cpp

```
Δ 代码量: ~150 行
验证方法: 透明物体（水、玻璃、烟雾）正确显示在半透明不透明之上
依赖: Phase 3
```

1. `SCENE_PASS_FORWARD_TRANSPARENT` 分支
2. 仅透明 + 天空盒使用原有前向着色器
3. 粒子在透明通道中渲染
4. `Flush()` 中透明相关调用

### Phase 5: 屏幕空间 Shroud Pass
**文件：** W3DDeferredRenderer.cpp, W3DShroud.h

```
Δ 代码量: ~150 行
验证方法: 迷雾区域正确显示在延迟渲染画面上
依赖: Phase 3
```

1. 全屏 Quad shader：读取 shroud 纹理 + G-Buffer 深度重建世界坐标
2. 应用迷雾遮罩到光照结果
3. 支持 fog_of_war 渐变

### Phase 6: 动态光 Stencil Volume
**文件：** W3DDeferredRenderer, W3DDynamicLight

```
Δ 代码量: ~300 行
验证方法: 爆炸/车灯动态光在延迟画面上出现
依赖: Phase 3
```

1. 预生成球体/圆锥包围体（Stencil Volume 几何）
2. 点光 Stencil 标记 Pass（仅渲染光体积内像素）
3. 点光 PBR 光照 Pass（读取 RT0 + Depth）
4. 聚光类似
5. CPU 光源裁剪（剔除屏幕外/距离过远的光源）

### Phase 7: 阴影集成
**文件：** W3DDeferredRenderer.cpp

```
Δ 代码量: ~100 行
验证方法: 物体阴影出现在延迟渲染画面
依赖: Phase 3 + 现有阴影系统
```

1. 在光照 Pass 中绑定阴影纹理
2. 阴影采样在太阳光 PS 中
3. 确保阴影纹理在 G-Buffer Pass 前更新（已在 W3DDisplay::draw 前置阶段）

### Phase 8: 画面一致性 + 调参
**文件：** 多个

```
Δ 代码量: ~100 行
验证方法: 正向/延迟逐像素对比误差 < 5%
依赖: Phase 4-7
```

1. PBR 参数数值匹配（GGX/Schlick 公式与前向一致）
2. 光照强度对齐
3. 环境光/阴影一致
4. 调试工具：截帧 + 像素比较

### Phase 9: 性能优化 + 全场景测试
**文件：** 多个

```
Δ 代码量: ~200 行
验证方法: 帧率、显存、稳定性达标
依赖: Phase 8
```

1. 性能基准测试（8 玩家满人口对战）
2. 显存优化（G-Buffer 纹理的 Mip 设置等）
3. 边界条件（Alt+Tab、分辨率切换、多视图）
4. 设备丢失全链路覆盖

---

## 五、文件清单（最终版）

| # | 文件 | 操作 | 预估 Δ |
|---|------|------|--------|
| 1 | `Include/W3DDevice/GameClient/W3DCustomScene.h` | 修改枚举 | +2 行 |
| 2 | `Include/W3DDevice/GameClient/W3DScene.h` | 新增成员 | +3 行 |
| 3 | `Source/W3DDevice/GameClient/W3DScene.cpp` | 构造/析构 + Customized_Render 分支 | +120 行 |
| 4 | `Source/W3DDevice/GameClient/W3DView.cpp` | draw() 编排（可选） | +10 行 |
| 5 | `Source/W3DDevice/GameClient/W3DDisplay.cpp` | init + 设备重置 + draw() 编排 | +80 行 |
| 6 | `Include/W3DDevice/GameClient/W3DDeferredRenderer.h` | **新建** | +200 行 |
| 7 | `Source/W3DDevice/GameClient/W3DDeferredRenderer.cpp` | **新建** | +1500 行 |
| 8 | `Source/W3DDevice/GameClient/W3DShaderManager.cpp` | G-Buffer PS 编译 | +50 行 |
| 9 | `Include/W3DDevice/GameClient/W3DShaderManager.h` | 新 shader 声明 | +5 行 |
| 10 | `Include/Common/GlobalData.h` | INI 配置 | +5 行 |
| 11 | `Source/Common/GameData.cpp` | INI 解析 | +10 行 |
| 12 | `RTS.dsp` | 注册新文件 | +2 行 |
| | **C++ 总计** | | **~2000 行** |
| | **HLSL 内联** (G-Buffer PS + Lighting PS + Shroud PS) | **新建** | **~600 行** |
| | **最终总计** | | **~2600 行** |

---

## 六、工期估算（修正版）

| Phase | 内容 | 单人工期 |
|-------|------|---------|
| 0 | 枚举 + 空壳框架 | 0.5 天 |
| 1 | G-Buffer RT 资源管理 | 1 天 |
| 2 | G-Buffer 写入 | 2 天 |
| 3 | 太阳光光照 Pass | 2 天 |
| 4 | 透明正向回退 | 1 天 |
| 5 | 屏幕空间 Shroud | 1 天 |
| 6 | 动态光 Stencil | 2 天 |
| 7 | 阴影集成 | 0.5 天 |
| 8 | 画面一致性 | 2 天 |
| 9 | 性能优化 + 测试 | 2 天 |
| | **合计** | **~14 个工作日** |

---

## 七、核心风险与应对（最终版）

| 风险 | 等级 | 概率 | 应对 |
|------|------|------|------|
| 法线重建质量不可接受 | P1 | 中 | 备选：G-Buffer 存法线（2 Pass），必要时切回 |
| DX9 ps_3_0 兼容问题 | P0 | 低 | 前向 Fallback 完整保留 |
| 显存不足（+6MB@1080p） | P2 | 低 | 动态分辨率降级 |
| 迷雾系统延迟路径 Bug | P0 | 中 | 独立测试，与正向对比 |
| Stencil 遮挡与灯光冲突 | P1 | 中 | 分阶段：GBuffer Pass 中不写 Stencil，光 Pass 写 |
| 多视图 G-Buffer 冲突 | P1 | 中 | 每个 View 独立 G-Buffer 或单帧序列化 |
| VC6 编译 HLSL 内联字符串 | P2 | 低 | 长字符串拆分拼接，避免反斜杠问题 |
| 水反射渲染时状态污染 | P1 | 中 | 保存/恢复 G-Buffer RT 状态 |
