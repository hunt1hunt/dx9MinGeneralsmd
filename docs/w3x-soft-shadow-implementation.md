# W3X 模型悍马软阴影实现 — 代码路径与要点

> 日期：2026-08-07
> 状态：已实现（悍马 W3X 模型软阴影可见，用户确认）
> 对应提交：`feat: W3X Hummer soft shadow — projected ground shadow (SHADOW_PROJECTION)`

## 1. 背景

W3X 是 RA3 模型格式（经 pugi::xml 解析的 XML 网格 + D3D9 VB/IB 渲染）。此前 W3X 模型完全没有阴影：

1. **延迟渲染阴影贴图（deferred shadow map）路径失败** —— 在 dgVoodoo2 下把 W3X 光栅化进阴影 color RT 得到 **0 像素**（SHADOWDBG 回读 R-min=1.0，即整张图只有清屏的深度 1.0）。
2. 悍马等单位的 `ThingTemplate` 本就有阴影配置（`Shadow = SHADOW_PROJECTION`），但 `W3XModelDraw` 从未把渲染对象接入引擎的 `TheW3DShadowManager`。

## 2. 最终方案（一句话）

> 悍马 W3X 模型改走 **引擎原生 W3D 投影软阴影（`SHADOW_PROJECTION`，即 `W3DProjectedShadow` 地面软影 blob）**，与标准 W3D 单位完全一致；同时在 W3X 的 `Render()` 中 **完全跳过延迟阴影贴图 pass**（早期 return），避免其污染后续对象的状态。**不用体阴影（volumetric）**——W3X 开放子网格部件会让 silhouette 缓冲溢出；**不用延迟阴影贴图**——dgVoodoo2 下光栅化为 0 像素。

## 3. 代码实现路径（调用链）

```
Drawable 生命周期 (Options 开关 / 创建)
  ├─ W3XModelDraw::allocateShadows()
  │    └─ TheW3DShadowManager->addShadow(m_renderObj, &shadowInfo)
  │         shadowInfo.m_type = (ShadowType)tmplate->getShadowType()  // SHADOW_PROJECTION
  │         sizeX/sizeY/offsetX/offsetY/texName 全部取自 ThingTemplate
  │         → 创建 W3DProjectedShadow（地面软影）
  ├─ W3XModelDraw::releaseShadows()     → m_shadow->release(); m_shadow=NULL
  ├─ W3XModelDraw::setShadowsEnabled()  → m_shadow->enableShadowRender(enable)
  └─ W3XModelDraw::setFullyObscuredByShroud() → m_shadow->enableShadowInvisible(...)

W3XRenderObjClass::Render()
  ├─ 检测 COLORWRITEENABLE==0  → 判定处于延迟阴影 pass
  │    └─ return;  // 立即跳过，W3X 不再往阴影贴图里画
  └─ 主 pass：Default technique，正常 PBR 渲染
```

**阴影足迹来源**：`W3XModelDraw::createRenderObject()` 用各子网格从 `.w3x <BoundingBox>` 解析出的 AABB 求并集 → `robj->SetBounds(bmin, bmax)`。`W3DProjectedShadow::updateBounds()` 投影这个包围盒计算阴影范围。**此前硬编码 ±100 大方块会让阴影盖住大半地图、撑爆 4096 顶点阴影缓冲而渲染空白。**

## 4. 各文件改动要点

### 4.1 W3XModelDraw.h / .cpp —— 接入阴影生命周期（核心）
- 新增成员 `Shadow *m_shadow`、`Bool m_shadowEnabled`。
- 新增三个虚函数 `allocateShadows / releaseShadows / setShadowsEnabled`（镜像 `W3DModelDraw`），由 `Drawable::allocateShadows/releaseShadows/setShadowsEnabled`（Options 屏幕）调用。
- 析构函数调用 `releaseShadows()`，避免泄漏。
- `doDrawModule()`：渲染对象创建后 `Set_Name(targetModel)` + 若阴影尚未分配则补 `allocateShadows()`（因为 `Drawable::allocateShadows()` 可能先于模型构建执行）。
- `setFullyObscuredByShroud()` 同步阴影显隐。
- 包围盒改为 AABB 并集（见 §3），带退化回退（缺 BoundingBox 时退回 ±100）。

### 4.2 W3XRenderObj.h / .cpp —— 渲染对象能力（核心）
- 新增 `enum { CLASSID_W3X = 0x00010001 }`，`Class_ID()` 返回它 → 供 `W3DShadowGeometryManager::Load_Geom` 路由。
- 新增子网格访问器 `GetSubMeshCount / GetSubMeshVB / GetSubMeshIB / GetSubMeshVertexCount / GetSubMeshTriangleCount`（给体阴影几何构建读 VB/IB）。
- **真实名字**：基类 `Get_Name()` 恒返回 "UNNAMED"，`Set_Name` 是 no-op，会让所有 W3X 模型共享一份阴影几何缓存 → 重写为 `m_name[64]` 存储真实模型名。
- `Render()`：
  - 通过 `D3DRS_COLORWRITEENABLE==0` 检测阴影 pass，**早期 return 跳过**，避免把 DS=NULL / ZEnable=0 / 2048 viewport / scissor 等状态泄漏给后续对象（曾导致 War Factory 崩溃）。
  - 主 pass 绑定 `ShadowMap` 真实阴影贴图采样器（有则真实贴图，无则 1x1 白色），`HasShadow`、`Shadowmap_Zero_Zero_OneOverMapSize...` 一并设置。
  - 重构出共享绑定辅助函数：`BindW3XMatrices / BindW3XBones / BindW3XConstants`，支持**逐子网格 shader 覆盖**（如履带子网格走 `w3x_tread.fx`，`SetSubMeshShader`）。
  - 子网格纹理**逐网格重绑**（否则第一个 PBR 子网格的 DiffuseTexture 泄漏到其它子网格 → 迷彩错位）。

### 4.3 W3DDeferredRenderer.h / .cpp —— 延迟阴影贴图管线修复（W3D 单位阴影 + W3X 阴影采样）
> 注意：这是为 W3X 软阴影落地而同步修复的**地基** —— W3X 本身不再画进阴影贴图，但 W3D 单位（建筑/工程车）与 W3X 主 pass 的阴影采样都依赖它。
- **阴影贴图改为 color RT**（A8R8G8B8，1024/2048），把太阳空间深度**以颜色形式**写入；D16 深度纹理在 dgVoodoo2 下不可靠采样。
- **新增 `m_shadowDepthSampler`**：每帧 `endShadowMapPass` 用 `StretchRect` 把 color RT 拷贝进普通 sampler 纹理，主 pass 采样这份已解析拷贝（RT→SRV 在 dgVoodoo2 下可靠），`getShadowMapTexture()` 优先返回它。
- `beginShadowMapPass(sunDir, camView, camPos)` 新增 `camPos` 参数：阴影正交相机**以玩家相机为中心**（RA3 风格，覆盖可见区域而非世界原点）；正交尺寸 300→1000，近远 0.1→2000。
- **`sunDir` 归一化保护**：零长度 sunDir 会让 `D3DXMatrixLookAtLH` 退化（row0 全 0 → 所有顶点塌缩到原点 → 空阴影图）。
- 清屏值必须为 `(1,1,1)`（= 深度 1.0 远处），否则 W3D 建筑/单位阴影全被破坏。
- `getShadowRTSurface()`：返回阴影 color RT 表面（新引用），供 W3X 阴影 pass 重绑 RT。
- 诊断：`DebugDumpShadowMap`（第 60 帧一次性回读阴影图，统计 R 通道 min/max/mean，区分“没画进去” vs “画了但采样失败”）+ `HalfToFloat`（半浮点回读）。

### 4.4 W3DScene.cpp —— 太阳方向唯一真源
- 阴影相机的 `sunDir` 从 `m_globalLight[0]->Get_Position()` **改为** `-TheGlobalData->m_terrainLightPos[0]`（与 W3X PBR shader 的光照方向同源），**且取负**（光照位置 ≠ 光线方向）。原写法读的是 `setTimeOfDay` 留在 transform 平移里的 (0,0,0)，导致零 sunDir 塌缩；取负反了则阴影相机偏离 180°。

### 4.5 W3DVolumetricShadow.h / .cpp —— 体阴影对 W3X 的适配（支撑性，非最终路径）
- `initFromW3X(RenderObjClass*)`：从 W3X 子网格 VB/IB 读回顶点/索引，构建体阴影几何（顶点去重、三角数组、`m_ownsShadowArrays` 负责释放）。被 `Load_Geom` 的 `CLASSID_W3X` 分支路由。
- `allocateSilhouette(meshIndex, numVertices, numPolygons)`：分配量从 `numVertices*5` 改为 `max(numVertices*5, numPolygons*6)`——开放网格每条边都是边界边，旧值会**下溢缓冲导致内存破坏**。
- 注意：`W3XModelDraw::allocateShadows` 明确选择**不用**体阴影（开放子网格部件溢出），`initFromW3X` 保留作能力支撑。

### 4.6 w3x_loader.h / .cpp —— 解析增强（支撑 PBR/阴影）
- `ComputeFallbackTangents()`：文件缺 TBN 时用 **Lengyel 方法**（位置+UV+三角形）生成切线与副切线，按 RA3 的 T-B 交换约定存储（binormal=+U 轴、tangent=−V 轴），否则 RA3 PBR shader 的 bump-normal 扰动失效。
- BoundingBox 解析前**先零初始化**，避免无节点时未初始化垃圾/NaN 泄漏进剔除与阴影几何。

### 4.7 W3DShaderManager.cpp —— 跨库法线贴图查询
- 新增 C 链接导出 `PBR_HasNormalMap / PBR_BindNormalMap`（供 dx8renderer.cpp 跨库访问 `_n.dds` 法线贴图），与既有 `PBR_BindIBL` 同模式。

### 4.8 W3XEffectManager.cpp
- 仅移除 `GetEffect` 缓存命中处的冗余日志（非功能性）。

## 5. 关键要点 / 避坑清单

1. **W3X 阴影 = 引擎原生投影软阴影，不是自研**：复用 `TheW3DShadowManager` + 模板阴影配置，数据驱动，天然“软”（模糊 blob），与全游戏一致。
2. **延迟阴影贴图对 W3X 是死路**（dgVoodoo2 光栅化 0 像素）→ 在 `Render()` 里检测并**早期 return 跳过**，同时避免状态泄漏导致其他单位阴影损坏/崩溃。
3. **包围盒决定阴影**：`W3DProjectedShadow` 靠对象包围盒投影算足迹。必须用 `.w3x <BoundingBox>` 的真 AABB 并集，硬编码大方块会撑爆阴影缓冲。
4. **名字必须真实**：阴影几何按 `Get_Name()` 缓存，基类恒返回 "UNNAMED" → 所有 W3X 共模；重写 `Set_Name/Get_Name` 按模型名缓存。
5. **太阳方向同源且取负**：`-m_terrainLightPos[0]`，否则阴影与光照方向错位/塌缩。
6. **阴影贴图是 color RT + StretchRect 采样拷贝**：D16 深度纹理在 dgVoodoo2 不可靠采样；RT 直接采样不可靠，必须解析拷贝。
7. **阴影相机以玩家相机为中心**（camPos），正交 1000×1000，才能覆盖可见区域。
8. **清屏值 = 深度 1.0（白）**，错则 W3D 单位阴影全毁。
9. 逐子网格 shader/纹理重绑，防履带等特殊子网格特效丢失与贴图泄漏。

## 6. 涉及文件清单

| 文件 | 改动性质 |
|---|---|
| `Include/.../Module/W3XModelDraw.h` + `Source/.../Drawable/Draw/W3XModelDraw.cpp` | 核心：阴影生命周期接入 + 真包围盒 |
| `Include/.../W3XRenderObj.h` + `Source/.../W3XRenderObj.cpp` | 核心：CLASSID_W3X / 真名字 / 阴影 pass 跳过 / 逐网格绑定 |
| `Include/.../W3DDeferredRenderer.h` + `Source/.../W3DDeferredRenderer.cpp` | 阴影贴图 color RT + sampler 拷贝 + camPos 正交相机 |
| `Source/.../W3DScene.cpp` | sunDir 取 `-terrainLightPos[0]` |
| `Include/.../W3DVolumetricShadow.h` + `Source/.../Shadow/W3DVolumetricShadow.cpp` | initFromW3X + silhouette 缓冲修复（支撑） |
| `Include/.../w3x_loader.h` + `Source/.../w3x_loader.cpp` + `Source/.../w3x_loader.h` | 切线回退计算 + BoundingBox 零初始化（支撑） |
| `Source/.../W3DShaderManager.cpp` | PBR_HasNormalMap/BindNormalMap C 导出（支撑） |
| `Source/.../W3XEffectManager.cpp` | 日志清理 |

## 7. 验证

- 悍马 W3X 模型地面软阴影（投影 blob）已可见，**用户确认已实现**。
- 阴影 pass 早期 return 后，War Factory 崩溃与建筑/工程车阴影损坏问题随之消失。
- 未执行编译验证（本会话不构建）；构建类型：`DebugW3D` / `Debug`（GeneralsMD GameEngineDevice）。
