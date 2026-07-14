# 延迟渲染改造 — 检查点

> 最后更新: 2026-07-14
> 执行入口: `docs/deferred-rendering-master-plan.md`

---

## 当前进度

| 阶段 | 状态 | 说明 |
|------|------|------|
| **P0** — DX8Wrapper MRT 支持 | ✅ **已完成** | 4 文件修改: dx8caps.h/cpp + dx8wrapper.h/cpp |
| **P1** — POT 解除 + 常量提升 | ✅ **已完成** | 3 处修改: 常量 96→256, 8→224, Create_Render_Target 增加 allowNonPOT |
| **Phase 0** — 枚举 + W3DDeferredRenderer 框架 | ✅ **已完成** | 9 文件: 新建 2, 修改 7 |
| **Phase 1** — G-Buffer RT 资源管理 | ✅ **已完成** | 3 文件: W3DDeferredRenderer.h/.cpp + dx8wrapper.h GetCleanupHook |
| **Phase 2** — G-Buffer 写入 | ✅ **已完成** | 6 文件: dx8wrapper.h/.cpp, W3DShaderManager.cpp, W3DScene.cpp, dx8renderer.cpp |
| **Phase 3-9** | ⏳ 待开始 | |

## Phase P0 变更清单

### dx8caps.h (+3 行)
- 新增 `int MaxSimultaneousRTs` 成员
- 新增 `Get_Num_Simultaneous_RTs()` 公开 getter

### dx8caps.cpp (+2 行)
- `Compute_Caps()` 中查询 `Caps.NumSimultaneousRTs`
- 日志输出: `Max simultaneous render targets: N`

### dx8wrapper.h (+5 行)
- 新增 `IDirect3DSurface8* CurrentMRTSurfaces[4]` 跟踪 MRT 表面
- 新增 `int m_activeMRTCount` 跟踪活跃 MRT 数量
- 新增 `Set_Render_Target(int index, IDirect3DSurface8*)` 声明

### dx8wrapper.cpp (+75 行)
- 静态成员初始化: `CurrentMRTSurfaces[4] = {NULL}`, `m_activeMRTCount = 0`
- 新增 `Set_Render_Target(int index, IDirect3DSurface8*)` 实现 (~55 行)
- 两处 `Set_Render_Target(NULL)` 恢复路径: 增加 MRT 槽位清理循环

### API 用法
```cpp
// 设置 MRT
Set_Render_Target(0, rt0);    // 现有路径，自动保存 DefaultRT
Set_Render_Target(1, rt1);    // 新重载
Set_Render_Target(2, rt2);
// 恢复默认
Set_Render_Target(NULL);      // 自动清除 RT1-RT3 + 恢复 DefaultRT
```

---

## Phase P1 变更清单

### dx8wrapper.h (+3 行)
- `MAX_VERTEX_SHADER_CONSTANTS`: 96 → 256
- `MAX_PIXEL_SHADER_CONSTANTS`: 8 → 224
- `Create_Render_Target` 两个重载添加 `bool allowNonPOT = false` 参数

### dx8wrapper.cpp (~+30 行)
- 两个 `Create_Render_Target` 重载增加 non-POT 路径:
  - 检测 `D3DCAPS2_CANRENDERWITHOUTPOW2` 能力位
  - `allowNonPOT=true` 且硬件支持时: 跳过 POT 强制, 仅限制 MaxTextureWidth/Height
  - 否则: 保留 legacy POT 行为
- G-Buffer 创建时传 `allowNonPOT=true`

---

## Phase 0 变更清单

### W3DCustomScene.h (+2 行)
- 枚举增加 `SCENE_PASS_GBUFFER`, `SCENE_PASS_FORWARD_TRANSPARENT`

### W3DScene.h (+1 行)
- 新增 `MaterialPassClass *m_gbufferMaterialPass` 成员

### W3DScene.cpp (~+15 行)
- 构造: 创建 G-Buffer 材质 pass（无光照、白色漫射）
- 析构: `REF_PTR_RELEASE(m_gbufferMaterialPass)`

### W3DDeferredRenderer.h (新建, ~95 行)
- 类框架: `init/shutdown/beginGBufferPass/endGBufferPass`
- 3 个 G-Buffer RT 指针 + `m_available` / `m_inGBufferPass`
- 全局指针 `g_theW3DDeferredRenderer`

### W3DDeferredRenderer.cpp (新建, ~130 行)
- `init()`: 检查 MRT≥3 + INI `UseDeferredRendering` → `m_available`
- `shutdown()`: 释放资源, 退出 G-Buffer Pass
- `beginGBufferPass()`: 骨架 (Phase 1 实现 MRT 绑定)
- `endGBufferPass()`: 恢复默认 RT

### GlobalData.h (+1 行)
- `Bool m_useDeferredRendering`

### GlobalData.cpp (+2 行)
- INI 表: `UseDeferredRendering` parse entry
- 默认值: `FALSE`

### W3DDisplay.cpp (~+15 行)
- include + init 中 `new`/`init` + 析构中 `shutdown`/`delete`

### GameEngineDevice.dsp (+2 行)
- 注册新 `.h` + `.cpp`


