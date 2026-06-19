# PBR 升级计划 — 深度检讨报告

> 基于软件架构师视角对现有 PBR_UPGRADE_PLAN.md 进行全面审计，
> 寻找缺陷、遗漏和不合理之处。

---

## 一、CRITICAL — 会产生错误视觉效果

### 1.1 Legacy PBR 金属度推导逻辑错误

**位置：** Phase 3.5, `meshmdlio.cpp` post_process() 追加推导

**问题：** `post_process()` 已经将所有极端 Specular 规范化为 (0.80, 0.80, 0.80)。然后 Legacy 推导用 `specLum > 0.5f → metalness=0.8`，结果**所有经过规范化的材质都会变成金属**。Generals 的车辆虽然大部分是涂漆金属，但建筑、兵种大量是非金属材质。全变成金属效果是错的。

**修正：**
```
metalness 默认应为 0.0（非金属 dielectric 假设）
仅在以下情况设为非零：
  1. 模型确实有高光贴图（specular map）且高光颜色偏白 → 金属
  2. 原始（未规范化前）的 Specular 值本身就很高（保存原值）
改为：metalness = 0.0f;  // 默认 dielectric，粗糙度控制视觉差异
```

### 1.2 Shader 常量寄存器全局冲突未审计

**位置：** Phase 4 常量寄存器布局

**问题：** 计划列出了 PBR 的寄存器布局，但没有对照现有全部 shader 验证是否有冲突。已知树 shader (`W3DTreeBuffer.cpp`) 已使用 `c4-c9` 和 `c32-c33`。需对所有 ShaderTypes 做完整审计。

**修正：** 新增一张寄存器占用总表，标注每个 shader 用了哪些范围，确保 PBR 的布局不与任何活跃 shader 重叠。

| Shader | 使用的常量寄存器 | 冲突风险 |
|--------|----------------|---------|
| Terrain PBR (Phase 2) | c0, c1 | PBR unit 不同时激活 → 无冲突 |
| Water PBR (Phase 1) | c0, c1, c2 | 同上 |
| Tree shader | c4-c9, c32-c33 | PBR 不同时激活，但注册习惯需注意 |
| Legacy PBR | c8, c9, c10, c11 | 与 PBR unit 不同时激活 |
| PBR Unit (新) | c0-c11, c16-c31 | 需要确保 c16-c31 全局未被占用 |

---

## 二、HIGH — 可能导致崩溃或性能问题

### 2.1 DX8TextureCategoryClass::Render() 热路径性能

**位置：** Phase 3.5, `dx8renderer.cpp` ~第 1705 行

**问题：** `Render()` 是每帧每个网格的 draw call（几百到几千次/帧）。在其中做字符串拼接、`strrchr`、`strcat`、哈希表查找等操作会严重拖慢性能。

**修正：**
```cpp
// 不要在 Render() 中做字符串操作！
// 改为在纹理加载时 (W3DAssetManager::Get_Texture()) 缓存结果到
// TextureClass 的某个 side-channel：
//   1. 在 Get_Texture 返回的纹理上附加一个 Bool 标记
//   2. 或维护一个纹理指针 → Bool 的快速哈希表
// Render() 只需：if (textureHasPBR[tex]) { ... }
```

或者更进一步：加载时在 `TextureClass` 派生类上加一个 `m_hasPBR` 字段。

### 2.2 8 光源对 RTS 来说过度设计

**位置：** Phase 4, 8 光源常量上传

**问题：** C&C Generals 实战中很少有超过 2-3 个动态光源（太阳 + 1-2 个爆炸/火焰）。8 光源：
- 浪费 shader 指令（PS 2.0 指令预算宝贵）
- 增加常量上传带宽
- 没有可见的视觉提升

**修正：** 初始实现用 **4 光源**，8 作为远期扩展选项。PS 2.0 fallback 用 2 光源展开（性能保障）。

### 2.3 GlobalData.ini 注册缺失

**位置：** 所有 Phase

**问题：** 路线图部分列出了 INI 开关（`UsePBRTextures` / `UseLegacyPBR`等），但 Phase 3/4/5 的技术细节中没有一处提到在 `GlobalData.h` 新增字段 + `GlobalData.cpp` 注册 INI 表。

**修正：** 每个 Phase 的 Step 1 都必须是：
```
1. GlobalData.h 新增 Bool m_usePBRTextures / m_useLegacyPBR / ...
2. GlobalData.cpp INI 表注册 + 构造函数初始化 FALSE
```

### 2.4 法线贴图退化时 PS 3.0 的必要性

**位置：** Phase 4 Step 2, W3DModelDraw.cpp 切线计算

**问题：** 切线计算需要在模型加载时遍历所有三角形做 UV 梯度计算。对于建筑等大网格（数千三角形），加载时间可能显著增加。更关键的是：如果没有 PS 3.0 的导数指令（`dsx`/`dsy`），法线贴图在 PS 2.0 上的效果会差很多（需要手动计算 tangent 方向）。

**修正：** 切线加载增加一个进度指示或后台加载选项。明确标注法线贴图的效果在 PS 3.0 上明显优于 PS 2.0。

---

## 三、MEDIUM — 计划缺口

### 3.1 缺少 VS 常量寄存器布局

**位置：** Phase 4 常量寄存器布局

**遗漏：** 计划只列出了 PS 常量，没有 VS 的。VS 需要：
```
c0-c3: WorldViewProj (VS 独立寄存器空间，不与 PS 冲突)
c4-c7: World matrix (法线变换)
c8:    摄像机世界坐标
```

### 3.2 缺少 BRDF LUT 生成工具和流程

**位置：** Phase 5

**遗漏：** `brdf_lut.dds` 不是手绘出来的，需要一个 GGX BRDF 积分渲染器生成。计划没提这个工具怎么写。

**补充：** 需要写一个小的 C++ 工具或用 Python + numpy 生成：
```python
# brdf_lut_generator.py — 预计算 GGX 积分查找表
for x in range(256):  # NdotV
    for y in range(256):  # roughness
        # 重要性采样 GGX，累加 F*G*V / (4*NdotV*NdotL)
        ...
```

### 3.3 IBL 环境贴图的来源不明确

**位置：** Phase 5

**遗漏：** `env_default_diff.dds` 和 `env_default_spec.dds` 的图像内容从哪里来？Generals 没有 HDR 天空盒系统。

**补充：** 两种方案：
1. 用代码合成（基于当前游戏 sun color + ambient + 简单 gradient sky）
2. 用外部工具对游戏实际场景截图再烘焙

推荐先用方案 1（纯代码生成，不依赖资产）。

### 3.4 没有 PBR Debug 可视化模式

**位置：** 全局

**遗漏：** PBR 调试极其困难，无法肉眼判断"是 roughness 错了还是光照方向错了"。应该有调试模式：

| 模式 | 显示内容 | 目的 |
|------|---------|------|
| `PBRDebugMode=0` | 正常渲染 | 关闭调试 |
| `PBRDebugMode=1` | Roughness 灰度图 | 确认粗糙度贴图加载正确 |
| `PBRDebugMode=2` | Metalness 二值图 | 确认金属度通道 |
| `PBRDebugMode=3` | 法线强度 | 确认法线贴图 |
| `PBRDebugMode=4` | 光源数量/位置 | 确认 8 光源系统 |

通过 `GlobalData.ini` + pixel shader 的 conditional output 实现（或切换不同调试 shader）。

### 3.5 没有明确的测试场景

**位置：** 所有 Phase 的验证步骤

**遗漏：** "游戏内确认"太模糊。

**补充：** 每个 Phase 准备标准测试场景清单：

| 场景 | 测试内容 | 通过标准 |
|------|---------|---------|
| 空地 + 太阳移动 | 地形 PBR (Phase 2 回归) | 路面正常，无闪烁 |
| 水面 + 反射 | 水面 PBR (Phase 1 回归) | 倒影可见，波光自然 |
| 单个单位 (坦克) | Phase 3 _pbr.dds + Phase 4 | 高光颜色正确 |
| 单个建筑 | Phase 3.5 Legacy PBR | 不出现全金属效果 |
| 大规模混战 (10单位+5建筑) | 性能测试 | 维持 30fps |
| 雪地/沙漠地图 | 贴图兼容性 | 无花屏/无崩溃 |

### 3.6 PS 2.0 Fallback 的 4 光源展开指令预算分析

**位置：** Phase 4

**遗漏：** PS 2.0 上限 96 条指令。一个完整的 GGX 评估（NDF + Smith G + Fresnel）约 15-20 条指令。4 光源展开 = 4 × 20 = 80 + 贴图采样 + 输出 ≈ 95 条。**刚好卡线**，必须验证。

**补充：** 在实现前先用 HLSL 写一个指令估算，确认 4 光源在 PS 2.0 的 96 条指令内可行。

### 3.7 缺少 Shader 编译构建集成

**位置：** 全局

**遗漏：** `.hlsl → .fxo` 的编译是手动执行还是自动集成到构建脚本？目前 `build_latest.bat` 只调 msdev.exe。

**补充：** 在 `build_latest.bat` 中加一行：
```batch
if exist "%DXSDK_DIR%\Utilities\bin\x86\fxc.exe" (
    fxc.exe /T ps_3_0 /E PBR_PS /Fo Shaders\pbr_unit_ps_30.fxo Shaders\pbr_unit_ps.hlsl
    fxc.exe /T ps_2_0 /E PBR_PS /Fo Shaders\pbr_unit_ps_20.fxo Shaders\pbr_unit_ps.hlsl
    fxc.exe /T vs_2_0 /E PBR_VS /Fo Shaders\pbr_unit_vs.fxo Shaders\pbr_unit_vs.hlsl
)
```
或者首次编译一次后 `.fxo` 提交到 git，不作为构建依赖。

### 3.8 水面 PBR 常量与 Phase 4 常量冲突

**位置：** Phase 1 ↔ Phase 4 接口

**问题：** 水面 PBR 使用 `c0=sunDirection, c1=sunColor, c2=timeOffset`。Phase 4 单位 PBR 使用 `c0-c3=WVP, c4-c7=World`。水面和单位是不同的 draw call，各自 set() 会覆盖常量寄存器。但如果在同一个 frame 中水面和单位先后渲染，顺序如下：

```
drawSea()       → SetPixelShaderConstantF(0, sunDir, 1)  // 覆盖 c0
drawUnit()      → SetVertexShaderConstantF(0, wvp, 4)     // VS c0，不与 PS 冲突
drawUnit()      → SetPixelShaderConstantF(0, sunDir, 1)   // 设置 PS c0
```

因为 VS 和 PS 的常量寄存器是**独立**的（VS c0 ≠ PS c0），所以没有问题。但需要注意同一 shader 类型内部的寄存器复用。

---

## 四、建议的结构性调整

### 4.1 Phase 顺序调整

当前是 Phase 3 → 3.5 → 4 → 5。建议改为：

```
Phase 3: PBR 贴图管线 (_pbr.dds 探测 + GlobalData INI + W3DPBRShader 框架)
    ↓ 这些是基础设施，Phase 4 和 Phase 5 都依赖
Phase 4: 单位/建筑 PBR (从最简单的开始：只改一个模型，验证通过再铺开)
    ↓ 
Phase 3.5: Legacy 旧模型兼容 (等 Phase 4 完整 PBR 跑通后再做 fallback)
    ↓ 有了完整 PBR 的经验，做 Legacy fallback 更准确
Phase 5: HDR/IBL (永远最后)
```

**理由：** Legacy 兼容本质上是对 Phase 4 的 fallback。先实现完整的 PBR 路径，理解所有边缘情况后，再做 Legacy 推导会更准确。

### 4.2 性能预算目标

**遗漏：** 整个计划没有性能目标。建议设为：

| 场景 | 最低帧率 | 硬件目标 |
|------|---------|---------|
| 空地 (无单位) | 60fps | GeForce 9600 GT |
| 小规模战斗 (20 单位) | 30fps | 同上 |
| 大规模战斗 (100 单位) | 20fps (可接受) | 同上 |
| PS 2.0 fallback | +10fps 相比 PS 3.0 | 同上 |

---

## 五、修正后计划需追加的内容清单

| # | 追加项 | 优先级 | 所属 Phase |
|---|--------|--------|-----------|
| 1 | Legacy metalness 默认 0.0 而非 0.8 | CRITICAL | 3.5 |
| 2 | 全局寄存器占用审计表 | HIGH | 4 |
| 3 | Render() 热路径改用预缓存标记而非运行时字符串 | HIGH | 3.5 |
| 4 | 4 光源起步，8 光源远期 | HIGH | 4 |
| 5 | GlobalData.h/.cpp INI 注册 | HIGH | 所有 |
| 6 | VS 常量寄存器布局 | MEDIUM | 4 |
| 7 | BRDF LUT 生成工具 | MEDIUM | 5 |
| 8 | IBL 环境贴图来源方案 | MEDIUM | 5 |
| 9 | PBR Debug 可视化模式 | MEDIUM | 全局 |
| 10 | 标准测试场景清单 | MEDIUM | 所有 |
| 11 | PS 2.0 指令预算估算 | MEDIUM | 4 |
| 12 | fxc.exe 构建集成 | MEDIUM | 全局 |
| 13 | 性能预算目标 | MEDIUM | 全局 |
| 14 | Phase 顺序：3→4→3.5→5 | MEDIUM | 全局 |
| 15 | 日冕 Mod 章节标注为"仅供参考" | LOW | 附录 |
