# RA3 参考资料库登记（2026-09-12 备案）

四个资料库的位置、内容与用途。本引擎（MinGeneralsMD）的 W3X 管线大量移植自 RA3，
这些库是 shader 移植、资产补缺、美术参考的第一手来源。

## 1. RA3 原版 Shader 完整源码（最重要）
`E:\Source\repos\CnC_Modding_Support\Red Alert 3\Shaders`
- 108 个 .fx/.fxh + 21 个辅助文件，**RA3 官方 shader 源码全集**
- 关键文件：`DefaultW3D.fx/.fxh`（灯/通用网格的原版实现——SKIN_LIGHT 的完整语义参考）、
  `MuzzleFlash.fx`、`BuildingsAllied/Soviet/Japan/Generic.fx`、`Terrain*.fx`、`Cloud*.fx`
- 上级目录 `CnC_Modding_Support\` 还有 Generals/Kanes Wrath/Renegade/TW/TT 全系列 shader
- 用途：任何 w3x_*.fx 移植/校对的第一参考（grep 即可，无需索引）

## 2. 社区自定义 Shader 库
`E:\Source\repos\custom-shaders-RedAlert3`
- `FXFXH\`：魔改版 shader 源码（basicw3dHSR/MASK、buildings 系列）+ `FXO\` 预编译版
- `SKYBOX Textures + test models\`、`VFX\`、`TOOLS\`
- 用途：社区魔改思路参考（HDR/蒙版变体等）

## 3. RA3 原版完整资产（平铺）
`D:\红警3有关的素材集合\ra3红警3原版完整素材`
- RA3 原版 .w3x 模型 + 贴图平铺（ABAIRFIELD 等）
- **`FX2ndPassA.dds` + `.xml` 就在这里**——建筑灯面片缺失的次级动画贴图！
  （未来做灯的扫光增强：拷入游戏 `ART\W3X\` 即可被 `ResolveTextureDDS` 命中）
- 用途：资产补缺的第一来源

## 4. 其余素材子库（同目录）
`D:\红警3有关的素材集合\`
- `北佬提供红警3资料\`：盟军/帝国/苏军/起义 ArtSourcePack 源包(.rar) + FBX 中英翻译版
- `红警3MODbatter素材\`：DDS 贴图+xml（含现代军事皮肤如 052D）
- `遗忘发来的红警3将军2里的资源\`：将军2(RA3引擎)资源
- 合计 14622 文件

## 检索约定
- shader 源码：直接 grep 库 1/2（文件名即功能名）
- 贴图/模型资产：`find <库> -iname "<资产名>.*"`（库 3 平铺结构最易命中）
- 灯光管线现状见 `w3x_lights.fx` 与记忆宫殿 `ez1ia3y`

---

# FXFXH 库可用资产清单（2026-09-12 精读评估）

> 库定位：本项目 shader 体系的**社区上游**（早于 EA 开源、非官方 fork）。
> ARPBR/head0-COMMON 等同名文件是我们的旧版——**本地已大量演化（阴影修复/灯修复），
> 严禁用库版本覆盖**。有价值的是我们没有的独有文件。

## A. 即刻采用（零/近零成本）
1. **fxc.exe + compileALL.bat 离线编译工具链**（FXFXH 根目录）
   语法检查 .fx 无须启动游戏——可拦截"注释漏//、构造器超4分量"类事故（本会话发生过两次）
2. **FXstarrysky256quad.dds / FXscreen_invasion.dds**（VFX\）特效贴图
3. **FX2ndPassA.dds**（库③）灯次级动画贴图——已定位待用

## B. 短期推荐（小改造）
4. **FXdeferredpointlight.FX** 体积球点光：读深度缓冲重建像素位置+距离衰减+加色
   = 建筑灯"真实照亮周围"的现成方案（我们延迟管线有 depth RT 可绑）
5. **IS_NANO_BUILDUP 纳米建造动画**：ARPBR 已有该代码路径（213-218），库的
   buildingsjapanbuildup.fx 就是纯 define 包装——做一个 w3x_nano.fx 即可激活
6. **VFX\laser_starry.fx / laserhc.fx** 星空激光特效（独立小 shader+贴图）

## C. 中期参考（需移植）
7. **TERRAIN.FX**（35KB 完整地形）：8 点光、更平滑阴影边、点光反射——地形管线升级蓝本
8. **FXscreenSpaceShadow.fx** 全屏阴影蒙版（另一条阴影路线，参考用）
9. **FXcutDepthBuffer.FX** 地形深度钻孔（RenderBin=Bridge）——桥梁/地下建筑可视化

## D. 工具
10. TOOLS\Max2w3x.dle（3ds导出插件）+ Rigid_Skin_Maker.MS（中英双语）
11. SKYBOX 贴图+测试模型（PBR 天空盒未来接真图直接用）
12. scrapeo-syntax-highlighter（RA3 SDK 脚本语法高亮）
