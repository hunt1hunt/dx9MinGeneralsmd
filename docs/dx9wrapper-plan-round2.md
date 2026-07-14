# 第2轮：DX8Wrapper→DX9Wrapper 改造 — 详细设计

> 三轮迭代之第2轮：深入设计三层改造的具体实现
> 日期: 2026-07-14

---

## 一、三层改造架构总览

```
┌───────────────────────────────────────────────┐
│   Layer C: 改名 DX8→DX9（纯符号，最后做）       │
│   Δ ≈ 750 处引用，不影响行为                    │
├───────────────────────────────────────────────┤
│   Layer B: 解除 POT 限制 + 常量提升           │
│   Δ ≈ 35 行，MRT 的前置条件                    │
├───────────────────────────────────────────────┤
│   Layer A: MRT 支持（核心功能）                │
│   Δ ≈ 95 行，按原计划做 3-RT G-Buffer          │
├───────────────────────────────────────────────┤
│   现有 DX8Wrapper — 零修改基础设施             │
│   状态管理 / 设备生命周期 / 纹理缓存            │
└───────────────────────────────────────────────┘
```

---

## 二、Layer A：MRT 支持 — 详细设计

### 2.1 DX8Caps 改动 (dx8caps.h/.cpp)

**新增文件 dx8caps.h：** (+2 行)

```cpp
class DX8Caps {
    // ... 现有内容 ...
    
    // 新增：
    static int Get_Num_Simultaneous_RTs() { return MaxSimultaneousRTs; }
    
private:
    // ... 现有内容 ...
    static int MaxSimultaneousRTs;  // 新增
};
```

**新增文件 dx8caps.cpp：** (+3 行)

```cpp
// 静态初始化
int DX8Caps::MaxSimultaneousRTs = 1;  // 默认 1（无 MRT）

// 在 Compute_Caps 或 Init_Caps 的 ForcedPostInit 位置添加
// 注意：因为用 DX8CALL 调用 GetDeviceCaps 时 D3DCAPS8 就是 D3DCAPS9
// NumSimultaneousRTs 已经存在
int DX8Caps::Check_MRT_Support(const D3DCAPS8& caps)
{
    MaxSimultaneousRTs = caps.NumSimultaneousRTs;
    if (MaxSimultaneousRTs < 1) MaxSimultaneousRTs = 1;
    return MaxSimultaneousRTs;
}
```

**调用位置：** `Init_Caps()` 中 `GetDeviceCaps` 调用之后，或在 `Check_Shader_Support()` 旁添加 `Check_MRT_Support()`。

### 2.2 DX8Wrapper 新增状态 (dx8wrapper.h)

**现有全局静态变量区：**

```cpp
// ... 现有 ...
static IDirect3DSurface8* DefaultRenderTarget;
static IDirect3DSurface8* CurrentRenderTarget;
static IDirect3DSurface8* CurrentDepthBuffer;

// 新增 — 紧跟在上面之后
#define MAX_SIMULTANEOUS_RTS 4      // D3D9 最大 MRT 数量
static IDirect3DSurface8* CurrentMRTSurfaces[MAX_SIMULTANEOUS_RTS];
static Int                 m_activeMRTCount;
```

**初始化（Init() 中现有的 memset 区增加）：**

```cpp
// 现有 memset 在 Init() L243-249
memset(Textures, 0, sizeof(IDirect3DBaseTexture8*) * MAX_TEXTURE_STAGES);
memset(RenderStates, 0, sizeof(unsigned) * 256);
// ... 等 ...

// 新增：
memset(CurrentMRTSurfaces, 0, sizeof(IDirect3DSurface8*) * MAX_SIMULTANEOUS_RTS);
m_activeMRTCount = 0;
```

### 2.3 Set_Render_Target(index, surface) — 核心实现 (dx8wrapper.cpp)

```cpp
// ============================================================================
// Set_Render_Target — 多索引版本 (MRT)
//
// index: 0 = 主 RT（复用现有逻辑），1..MAX_SIMULTANEOUS_RTS-1 = 附加 RT
// surface: NULL = 解除该索引的 RT
//
// 使用约束:
//   - 必须先设置 index=0 的主 RT，然后才能设置 index>=1 的附加 RT
//   - 所有 MRT 表面必须同尺寸、同格式（D3D9 限制）
//   - 恢复默认 RT 时(index=0, surface=NULL)，自动解除所有附加 RT
// ============================================================================
void DX8Wrapper::Set_Render_Target(Int index, IDirect3DSurface8* surface)
{
    DX8_THREAD_ASSERT();
    DX8_Assert();
    
    // 验证 index 范围
    if (index < 0 || index >= MAX_SIMULTANEOUS_RTS)
        return;
    
    if (index == 0) {
        // 主 RT：复用现有逻辑
        // 注意：现有 Set_Render_Target(surface) 在切换到 NULL/默认时会走不同路径
        // 保持完全一致的行为
        Set_Render_Target(surface);
        return;
    }
    
    // MRT 附加目标 (index >= 1)
    // 检查硬件支持
    if (DX8Caps::Get_Num_Simultaneous_RTs() <= index) {
        WWDEBUG_SAY(("DX8Wrapper: MRT index %d not supported (max %d)\n", 
            index, DX8Caps::Get_Num_Simultaneous_RTs() - 1));
        return;
    }
    
    // 检查主 RT 已设置
    if (CurrentRenderTarget == NULL) {
        WWDEBUG_SAY(("DX8Wrapper: Must set render target 0 before MRT index %d\n", index));
        return;
    }
    
    // 释放旧的 surface
    if (CurrentMRTSurfaces[index] != NULL) {
        CurrentMRTSurfaces[index]->Release();
        CurrentMRTSurfaces[index] = NULL;
    }
    
    if (surface != NULL) {
        // 检查同尺寸（仅 debug 模式）
        #ifdef _DEBUG
        D3DSURFACE_DESC desc0, descN;
        CurrentRenderTarget->GetDesc(&desc0);
        surface->GetDesc(&descN);
        WWASSERT(desc0.Width == descN.Width && desc0.Height == descN.Height);
        WWASSERT(desc0.Format == descN.Format);
        #endif
        
        // 设置 MRT
        DX8CALL(SetRenderTarget(index, surface));
        surface->AddRef();
        CurrentMRTSurfaces[index] = surface;
        
        // 更新活跃 MRT 计数
        if (index >= m_activeMRTCount)
            m_activeMRTCount = index + 1;
    } else {
        // 解除 MRT
        DX8CALL(SetRenderTarget(index, NULL));
        
        // 重新计算活跃计数
        while (m_activeMRTCount > 1 && CurrentMRTSurfaces[m_activeMRTCount - 1] == NULL)
            m_activeMRTCount--;
    }
}
```

### 2.4 修改现有 Set_Render_Target(surface) — 在恢复默认时解除 MRT

```cpp
void DX8Wrapper::Set_Render_Target(IDirect3DSurface8* render_target)
{
    // ... 现有逻辑 ...
    
    if (render_target == NULL || render_target == DefaultRenderTarget) {
        
        // *** 新增：在切换到主 RT 前解除所有附加 MRT ***
        for (int i = 1; i < m_activeMRTCount; i++) {
            if (CurrentMRTSurfaces[i] != NULL) {
                DX8CALL(SetRenderTarget(i, NULL));  // 解除
                CurrentMRTSurfaces[i]->Release();
                CurrentMRTSurfaces[i] = NULL;
            }
        }
        m_activeMRTCount = 0;
        // *** 结束新增 ***
        
        // ... 继续原逻辑 —— 恢复默认 RT ...
        if (DefaultRenderTarget != NULL) {
            DX8CALL(SetRenderTarget(DefaultRenderTarget, depth_buffer));
            // ...
        }
    }
    // ...
}
```

### 2.5 Device Reset 中的 MRT 处理

```cpp
bool DX8Wrapper::Reset_Device(bool reload_assets)
{
    if ((IsInitted) && (D3DDevice != NULL)) {
        // 新增：释放 MRT 引用
        for (int i = 0; i < MAX_SIMULTANEOUS_RTS; i++) {
            if (CurrentMRTSurfaces[i] != NULL) {
                CurrentMRTSurfaces[i]->Release();
                CurrentMRTSurfaces[i] = NULL;
            }
        }
        m_activeMRTCount = 0;
        
        // ... 继续原有 Reset 逻辑 ...
        Set_Vertex_Buffer(NULL);
        Set_Index_Buffer(NULL, 0);
        // ...
    }
}
```

**注意：** MRT 表面的 `ReleaseResources` 应由拥有该表面的具体类（如 `W3DDeferredRenderer`）调用。这里的代码只释放包装器持有的引用（AddRef 在 Set_Render_Target 中），实际的 D3D 表面释放由纹理/持有者的 `ReleaseResources` 负责。

### 2.6 MRT 使用示例（W3DDeferredRenderer）

```cpp
// 3-RT G-Buffer 设置
void W3DDeferredRenderer::beginGBufferPass()
{
    // 1. 获取 3 张 RT 的 surface
    IDirect3DSurface8* surf0 = m_rtAlbedo->Get_D3D_Surface_Level(0);
    IDirect3DSurface8* surf1 = m_rtNormal->Get_D3D_Surface_Level(0);
    IDirect3DSurface8* surf2 = m_rtEmissive->Get_D3D_Surface_Level(0);
    
    // 2. 设置主 RT (index 0) — 复用现有逻辑，自动保存旧 RT
    DX8Wrapper::Set_Render_Target(surf0);
    
    // 3. 设置附加 MRT (index 1, 2)
    DX8Wrapper::Set_Render_Target(1, surf1);
    DX8Wrapper::Set_Render_Target(2, surf2);
    
    // 4. 设置深度缓冲 — 主 RT 已有深度，无需额外操作
    
    // 5. 清除
    DX8Wrapper::Clear(false, true, Vector3(0,0,0), 1.0f);  // Z+Stencil
    // 注意：Clear 只清除主 RT 颜色。需要额外清除附加 RT
    // D3D9 的 Clear 可以同时清除所有 MRT — 使用 D3DCLEAR_TARGET
    // 但 DX8Wrapper::Clear 需要检查是否支持
    DX8Wrapper::Clear(true, false, Vector3(0,0,0), 1.0f);  // Color
    // DX9 中 Clear 清除所有 MRT 的 color（如果传 D3DCLEAR_TARGET）
    
    // 释放临时 surface 引用
    surf0->Release();
    surf1->Release();
    surf2->Release();
}

void W3DDeferredRenderer::endGBufferPass()
{
    // 解除 MRT（附带在 Set_Render_Target(NULL) 中自动处理）
    // 也可手动逐个解除：
    // DX8Wrapper::Set_Render_Target(2, NULL);
    // DX8Wrapper::Set_Render_Target(1, NULL);
    
    // 恢复默认 RT
    DX8Wrapper::Set_Render_Target((IDirect3DSurface8*)NULL);
}
```

---

## 三、Layer B：POT 限制解除 + 常量提升

### 3.1 Create_Render_Target 添加 allowNonPOT 参数

**dx8wrapper.h：**
```cpp
// 新增重载
static TextureClass* Create_Render_Target(int width, int height, bool alpha, bool allowNonPOT);
```

**dx8wrapper.cpp：**

增加参数后的实现（保留原 3 参数版本为向后兼容包装）：

```cpp
TextureClass* DX8Wrapper::Create_Render_Target(int width, int height, bool alpha)
{
    return Create_Render_Target(width, height, alpha, false);  // 默认 POT
}

TextureClass* DX8Wrapper::Create_Render_Target(int width, int height, bool alpha, bool allowNonPOT)
{
    DX8_THREAD_ASSERT();
    DX8_Assert();
    const D3DCAPS8& dx8caps = DX8Caps::Get_Default_Caps();
    
    if (!allowNonPOT) {
        // 原有 POT 逻辑
        float poweroftwosize = width;
        if (height > 0 && height < width) poweroftwosize = height;
        poweroftwosize = ::Find_POT(poweroftwosize);
        if (poweroftwosize > dx8caps.MaxTextureWidth) poweroftwosize = dx8caps.MaxTextureWidth;
        if (poweroftwosize > dx8caps.MaxTextureHeight) poweroftwosize = dx8caps.MaxTextureHeight;
        width = height = poweroftwosize;
    }
    // ... 继续原有格式检测和创建逻辑 ...
}
```

**验证条件：** 非 POT 纹理要求：
- `D3DPTEXTURECAPS_NONPOW2CONDITIONAL` 标志
- 寻址模式必须为 `CLAMP`（对 RT 就是默认值）
- 不能有 mipmap（RT 使用 `MIP_LEVELS_1`）
- 这些条件延迟渲染完全满足

### 3.2 MAX_VERTEX_SHADER_CONSTANTS 提升

**当前值：** 96（D3D8 时代的上限）
**实际 SM3.0 值：** 256
**影响范围：** 只有 `dx8wrapper.h` 这一行

```cpp
// 原
#define MAX_VERTEX_SHADER_CONSTANTS 96

// 改为 — 条件编译
#define MAX_VERTEX_SHADER_CONSTANTS 256
```

注意：这个常量仅用于数组大小和 memcpy。编译器只分配实际需要的内存。提升到 256 增加 ~2.5KB 静态内存，无运行时开销。

**安全策略：** 仅在 SM3.0 路径中要求。如果查询到硬件只支持 vs_2_0（96 常量），应使用裁剪后的 shader 或回避。

### 3.3 MAX_PIXEL_SHADER_CONSTANTS

**当前值：** 8（ps_1_x - ps_2_0）
**实际 SM3.0 值：** 224

这个常量的影响更大——它控制 pixel shader 常量数组大小。

```cpp
// 改为运行时查询
#define MAX_PIXEL_SHADER_CONSTANTS_20 8
#define MAX_PIXEL_SHADER_CONSTANTS_30 224

// 动态选择
static int Get_Max_Pixel_Shader_Constants() {
    return (DX8Caps::Get_Pixel_Shader_Majon_Version() >= 3) 
        ? MAX_PIXEL_SHADER_CONSTANTS_30 
        : MAX_PIXEL_SHADER_CONSTANTS_20;
}
```

---

## 四、Layer C：改名 DX8→DX9 — 精确映射表

### 4.1 类名映射

```
DX8Wrapper              → DX9Wrapper
DX8Caps                 → DX9Caps
DX8_CleanupHook         → DX9_CleanupHook     (或 D3D_CleanupHook)
DX8TextureManagerClass  → DX9TextureManagerClass
DX8MeshRenderer         → DX9MeshRenderer      (或保留，非 DX8 命名)
DX8VertexBufferClass    → DX9VertexBufferClass
DX8IndexBufferClass     → DX9IndexBufferClass
```

### 4.2 宏/函数映射

```
DX8CALL                 → D3DCALL
DX8_THREAD_ASSERT       → D3D_THREAD_ASSERT
DX8_Assert              → D3D_Assert
DX8_RECORD_*            → D3D_RECORD_*
Get_DX8_Texture_*      → Get_D3D_Texture_*  (这是名称查找函数，不影响运行时)
```

### 4.3 d3d8compat.h 替换

改名完成后逐步消除 d3d8compat.h：
1. 先将 typedef 从 `IDirect3DDevice8 → IDirect3DDevice9` 改为源代码直接使用 `IDirect3DDevice9`
2. 这需要在所有源文件中替换类型名（~320 处）
3. 最后删除 d3d8compat.h 中不再需要的 typedef

**最佳实践：** 对于新代码，直接使用 D3D9 类型名。不在新代码中继续使用 D3D8 别名。

---

## 五、依赖关系与实施顺序

```
Layer A: MRT 支持
  ├── 前置: 无需 Layer B/C
  ├── 只改: dx8caps.h/cpp, dx8wrapper.h/cpp
  ├── 编译验证: 无需其他模块修改
  └── 后续: W3DDeferredRenderer 可直接使用 3-RT MRT

Layer B: POT 解除 + 常量提升
  ├── 前置: 可在 Layer A 之前或之后，独立
  ├── 只改: dx8wrapper.h/cpp
  ├── 编译验证: 向后兼容（不影响现有代码）
  └── 后续: 延迟渲染可创建任意尺寸的 G-Buffer RT

Layer C: 改名 DX8→DX9
  ├── 前置: 延迟渲染改造完成后
  ├── 改: 全库 ~750 处引用
  ├── 编译验证: 需全量编译
  └── 风险: 高工作量，与功能改造混合会产生大量冲突
```

**推荐实施顺序：** Layer A → Layer B → 延迟渲染改造 → Layer C
