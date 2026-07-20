# 延迟渲染改造 — 工作检查点

> 存档时间: 2026-07-14
> 状态: 最终计划完成，准备进入实施
> 会话上下文耗尽，转交下一对话

---

## 已完成的工作

1. **原始计划检讨** — 3 轮检讨式反思，发现 40+ 项差距
2. **完整代码走读** — W3DScene/W3DView/W3DDisplay/dx8wrapper/W3DShaderManager/W3DDynamicLight/W3DShroud 等 20+ 核心文件
3. **核心发现：DX8Wrapper 不支持 MRT** — 所有 RT 操作仅用 index 0
4. **DX8Wrapper MRT 支持设计** — 仅需 ~95 行新增代码
5. **完整合并计划** — `docs/deferred-rendering-master-plan.md`

## 项目架构

```
前置改造（P0+P1）: DX8Wrapper MRT 支持 + POT解除  ← 第1步
延迟渲染核心（Phase 0-9）: 3-RT G-Buffer 延迟渲染  ← 第2步
总计: 12 阶段, ~14 天, ~2700 行代码
```

## 下次对话首批工作

从 **Phase P0** 开始：DX8Wrapper MRT 支持

```cpp
// 1. dx8caps.h — 新增
static int Get_Num_Simultaneous_RTs() { return MaxSimultaneousRTs; }

// 2. dx8caps.cpp — Init_Caps 中
DX8Caps::MaxSimultaneousRTs = caps.NumSimultaneousRTs;

// 3. dx8wrapper.h — 新增状态
#define MAX_SIMULTANEOUS_RTS 4
static IDirect3DSurface8* CurrentMRTSurfaces[MAX_SIMULTANEOUS_RTS];
static Int                m_activeMRTCount;

// 4. dx8wrapper.cpp — 新增函数
void DX8Wrapper::Set_Render_Target(Int index, IDirect3DSurface8* surface);

// 5. dx8wrapper.cpp — 修改 Set_Render_Target(surf)
//    恢复默认时解除所有附加 MRT
```

## 关键文件索引

| 文件 | 路径 |
|------|------|
| 完整执行计划 | `docs/deferred-rendering-master-plan.md` |
| DX8Wrapper 头文件 | `Libraries/Source/WWVegas/WW3D2/dx8wrapper.h` |
| DX8Wrapper 实现 | `Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp` |
| DX8Caps 头文件 | `Libraries/Source/WWVegas/WW3D2/dx8caps.h` |
| DX8Caps 实现 | `Libraries/Source/WWVegas/WW3D2/dx8caps.cpp` |
| d3d8compat.h | `libraries/dxsdk/include/d3d8compat.h` |
| W3DCustomScene.h | `GameEngineDevice/Include/W3DDevice/GameClient/W3DCustomScene.h` |
| W3DScene.h | `GameEngineDevice/Include/W3DDevice/GameClient/W3DScene.h` |
| W3DScene.cpp | `GameEngineDevice/Source/W3DDevice/GameClient/W3DScene.cpp` |
| W3DView.cpp | `GameEngineDevice/Source/W3DDevice/GameClient/W3DView.cpp` |
| W3DDisplay.cpp | `GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp` |
| W3DShaderManager.cpp | `GameEngineDevice/Source/W3DDevice/GameClient/W3DShaderManager.cpp` |

## 参考资料

- 完整计划: `docs/deferred-rendering-master-plan.md`
- 历史分析文档: `docs/deferred-rendering-critique-round{1,2,3}.md`, `docs/dx9wrapper-plan-round{1,2}.md`
- CLAUDE.md: 项目架构总览
