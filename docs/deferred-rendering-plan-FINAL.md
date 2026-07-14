# 延迟渲染改造 — 正式实施计划（最终审定版）

> 经过 3 轮检讨式反思，综合 40+ 项差距分析，基于完整代码走读（W3DScene/W3DView/W3DDisplay/dx8wrapper/W3DShaderManager/W3DDynamicLight/W3DShroud 等 20+ 核心文件）
> 日期: 2026-07-14

---

## 核心变更概要

原计划基于传统 3-RT MRT G-Buffer 方案。**代码走读发现 DX8Wrapper 完全不支持 MRT**（仅 index 0 单 RT），且 `MAX_PIXEL_SHADER_CONSTANTS = 8`（ps_2_0）。因此：

### ❌ 废弃：3-RT MRT G-Buffer
### ✅ 采用：单 Pass G-Buffer + 法线重建（ddx/ddy）+ 24-bit Z-Buffer

---

## 1. 最终渲染管线架构

```
W3DDisplay::draw()
  │
  ├── [前置 RT 阶段] (不变)
  │     ├── 水反射纹理更新
  │     └── 阴影纹理更新
  │
  ├── [判定: 启动延迟渲染?]
  │     条件: SM3.0 支持 && INI 启用 && 非滤镜模式 && 非线框
  │
  ├── YES → [延迟渲染路径]
  │     │
  │     ├── G-Buffer Pass (单 RT0 A8R8G8B8 + Depth)
  │     │   RT0.RGB = Albedo (线性)
  │     │   RT0.A   = Packed: Metallic[7..4] | Roughness[3..0] (各4-bit)
  │     │   Z-Buffer = 24-bit 原生深度 (利用现有 DepthStencil)
  │     │   场景: SCENE_PASS_GBUFFER → 仅不透明物体
  │     │   遮挡系统: 保留 ERF_DELAYED_RENDER 等标志
  │     │
  │     ├── 光照 Pass (全屏 Quad, backbuffer 输出)
  │     │   绑定: s0=RT0, s1=Depth纹理
  │     │   常量: c0=太阳方向, c1=太阳颜色, c2=相机位置,
  │     │         c3-c6=invViewProj, c7=环境光
  │     │   法线重建: ddx/ddy(worldPos) → normalize(cross(dx,dy))
  │     │   PBR: Cook-Torrance BRDF (同现有前向)
  │     │
  │     ├── 屏幕空间 Shroud Pass (全屏 Quad)
  │     │   绑定 shroud 纹理 + G-Buffer → 迷雾遮罩
  │     │
  │     ├── 正向透明 Pass (SCENE_PASS_FORWARD_TRANSPARENT)
  │     │   玻璃/烟雾/粒子/爆炸/天空盒
  │     │
  │     ├── 后处理 (可选, 后续阶段)
  │     │   Tone Mapping / Gamma / Bloom / FXAA
  │     │
  │     └── UI + 2D 场景 (W3DDisplay::m_2DScene)
  │
  └── NO  → [原有前向路径 — 完全保留]
```

---

## 2. G-Buffer 布局（最终版）

| 资源 | 格式 | 内容 | 精度说明 |
|------|------|------|---------|
| RT0 | A8R8G8B8 | Albedo(RGB) + PackedPBR(A) | 8-bit/通道线性, PBR各4-bit |
| Depth | D24S8 | 原生 Z-Buffer | 24-bit 深度 (~16M 级) |
| Backbuffer | 桌面格式 | 光照合成输出 | 前向 fallback 兼容 |

**法线重建方案：** 不存储法线纹理。光照 Pass 中用 `ddx/ddy` 偏导从世界坐标重建。

---

## 3. Shader 常量规划

```
ps_3_0 (224 个 float4 寄存器, 充足)
  c0:    太阳方向 (normalized world space)
  c1:    太阳颜色 (diffuse * intensity)
  c2:    相机位置 (world space)
  c3-c6: 逆 ViewProj 矩阵 (4×4)
  c7:    环境光颜色
  c8:    屏幕参数 (1/w, 1/h, pad, pad)
  c9:    (预留) 调试/阴影参数
  c10+:  (预留) 扩展
```

**注意：** 不存法线，所以不需要 `normal*0.5+0.5` 编码寄存器。不存 Emissive，所以不需要 RT2。

---

## 4. 实施路线图（10 个可测试增量，每步可验证）

```
Phase 0 ───→ Phase 1 ───→ Phase 2 ───→ Phase 3 ───→ Phase 4
枚举+框架      RT 分配      G-Buffer     光照 Pass    透明回退
0.5天         1天          2天           2天          1天

                ↓
           Phase 5 ───→ Phase 6 ───→ Phase 7 ───→ Phase 8 ───→ Phase 9
           Shroud Pass   动态光     阴影集成      调参对齐     性能+测试
           1天           2天         0.5天        2天          2天
```

**总计：~14 个工作日，~2600 行代码（2000 C++ + 600 HLSL）**

各阶段详细内容见 `docs/deferred-rendering-critique-round3.md` 第四章。

---

## 5. 关键风险与应对

| 风险 | 等级 | 应对 |
|------|------|------|
| **DX8Wrapper 没有 MRT** (已验证) | P0 | 单 Pass G-Buffer + 法线重建，无需 MRT |
| **法线重建质量不可接受** | P1 | 备选方案：2-Pass G-Buffer（第二 Pass 写法线），在 Phase 3 后评估 |
| **迷雾/Shroud 在延迟路径中需重新实现** | P0 | Phase 5 单独实现屏幕空间 Shroud Pass |
| **模板遮挡系统与 G-Buffer 冲突** | P1 | G-Buffer Pass 保留遮挡标志分类，不写 Stencil |
| **阴影需在光照 Pass 中采样** | P1 | 确保阴影纹理在 G-Buffer Pass 前更新（前置阶段已存在） |
| **正向/延迟画面不一致** | P1 | Phase 8 专用调参阶段，逐像素 A/B 对比 |
| **VC6 编译问题** | P2 | 避免复杂模板，长字符串拆分 |
| **多视图 G-Buffer 冲突** | P1 | DeferredRenderer 支持每帧多次 begin/end |

---

## 6. 相对原计划的主要改动

| 原计划 | 修正 | 原因 |
|--------|------|------|
| 3 RT MRT | 单 Pass + Z-Buffer | DX8Wrapper 不支持 MRT |
| 存储法线纹理 | ddx/ddy 法线重建 | MRT 不支持后无法多输出 |
| 存储深度到 RT2 | 直接使用 24-bit Z-Buffer | 8-bit 深度精度不足 (< 40m@10km) |
| 在 Customized_Render 中完成全部编排 | 在 W3DDisplay::draw() 层编排 | 需要多阶段渲染（不透明→光→透明） |
| 存储 Emissive 到 RT2 | 前向透明 Pass 中 additive 叠加 | 无 RT2 可用 |
| 未处理迷雾 | Phase 5 屏幕空间 Shroud | RTS 必需功能 |
| 未处理遮挡系统 | GBuffer Pass 保留标志 | 玩家颜色标记必需 |

---

## 7. 参考资料

- 原始计划: `docs/deferred-rendering-plan.md`
- 第1轮检讨（25项差距）: `docs/deferred-rendering-critique-round1.md`
- 第2轮检讨（MRT 连锁分析）: `docs/deferred-rendering-critique-round2.md`
- 第3轮检讨（最终审定稿）: `docs/deferred-rendering-critique-round3.md`
- 检查点: `docs/deferred-rendering-checkpoint.md`
