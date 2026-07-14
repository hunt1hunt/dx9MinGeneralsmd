# 延迟渲染改造 — 完整工程计划

> 本文件整合了前置改造（DX8Wrapper→DX9Wrapper + MRT 支持）与原延迟渲染改造的所有阶段，
> 形成一份完整的、按顺序可执行的工程计划。
>
> 前身文档（已归档，内容被本文件完全覆盖）：
> - `deferred-rendering-plan.md` — 原始计划（被替代）
> - `deferred-rendering-plan-FINAL.md` — 最终审定稿（已被整合）
> - `dx9wrapper-plan-FINAL.md` — 前置改造计划（已被整合）

---

## 一、总体架构

### 技术栈现状

```
d3d8compat.h — 零开销 typedef 层（IDirect3DDevice8 → IDirect3DDevice9 等）
    ↓
DX8Wrapper     — 4000 行状态管理/设备生命周期/纹理缓存（底层已是 D3D9）
    ↓
IDirect3DDevice9 — 实际 GPU 接口
```

### 改造目标

| 层 | 改什么 | Δ 代码 | 工期 |
|---|--------|--------|------|
| **前置** | DX8Wrapper 加 MRT + 解除 POT 限制 | ~105 行 | **0.5 天** |
| **核心** | 3-RT G-Buffer + 延迟光照 + 透明回退等 | ~2600 行 | **~14 天** |
| **收尾** | 改名 DX8→DX9（纯符号，可选） | ~750 处 | 后期单独做 |

---

## 二、G-Buffer 布局

```
RT0 (A8R8G8B8): Albedo(RGB) + Metallic(A)          ← 8-bit 全精度
RT1 (A8R8G8B8): WorldNormal*0.5+0.5(RGB) + Roughness(A)  ← 法线贴图保留
RT2 (A8R8G8B8): Emissive(RGB) + LinearDepth(A)     ← 自发光 + 辅助深度

位置重建: screenUV + LinearDepth + inverse(ViewProj) → worldPosition
总带宽: 3 × 4 字节 × 分辨率 = 12 字节/像素 (1920×1080 = 24.9MB/帧)
```

---

## 三、完整渲染管线数据流

```
W3DDisplay::draw()
  │
  ├── [前置 RT Passes] (不变)
  │     ├── 水反射纹理更新
  │     └── 阴影纹理更新
  │
  ├── [判定: 启用延迟渲染?]
  │     条件: MRT(3+) 支持 && INI启用 && 非滤镜模式 && 非线框
  │
  ├── YES → [延迟渲染路径]
  │     │
  │     ├── Step A: G-Buffer Pass (3-RT MRT)
  │     │     Set_Render_Target(0, rt0)  // Albedo+Metallic
  │     │     Set_Render_Target(1, rt1)  // Normal+Roughness
  │     │     Set_Render_Target(2, rt2)  // Emissive+Depth
  │     │     Clear(3 RTs + DepthStencil)
  │     │     RTS3DScene::doRender(SCENE_PASS_GBUFFER)
  │     │       → 不透明物体 → G-Buffer PS → MRT
  │     │       → 透明/天空盒: continue
  │     │       → 地形: 使用地形 G-Buffer 着色器
  │     │     endGBufferPass() → 恢复单 RT
  │     │
  │     ├── Step B: 太阳光光照 Pass (全屏 Quad)
  │     │     Set_Render_Target(0, BackBuffer)
  │     │     绑定 RT0/RT1/RT2 作为纹理 s0/s1/s2
  │     │     设置 PBR 常量 (c0-c11)
  │     │     DrawPrimitive(全屏四边形)
  │     │
  │     ├── Step C: 动态光 Pass (Stencil Volume)
  │     │     对每个有效动态光:
  │     │       SetStencil(光体积内)
  │     │       Draw(光体积包围体)
  │     │
  │     ├── Step D: 屏幕空间 Shroud Pass (全屏 Quad)
  │     │     绑定 shroud 纹理 → 对光照结果迷雾遮罩
  │     │
  │     ├── Step E: 自发光合并
  │     │     从 RT2 读取 Emissive → additive 到 BackBuffer
  │     │
  │     ├── Step F: 正向透明 Pass
  │     │     RTS3DScene::doRender(SCENE_PASS_FORWARD_TRANSPARENT)
  │     │       → 玻璃/烟雾/粒子/激光/爆炸/天空盒
  │     │
  │     ├── Step G: 阴影叠加 (在光照 Pass 中已完成)
  │     │
  │     ├── Step H: 后处理 (可选)
  │     │     Tone Mapping, Gamma, Bloom, FXAA
  │     │
  │     └── Step I: UI + 2D (不变)
  │           RTS2DScene::doRender()
  │
  └── NO → [原有前向路径 — 完全保留]
```

---

## 四、总实施路线图（12 个阶段，~14 天）

### Phase P0: DX8Wrapper MRT 支持（前置）
> **工期: 0.5 天 | Δ 代码: ~70 行 | 目的: 使 3-RT G-Buffer 可行**

```
文件改动:
  dx8caps.h      +Get_Num_Simultaneous_RTs() + MaxSimultaneousRTs      +2行
  dx8caps.cpp    Init_Caps中查询 caps.NumSimultaneousRTs                +5行
  dx8wrapper.h   +CurrentMRTSurfaces[4] + m_activeMRTCount             +5行
  dx8wrapper.cpp +Set_Render_Target(Int index, IDirect3DSurface8*)     +55行
  dx8wrapper.cpp  修改 Set_Render_Target(surf) — 恢复默认时解除 MRT    +8行

验证方法: 创建 2 张 RT，Set(0)+Set(1)，Clear 不同颜色，读回验证
```

### Phase P1: POT 解除 + 常量提升
> **工期: 0.5 天 | Δ 代码: ~35 行 | 与 P0 并行**

```
文件改动:
  dx8wrapper.cpp  Create_Render_Target allowNonPOT 参数               +20行
  dx8wrapper.h    MAX_VERTEX_SHADER_CONSTANTS 96→256                  +1行
  dx8wrapper.h    MAX_PIXEL_SHADER_CONSTANTS  8→224                   +1行

验证方法: 编译通过，非 POT RT 创建成功
```

### Phase 0: 枚举 + W3DDeferredRenderer 框架
> **工期: 0.5 天 | Δ 代码: ~180 行 | 依赖: P0**

```
文件改动:
  W3DCustomScene.h      枚举 +2 值 (SCENE_PASS_GBUFFER, SCENE_PASS_FORWARD_TRANSPARENT)
  W3DScene.h            新增 m_gbufferMaterialPass 成员
  W3DScene.cpp          构造创建禁光材质、析构释放
  W3DDeferredRenderer.h 新建类框架 (init/shutdown/beginGBufferPass/endGBufferPass)
  W3DDeferredRenderer.cpp 新建实现
  W3DDisplay.cpp        init 中初始化 TheW3DDeferredRenderer
  GlobalData.h + GameData.cpp  INI 开关 (UseDeferredRendering)
  RTS.dsp               注册新文件

验证: 编译通过，前向渲染不变
```

### Phase 1: G-Buffer RT 资源管理
> **工期: 1 天 | Δ 代码: ~200 行 | 依赖: Phase 0**

```
文件改动:
  W3DDeferredRenderer::init() —
    Create_Render_Target(宽,高,A8R8G8B8,allowNonPOT) × 3
  W3DDeferredRenderer::releaseResources() —
    REF_PTR_RELEASE 3 张 RT
  W3DDeferredRenderer::reAcquireResources() —
    重新创建
  W3DDeferredRenderer::beginGBufferPass() —
    Set_Render_Target(0/1/2) + Clear
  W3DDeferredRenderer::endGBufferPass() —
    Set_Render_Target(NULL) → 自动解除 MRT

验证: INI 启用后画面变黑（3 RTs Clear），关闭后前向正常
```

### Phase 2: G-Buffer 写入
> **工期: 2 天 | Δ 代码: ~300 行 | 依赖: Phase 1**

```
文件改动:
  W3DScene.cpp Customized_Render:
    SCENE_PASS_GBUFFER 分支:
      - 地形 → 地形 G-Buffer 着色器
      - 不透明物体 → Push_Material_Pass(m_gbufferMaterialPass) + G-Buffer PS
      - 透明/天空盒 → continue
      - 保留 ERF_DELAYED_RENDER 等遮挡标志
  W3DShaderManager.cpp:
    G-Buffer 像素着色器 (HLSL → D3DXCompileShader)
  G-Buffer PS:
    COLOR0: Albedo.rgb + Metallic.a    [RT0]
    COLOR1: Normal*0.5+0.5 + Roughness.a [RT1]
    COLOR2: Emissive.rgb + clipPos.z   [RT2]

验证: Debug View 肉眼确认 RT0/RT1/RT2 内容正确
```

### Phase 3: 太阳光光照 Pass（首个可见画面）
> **工期: 2 天 | Δ 代码: ~250 行 | 依赖: Phase 2**

```
文件改动:
  W3DDeferredRenderer::sunLightPass():
    - 全屏 Quad VB 工具
    - 绑定 RT0/RT1/RT2 为纹理
    - 设置 PBR 常量: 太阳方向/颜色, 相机位置, invViewProj, 环境光
    - DrawPrimitive 全屏四边形
  W3DView.cpp / W3DDisplay.cpp:
    draw() 中编排: GBuffer Pass → 光照 Pass
  光照 PS:
    从 G-Buffer 采样 → 解码 → PBR Cook-Torrance → 输出

验证: 画面出现（有瑕疵但可辨识）, 前向/延迟 A/B 对比
```

### Phase 4: 正向透明 Pass
> **工期: 1 天 | Δ 代码: ~150 行 | 依赖: Phase 3**

```
文件改动:
  Customized_Render SCENE_PASS_FORWARD_TRANSPARENT:
    - 仅透明 + 天空盒
    - 使用原有前向着色器
  Flush() 中透明相关调用:
    - 粒子 (queueParticleRender)
    - 水面 (Render_And_Clear_Static_Sort_Lists)
    - 强制透明物体 (flushTranslucentObjects)

验证: 玻璃/水/烟雾/粒子正确显示在半透明不透明之上
```

### Phase 5: 屏幕空间 Shroud Pass
> **工期: 1 天 | Δ 代码: ~150 行 | 依赖: Phase 3**

```
文件改动:
  W3DDeferredRenderer::shroudPass():
    全屏 Quad:
      - 绑定 shroud 纹理
      - 从 G-Buffer 深度重建世界坐标
      - 查询 shroud → 对光照结果迷雾遮罩
  支持 fog_of_war 渐变

验证: 迷雾区域正确显示
```

### Phase 6: 动态光 Stencil Volume
> **工期: 2 天 | Δ 代码: ~300 行 | 依赖: Phase 3**

```
文件改动:
  W3DDeferredRenderer:
    预生成球体/圆锥包围体 VB/IB
    pointLightPass/spotLightPass:
      对每个动态光:
        1. 视锥体裁剪 + 距离裁剪
        2. SetStencil → 渲染光包围体标记 Stencil
        3. 在 Stencil 区域内叠加 PBR 光照
    CPU 光源收集 & 排序:
      从 m_dynamicLightList 获取 → 剔除 → 排序 → 最多 N 个

验证: 爆炸/车灯动态光正确出现
```

### Phase 7: 阴影集成
> **工期: 0.5 天 | Δ 代码: ~100 行 | 依赖: Phase 3**

```
文件改动:
  光照 PS 中增加:
    - 绑定阴影纹理
    - 阴影采样
    - 阴影衰减叠加

验证: 物体阴影正确出现在延迟画面
```

### Phase 8: 画面一致性调参
> **工期: 2 天 | Δ 代码: ~100 行 | 依赖: Phase 4-7**

```
调整项:
  PBR 公式一致性 (GGX/Schlick 与前向完全一致)
  光照强度对齐 (太阳、环境光)
  阴影一致性
  8-bit 精度补偿 (伽马校正 + 线性空间)
  Shroud 透明度对齐

验证: 前向/延迟逐像素对比, RGB 差异 < 5%
```

### Phase 9: 性能优化 + 全场景测试
> **工期: 2 天 | Δ 代码: ~200 行 | 依赖: Phase 8**

```
测试项:
  8 玩家满人口对战 → 帧率稳定 > 30fps
  20+ 动态光源 → 不掉帧
  Alt+Tab × 10 → 无崩溃/黑屏
  分辨率切换 (800×600↔1920×1080) → G-Buffer 重建正确
  窗口↔全屏 → 无状态丢失

优化项:
  G-Buffer 纹理关闭 Mipmap
  合并 Clear 操作
  动态分辨率降级 (GBufferScale 75%/50%)
  VC6 兼容性规避 (拆分长函数)
```

---

## 五、文件变更完整清单

```
前置改造（P0 + P1）:
  1. Libraries/Source/WWVegas/WW3D2/dx8caps.h          修改  +2 行
  2. Libraries/Source/WWVegas/WW3D2/dx8caps.cpp        修改  +5 行
  3. Libraries/Source/WWVegas/WW3D2/dx8wrapper.h       修改  +8 行
  4. Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp     修改  +90 行

延迟渲染核心（Phase 0-9）:
  5. Include/W3DDevice/GameClient/W3DCustomScene.h      修改  +2 行
  6. Include/W3DDevice/GameClient/W3DScene.h            修改  +3 行
  7. Source/W3DDevice/GameClient/W3DScene.cpp           修改  +120 行
  8. Source/W3DDevice/GameClient/W3DView.cpp            修改  +10 行
  9. Source/W3DDevice/GameClient/W3DDisplay.cpp         修改  +80 行
  10. Include/W3DDevice/GameClient/W3DDeferredRenderer.h 新建  +200 行
  11. Source/W3DDevice/GameClient/W3DDeferredRenderer.cpp 新建 +1500 行
  12. Source/W3DDevice/GameClient/W3DShaderManager.cpp   修改  +50 行
  13. Include/W3DDevice/GameClient/W3DShaderManager.h    修改  +5 行
  14. Include/Common/GlobalData.h                        修改  +5 行
  15. Source/Common/GameData.cpp                          修改  +10 行
  16. RTS.dsp                                             修改  +2 行

  HLSL 着色器 (内联在 .cpp 中):
    17. G-Buffer 像素着色器                             新建  +200 行
    18. 太阳光光照像素着色器                             新建  +300 行
    19. 点光/聚光像素着色器                              新建  +150 行
    20. 屏幕空间 Shroud 着色器                           新建  +50 行
    21. 自发光合并着色器                                 新建  +30 行

总计: 代码 ~2700 行 (105 前置 + 2200 C++/HLSL 核心 + 400 HLSL)
```

---

## 六、工期汇总

| 阶段 | 内容 | 单人 |
|------|------|------|
| P0 | DX8Wrapper MRT 支持 | 0.5 天 |
| P1 | POT 解除 + 常量提升 | 0.5 天 |
| 0 | W3DDeferredRenderer 框架 | 0.5 天 |
| 1 | G-Buffer RT 资源管理 | 1 天 |
| 2 | G-Buffer 写入 | 2 天 |
| 3 | 太阳光光照 Pass | 2 天 |
| 4 | 正向透明回退 | 1 天 |
| 5 | 屏幕空间 Shroud | 1 天 |
| 6 | 动态光 Stencil | 2 天 |
| 7 | 阴影集成 | 0.5 天 |
| 8 | 画面一致性调参 | 2 天 |
| 9 | 性能优化 + 测试 | 2 天 |
| | **合计** | **~14 天** |

---

## 七、核心风险

| 风险 | 等级 | 应对 |
|------|------|------|
| D3D9 驱动 MRT 支持 | P0 | Set_Render_Target 检查返回值，失败 mark MRT unavailable → 前向回退 |
| Device Reset MRT 表面重建 | P1 | DX8_CleanupHook 模式释放/重建 |
| Shroud/遮挡系统兼容 | P0 | 独立屏幕空间 Pass 处理 |
| 法线贴图精度损失（8-bit） | P2 | 可接受。后期可选扩展为 A2R10G10B10 格式 |
| 正向/延迟画面对齐 | P1 | Phase 8 专用调参阶段 |
| VC6 编译 HLSL 内联字符串 | P2 | 长字符串拆分，避免反斜杠和超长行 |

---

## 八、实施顺序图

```
第 1 步: P0  MRT 支持 ────────────────────────── 0.5 天
第 2 步: P1  POT 解除 + 常量提升 (与 P0 并行) ─── 0.5 天
                  ↓
第 3 步: Phase 0  枚举 + 框架 ────────────────── 0.5 天
第 4 步: Phase 1  G-Buffer RT 资源 ───────────── 1 天
第 5 步: Phase 2  G-Buffer 写入 ──────────────── 2 天
第 6 步: Phase 3  太阳光光照 Pass (首个画面!) ─── 2 天
                  ↓
第 7 步: Phase 4  透明回退 ───────────────────── 1 天
第 8 步: Phase 5  Screen Space Shroud ────────── 1 天
第 9 步: Phase 6  动态光 Stencil ─────────────── 2 天
第 10 步: Phase 7 阴影集成 ───────────────────── 0.5 天
第 11 步: Phase 8 画面一致性调参 ──────────────── 2 天
第 12 步: Phase 9 性能优化 + 全场景测试 ───────── 2 天
```

---

## 九、文档树

```
docs/
├── deferred-rendering-master-plan.md   ← 本文件（唯一执行入口）
│
├── (以下为历史分析文档，参考用)
├── deferred-rendering-critique-round1.md  — 原始计划的 25 项遗漏
├── deferred-rendering-critique-round2.md  — MRT 缺失的连锁分析
├── deferred-rendering-critique-round3.md  — 最终审定稿（已合并入本文件）
├── dx9wrapper-plan-round1.md              — DX8Wrapper 全量清单
├── dx9wrapper-plan-round2.md              — 三层详细设计
└── deferred-rendering-checkpoint.md       — 状态存档
```
