# 第1轮：DX8Wrapper→DX9Wrapper 改造 — 全量清单与影响分析

> 基于对 dx8wrapper.h/cpp、dx8caps.h/cpp、d3d8compat.h、ww3d.cpp、W3DWater.cpp、W3DProjectedShadow.cpp 等核心文件的完整代码走读
> 日期: 2026-07-14

---

## 1. 核心架构事实

### 1.1 实际技术栈

```
d3d8compat.h — typedef D3D9 接口为 D3D8 名称（零运行时开销）
    ↓
DX8Wrapper — 状态管理 + 设备生命周期 + 纹理缓存（~4000 行）
    ↓
IDirect3DDevice9 — D3D9 底层 API
```

**关键事实：** 所有 "D3D8" 类型（`IDirect3DDevice8`, `IDirect3DSurface8`, `D3DCAPS8`）都是 D3D9 的纯 typedef。`D3DCAPS8` ⊂ `D3DCAPS9` 的子集关系不存在——它们是同一结构体。

### 1.2 这意味着

- `DX8Caps::Get_Default_Caps().NumSimultaneousRTs` **已经可以编译**（D3DCAPS9 的字段）
- 只是 `DX8Caps` 的 `Init_Caps()` 和 `Compute_Caps()` 从未查询过这个字段
- 改造仅需：添加查询 + 暴露接口

### 1.3 命名分布范围（影响改名的工作量）

```
"DX8" 命名的类/接口（影响 ≈320 个引用点）:
  DX8Wrapper           ~320 引用  — 核心类
  DX8Caps              ~120 引用  — 能力查询
  DX8TextureManager    ~20 引用   — 纹理缓存
  DX8MeshRenderer      ~100 引用  — 网格渲染器
  DX8_CleanupHook      ~10 引用   — 设备重置钩子
  
  子类型:
  DX8VertexBufferClass ~40 引用
  DX8IndexBufferClass  ~40 引用
  DX8FVF               ~15 引用
  
  宏/辅助:
  DX8CALL              ~80 引用   — D3D API 调用宏
  DX8_THREAD_ASSERT    ~20 引用
  DX8_Assert           ~5 引用
  DX8_RECORD_*         ~10 引用
  
  类型别名 (d3d8compat.h):
  IDirect3DDevice8     ~60 引用   — 实际是 IDirect3DDevice9
  IDirect3DSurface8    ~30 引用   — 实际是 IDirect3DSurface9
  D3DCAPS8             ~80 引用   — 实际是 D3DCAPS9
  IDirect3DTexture8    ~50 引用   — 实际是 IDirect3DTexture9
  其他 D3D8 类型       ~40 引用
```

**总量估算：** ~750 个引用点。其中 ~430 个是类名/函数名，~320 个是 d3d8compat.h 的类型别名。

---

## 2. MRT 支持 — 需要修改的位置（精确清单）

### 2.1 DX8Caps — 添加 NumSimultaneousRTs 查询

**文件：** `dx8caps.h` / `dx8caps.cpp`

```cpp
// dx8caps.h — 新增
static int Get_Num_Simultaneous_RTs() { return MaxSimultaneousRTs; }

// dx8caps.cpp — 新增
int DX8Caps::MaxSimultaneousRTs = 1;  // 静态初始化 = 1

// Init_Caps() 中 — 已存在 D3DDevice->GetDeviceCaps，加入：
DX8Caps::MaxSimultaneousRTs = caps.NumSimultaneousRTs;
```

Δ 代码量：~5 行。风险：无。

### 2.2 DX8Wrapper — 添加 MRT SetRenderTarget

**文件：** `dx8wrapper.h` / `dx8wrapper.cpp`

**当前状态：** `Set_Render_Target` 仅 index 0，使用 `IDirect3DSurface8*`

**需要新增：**

```cpp
// dx8wrapper.h — 新增 MRT 版本
// 设置指定索引的渲染目标。index=0 是主 RT，index>=1 是附加 MRT。
// 必须先调用 Set_Render_Target(0, surf) 建立主 RT，然后调用此函数设置附加 RT。
// 恢复时调用 Set_Render_Target_Index(1, NULL) 逐个解除，最后调用 Set_Render_Target(NULL) 恢复默认。
static void Set_Render_Target(Int index, IDirect3DSurface8* surface, Bool isDefault = false);

// 保存当前 MRT 状态（用于设备重置/恢复）
static void Save_Render_Targets();
static void Restore_Render_Targets();

// 当前活跃的 MRT 数量（除 index 0 外）
static Int m_currentMRTCount;
```

**dx8wrapper.h 现有状态变量区需扩展：**
```cpp
// 现有：
static IDirect3DSurface8* DefaultRenderTarget;
static IDirect3DSurface8* CurrentRenderTarget;
static IDirect3DSurface8* CurrentDepthBuffer;

// 新增：
static IDirect3DSurface8* CurrentMRTSurfaces[MAX_SIMULTANEOUS_RTS];
static Int m_activeMRTCount;
```

**dx8wrapper.cpp Set_Render_Target(index, surf)：**
```cpp
void DX8Wrapper::Set_Render_Target(Int index, IDirect3DSurface8* surface)
{
    DX8_THREAD_ASSERT();
    DX8_Assert();
    
    if (index == 0) {
        // 复用到原 Set_Render_Target(surf) 逻辑
        Set_Render_Target(surface);
        return;
    }
    
    // index > 0: MRT 附加目标
    // 保存旧的 surface
    if (CurrentMRTSurfaces[index] != NULL) {
        CurrentMRTSurfaces[index]->Release();
        CurrentMRTSurfaces[index] = NULL;
    }
    
    if (surface != NULL) {
        // 设置 MRT 目标
        DX8CALL(SetRenderTarget(index, surface));
        surface->AddRef();
        CurrentMRTSurfaces[index] = surface;
        m_activeMRTCount = max(m_activeMRTCount, index);
    } else {
        // 解除 MRT 目标
        DX8CALL(SetRenderTarget(index, NULL));
        // 重新计算 m_activeMRTCount
        while (m_activeMRTCount > 0 && CurrentMRTSurfaces[m_activeMRTCount] == NULL)
            m_activeMRTCount--;
    }
}
```

**关键注意事项：**
- `IDirect3DDevice9::SetRenderTarget(index, surf)` 的 index 0 必须始终是有效的 RT
- DX9 要求 MRT 的所有 RT 必须**同尺寸、同格式**（`D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING` 有额外要求）
- MRT 不需要单独的深度缓冲——深度缓冲只从 index 0 获取

**dx8wrapper.h Set_Render_Target(surf) 原函数需修改：**

```cpp
// 当解除 MRT 时，逐个释放附加 RT
// 原函数 Set_Render_Target((IDirect3DSurface8*)NULL) 需要：
// 1. 先解除所有 MRT 附加目标
// 2. 再解除主 RT
void Set_Render_Target(IDirect3DSurface8 *render_target)
{
    // ...原有逻辑...
    
    if (render_target == NULL || render_target == DefaultRenderTarget) {
        // 新增：先解除所有 MRT 附加目标
        for (int i = 1; i <= m_activeMRTCount; i++) {
            if (CurrentMRTSurfaces[i]) {
                DX8CALL(SetRenderTarget(i, NULL));
                CurrentMRTSurfaces[i]->Release();
                CurrentMRTSurfaces[i] = NULL;
            }
        }
        m_activeMRTCount = 0;
        // ...继续原逻辑...
    }
}
```

Δ 代码量：~50 行。风险：低——只扩展不修改现有逻辑。

### 2.3 Device Reset 中的 MRT 保存/恢复

**文件：** `dx8wrapper.cpp`

```cpp
// Reset_Device() 中 — 在 ReleaseResources 之前保存
// 新增：保存 MRT 状态
Save_Render_Targets();

// 在 Reset 成功后恢复
Restore_Render_Targets();
```

```cpp
void DX8Wrapper::Save_Render_Targets()
{
    // MRT 附加目标在设备重置后自动丢失
    // 不需要保存（CleanupHook 的 ReAcquireResources 会重建）
    for (int i = 0; i < MAX_SIMULTANEOUS_RTS; i++) {
        // 清除引用，避免悬空
        CurrentMRTSurfaces[i] = NULL;
    }
    m_activeMRTCount = 0;
}
```

Δ 代码量：~15 行。风险：低。

### 2.4 Create_Render_Target 解除 POT 限制（可选但推荐）

**文件：** `dx8wrapper.cpp`

当前 `Create_Render_Target` 强制 POT：
```cpp
float poweroftwosize = width;
if (height > 0 && height < width) poweroftwosize = height;
poweroftwosize = ::Find_POT(poweroftwosize);
width = height = poweroftwosize;
```

改为可选：
```cpp
TextureClass* DX8Wrapper::Create_Render_Target(int width, int height, bool alpha, bool allowNonPOT)
{
    // ...原有格式检测逻辑...
    
    if (!allowNonPOT) {
        float poweroftwosize = width;
        if (height > 0 && height < width) poweroftwosize = height;
        poweroftwosize = ::Find_POT(poweroftwosize);
        if (poweroftwosize > dx8caps.MaxTextureWidth) poweroftwosize = dx8caps.MaxTextureWidth;
        if (poweroftwosize > dx8caps.MaxTextureHeight) poweroftwosize = dx8caps.MaxTextureHeight;
        width = height = poweroftwosize;
    }
    // ...继续创建...
}
```

**注意：** SM3.0 硬件 `D3DPTEXTURECAPS_NONPOW2CONDITIONAL` 支持非 POT 纹理，但条件限制是：
- 不能对非 POT 纹理使用 wrap 寻址模式（必须 clamp）
- Mipmap 自动禁用

对于 G-Buffer RT（`MIP_LEVELS_1`，`CLAMP` 寻址），非 POT 完全可行。

```cpp
// 更安全的方案 — 自动检测
bool canUseNonPOT = false;
if (!allowNonPOT) {
    // ... 原有 POT 逻辑 ...
} else {
    // 验证硬件支持非 POT RT
    const D3DCAPS8& caps = DX8Caps::Get_Default_Caps();
    if (caps.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL) {
        canUseNonPOT = true;
    }
    if (!canUseNonPOT) {
        // fallback to POT
        // ... 原有 POT 逻辑 ...
    }
}
```

Δ 代码量：~20 行。风险：中——需要验证非 POT 纹理在 D3D9On12 上兼容。

---

## 3. 改名 DX8→DX9 的策略分析

### 3.1 三种改名策略

| 策略 | 工作量 | 风险 | 特点 |
|------|--------|------|------|
| A: 全量替换 | ~750 处 | 高 | 一次性 sed 替换所有"DX8"→"DX9" |
| B: 类名替换 + d3d8compat.h 保留 | ~430 处 | 中 | 只改类名/函数名，保留类型别名 |
| C: 渐进式 — 功能改造时不改名 | 0 处（现改功能） | 低 | 先加 MRT，以后集中改名 |

**推荐：策略 C（先功能，后改名）。**

理由：
1. 改名是纯符号操作，不改变任何行为
2. 改名会与正在进行的 MRT 改造产生大量合并冲突
3. 改名后的验证需要全量编译，而当前项目处于"恢复构建"阶段
4. 改名后 `d3d8compat.h` 是否保留不改变任何功能

### 3.2 改名计划（策略 C — 在延迟渲染改造完成后执行）

**阶段划分：**
```
改名阶段 A: DX8Wrapper → DX9Wrapper（~320 处）
改名阶段 B: DX8Caps → DX9Caps（~120 处）
改名阶段 C: DX8*Buffer → DX9*Buffer（~80 处）
改名阶段 D: d3d8compat.h → d3d9direct.h + 类型别名清理（~320 处）
```

每个阶段的步骤相同：
1. sed 替换类名（`DX8Wrapper`→`DX9Wrapper` 等）
2. 更新引用头文件
3. 全量编译 → 修复错误
4. 运行验证

---

## 4. 风险清单

| 风险 | 等级 | 说明 |
|------|------|------|
| MRT 附加目标 SetRenderTarget 失败 | 中 | 某些 D3D9 驱动/虚拟显卡不支持 MRT。处理：在 Set_Render_Target(index) 时检查返回值，失败时 mark MRT unavailable |
| DX9 MRT 同尺寸/同格式限制 | 中 | 所有 RT 必须同一尺寸和格式。对于 3-RT G-Buffer (都是 A8R8G8B8, 同分辨率) 没问题 |
| Device Reset 后 MRT 表面悬空 | 中 | D3D 设备重置会使 POOL_DEFAULT 的 RT 表面失效。ReleaseResources 中必须释放所有 CurrentMRTSurfaces |
| 非 POT RT 在 D3D9On12 上兼容性 | 中 | D3D9On12 可能不支持某些非 POT 纹理。处理：非 POT 作为可选项，运行时检测 |
| MAX_VERTEX_SHADER_CONSTANTS=96 限制 | 低 | 与 MRT 无关，但 SM3.0 实际支持 256。可分阶段提高 |
| MAX_PIXEL_SHADER_CONSTANTS=8 限制 | 低 | ps_2_0 路径的限制。新延迟渲染代码只走 ps_3_0（224 常量） |
