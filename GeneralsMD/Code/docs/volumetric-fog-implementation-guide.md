# MinGenerals 体积雾（VF-2）实现代码指南

> 2026-09-19 交付版。本文档按"对标源码"原则书写：每个论断锚定 `文件:行号`。
> 适用构建：dgVoodoo 2.87.5（D3D9→D3D11/12 翻译层）+ 本项目 d3d8compat 原生 D3D9 栈。
> 配套：`.zcode/plans/vf2-expert-consultation.md`（专家咨询与判别实验全记录）。

---

## 一、思路（设计哲学与约束）

### 1.1 目标

全图统一高度雾（空气感）：近景清晰、远景按深度溶进雾色、太阳方向雾中透亮。像素级 raymarch（16~48 步），**深度门控**——雾只积聚到物体表面为止（近景物体自动无雾）。

### 1.2 三件配方（vf2-fog-reference-digest.md 消化定稿）

| 来源 | 采纳的部分 |
|---|---|
| 豆包（VC6+DX9 高度雾示例） | 密度函数 `exp(-z/HeightScale) × GroundDensity`，高度尺度默认 12.5 |
| pizi0475《体积雾(dx9)》 | 深度门控：`marchLen = min(sceneDist, FogEnd)`；合成式 `scene*trans + fogCol` |
| KW PostFX_LightRays | 太阳 in-scattering 思路（最终实现简化为吸收上界式，见避坑 §5.5） |

### 1.3 本栈两条硬约束（专家裁决 + 实测）

1. **D3D9 里"可采样的深度"只有 INTZ**。dgVoodoo 对 D24S8/D24X8 深度纹理不创建着色器资源视图（SRV）——任何直接采样都失败（恒 1.0=未绑采样器特征值，或恒 0.0=空 SRV）。INTZ 可 `CreateTexture(D3DUSAGE_DEPTHSTENCIL)` + 绑为深度目标 + 采样（`.r` = 0..1 深度）。
2. **INTZ 没有模板位**。本引擎大量系统依赖模板（体积软阴影的屏幕 quad 门控、建筑遮挡标记、夜间点光体积剔除）——它们必须在**带模板的 D24S8** 上跑。

### 1.4 终局架构：分裂深度（Split-Depth）

一块深度缓冲无法既可采样又带模板 → **按帧阶段分治**：

```
帧开始（DS = INTZ）
 ├─ 阴影pass（内部自管DS，恢复INTZ）
 ├─ GBuffer pass ──── DS=INTZ：全部不透明场景深度 → 雾的采样源
 ├─ 延迟中段（SSAO/太阳光/tonemap，quad类pass不深度测试）
 ├─ 【换绑】DS = auto D24S8 + Clear(z=1,stencil=0)          ← splitDepthBindAutoForForward
 ├─ 前向pass ──────── 全场景重渲 + 全部模板系统（生产行为）
 ├─ 【换绑】DS = INTZ                                        ← splitDepthBindINTZAfterForward
 ├─ AO/IBL 合成 → 体积雾pass（采INTZ+采场景色副本）→ bloom
帧结束（DS = INTZ，下一帧一致）
```

关键机制：**DX8Wrapper 的 DefaultDepthBuffer 缓存**在两阶段各自指向正确的表面——嵌套自定义 RT 切换（水面反射等）的恢复路径才能绑对 DS（这是唯一需要动 WW3D2 库的点）。

---

## 二、渲染流程（一帧时序）

```
W3DScene::Render (deferred 分支)
 ① beginShadowMapPass/endShadowMapPass     （阴影图，自管DS，与雾无关）
 ② beginGBufferPass                        W3DDeferredRenderer.cpp:494
    ├ MRT绑定(3张gbuffer RT) + [SPLIT-DEPTH]绑INTZ为DS (:546)
    ├ Clear(颜色+z) → 场景渲染 → 深度全部落入INTZ
 ③ endGBufferPass                          :546区/591区
    ├ 恢复默认RT(包装器缓存→绑回auto D24S8)
    └ [SPLIT-DEPTH]Clear auto D24S8 的 z+stencil (:591)
 ④ SSAO → 延迟光照(sunLightPass+点光) → tonemap
 ⑤ 前向pass                                 W3DScene.cpp:1368
    ├ splitDepthBindAutoForForward()        (换D24S8+清) W3DDeferredRenderer.cpp:2272
    ├ Customized_Render+Flush（全场景重渲+体积软阴影等全部模板系统）
    └ splitDepthBindINTZAfterForward()      (换回INTZ)   W3DDeferredRenderer.cpp:2290
 ⑥ aoCompositePass → iblCompositePass
 ⑦ volumetricFogPass                        W3DScene.cpp:1388 → W3DDeferredRenderer.cpp:1936
    ├ StretchRect 后缓冲→场景色副本RT（resolve，采后缓冲非法）
    ├ 解绑DS（采绑定中的深度纹理非法）
    ├ D3DX Effect：s0=场景色 s1=INTZ(钉register+裸SetTexture直绑)
    ├ 全屏quad：重建世界坐标→FogStart起点→48步抖动raymarch→合成
    └ 解绑s1 + 恢复全部状态
 ⑧ bloomPass → Present
```

**世界坐标重建**（生产验证约定，与 sunLightPass 完全一致）：
`screenPos = uv*2-1 → clipPos=(sx,sy,zw,1) → wp = 行向量×invViewProj列 → /w`；invViewProj 由 W3DScene.cpp:1178-1187 计算（view/proj 转置相乘后 D3DXMatrixInverse）。

---

## 三、代码文件与段落清单

### 3.1 `GameEngineDevice/Source/W3DDevice/GameClient/W3DDeferredRenderer.cpp`（主战场）

| 行号 | 函数/段 | 作用 |
|---|---|---|
| ~490-600 | `beginGBufferPass`/`endGBufferPass` | MRT 绑定；**:546 绑 INTZ 为 DS**（帧清除落入 INTZ）；**:591 恢复后清 auto D24S8 的 z+stencil**（前向干净起跑） |
| 1653-1751 | `createMainZTexture`/`releaseMainZTexture` | 创建 INTZ（尺寸镜像 auto DS）；**保存 auto DS 引用 m_mainZAutoDS**；创建时清 z=1；失败→保持单 DS 生产态。`D3DFMT_INTZ` 本地 `MAKEFOURCC` 定义（SDK 头无此定义） |
| 1775-1917 | `createFogResources`/`releaseFogResources` | 场景色 resolve RT（全分辨率 A8R8G8B8）+ **D3DX Effect 编译**（雾 shader 源码字符串在此，:1855-1895 为 march 主体） |
| 1936-2246 | `volumetricFogPass` | 雾主体：resolve→状态保存→解绑DS→effect 参数上传（**:2150 区 gCamPos.w=FogStart**）→BeginPass 后**裸 SetTexture(1,zTex)**→draw→解绑 s1→恢复。含调试模式 1/2/3/4/5 分支 |
| 2248-2270 | `debugLogDSIdentity` | 一次性 DS 身份核查（帧内路由诊断，保留） |
| 2272-2300 | `splitDepthBindAutoForForward`/`splitDepthBindINTZAfterForward` | 两阶段 DS+包装器缓存换绑（引用计数内建） |
| 构造/init/ReAcquire/Release/shutdown | 生命周期接线 | createMainZTexture/createFogResources 在 init 与 Reset 后重建；释放走钩子链 |

**雾 shader 关键段**（createFogResources 内字符串，最终形态）：
```hlsl
// 采样器：钉寄存器（s1 必须钉，见避坑§5.2）
sampler2D SceneSampler : register(s0) = sampler_state { Point/Clamp };
sampler2D ZSampler     : register(s1) = sampler_state { Point/Clamp };
// 主体
zw = tex2D(ZSampler, uv).x;                          // INTZ深度
// 重建世界坐标(§二) → sceneDist
marchLen = min(sceneDist, gFogParams.z);             // 深度门控(FogEnd上限)
startDist = max(gCamPos.w, 0); span = marchLen-startDist;  // FogStart起点带
cosT/phase/sunTerm                                    // 太阳相位(0.75+0.25cos²)
if (span>0) for 48步: t=startDist+(i+jitter)*dt;     // 48步+像素抖动(抗条带)
    trans *= exp(-density(p)*dt);  density=ext*exp(-max(p.z,0)/H);
scat = sunTerm * (1-trans);                           // 吸收上界式(抗量化)
return scene*trans + fogColor*(1-trans) + scat*gSunDir.w;
```

### 3.2 `GameEngineDevice/Include/W3DDevice/GameClient/W3DDeferredRenderer.h`

成员声明区：`m_mainZTex/m_mainZSurface/m_mainZAutoDS/m_mainZAvailable`（:209 区）、`m_fogSceneRT/m_fogFX/m_fogAvailable`、两个 splitDepth 方法声明。注意 `struct ID3DXEffect;` 前置声明（:36，d3dx9effect.h 只进 cpp——项目惯例）。

### 3.3 `GameEngineDevice/Source/W3DDevice/GameClient/W3DScene.cpp`

| 行号 | 内容 |
|---|---|
| :1368/:1373 | 前向 pass 前/后的 splitDepth 换绑调用 |
| :1388 | `volumetricFogPass` 挂点——**AO/IBL 合成之后、bloom 之前**（雾必须最后作用于全部局部光照，见避坑§5.4） |
| 1178-1187 | invViewProj 计算（传给雾 pass 的就是它） |

### 3.4 `Libraries/Source/WWVegas/WW3D2/dx8wrapper.h/.cpp`

- `:3497 Set_Default_Depth_Buffer`：公有静态方法，引用计数内建的 DefaultDepthBuffer 缓存交换（分裂深度的唯一库改动）。
- 背景知识：`Set_Render_Target`（:3607-3754）的 custom-RT 路径用 `GetDepthStencilSurface` 保存/恢复 DS；`Clear`（:1882）**运行时查当前 DS 格式**才决定带不带 D3DCLEAR_STENCIL（INTZ 下帧清除天然合法——无需改）。

### 3.5 `GameEngine/Include/Common/GlobalData.h` + `Source/Common/GlobalData.cpp`

字段（:150-157）：`m_useSampleableZBuffer / m_sampleableZFormat / m_useVolumetricFog / m_volFogDensity / m_volFogHeightScale / m_volFogGroundDensity / m_volFogSunScatter / m_volFogDebug`。
INI 键（GlobalData.cpp fieldParse 表 + ctor 默认值）：`UseSampleableZBuffer / SampleableZFormat / UseVolumetricFog / FogDensity / FogVolumeHeight / FogGroundDensity / SunScatterStrength / VolumetricFogDebug`；**复用 P4 键**：`FogStart`（起点带）、`FogEnd`（门控上限）、`FogColorR/G/B`（雾色）。

### 3.6 用户 INI（`Data\INI\GameData.ini`）

"体积雾 VF-2 调整面板"（带浓淡速查表）：FogDensity=0.0016 / FogVolumeHeight=12.5 / FogGroundDensity=1.0 / **FogStart=450 / FogEnd=1300** / SunScatterStrength=0.5 / VolumetricFogDebug=0 / SampleableZFormat=0。

### 3.7 诊断设施（默认关，保留勿删）

`VolumetricFogDebug`：1=raw z 灰度直读（采样健康探针）/2=雾因子 viz/3=PPM dump（注意：dump 机器绘制侧有已知缺陷，历史"读0"证据作废，勿再当判据）/4=场景采样器直通/5=INTZ 最小自检（已知深度画入再采出，黑=链路通）。日志：`E:\GeneralsMD_DeferredRT.log` 认 `VF-2 ENTRY`（含 zFmt 自证）/`VF-2 DRAW`/`VF-2 SETTEX`/`VF-2 DSCHK`/`SPLIT-DEPTH ready` 行。

---

## 四、数据与生命周期

- INTZ/autoDS 均为 `D3DPOOL_DEFAULT`：设备 Reset 经 `DX8_CleanupHook`（ReleaseResources→Reset→ReAcquireResources）重建；m_mainZAutoDS 的引用在 create 时获取、release 时归还。
- 场景色 RT 与 effect 同生命周期（createFogResources/releaseFogResources），effect 一次编译全程复用（**勿每帧 D3DXCreateEffect**——历史 dump 代码曾每帧编译+回读，帧率 1.3s 且 AV 崩溃）。
- 帧序不变量：帧必须**以 INTZ 为当前 DS 结束**（下一帧首段缓存一致）；前向必须拿到**清过 z+stencil 的 auto D24S8**（前向全场景重渲自建深度）。

---

## 五、避坑注意事项（全部实战踩过，条条有伤疤）

### 5.1 格式层：深度纹理采样
- **D24S8/D24X8 在 dgVoodoo 无 SRV**——采样恒 1.0（=D3D9 未绑采样器返回值，易误诊为"读到 clear 值"）或恒 0.0。**只有 INTZ 可采**。INTZ 无模板位（连锁后果见 §5.6）。
- 阴影图 D24X8 的"创建+当深度目标"是老先例，但**它的"可采样"从未被生产验证**——现役阴影采样走的全是 COLOR RT 的 StretchRect 拷贝。别把"创建成功"当"可采样"。

### 5.2 绑定层：effect 的 SetTexture 不可信
- `fx->SetTexture("ZTex", 深度纹理)` 返回 S_OK、Begin hr=0、draw 正常执行，但**纹理可能根本没上采样槽**（读 1.0）。定式修法：**采样器钉 `register(sN)` + BeginPass 之后设备级 `SetTexture(N, tex)` 直绑 + 显式 Point/Clamp 采样态**。
- D3D9 规范：未绑纹理的采样器返回 (1,1,1,1)——"白"是绑定失败的指纹。

### 5.3 状态泄漏
- 雾 pass 结束**必须解绑 s1**（`SetTexture(1,NULL)`）——泄漏的 INTZ 会被下一帧不重绑 s1 的地形/W3X 变体当颜色采（深度当颜色=近暗远亮的"阴影"图案，**锚定屏幕随镜头滑动**；同款先例：阴影 pass 结束解绑 s4）。
- **采绑定中的深度纹理=非法**——采样前 `SetDepthStencilSurface(NULL)`，画完恢复。

### 5.4 帧序层
- 雾必须挂在 **AO/IBL 合成之后、bloom 之前**：AO 暗化乘在雾上=移动暗斑、IBL 高光加在雾上=移动亮斑（两者皆屏幕空间/视角相关）。雾是大气效果，在全部局部光照之后合成。
- 参考文献的合成顺序**不能照搬**（其"AO"语义与本引擎屏幕空间 AO 不同）——对标适用性原则的典型案例。

### 5.5 算法层：march 量化条带
- 均匀 N 步的中点积分把光深量化成**相机为中心的圈层条带**——平水面（等深面）上最显形，随镜头平移滑动（曾被误报为"舰艇阴影在动"）。定式修法三合一：**步数 48 + 每像素抖动起点（`frac(sin(dot(uv,...)))`）+ 散射改吸收上界式 `scat=sunTerm*(1-trans)`**（步进累积式 `scat+=od*trans` 的量化敏感度远高于透射）。
- FogStart 起点带：`span<=0` 直接无雾，注意 dt=span/48（不是 marchLen/48）。

### 5.6 模板连锁（INTZ 的代价→分裂深度的由来）
- 体积软阴影合成=屏幕 quad+`STENCILFUNC LESSEQUAL REF 1` 门控；无模板位→读数恒 0→`0<=1` 恒过→**每块阴影画满屏幕矩形随镜头滑**（舰艇/岩石全部中招）。任何"整帧绑 INTZ"的方案都会复现此案。
- 嵌套自定义 RT 切换会恢复包装器缓存的 DefaultDepthBuffer——分裂深度两阶段**必须显式翻转缓存**（Set_Default_Depth_Buffer），否则阶段内深度被写花。

### 5.7 工程与流程
- **INI 配置是证据链一环**：每轮实测前核验生效值（`SampleableZFormat` 化石值回归曾让两轮实验测错对象）。ENTRY 日志自证 `zFmt=` 就是为此。
- Clear 全有或全无：对无模板格式带 STENCIL 标志整次失败。引擎 `DX8Wrapper::Clear` 已运行时查格式，自己写的 Clear 要同样处理。
- INI 是 GBK：写入必须字节级（UTF-8 中文写进去=乱码+可能吞行）；改前备份、改后 `cmp`。
- 部署铁律：构建→**LAA 补丁**（apply_laa.py，先打 Run 产物再拷贝）→D: 部署→改过的资源文件**同步 E: 目录（同名先时间戳备份）**。
- 独立编译 PS 直采深度在本栈从未成功过（历史注释"effect 才可靠"也有反例）——**一切深度采样走 effect sampler_state 或钉寄存器裸绑**，且上线前用 Debug=5 自检（黑=通）打一次样。

---

## 六、调参速查（INI 面板同步版）

| 键 | 当前值 | 语义 |
|---|---|---|
| FogDensity | 0.0016 | 起点后的浓度（0.001 薄纱~0.006 浓） |
| FogStart | 450 | 零雾干净带半径（起点后移就加它） |
| FogEnd | 1300 | 浓度累积上限（远处遮盖程度） |
| FogVolumeHeight | 12.5 | 雾层高度尺度（30=半空弥漫） |
| SunScatterStrength | 0.5 | 朝太阳透亮（0=关） |
| FogColorR/G/B | 0.65/0.72/0.80 | 雾色（与 P4 共用） |

调试档：VolumetricFogDebug 1/2/4 可用，3 的 dump 机器有缺陷勿作判据，5=INTZ 自检。

---

*文档锚点行号对应提交 65398df3 时代的源码；后续改动以函数名+注释标记（"SPLIT-DEPTH"/"2026-09-19"）检索为准。*
