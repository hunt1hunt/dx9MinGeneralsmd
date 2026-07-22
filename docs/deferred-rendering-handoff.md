# 延迟渲染 — 工作交接文档

> 生成于 2026-07-20 | 上下文窗口限制，转入新对话
> 上一个对话完成了 Steps 1-7

---

## 完成状态

| Step | 内容 | 状态 | 日志验证 |
|------|------|------|---------|
| 1 | WW3DFormat 扩展 (R32F/G16R16F/A16B16G16R16F) | ✅ | ROUNDTRIP_OK |
| 2 | Full-screen quad 修复 (XYZRHW + half-pixel) | ✅ | vert[0..3] 正确 |
| 3 | PS 3.0 升级 + 能力检测 | ✅ | PixelShaderVersion=3.0 |
| 4 | G-Buffer 重写 (octa编码/sRGB→linear/16-bit深度/材质通道) | ✅ | 编码误差 0.028° |
| 5 | HDR RT + Tone Mapping (Reinhard + Gamma) | ✅ | A16B16G16R16F 创建 |
| 6 | Sun Shadow Map (2048² RT + PCF 2×2 shader) | ✅ | Shadow PS compiled |
| 7 | SSAO (compute + blur) | ✅ | AO RTs + shaders OK |
| 8 | 完整管线集成 + D24X8 阴影 | ✅ | checkpoint da8d3ca1 |
| P0 | sRGB修正 + PBROverride.ini | ⚠️ 编译中 | 4个文件已改，有编译错误待修 |

---

## 未解决的遗留事项

### 1. [P1] Shadow map 场景深度渲染未集成
**问题**：`W3DScene.cpp` 中的 shadow pass 已被注释掉（`beginShadowMapPass`/`endShadowMapPass`），因为直接调用 `SetRenderTarget` 导致黑屏。
**当前状态**：Shadow map 纹理（2048×2048）被清为全白（远处），SunLightShadow PS 采样全白→`shadow = 1.0`（无阴影）。
**修复方案**：
1. 使用 `DX8Wrapper::Set_Render_Target(0, surf)`（已修复）
2. 在 W3DScene 中 shadow pass 时需要用场景相机渲染深度到 shadow RT
3. 需要设置合适的 shadow VP 等矩阵作为顶点变换
4. 着色器禁用颜色写入，只写深度

**相关代码位置**：
- `W3DDeferredRenderer.cpp` lines ~1305-1350: `beginShadowMapPass`/`endShadowMapPass`
- `W3DScene.cpp` lines ~1053: shadow pass 集成点（当前已注释）

### 2. [P2] SSAO 质量待优化
**问题**：当前 SSAO 使用 4 个屏幕空间偏移采样，质量较低。
**当前状态**：computeAO() 已接入管线但在 W3DScene 中未调用（`g_pbrDebugMode=5` 时才显示）。
**改进方向**：
- 增加采样数（4→16）
- 使用半球随机采样替代固定偏移
- 接入 G-buffer 法线进行朝向修正

### 3. [P2] 法线编码在 GPU 下半球符号修复
**问题**：Code Review 发现 `octEncode` 中 `step(0, p.yx)` 应为 `step(0, p)`（已在 `W3DShaderManager.cpp` line 3361 修复）。
**状态**：✅ 已修复

### 4. [P3] Shader 文件化（提取到独立的 `.fx` 文件）
**问题**：所有 shader 硬编码在 C++ 字符串中，每次修改需全编译。
**时机**：等待 G-Buffer 通道分配完全稳定后再提取。

---

## Step 8: 完整管线集成 实施计划

### 目标

将 Steps 1-7 的所有组件串联成完整的延迟渲染管线：

```
Frame Render Flow (最终态):
  1. [Shadow]   Shadow map pass（场景深度 → 2048² shadow RT）
  2. [GBuffer]  不透明物体 → RT0/1/2 (MRT)
  3. [SSAO]     环境遮挡计算（可选）
  4. [Lighting] 阳光 PBR + 阴影采样 → HDR RT
  5. [Lights]   动态点光源叠加（additive）
  6. [Tone]     HDR→LDR tone mapping → Back Buffer
  7. [Forward]  半透明物体特效覆盖
```

### 需要实现的子任务

#### 8a. Shadow pass 场景深度渲染
- 在 W3DScene 中恢复 shadow pass，使用 `DX8Wrapper::Set_Render_Target`
- 设置 shadow camera（从太阳方向正交投影）
- 只写深度，不写颜色
- 存储 shadow VP 矩阵供 lighting pass 使用

#### 8b. SSAO 接入场景管线
- 在 W3DScene deferred rendering 分支中调用 `computeAO()`
- SSAO 结果绑定到 sunLightPass 的 texture slot 5
- ambient light *= AO mask

#### 8c. INI 开关系统
- 为每个组件添加独立开关：
  - `UseDeferredRendering` → 主开关
  - `UseShadowMap` → 阴影（默认开启）
  - `UseSSAO` → 环境遮挡（默认关闭）
  - `UseHDR` → 高动态范围（默认开启）

#### 8d. Pipeline 性能计时
- 每个 pass 前后插入 QueryPerformanceCounter
- 输出各 pass 耗时到 DIAG_LOG

#### 8e. 开关组合验证
- 测试所有 8 种开关组合
- 验证每个组合画面正确

### 涉及文件

| 文件 | 改动 |
|------|------|
| `W3DScene.cpp` | Shadow pass 恢复、SSAO 调用、INI 开关检查 |
| `W3DDeferredRenderer.h/cpp` | shadow pass 修复、INI 开关、性能计时 |
| `W3DShaderManager.cpp` | 可能需要的 shadow VS |

### 验证方式

```
PIPELINE: === Shadow Map Pass ===
PIPELINE: === G-Buffer Pass ===
PIPELINE: === SSAO Pass ===
PIPELINE: === Deferred Lighting Pass ===
PIPELINE: === Tone Mapping Pass ===
PIPELINE: === Forward Transparent Pass ===
PIPELINE: G-Buffer Pass took 3.24 ms
PIPELINE: Lighting Pass took 0.87 ms
```

---

## 构建关键信息

- **工具链**：MSVC 6.0 (VC6)
- **项目文件**：`GeneralsMD/Code/RTS.dsw`
- **D3D 版本**：D3D9 (通过 d3d8compat.h 封装为 D3D8 API)
- **Shader 编译**：运行时 `D3DXCompileShader`，目标 `ps_3_0`
- **调试输出**：`DIAG_LOG` 宏写入 `E:\GeneralsMD_DeferredRT.log`
- **调试模式**：`g_pbrDebugMode` (INI: `PBRDebugMode`) = 1~6 显示各通道

### 已修改的所有文件清单

| 文件 | 修改内容 |
|------|---------|
| `Libraries/.../ww3dformat.h` | +R32F/G16R16F/A16B16G16R16F 枚举 |
| `Libraries/.../formconv.cpp` | +格式转换表 + HIGHEST_SUPPORTED 提升 |
| `Libraries/.../ww3dformat.cpp` | +格式名称 + BytesPerPixel |
| `W3DDeferredRenderer.h` | +HDR/Shadow/SSAO 声明 |
| `W3DDeferredRenderer.cpp` | 全部实现 |
| `W3DShaderManager.cpp` | +octEncode + sRGB→linear + 16-bit深度 |
| `W3DScene.cpp` | +HDR pass wrapping |
