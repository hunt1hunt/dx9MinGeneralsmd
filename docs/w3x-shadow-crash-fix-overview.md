# W3X 阴影管线修复 — 最终改动总览（存档）

> 日期：2026-08-07
> 状态：W3X 悍马软阴影已实现（体阴影 SHADOW_VOLUME）；RA3 shadow maps 残留已清理；建筑散乱 + 盖特林坦克崩溃 + 推土机/悍马移动崩溃 三处已修复
> 对应提交：`feat: W3X shadow pipeline — RA3 shadow maps cleanup + volumetric shadow crash fixes`
> 前置提交：`882011be feat: W3X Hummer soft shadow`

## 1. 背景与目标

W3X 悍马软阴影此前已走**原生体阴影（SHADOW_VOLUME → W3DVolumetricShadow）**实现并生效（见 docs/w3x-soft-shadow-implementation.md）。随后发现并修复了三个问题：

| # | 问题 | 根因 | 状态 |
|---|---|---|---|
| 1 | 建筑阴影散乱 | 体阴影 `getSlot` 的 `sizeIndex` 越界读缓冲池表 → 垃圾槽 → 体积错乱 | ✅ 修复 |
| 2 | 盖特林坦克（W3X）进入遭遇战崩溃 | `getSlot` 返回 NULL 后 `constructVolumeVB` 解引用 NULL | ✅ 修复 |
| 3 | 推土机/悍马移动一阵后崩溃 | 体阴影重建时静态 VB/IB 槽泄漏 → 槽数组耗尽 → `allocateSlotStorage` 越界 | ✅ 修复 |

## 2. 改动文件清单（7 个，净 -462/+195 行）

### 2.1 W3XRenderObj.cpp（-440 行）— RA3 shadow maps 残留清理
- 删除约 280 行死代码：阴影 pass 检测后的不可达块（阴影矩阵读取/诊断/ShadowDepth technique/150 行设备状态强制/OcclusionQuery）
- 保留干净的阴影 pass 早期 return（W3X 走体阴影，不写延迟贴图）
- `shadowW2S` 精简为恒等矩阵；主 pass 不再绑定 ShadowMap/HasShadow 采样器；Shroud/Cloud 白色回退保留

### 2.2 W3DDeferredRenderer.h/.cpp（-14/-20 行）— 删除仅 W3X 用的 API
- 删除 `getShadowView / getShadowProj / getShadowViewProj / getShadowMapTexture`
- 延迟阴影贴图基础设施（begin/endShadowMapPass、sunLightPass、D24X8）完整保留

### 2.3 W3DVolumetricShadow.cpp（+133 行）— 体阴影加固 + 崩溃修复
- **`W3DShadowGeometry` 构造**初始化 `m_meshCount/m_numTotalsVerts`；`initFromHLOD/initFromMesh` 加 `m_meshCount=0`
- **`W3DShadowGeometryMesh` 构造**初始化全部 12 个字段
- **`SetGeometry`** 重分配条件从 `numNewVertices>numPrevVertices` 改为按所需容量 `max(5V,6P)` 判断
- **`constructVolumeVB`**：① getSlot 后立即 NULL 处理（防解引用）；② **重建前释放旧 VB/IB 槽**（治泄漏）；③ 一次性诊断日志
- `buildSilhouette` 一次性 silhouette 容量诊断

### 2.4 W3DBufferManager.h/.cpp（+43 行）— 缓冲池稳定
- **`getSlot`(VB/IB) 越界保护**：`size<=0 || sizeIndex>=MAX` 返回 NULL（防越界读）
- **`allocateSlotStorage`(VB/IB) 槽位耗尽保护**：`m_numEmptySlotsAllocated < MAX_NUMBER_SLOTS` 才新建缓冲（防 `[4096]` 越界）
- **`MAX_IB_SIZES` 128→2048**（覆盖 65536，匹配原注释意图）、**`MAX_VB_SIZES` 128→256**（覆盖 8192）—— 让大 silhouette 模型（盖特林坦克 6240 索引）能分配体积

### 2.5 W3DScene.cpp（+7 行）— 阴影 pass 对象计数诊断（一次性）

## 3. 三个崩溃的根因链（诊断依据：E:\GeneralsMD_DeferredRT.log + DebugLogFileI.txt）

### Bug 1：建筑散乱（getSlot 越界）
```
constructVolumeVB 计算 polygonCount → getSlot(polygonCount*3)
sizeIndex = (size>>5)-1 超过 MAX_IB_SIZES=128（最大 4096）→ 越界读 m_W3DIndexBufferSlots[128] → 垃圾槽 → 体积写到垃圾缓冲 → 散乱
```
日志：`getSlot(IB): size=6240 sizeIndex=194 MAX=128 -> too large, skip`

### Bug 2：盖特林坦克崩溃（NULL 解引用）
```
Bug 1 修复后 getSlot 返回 NULL → 但 constructVolumeVB 在 if(!ibSlot) 检查【之前】先解引用了 ibSlot->m_size → 崩溃
```
日志：`ASSERTION FAILURE: Can't allocate index buffer slot` + `W3DVolumetricShadow.cpp(3090)`

### Bug 3：推土机/悍马移动崩溃（槽泄漏 + 越界）
```
单位爬坡（isLightMoving 非旋转）→ 阴影重建 → constructVolumeVB 覆盖旧槽不释放 → 每次泄漏 2 槽
→ ~2048 次后 m_numEmptySlotsAllocated=4096 → allocateSlotStorage 新建缓冲写 EmptySlots[4096] 越界 → 垃圾槽 → 崩溃（≈1分钟@30fps）
```
（`DEBUG_ASSERTCRASH(m_shadowVolumeVB==NULL,...)` 本应防此，Release 下关闭）

## 4. 诊断输出（日志标记，供后续排查）

| 标记 | 位置 | 含义 |
|---|---|---|
| `[W3X_SIL_DIAG] SetGeometry '..' meshes=N` | DebugLog | 体阴影几何绑定（捕捉 m_meshCount 未初始化） |
| `[W3X_SIL_DIAG] mesh[N] silhouette used/max` | DebugLog | silhouette 容量使用（捕捉溢出） |
| `[W3X_SIL_DIAG] constructVolumeVB mesh[N] indices/vertexCount/polygonCount` | DebugLog | 体积计数（判断巨大 vs 垃圾） |
| `[W3X_SIL_DIAG] getSlot(VB/IB): size=.. too large, skip` | DebugLog | 超容量被跳过 |
| `SHADOW_PASS: rendered N objects` | DeferredRT | 阴影 pass 光栅化对象数 |
| `SHADOWDBG: shadow map .. R-min=..` | DeferredRT | 阴影 color RT 内容（1.0=空，CWE=0 证实） |
| `STEP1: Shadow PS activated.` | DeferredRT | 延迟光照使用 SunLightShadow PS（采样 D24X8） |

## 5. 当前状态与验证

**已验证**（用户构建 DebugW3D/Debug 后）：
- ✅ 建筑阴影散乱消失
- ✅ 推土机、悍马均有阴影
- ✅ 盖特林坦克进入遭遇战不崩
- ✅ 悍马软阴影（体阴影）正常

**待验证**（最新修复后需再构建）：
- 推土机/悍马**长时间移动/爬坡不崩溃**（槽泄漏 + 越界修复）

## 6. 下一步（新对话接手点）

1. **再构建验证移动不崩**（本次最后修复：槽释放 + 耗尽保护）
2. **延迟阴影贴图 Phase 2（未做）**：`sunLightPass` 把 D24X8 深度模板纹理当颜色采样（s4）在 D3D9 非法；color RT 因 `COLORWRITEENABLE=0` 恒空（SHADOWDBG 1.0 证实）。若想让 W3D 建筑有正确动态阴影，需完成 color-depth 设计（阴影 pass 写 depth-as-color + sunLightPass 采样 resolved 拷贝），或禁用延迟阴影贴图（建筑只用体阴影）
3. 后续可清理 W3X 残留的 `w3x_soviet.fx` ShadowDepth technique（已无调用方）

## 7. 关联文档
- `docs/w3x-soft-shadow-implementation.md` — W3X 悍马软阴影实现路径
- `docs/deferred-rendering-*.md` — 延迟渲染管线计划/修复
