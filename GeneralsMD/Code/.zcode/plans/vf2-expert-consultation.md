# VF-2 主 z 深度采样：专家咨询文档

> 2026-09-19。本文档每个论断都锚定源码/配置/实测日志（对标源码原则），供专家一次读懂全案。
> 工作原则：对标源码 + 疑问卡点清单 = 最高思维原则。不猜，不编，拿不准就进清单。

## 0. 咨询目标（一句话）

在 dgVoodoo 2.87.5（D3D9→D3D11/12 翻译层）上，让 ps_3_0 像素着色器读到**主场景 z-buffer 的每像素深度**。已尝试"主 z 改 D3DUSAGE_DEPTHSTENCIL 纹理 + D3DX Effect sampler_state 采样"，两种上下文分别恒读 1.0 与 0.0，都不是真实深度。

## 1. 运行栈事实（全部可查证）

| 事实 | 证据 |
|---|---|
| 引擎是原生 D3D9 API，不是 D3D8 | `libraries/dxsdk/include/d3d8compat.h:63-75`（IDirect3DDevice8 等全部 typedef 为 D3D9 接口） |
| 设备 1920×1080，MSAA=NONE，EnableAutoDepthStencil=TRUE，自动深度格式 D24S8 | `dx8wrapper.cpp:1056`（MultiSampleType=D3DMULTISAMPLE_NONE）、`:1062`（AutoDepthStencil=TRUE）、`:1105`+`Find_Z_Mode :1623`（格式选择链，D24S8 优先）；实测日志 `fmt=75`（=D3DFMT_D24S8） |
| 翻译层 = dgVoodoo 2.87.5 | 游戏目录 `D3D9.dll` VersionInfo：ProductVersion 2.8.7.5，FileDescription "dgVoodoo 2.87.5 - Direct3D9" |
| dgVoodoo 关键配置 | `dgVoodoo.conf [DirectX]`：OutputAPI=bestavailable，VideoCard=internal3D，VRAM=256，Antialiasing=appdriven，FastVideoMemoryAccess=false，PhongShadingWhenPossible=false，Resolution=unforced |

## 2. 我们做了什么（精确步骤 + 实测结果）

### 2.1 主 z 改可采样深度纹理（"新 VF-1c"）

1. `CreateTexture(1920×1080, Levels=1, D3DUSAGE_DEPTHSTENCIL, D3DFMT_D24S8, D3DPOOL_DEFAULT)` → **S_OK**
2. `GetSurfaceLevel(0)` → `SetDepthStencilSurface(该表面)`；从此整帧深度测试/写入都发生在该纹理（auto DS 仍在但不再绑定）
3. 创建后 `Clear(z=1.0, stencil=0)` 一次
4. 设备 Reset 生命周期：释放→Reset→重建→重绑（DX8_CleanupHook 钩子）
5. **验证通过**：画面渲染完全正常（深度功能有效）；帧末 `GetDepthStencilSurface()` 仍返回该表面（日志 `curDS==ours=1`）
   - 结论：**深度写入进纹理这条链是活的**（否则整帧错绘）
6. 采样时先 `SetDepthStencilSurface(NULL)`（不绑着采，D3D9 合法性要求）

### 2.2 采样路径 A：D3DX Effect（雾合成 pass，读到 1.0）

- `D3DXCreateEffect`（源码字符串）→ S_OK。两个 `sampler2D sampler_state`：
  - SceneSampler = A8R8G8B8 场景色拷贝（当帧 StretchRect resolve，本栈已验证规律）
  - ZSampler = 上述深度纹理
  - 两者均 `MinFilter/MagFilter=Point、MipFilter=None、AddressU/V=Clamp`
- `fx->SetTexture("ZTex", 深度纹理)` → `fx->Begin` → **hr=0x00000000, passes=1**（日志 `VF-2 DRAW`）
- XYZRHW 全屏 quad 画到后缓冲（FF 顶点管线 + ps_3_0，同管线在本栈太阳光/bloom pass 生产在用）
- **结果：`tex2D(ZSampler, uv).x` 恒 = 1.0**（调试直通模式整屏纯白，用户肉眼确认）

### 2.3 采样路径 B：同 Effect 模式的 dump 回读机器（读到 0.0）

- 单 `sampler_state`（同样 Point/Clamp），把采样值画进 1024×1024 **A8R8G8B8 彩色 RT**，`GetRenderTargetData` 回读存 PPM
- 该机器与"历史验证过的阴影 dump 机器"同一份代码（`dumpShadowTexToPPM`）
- **结果**：
  - 主 z 纹理（D24S8）→ PPM 全黑（min=max=0，1024×1024 全部 1048576 像素 = 0）
  - 阴影图自己的 **D24X8** 深度纹理（同方式创建、阴影 pass 写入过、采样时非当前 DS）→ 同样全黑 0.0
- 机器有效性旁证：被写入的彩色 RT 当帧被阴影 pass clear 成 (1,1,1) 白（`beginShadowMapPass` 每帧如此，日志确认阴影 pass 在跑），PPM 却全黑 → **确实有东西把 0 画上去了（=采样返回 0），不是"没画"**

### 2.4 对照背景：本栈一直正常的采样

- **彩色纹理**经普通 `SetTexture` + 独立编译 PS 采样完全正常（A8R8G8B8 阴影采样拷贝被地形接收每帧消费）
- 彩色 RT 曾作为渲染目标 → 必须 StretchRect 到普通纹理再采才可靠（RT→SRV 需显式 resolve，本栈实证规律，代码注释多处记录）
- 历史上 R32F 曾"读 0"、fp16 存疑，A8R8G8B8 稳定（W3DDeferredRenderer.cpp 注释链）

## 3. 引擎参考源码对标（RA3 对齐文件）

- `Shaders/RA3/head0-COMMON.FXH:282-293`：`ShadowMapSampler = sampler_state { MinFilter=1(Point); MagFilter=1; MipFilter=0(None); AddressU/V=3(Clamp) }`——采的 ShadowMap 是 **R32F 彩色纹理**（记录阳光空间深度）。**RA3 从不采样深度格式纹理；阴影接收走彩色纹理是 RA3 自己的路线。**
- 本引擎阴影实现现状（`createShadowResources`/`beginShadowMapPass`/`endShadowMapPass`）：阴影深度由 PS 写 sun-space z/w 进 **fp16/A8R8G8B8 彩色 RT**，每帧 StretchRect 到采样拷贝，再被消费——**全链路没有一处深度纹理采样**。
- 代码里 2026-09-05 的注释断言"D24X8 只有经 effect sampler_state 可采，独立 PS 读 1.0"（`dumpShadowTexToPPM` 头注释）——**今天 effect 路径对 D24X8 也读 0**，该历史断言与现状矛盾（可能当时验证的是别的通道，或版本/配置变迁）。

### 3.5 全链路环节对标表（系统对标：每环节记录对标对象/锚点/结论/适用性，无遗漏）

| # | 环节 | 对标对象（锚点） | 对标结论 | 适用性判定 |
|---|---|---|---|---|
| 1 | API 表面 | `d3d8compat.h:63-75` | IDirect3DDevice8 等全部 = D3D9 接口，原生 D3D9 语义 | **必须对标**（本仓库权威）；已照做 |
| 2 | 设备参数 | `dx8wrapper.cpp:1056`(MSAA=NONE) `:1062`(AutoDS=TRUE) `:1623-1666`(格式链 D24S8 优先) | 主 z 镜像当前 DS 格式（D24S8，保模板） | **必须对标**（引擎源码）；已照做 |
| 3 | 深度纹理创建 | 引擎阴影先例 `createShadowResources`（dev9->CreateTexture, DEPTHSTENCIL, D24X8, POOL_DEFAULT）；引擎遗留 `ZTextureClass`（texture.cpp:1209 → dx8wrapper `_Create_DX8_ZTexture`） | 创建参数（usage/levels=1/pool）完全同构，S_OK | 参数层**可对标**（照套无歧义）；但遗留路线是 D3D8/NVIDIA 特供年代产物，其"创建成功 ⇒ 可采样"隐含假设**不能照套** |
| 4 | 深度表面绑定 | `beginShadowMapPass`（GetSurfaceLevel(0)+SetDepthStencilSurface），阴影 pass 生产在用 | 同款绑定，帧渲染正常 | **必须对标**（引擎源码）；已照做 |
| 5 | Reset 生命周期 | `DX8_CleanupHook` + `Reset_Device`（dx8wrapper.cpp:700-735，释放逆序/重建正序） | 钩子接线同款 | **必须对标**；已照做 |
| 6 | 帧内清除 | `DX8Wrapper::Clear` → 设备 Clear 当前 DS | 创建后 Clear z=1 一次 + 每帧常规清除 | **必须对标**；已照做 |
| 7 | 场景深度写入 | 全部 pass 的 DS 保存恢复链（`Set_Render_Target` dx8wrapper.cpp:3607-3754；shadow 1625/1993；W3DShaderManager 6086/6692） | 链路自洽，实测帧末 `curDS==ours=1`，画面正常 | **必须对标**；写入链活的 |
| 8 | 采样前解绑 | D3D9 规范（采绑定中的 DS 纹理=UB）+ 本栈实测（首发 dump 双绑读 0，证据作废） | 必须 `SetDepthStencilSurface(NULL)` | **规范内，必须对标**；已照做 |
| 9 | 纹理→采样器绑定 | `dumpShadowTexToPPM` 的 `fx->SetTexture` 模式（引擎内唯一 effect 绑定先例）；RA3 head0-COMMON 是编译期 SAS 参数绑定，运行期方式不同 | effect 用法模式照套 | 模式**可对标**；但该先例的"采样成功"断言今日复测失效（读 0）——先例有效性本身待专家裁决（Q5） |
| 10 | 采样器状态 | `head0-COMMON.FXH:282-293`（MinFilter=1=Point、AddressU/V=3=Clamp、MipFilter=0=None） | 值编码与声明照套 | **必须对标**（.fx 语义层跨栈一致）；已照套 |
| 11 | 采样返回语义 | D3D9 规范：未绑采样器返 (1,1,1,1)；深度纹理返回值属厂商扩展区（真机 NVIDIA 返 .r 深度） | 规范内/外分界点 | **不能照套真机经验**：dgVoodoo 对规范外行为的实现无源码可查——Q1/Q3 问专家 |
| 12 | 回读验证机器 | `dumpShadowTexToPPM`（RTSTATS/PPM 同族） | 机器模式沿用；对照设计须自审（历史自反馈缺陷教训） | **可对标但必须带已知内容对照**（彩色对照代码已在 7c89913a，未跑） |
| 13 | dgVoodoo SRV 实现 | **无源码**（二进制 2.87.5）；conf 可查项（FastVideoMemoryAccess/PhongShading 等与深度纹理无已知关联） | 唯一不可对标层 | **规范外 + 无源码 = 必须问专家**（Q1-Q6）；禁止用真机 D3D9 经验或引擎遗留代码假设填充 |

### 3.6 明确"不能照套"清单（及原因）

| 不能照套的对象 | 为什么 |
|---|---|
| RA3 阴影采样路线 | RA3 采的是 **R32F 彩色纹理**（head0-COMMON.FXH + 本引擎注释链），平台是真机 D3D9。把"RA3 这么干"当成"深度纹理能采"是**类别错误**——格式类型（彩色 vs 深度）与平台（真机 vs dgVoodoo）双重不同 |
| 引擎 `ZTextureClass`/texproject 遗留路线 | D3D8 时代代码，目标硬件是 NVIDIA GeForce3/4 深度纹理特供年代；"CreateTexture(DEPTHSTENCIL) 成功 ⇒ 可当普通纹理采"是当时 NVIDIA 驱动特供行为，dgVoodoo 不承诺复现 |
| 阴影 D24X8 "经 effect 可采"历史注释 | 2026-09-05 断言与 2026-09-19 同栈实测（读 0）矛盾；断言可能基于别的通道或版本变迁，已在 Q5 挂账，重验前不得作为依据 |
| D3D9 真机厂商扩展经验（INTZ 在真机 NV/ATI 的行为等） | dgVoodoo 是 D3D11/12 上的重新实现，规范外行为自由裁量；真机经验在本栈只能当"待验证假设"，不是依据 |


## 4. D3D9 规范事实（非本栈特有）

1. **未绑纹理的采样器，采样返回 (1,1,1,1)（白）**——路径 A 的恒 1.0 与"SetTexture 实际未落台"吻合
2. **StretchRect 深度面→彩色面非法**（D3D9 仅允许 color↔color；depth↔depth 亦有限制）
3. 官方可采样深度 = FOURCC 格式（**INTZ**/RAWZ/DF24，厂商扩展）；直接 `CreateTexture(DEPTHSTENCIL, D24S8/D24X8)` 后当普通纹理采，在真机上本就是 NVIDIA 特供行为，不是 D3D9 可移植特性

## 5. 疑问卡点清单（核心交付）

**Q1（核心）**：dgVoodoo 2.87.5 中 `CreateTexture(D3DUSAGE_DEPTHSTENCIL, D24S8/D24X8, POOL_DEFAULT)` 成功后，其着色器资源视图（SRV）与深度模板视图（DSV）是否同一块内存？即通过 `SetDepthStencilSurface` 写入的深度，采样能否看到？
（我们实测恒 0.0——连创建时 Clear 的 1.0 都读不到，读到的像一块从未被写过的零资源。这是"不支持但静默"，还是需要特定条件？）

**Q2**：dgVoodoo 是否支持 **INTZ**（FOURCC）的 `CreateTexture`+采样？若支持，INTZ 纹理能否同时作为 `SetDepthStencilSurface` 的目标（即直接当主 z 用）？

**Q3**：同一张深度纹理，为什么"双采样器 effect + 后缓冲目标"读 1.0，"单采样器 effect + 彩色 RT 目标"读 0.0？1.0 是否就是 D3D9 未绑采样器返回值（= dgVoodoo 的 effect 对深度纹理的 SetTexture 被静默丢弃）？是否存在"深度纹理只能绑特定 stage / 同帧绑过 DS 之后不能再采"之类的隐藏限制？

**Q4**：dgVoodoo 有没有**深度→深度 StretchRect**（resolve）？把主 z resolve 到一张"从未绑为 DS"的深度纹理再采，是否可行？（用于隔离"绑过 DS"与"深度格式本身"两个变量。）

**Q5**：dgVoodoo 配置里（我们当前 `FastVideoMemoryAccess=false`、`PhongShadingWhenPossible=false`、`VideoCard=internal3D`、`OutputAPI=bestavailable`）有没有影响深度纹理 SRV 可见性的开关？2.87.5 相邻版本对深度纹理行为有无已知变化？（我们代码历史注释曾声称 D24X8 经 effect 可采且见内容，今天不复现。）

**Q6（综合）**：在约束"无 MSAA、固定管线地形不能 MRT 写深度、StretchRect 深度→彩色非法"下，dgVoodoo 上获取**全场景每像素深度**给 ps_3_0 的推荐姿势是什么？

## 6. 决策表（按专家答案走）

| 答案 | 行动 |
|---|---|
| Q2=INTZ 可用且可当 DS | 主 z 改 INTZ，全链路最小改动（保留 D24S8 回退开关） |
| Q4=深度 resolve 可采 | 主 z 保持 D24S8 + 每帧 resolve 到采样纹理 |
| 都不可 | 备选案 A：COLOR RT 编码深度——但注意本引擎地形走 FF/TSS 不能 MRT 写 z（VS/MRT 路线已死亡），"场景色 MRT 顺带写 z"对地形不可行；只能独立 z 预 pass（全场景再画一遍，PS 写 z/w，代价=一帧两次场景几何）。备选案 B：雾门控退化为纯距离/高度函数（无近景穿透，画质降级） |
| Q3 有隐藏限制 | 针对性绕开（stage 安排/时序） |

**生产回退**：任何方案落地前，`UseSampleableZBuffer=No` 一键回到 auto DS 生产态（现状已实现）。

## 7. 当前工作树状态（供参考）

- 主线代码已提交：c203a178（全链）→ 5fda15fc/2ba9011c/31f6179a/7c89913a（诊断轮）
- 全部新路径 INI 默认关；生产终态不受影响（TerrainVSRoute=No + UseDeferredRendering=Yes = 干净可玩）
- 待跑未跑的最后一个自证实验：mode3 四文件 dump（两个彩色对照=已知内容，验证 dump 机器绘制侧），代码在 7c89913a，默认关
