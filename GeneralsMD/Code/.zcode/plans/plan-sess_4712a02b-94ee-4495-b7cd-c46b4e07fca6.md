# 体积雾全面计划（基于 RA3/KW 全源摸查 + 本项目疑难点全破）

## 情报结论（决定架构）
- **无现成体积雾可抄**：RA3=死代码+云影/LUT/粒子假雾；KW=活的线性距离雾+**唯一射线积分代码 PostFX_LightRays.fx**（屏幕空间 raymarch，深度门控，云纹×阴影，加法合成）
- **本引擎白色剪影已破**：模型加载时自动 FOG_WHITE 映射（meshmdlio.cpp:2112、matpass.cpp:164）→ 场景雾一开就白影。停用映射+既有 shader.cpp v2 覆盖=树/岩/桥吃我们的雾色
- **扇面之谜头号嫌疑**：水体 TRIANGLEFAN 双通道（drawSea 主绘制+战雾二次绘制，半透明放射扇，侧看压线——特征全中）；次嫌疑=VS 下错投影地形瓦片
- 屏幕空间雾深度前提：需"地形/W3X 写 GB 深度"（=VS 路线重启连锁）或深度预通行

## 分阶段实施（每段=构建+验证+提交，独立开关可回退）

### VF-0 材质雾收编（快胜，1-2 轮构建）
- **0a 白影修复**：meshmdlio.cpp:2112-2115 与 matpass.cpp:164-168 的自动 Enable_Fog(FOG_WHITE) 改为 FOG_DISABLE（半透明/乘法网格不再自带白雾位）；W3DScene 的 Set_Fog(true) 喂参恢复（现回退块）——树/岩/桥经 shader.cpp v2 覆盖统一吃蓝灰雾色
- **0b 道路雾**：TSS stage3 世界坐标戏法（相机空间位置×逆视图 → 道路 PS 新 TEXCOORD3）+同款距离×高度雾项
- 验证：远近树/岩/桥/路全部与地形同步渐隐，无白影无突兀

### VF-1 地形写 GB 深度（解锁屏幕空间，含扇面结案）
- **1a 扇面结案探针**：临时开关二分——VS 绑定期间分别跳过 drawSea 战雾二次绘制（W3DWater 4527-4609）与主绘制（4421 TRIANGLEFAN）→ 锁定元凶并修（预期：水扇在 VS 时代的 FF 状态窗口内被错误渲染）
- **1b VS 路线重启**：s_terrainVsEnabled=true（转置修复已在）+ 1a 修复 → 地形 vs_3_0+ps_3_0 双档跑通
- **1c 地形 MRT 深度**：GB 通道给地形绑深度写入版 PS（ps_3_0 twin 写 rt2 NDC z——引擎 g_gbufferPS 同域）
- **1d W3X 深度**：去掉 GB 早退 + 深度仅技术（借鉴 W3X ShadowDepth 已有 VS）
- 1e StretchRect 场景深度副本（阴影解析同款模式）
- 验证：地形/物件全绿+深度副本可视化正确+水正常

### VF-2 屏幕空间体积雾（KW LightRays 模式移植，压轴主菜）
- 全屏 pass（挂 AO 合成前）：沿视线 raymarch 16-32 步，**高度雾密度场**（密度=f(z) 指数/线性衰减+地面浓度）×步长积分，深度副本做射线终点门控（近景不雾景物体）
- **太阳 in-scattering**（可选加强）：沿射线朝太阳方向加权（KW 神光同思路，雾中光柱）
- 加法合成（雾色+散射光）；INI 全参数：UseVolumetricFog/FogDensity/FogVolumeHeight/FogGroundDensity/SunScatterStrength
- 与 VF-0 材质雾共存策略：材质雾管"表面着色"，体积雾管"空气柱"——或按用户观感二选一开关
- 验证：镜头拉远=空气变浓、物体从雾中浮现、太阳方向有光柱、近景清晰

### VF-3 RA3 式氛围增强（可选收尾）
- 云影层全面化（引擎喂 CloudSetup 滚动投影——RA3 空气感的真正来源，我们已有云 stage 基建）
- 每图 LUT 调色（KW PostFX_LookupTable 模式，复用 P2/P3 后处理基建）

## 风险与回退
- 全部独立 INI 开关；VF-1 扇面若不可修则 VS 路线再封存，VF-2 改用"深度预通行"备胎（forward 期深度仅渲染地形+W3X 到色 RT）
- 工作量预估：VF-0 半天、VF-1 一天（含探针）、VF-2 一天、VF-3 半天

## 宫殿已录关键情报（本报告全文归档后开干）