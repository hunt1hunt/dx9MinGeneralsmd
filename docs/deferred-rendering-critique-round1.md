# 延迟渲染计划 — 第1轮检讨：系统级差距分析

> 基于对 SAGE 引擎 W3DScene、W3DView、W3DDisplay、dx8wrapper、W3DShaderManager、W3DDynamicLight、shader.h、matpass.h、W3DShroud 等关键文件的深入代码走读
> 日期: 2026-07-14

---

## 一、绝对缺失项（可能导致改造失败的严重遗漏）

### 1.1 迷雾/战争迷雾系统（SHROUD）完全未被提及

**现状：** 迷雾通过 `W3DShroudMaterialPassClass` 实现，这是一个自定义 `MaterialPassClass`，在 `Customized_Render` 中通过 `Push_Material_Pass(m_shroudMaterialPass)` 应用到地形和物体渲染。它不是后处理，而是在每个物体绘制时通过纹理投影叠加。

**延迟渲染中的问题：**
- G-Buffer Pass 中 shroud 如何写入？无法用 `m_gbufferMaterialPass` 兼容
- 如果 G-Buffer 不包含 shroud 信息，光照合成后的画面将没有迷雾
- 必须设计屏幕空间 shroud 作为后处理，或者 forward transparent pass 中处理
- **影响：所有 RTS 游戏模式（单人战役、多人对战）都会黑屏或透视**

**解决方案方向：**
- 新增一个屏幕空间 shroud Pass：利用 G-Buffer 深度重建世界坐标 → 查询 shroud 纹理 → 对光照结果进行雾化/遮罩
- 或者将 shroud 作为 forward transparent pass 的最后一步全屏 quad

### 1.2 模版遮挡剔除系统（Stencil Occlusion）完全未被提及

**现状：** RTS3DScene 有极其复杂的模版遮挡系统：
- `flagOccludedObjects()`：CPU 射线检测判定哪些物体被遮挡
- `flushOccludedObjectsIntoStencil()`：用 4 重 Pass（potentially occluded objects → non-occluders → occluders → player color overlay）在 stencil buffer 中编码遮挡信息
- Stencil 缓冲区被共享给：玩家颜色索引（bit 3-6）、遮挡标记（MSB）、阴影体积（低位 bit）
- `m_occludedMaterialPass[MAX_PLAYER_COUNT]`：每个玩家一个材质通道用于渲染被遮挡物体的半透明颜色

**延迟渲染中的问题：**
- G-Buffer Pass 中必须正确处理遮挡关系，否则 G-Buffer 中会出现本应被遮挡的物体
- 如果没有遮挡剔除，G-Buffer 会包含大量不可见像素，浪费带宽
- Stencil 缓冲区在 GBuffer Pass 中被用于写入操作会被遮挡系统污染
- 当前遮挡系统深度绑定在 `Customized_Render` 的主循环中

### 1.3 地形渲染的特殊性被忽略

**现状：** 地形 (`HeightMapRenderObjClass` / `BaseHeightMapRenderObjClass`) 有以下特殊机制：
- 每帧通过 `m_dynamicLightList` 进行逐顶点动态光照（地形有自己的光照路径）
- 地形渲染使用 `On_Frame_Update()` 中的动态光更新
- 地形渲染受 `m_shroudMaterialPass` 影响（地形先画，迷雾叠加在地形上）
- 地形可能使用多纹理混合、细节纹理、高度混合等复杂着色技术

**延迟渲染中的问题：**
- 简单 `m_gbufferMaterialPass` 覆盖对地形无效——地形使用自己的材质路径
- 地形需要自己的 G-Buffer 着色器，这不是简单 PS 替换能解决的
- 地形在前向中已经 self-lit（`rinfo.light_environment = NULL`），G-Buffer 方案需要不同处理

---

## 二、架构级遗漏

### 2.1 多视图支持未设计

**现状：** 一个 `W3DDisplay` 持有 `m_3DScene`，但是 `W3DView::draw()` 可以存在多个实例。回放、观察者模式都可能创建额外视图。

**问题：** `TheW3DDeferredRenderer` 如果是全局单例，在每帧调用 `W3DDisplay::draw()` 时，每个视图都会触发 `Customized_Render`。这意味着 G-Buffer 会在同帧内被多次覆盖。每个视图需要独立的 G-Buffer，或者 deferred renderer 需要管理视图索引。

### 2.2 水反射与延迟管线的冲突未分析

**现状：** 水面渲染流程：
1. 先调用 `updateRenderTargetTextures()` — 这会将场景渲染到反射纹理（使用前向管线）
2. 主渲染循环中水面在 forward transparent pass 绘制

**问题：** 水反射渲染时，G-Buffer RTs 不应被绑定——它们会被覆盖。反射渲染完成后需要恢复 G-Buffer 状态。

### 2.3 W3DView Filter 交互未考虑

**现状：**
- W3DView 有 `m_viewFilter` 系统（FT_NULL_FILTER, FT_VIEW_DEFAULT 等）
- `W3DShaderManager::filterPreRender()` 和 `filterPostRender()` 可能会设置 `customScenePassMode` 并做额外的 RT 重定向
- 在 filter 模式下，场景可能被渲染到纹理中

**问题：** filter 模式当前期望前向渲染结果。延迟渲染和 filter 的交互需要明确定义。有些 filter（如热成像）可能需要特殊的 G-Buffer 读取 shader。

### 2.4 阴影映射与延迟光照的结合未设计

**现状：** 
- `TheW3DShadowManager->queueShadows(TRUE)` 在 Customized_Render 末尾调用
- 阴影映射生成阴影纹理
- 前向着色器在逐物体光照阶段采样阴影纹理

**问题：** 延迟光照的太阳光 Pass 需要采样阴影贴图。这意味着阴影贴图必须先在 G-Buffer Pass 之前或之间完成渲染。但现有代码在 Customized_Render 末尾才排队阴影渲染。

---

## 三、精度与质量风险

### 3.1 8-bit 线性深度的严重精度不足

**分析：** 计划在 RT2.a 中存储 8-bit 线性深度。RTS 游戏视距可达 5-10km。对于 8km 视距：
- 256 个深度级 → 每级 31 米
- 这意味着相距 30 米的两个物体会获得相同深度值
- 近处单位（坦克、步兵，距离几米）完全无法区分前后
- 位置重建精度崩溃

**建议：** 
- 使用 24-bit 深度缓冲（Z-buffer）重建位置，这是标准做法
- RT2.a 存储编码后的深度（logarithmic 或 float 编码），用 8-bit 只能作为辅助
- 真正的解决方案：不存深度到 G-Buffer，仅通过 full-res Z-buffer + screenUV 重建
- 或者使用多段深度编码（前近后粗）

### 3.2 8-bit 法线编码精度

**分析：** RT1 中法线编码为 `normal*0.5+0.5` 使用 8-bit。对于高光反射，法线质量直接影响高光形状。8-bit 法线（256 级/分量）的量化误差在高光区域会产生可见条带。

**缓解方案：** 
- 使用 ddx/ddy 偏导重建法线（如果 G-Buffer 存深度）
- 或者接受此精度但需要后期去带（debanding）

### 3.3 着色器指令数风险

**分析：** 计划中的太阳光 PBR 着色器包含：
- 3 次 G-Buffer 纹理采样
- 1 次阴影贴图采样
- 完整 Cook-Torrance BRDF（GGX NDF + Schlick-GGX Geometry + Schlick Fresnel）
- 世界坐标重建（矩阵乘法）
- 环境光 + IBL

**风险：** ps_3_0 的指令限制是 512 算术 + 512 纹理。普通 BRDF 约 60-80 条指令，加上位置重建（~20 条）和 IBL（~50 条），勉强够用。但如果点光/聚光着色器也包含完整 BRDF，加上光源衰减计算，可能超标。

---

## 四、集成漏洞

### 4.1 INI 开关与现有 PERF 系统的冲突

**风险：** `GlobalData.h` 中已有大量 `m_perf*` 渲染控制选项。新加 `m_useDeferredRendering` 可能与 `m_perf*` 配置冲突或不一致。

### 4.2 设备丢失处理链不完整

**分析：** 从现有代码看，设备丢失/重置散布在多个地方：
- `W3DShadowManager::ReleaseResources()` → 递归调用 `VolumetricShadow` 和 `ProjectedShadow`
- `BaseHeightMap::ReleaseResources()` → 释放树/桥/地形资源
- `W3DShroud::ReleaseResources()` → 释放迷雾纹理

**风险：** `W3DDeferredRenderer` 必须注册到设备丢失处理链中，在所有释放/重建点都被正确调用。

### 4.3 DX8Wrapper MRT 抽象缺失

**分析：** 现有的 `DX8Wrapper` 封装类可能没有 MRT 支持。需要检查：
- `DX8Wrapper::Set_Render_Target()` 是否支持索引参数
- `DX8Wrapper` 是否有 `SetRenderTarget(0..N)` 的方式
- 直接调用 `IDirect3DDevice9::SetRenderTarget()` 会绕过 DX8Wrapper 的状态跟踪

### 4.4 m_gbufferMaterialPass 对复杂材质无效

**分析：** 计划通过 `Push_Material_Pass(m_gbufferMaterialPass)` 让所有不透明物体使用 G-Buffer 着色器。但这对于以下情况无效：
- 使用自定义纹理混合的物体（多层材质）
- 使用法线贴图的物体（需要 decode normal in PS）
- 使用 alpha test 的物体（需要保留 alpha test）
- 非标准 W3D 材质（如粒子的点精灵渲染）

---

## 五、流程与测试遗漏

### 5.1 增量验证策略缺失

**问题：** 计划没有定义"功能完成的最小可测试单元"。实施 3-5 天后才能看到第一个画面，风险过高。

**建议的增量里程碑：**
1. 编译通过 + 前向回退（INI 开关工作，显示不变）
2. G-Buffer RTs 创建 + Clear → 显示纯色画面（验证 RT 分配）
3. 单个不透明物体写入 G-Buffer → 读回验证数据正确性
4. 全屏 Quad 显示 G-Buffer RT0 内容（肉眼验证）
5. 太阳光光照 Pass 输出画面
6. 动态光 Stencil Volume
7. 透明 forward 回退

### 5.2 画面一致性 A/B 对比方案缺失

**问题：** 没有定义如何验证延迟渲染的画面质量和前向一致。计划说"RGB 差异 < 5%"但未给出测量方法。

### 5.3 边界条件测试缺失

- Alt+Tab 切换
- 窗口↔全屏切换
- 分辨率动态修改
- 使用 spectator/replay 多视图模式
- 超宽屏（多 monitor）？
