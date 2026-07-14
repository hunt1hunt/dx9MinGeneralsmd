# DX8Wrapper→DX9Wrapper + MRT 改造 — 最终实施计划

> 经过 3 轮检讨式迭代和完整代码走读（dx8wrapper.h/cpp、dx8caps.h/cpp、d3d8compat.h、ww3d.cpp）
> 日期: 2026-07-14

---

## 核心理念

这个 wrapper**底层已经是 D3D9**（加载 `D3D9.DLL`，使用 `IDirect3DDevice9`）。"DX8" 只是 2003 年留下的命名包袱，通过 `d3d8compat.h`（一份 typedef 映射表）实现类型兼容。改造分三层，不耦合，可独立进行：

1. **Layer A（核心功能）：** 添加 MRT 支持（~95 行）
2. **Layer B（前置优化）：** 解除 POT 限制 + 常量提升（~35 行）
3. **Layer C（符号清理）：** 改名 DX8→DX9（最后做）

---

## Layer A：MRT 支持（开工第 1 天）

### A1. DX8Caps — 添加 MaxSimultaneousRTs 查询

**文件：** `dx8caps.h` / `dx8caps.cpp`

```
dx8caps.h: +2 行（Get_Num_Simultaneous_RTs() + MaxSimultaneousRTs 声明）
dx8caps.cpp: +3 行（静态初始化 + Init_Caps 中查询 caps.NumSimultaneousRTs）
```

`D3DCAPS8` 就是 `D3DCAPS9` 的 typedef——`NumSimultaneousRTs` 字段已经存在，只需读出来。

### A2. DX8Wrapper — 新增状态变量

**文件：** `dx8wrapper.h`

```cpp
// 在现有的 DefaultRenderTarget / CurrentRenderTarget 旁边
#define MAX_SIMULTANEOUS_RTS 4
static IDirect3DSurface8* CurrentMRTSurfaces[MAX_SIMULTANEOUS_RTS];
static Int                m_activeMRTCount;
```

`Init()` 中 memset 零。

### A3. DX8Wrapper — Set_Render_Target 多索引版本

**文件：** `dx8wrapper.cpp`

```cpp
// 新增：
void DX8Wrapper::Set_Render_Target(Int index, IDirect3DSurface8* surface);

// 修改：
Set_Render_Target(IDirect3DSurface8*) — 
  在恢复默认 RT 前解除所有附加 MRT（for i=1..m_activeMRTCount）
  
Reset_Device() — 在释放资源前清除 CurrentMRTSurfaces 引用
```

### A4. 验证方法

**最小可测试单元：** 创建一个测试函数：
```
1. 创建 2 张 A8R8G8B8 RT
2. Set_Render_Target(0, rt0)
3. Set_Render_Target(1, rt1)
4. Clear → DX9 的 Clear 会清除所有 MRT
5. 渲染一个三角形到 color
6. Set_Render_Target(NULL) 恢复
7. 读回两张 RT 的内容验证
```

**无需延迟渲染代码即可验证 MRT 是否工作。**

---

## Layer B：POT 解除 + 常量提升（开工第 1 天，与 A 并行）

### B1. Create_Render_Target 添加 allowNonPOT

**文件：** `dx8wrapper.h` / `dx8wrapper.cpp`

```cpp
// 新重载
TextureClass* Create_Render_Target(int w, int h, bool alpha, bool allowNonPOT);

// 旧版本包装
TextureClass* Create_Render_Target(int w, int h, bool alpha) {
    return Create_Render_Target(w, h, alpha, false);
}
```

实现中：`allowNonPOT=true` 时跳过 `Find_POT` 强制扩大的逻辑。适用于 G-Buffer RT，不破坏现有阴影/水 RT 代码。

### B2. Shader 常量上限提升

**文件：** `dx8wrapper.h`

```cpp
// vs_3_0 实际支持 256，ps_3_0 实际支持 224
#define MAX_VERTEX_SHADER_CONSTANTS 256   // 原 96
#define MAX_PIXEL_SHADER_CONSTANTS  224   // 原 8（走 ps_3_0 路径时）
```

仅增加静态数组大小，无运行时开销。ps_2_0 回退路径使用裁剪后的 shader。

---

## Layer C：改名（延迟渲染完成后再做）

### 策略：先改新代码用 D3D9 原生类型，再改旧代码

**新代码规则（立即生效）：**
- 新写的源文件直接 `#include <d3d9.h>`，用 `IDirect3DDevice9`, `IDirect3DSurface9` 等原生 D3D9 类型
- 不在新代码中传播 `DX8Wrapper` 名称——内部统一叫 `D3DWrapper`，外部兼容别名保留
- `d3d8compat.h` 的 typedef 保持不动（维护旧代码编译）

**全量改名（后期独立阶段）：**
- 阶段性 `sed` 替换：`DX8Wrapper` → `D3DWrapper`（或 `DX9Wrapper`）
- 每个替换后全量编译 + 验证
- 不与其他功能改造同时进行

---

## 文件级变更清单（精确）

| # | 文件 | 操作 | Δ 代码 | 阶段 |
|--|------|------|--------|------|
| 1 | `dx8caps.h` | 新增 Get_Num_Simultaneous_RTs + MaxSimultaneousRTs | +2 行 | A |
| 2 | `dx8caps.cpp` | 初始化 + Init_Caps 中查询 | +5 行 | A |
| 3 | `dx8wrapper.h` | 新增 CurrentMRTSurfaces[MAX_SIMULTANEOUS_RTS] 等 | +5 行 | A |
| 4 | `dx8wrapper.h` | MAX_VERTEX/PIXEL_SHADER_CONSTANTS 提升 | +2 行 | B |
| 5 | `dx8wrapper.cpp` | Set_Render_Target(index, surf) 新函数 | +55 行 | A |
| 6 | `dx8wrapper.cpp` | Set_Render_Target(surf) 修改 — 解除 MRT | +8 行 | A |
| 7 | `dx8wrapper.cpp` | Reset_Device — 清除 MRT 引用 | +6 行 | A |
| 8 | `dx8wrapper.cpp` | Init — MRT 状态 memset/k 新增 | +2 行 | A |
| 9 | `dx8wrapper.cpp` | Create_Render_Target allowNonPOT 重载 | +20 行 | B |
| | **总计** | | **~105 行** | |

---

## 工期估算

| 阶段 | 内容 | 单人 | 验证方法 |
|------|------|------|---------|
| **A1** | DX8Caps 查询 + 测试打印 | 30 分钟 | 编译 + 运行时日志看到 NumSimultaneousRTs |
| **A2** | 状态变量 + Init 初始化 | 15 分钟 | 编译通过 |
| **A3** | Set_Render_Target(index) 实现 | 60 分钟 | 2-RT 测试（清除不同颜色后读回） |
| **B1** | Create_Render_Target allowNonPOT | 30 分钟 | 编译 + 运行时验证非 POT RT 创建 |
| **B2** | 常量提升 | 5 分钟 | 编译通过 |
| | **合计** | **~2.5 小时** | |

---

## 风险与应对

| 风险 | 概率 | 应对 |
|------|------|------|
| 某 D3D9 驱动 SetRenderTarget(index) 返回 D3DERR_INVALIDCALL | 中 | Set_Render_Target 中检查 HRESULT，失败时 mark MRT unavailable（后续调用自动跳过） |
| D3D9On12 (Win10/11) 不支持 MRT | 低 | D3D9On12 支持 SM3.0，MRT 是 SM2.0+ 的标配功能。D3D9On12 已通过大量游戏验证 |
| MRT Clear 只清除了主 RT | 中 | DX9 的 Clear 行为：Clear(ALL_TARGETS) 清除所有 MRT。测试验证 |
| Device Reset 后 MRT 表面悬空 | 低 | Reset_Device 中清除 CurrentMRTSurfaces 引用。实际的 D3D 表面由拥有者（W3DDeferredRenderer）的 ReleaseResources 释放 |
| 与现有代码的冲突 | 极低 | 只新增不修改。Set_Render_Target(surf) 的修改仅在 surface==NULL 时执行额外循环，不影响任何现有非 NULL 路径 |

---

## 后续路线图

```
Layer A+B 完成（~2.5 小时）
    │
    ├──→ 延迟渲染 Phase 0: W3DDeferredRenderer 框架
    │     使用 3-RT MRT 方案（原计划真正可执行了！）
    │     文件: W3DDeferredRenderer.h/.cpp 等
    │
    ├──→ 延迟渲染 Phase 1-9（按之前计划的 10 Phase）
    │     完整 3-RT G-Buffer + 光照 Pass + 透明回退等
    │
    └──→ Layer C: DX8→DX9 改名（可选，无功能影响）
```

**MRT 支持没有浪费任何工作量**——它是延迟渲染的必需前置，且 Layer A 的代码（~95 行）独立、低风险、不依赖其他任何改造。
