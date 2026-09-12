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
