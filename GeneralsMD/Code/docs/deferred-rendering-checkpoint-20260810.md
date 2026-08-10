# 延迟渲染 + W3X 阴影管线 — 第二台机器验证检查点

> 存档时间：2026-08-10
> 状态：编译构建通过，在另一台电脑上运行无崩溃，完整延迟渲染管线正常工作
> 本存档基于上一检查点 `deferred-rendering-checkpoint.md` 与 `docs/w3x-integration/` 方案

---

## 一、本次验证结论

在本机编译构建后，将构建产物拷贝到另一台电脑运行，**未发现任何崩溃问题**。两日志均确认：

- `GeneralsMD_DeferredRT(6).log`（242,904 行）— 完整延迟渲染管线稳定循环运行，无异常
- `DebugLogFileI(53).txt`（17,720 行）— 全部 PBR 着色器编译/创建成功，仅常规 SAGE 自动忽略断言

## 二、延迟渲染管线状态（第二台机器确认）

```
Shadow Map → G-Buffer → SSAO → Deferred Lighting → Tone Map → Forward Transparent（每帧循环）
```

| 组件 | 初始化结果 | 说明 |
|------|-----------|------|
| 渲染目标 | PS3.0 OK；R32F / G16R16F / A16B16G16R16F | MRT=4，1920×1080 |
| SunLight / PointLight / ToneMap PS | 编译成功 | — |
| SunLightShadow PS | PCF 2×2 编译成功 | 阴影贴图 1024×1024 D24X8 |
| SSAO | STEP8 16-sample + normal-weighted | AO RT 1920×1080 |
| IBL | DDS 文件加载 OK | — |
| GBuffer 自检 | octahedral 编码最大误差 0.0343°；深度 16-bit 误差 0 | 通过 |

### 性能（第二台机器实测）

| Pass | 耗时 |
|------|------|
| G-Buffer | ~10–14 ms |
| Forward Transparent | ~10–14 ms |
| SSAO | 0.03–0.07 ms |
| Shadow Map | 0.4–1.9 ms |
| Lighting + Tonemap | 0.17–0.20 ms |

## 三、关键修复点验证

1. **shadowView 矩阵不再是零矩阵**（此前 W3X 模型不可见根因）：

   ```
   [NEW] shadowView r0=(-1.000,0.000,0.000,0.000) r3=(-151.013,555.012,500.000,1.000)
   ```

   光照方向、相机位置、阴影视图矩阵均有效 → 阴影 pass 实际在渲染。

2. **PBR 着色器全部编译/创建成功**（每行 `D3DXCompileShader hr = 0`、`CreatePixelShader hr = 0`）→ 之前 PBR 黑模的常量上传问题在第二台机器确认通过。

## 四、日志中出现的无害项说明

`DebugLogFileI` 中的 `ASSERTION FAILURE` / `CRASH IN FULL SCREEN - Auto-ignored` 为 SAGE 引擎调试断言系统的正常行为（记日志后自动忽略并继续，非真实崩溃）：

- `width==height`、`DefaultRenderTarget==NULL`（dx8wrapper 设备初始化断言）
- `ASSET ERROR`（avlasertnk_d1 炮塔骨骼 / 0qsnwateryy1 / UIWRKR_R_TOE0 等缺失模型）— 本 fork 未含美术资源，属预期
- `FindHierarchicalPath failed`、`neg Energy numbers` — 寻路/经济类常规断言

> 注意：`pbr_unit_ps30_specibl` 等行前缀为 `error:`，实际内容是 HLSL **warning X4121**（flow control 内梯度运算的性能建议），着色器仍编译成功。

## 五、遗留事项

1. **[待办] 优化 X4121**：把 spec/IBL 着色器里 flow control 内的 `ddx/ddy`/纹理采样梯度移到分支外，消除 4 条 X4121 性能警告：
   - `pbr_unit_ps30_specibl`
   - `pbr_unit_alpha_ps30_specibl`
   - `pbr_unit_nt_ps30_specibl`
   - `pbr_unit_alpha_nt_ps30_specibl`

2. **[待办] 延迟阴影贴图 Phase 2**：内存记录标记的未做项（阴影贴图延迟化第二阶段）。

## 六、相关索引

| 项目 | 路径/提交 |
|------|----------|
| 本次存档文档 | `docs/deferred-rendering-checkpoint-20260810.md` |
| 延迟渲染计划 | `docs/deferred-rendering-plan.md` |
| 延迟渲染检查点 | `docs/deferred-rendering-checkpoint.md` |
| W3X 集成方案 | `docs/w3x-integration/00-总结报告.md` ~ `04-系统修复计划.md` |
| 最近提交 | `b9053bc8`（W3X 阴影管线）/ `882011be`（悍马软阴影）/ `3cb96807`（W3X 场景渲染管线） |
| 验证日志 | `GeneralsMD_DeferredRT(6).log` / `DebugLogFileI(53).txt`（用户机器） |
