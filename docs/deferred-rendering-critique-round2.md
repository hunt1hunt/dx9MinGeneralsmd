# 延迟渲染计划 — 第2轮检讨：MRT 缺失的连锁反应与深度风险分析

> 基于第1轮 25 项差距 + 代码走读发现的核心架构约束
> 日期: 2026-07-14

---

## 一、核心发现：DX8Wrapper 不支持 MRT（致命级别）

### 1.1 现状验证

通过 `dx8wrapper.h/cpp` 源码确认：

```cpp
// dx8wrapper.h 关键常量
MAX_VERTEX_SHADER_CONSTANTS = 96
MAX_PIXEL_SHADER_CONSTANTS = 8   // 仅 8 个 float4 寄存器 (ps_2_0)
MAX_TEXTURE_STAGES = 8
MAX_SHADOW_MAPS = 1
```

- `DX8Wrapper::Set_Render_Target()` 三个重载全部只使用 index 0
- 全局搜索 `MRT` `MultipleRenderTarget` `SetRenderTarget(1` `SimultaneousRT` → 零结果
- 水渲染注释明确写 "D3D doesn't [support] multiple render targets"
- `Create_Render_Target()` 强制 POT（2 的幂）尺寸
- `DX8Wrapper` 虽然加载 D3D9 DLL，但接口设计仍停留在 D3D8 时代

### 1.2 连锁影响

| 原计划 | 现实 | 影响 |
|--------|------|------|
| 3 RT MRT (RT0/1/2) | 一次只能 set 1 个 RT | MRT 方案不可行 |
| 一次 Clear 3 RTs + Depth | 需要 3 次 Clear | 增加 API 调用 |
| 全屏 Quad + 绑定 3 张 GBuffer 纹理 | 可行（作为纹理采样） | 光照 Pass 不受影响 |
| GBuffer PS 输出 COLOR0/1/2 | 无法一次写入 3 个 RT | G-Buffer Pass 需要重新设计 |

### 1.3 可行的替代方案

#### 方案 A：多 Pass G-Buffer（推荐，改动最小）
```
Pass 1: 写入 RT0 (Albedo+Metallic) + Depth
Pass 2: 写入 RT1 (Normal+Roughness) + 复用 Depth（利用已写的 Z-buffer）
Pass 3: 写入 RT2 (Emissive+Depth/自定义)
```
- 优点：不修改 DX8Wrapper，不破坏现有状态跟踪
- 缺点：3 倍几何吞吐量（每帧所有不透明物体绘制 3 次），RTS 500+ 物体 → 1500+ 次绘制调用
- **优化：** 利用 Z-buffer 的 EQUAL 测试跳过被遮挡片段，Pass 2/3 的像素着色器工作量大幅减少（early-Z 剔除）

#### 方案 B：G-Buffer 打包为单张纹理（带宽最优）
```
RT0 (A8R8G8B8): 
  RGB = Albedo 
  A   = Packed(Metallic*16 + Roughness*16/16) — 8-bit 打包两个 4-bit 值
Depth: 单独使用 Z-Buffer（24-bit）
Normal: 不存纹理，在光照 Pass 中用 ddx/ddy 从深度重建
Emissive: 前向透明 Pass 中作为 additive 叠加
```
- 优点：没有 MRT 需求，只需 1 个 RT + depth
- 缺点：法线从深度重建质量差、Metallic/Roughness 精度损失
- 不会增加几何吞吐量

#### 方案 C：混合方案（推荐，平衡质量与性能）
```
Pass 1 (几何): RT0 = Albedo(RGB) + PackedPBR(A)
  其中 PackedPBR = (Metallic << 4) | (Roughness & 0x0F) — 各 4-bit
  不存法线 → 光照 Pass 用 ddx/ddy 从深度重建法线
  不存 Emissive → 前向透明 Pass 叠加
  Z-Buffer 正常写入

Lighting Pass: 绑定 RT0 + Depth + 重建法线（ddx/ddy）
  需要 ps_3_0（支持 ddx/ddy 指令 + 足够常量寄存器）
```
- 优点：单几何 Pass，不增加几何吞吐
- 缺点：法线重建质量依赖分辨率（全屏效果可接受，边缘有瑕疵）
- 不需要改 DX8Wrapper

---

## 二、方案 A 的详细评估（多 Pass G-Buffer）

如果坚持传统 3-RT G-Buffer（用多 Pass 模拟 MRT），需要评估：

### 2.1 性能成本

**RTS 典型场景：** 500 不透明物体 × 2 色/物体（基础+玩家色）= 1000 次绘制调用/Pass

| Pass | 绘制调用 | 说明 |
|------|---------|------|
| 正常前向 | 1000 | 一次渲染全部 |
| GBuffer Pass 1 | 1000 | Albedo + Depth |
| GBuffer Pass 2 | 1000 | Normal + Roughness（利用 early-Z） |
| GBuffer Pass 3 | 1000 | Emissive + Depth（利用 early-Z） |
| 总和 | 3000 | 3x 几何吞吐 |

Pass 2/3 的 early-Z 优化：
- Z-buffer 已在 Pass 1 写完
- Pass 2/3 设置 `ZTest=EQUAL, ZWrite=OFF`
- 只有最前面片元执行 PS（被遮挡片元在 early-Z 阶段丢弃）
- **预期：** Pass 2/3 的 PS 执行量约为 Pass 1 的 10-30%（取决于场景深度复杂度）
- 绘制调用仍是 3 倍（CPU 瓶颈可能明显）

### 2.2 对 renderOneObject 的改造

renderOneObject 需要支持三种模式：Albedo Pass / Normal Pass / Emissive Pass。每种模式需要不同的 PS 和常量设置。这意味着不能简单地用 `m_gbufferMaterialPass` 覆盖所有情况：

```cpp
// 伪代码：G-Buffer 多 Pass 渲染
for (mode = ALBEDO; mode <= EMISSIVE; mode++) {
    SetRenderTarget(gbufferSurfaces[mode], depthSurface);
    // 仅 Pass 1 需要 Clear
    if (mode == ALBEDO) Clear(3 RTs + Depth);
    
    for (all opaque objects) {
        // 仅 Pass 1：完整的对象遍历、材质绑定、VS 设置
        // Pass 2/3：使用更轻量的路径
        if (mode == ALBEDO_PASS) {
            renderOneObject_GBufferAlbedo(rinfo, robj);
            // 这与现有的 renderOneObject 最相似
            // 但必须替换 PS 并使用禁光的材质
        } else {
            renderOneObject_GBufferAux(rinfo, robj, mode);
            // 轻量路径：
            // - 相同的 VS（复用已绑定的）
            // - 不同的 PS
            // - ZTest=EQUAL, ZWrite=OFF
            // - 不需要光照环境
        }
    }
}
```

### 2.3 地形多 Pass 问题

地形需要特殊处理：
- Pass 1：地形使用自己的着色器写入 Albedo + Depth（需要地形 G-Buffer VS/PS）
- Pass 2：地形再次渲染写入 Normal（但地形可能没有法线贴图，使用几何法线）
- Pass 3：Emissive（地形通常不自发光）

---

## 三、方案 C（推荐）的详细评估：单 Pass G-Buffer + 法线重建

### 3.1 架构

```
[G-Buffer] — 单 Pass，单 RT0
  RT0 (A8R8G8B8): Albedo(RGB) + PackedPBR(A)
    A[7..4] = Metallic (4-bit, 16 levels)
    A[3..0] = Roughness (4-bit, 16 levels)
  
  Depth: 现有 Z-Buffer (24-bit)，正常写入

[光照 Pass] — 全屏 Quad
  s0 = RT0 (Albedo + PackedPBR)
  s1 = … (预留，可用于后续扩展)
  Depth = Z-Buffer（通过 tex2D 或 LOAD 读取）
  
  法线重建：
    float ddx = ddx(linearDepth);
    float ddy = ddy(linearDepth);
    float3 normal = normalize(cross(ddx, ddy));
    // 需要 ps_3_0 支持 ddx/ddy
  
  PBR 光照：
    unpack PBR from RT0.a
    standard Cook-Torrance with reconstructed normal
```

### 3.2 常量寄存器规划

```
ps_3_0 (最多 224 个 float4 常量):
  c0: 太阳方向 (world space, normalized)
  c1: 太阳颜色 (diffuse * intensity)
  c2: 相机位置 (world space)
  c3-c6: 逆 ViewProj 矩阵 (4x4)
  c7: 环境光颜色
  c8: 屏幕参数 (1/width, 1/height, 0, 0)
  c9-c10: 额外全局光 (1 个方向 + 颜色)
  c11-c?: 时间、调试参数等
```

限制分析：ps_3_0 的 224 常量寄存器完全足够。即使包含阴影贴图采样矩阵也绰绰有余。

### 3.3 法线重建质量

需要评估的质量问题：
- **陡峭边缘：** 法线在深度不连续处有跳跃（几何边缘）
- **锯齿：** 法线重建依赖屏幕空间偏导，分辨率越低越明显
- **与法线贴图的差异：** 重建法线损失了法线贴图的细节（砖缝、铆钉等）

缓解方案：
- 边缘检测 + 法线矫正（使用 Sobel 算子的变种）
- 对不连续处使用几何法线 fallback（从 World 矩阵推导）
- 接受这是权衡——法线贴图细节损失换取单 Pass G-Buffer

---

## 四、像素着色器常量不足问题（ps_2_0 路径）

### 4.1 问题描述

`MAX_PIXEL_SHADER_CONSTANTS = 8` 意味着 ps_2_0 路径只有 8 个 float4 寄存器：
```
c0: 保留给…？
c1: ?
... 仅 8 个总寄存器
```

当前前向 PBR 着色器已经使用了 c0-c11（部分在 VS 中）。延迟光照 Pass 的常量需求更大（需要逆 ViewProj 矩阵 = 4 个寄存器）。

### 4.2 解决方案

1. **放弃 ps_2_0 路径：** 仅支持 SM3.0（ps_3_0），接受低端显卡走前向 fallback
2. **在 VS 中计算部分光照：** 但这对延迟渲染没有意义（光照是屏幕空间）
3. **拆分常量上传：** 部分通过顶点着色器传递（如屏幕参数作为纹理坐标）

**推荐：** 前向 Fallback 兼容 ps_2_0，延迟路径仅 SM3.0。这是合理的——SM3.0 硬件（2004+）已经覆盖几乎所有用户。

---

## 五、实施顺序的关键问题

### 5.1 原计划的顺序问题

原计划建议的实施顺序：
```
Step 1: 枚举+类框架 (编译验证)
Step 2: W3DDisplay init/设备重置 (编译验证)
Step 3: G-Buffer 着色器 (编译验证)
Step 4: 太阳光全屏 Quad (RenderDoc 截帧)
Step 5: Customized_Render + draw 编排 (画面显示)
```

**问题：** Step 3 和 Step 4 在 Step 5 之前无法测试。没有 `Customized_Render` 分支，G-Buffer 和光照着色器无处运行。

### 5.2 正确的实施顺序

```
Phase A: 框架搭建 (可测试)
  A1: W3DCustomScene.h 枚举扩展 (+2 行)
  A2: W3DScene.h 新增成员 (MaterialPassClass*)
  A3: W3DScene.cpp 构造/析构
  A4: W3DDeferredRenderer.h/cpp 空壳 (init/shutdown 存根)
  A5: W3DDisplay init 集成 + INI 开关
  → 验证: 编译通过，前向渲染不变

Phase B: G-Buffer 资源分配 (可测试)
  B1: W3DDeferredRenderer 的 init() 创建 RT0 (根据方案 C 或 A)
  B2: releaseResources/reAcquireResources
  B3: beginGBufferPass/endGBufferPass (清空 RT)
  → 验证: INI 开关启用后，画面全黑 (G-Buffer 被 Clear 但未写入)
        INI 开关关闭 → 前向渲染正常
  这时就知道框架在运行！

Phase C: G-Buffer 写入 (可测试)
  C1: Customized_Render 中 SCENE_PASS_GBUFFER 分支
  C2: 不透明物体跳过透明+天空盒
  C3: renderOneObject 使用 m_gbufferMaterialPass (禁光材质)
  C4: G-Buffer 着色器 (仅 Albedo + PackedPBR)
  → 验证: INI 启用后，通过读回或 Debug View 确认 G-Buffer 内容

Phase D: 光照合成 (可见画面!)
  D1: 全屏 Quad VS/PS 工具函数
  D2: 太阳光着色器 (法线重建 + PBR)
  D3: W3DView::draw 或 W3DDisplay::draw 中的光照 Pass 编排
  → 验证: 完全不透明场景的正确渲染画面
  
Phase E: 透明回退 (完整画面)
Phase F: 动态光 + Stencil Volume
Phase G: 迷雾兼容
Phase H: 遮挡兼容
Phase I: 后处理 + 调参
Phase J: 测试
```

---

## 六、第1轮遗漏项的优先级分类

### P0（必须解决，否则不可用）
1. ~~MRT 不支持~~ → 方案 C 单 Pass G-Buffer
2. 迷雾系统兼容 → 屏幕空间 shroud pass
3. 模板遮挡系统兼容 → GBuffer Pass 中保留遮挡逻辑
4. 深度精度 → 使用 24-bit Z-Buffer，G-Buffer 不存深度

### P1（重要，影响画面质量）
5. 地形特殊路径 → 地形 G-Buffer 着色器
6. 水反射冲突 → 在反射渲染前后保存/恢复 GBuffer RT
7. 阴影采样 → 延迟光 Pass 绑定阴影纹理
8. 法线重建质量 → 边缘检测 + 矫正
9. 多视图 → DeferredRenderer 支持每帧多视图

### P2（影响开发效率）
10. 增量验证策略 → Phase A-J 分割
11. A/B 对比方法 → 截帧 + 像素比较工具
12. 设备丢失全链路 → 注册 CleanupHook

### P3（可延迟）
13. 后处理管线（Bloom/FXAA）→ 与延迟无关，可独立开发
14. IBL 在延迟路径中的实现
15. 动态分辨率降级
