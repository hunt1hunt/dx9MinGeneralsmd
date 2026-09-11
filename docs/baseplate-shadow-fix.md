# 建造底板"无阴影"案例归档（2026-09-11 结案）

## 症状
中方指挥中心（APACONSTRUCTIONYARD_SKN）与兵营（APABARRACKS_SKN）的建造底板
（子网格 SKIN_G00）不接受 W3X 模型贴图阴影，且底板底色异常亮白（正常应为暗色泥土）。

## 真相：阴影接收一直是正常的
经灰度可视化证实，底板的阴影项（hp_invshadow_trilinear）计算完全正确。
"看不见阴影"的根因是**素材缺陷把底板洗白、同时把正常光照压黑**，阴影对比度被摧毁：

1. `TasCC3_G_SPM.dds` / `TasBK3_G_SPM.dds`（底板高光贴图，DXT3）的
   **alpha 通道 ≈ 0.06**（导出残留，应为 1.0）。
2. ARPBR shader 约定：`spm.w` 同时控制 `extra_cavity *= spm.w`（环境遮蔽乘法）
   与 `glowchannel = saturate(0.5 - spm.w)`（自发光掩码）。
3. 结果：`extra_glow = albedo × 0.44`，末尾 `OUTCOLOR += extra_glow × glow_mult(4)`
   → 叠加 albedo×1.75 的白光；同时 cavity≈0.06 把正常光照几乎压黑。
   合成效果 = "有纹理图案但底色亮白、阴影不可见"。

## 修复
- **素材侧（游戏目录，不在本仓库）**：将两张 _G_SPM 的 DXT3 alpha 块全部写 0xFF
  （备份 `.bak-alpha` 同目录）。路径：
  `D:\!!!!!!!QWCSB\!!!!!!!QWCSB\ART\W3X\AP\TasCC3_G_SPM.dds`、`TasBK3_G_SPM.dds`
  （本体的 TasCC3_SPM/TasBK3_SPM alpha=0.25 也有同样问题，未处理，观察后再定）。
- **shader 侧（本提交）**：PBR5-10-objects-ARPBR.FX 两处增强，让阴影在 W3X 上可见：
  1. 环境光软化阴影 `ambientALL *= (0.5 + 0.5*arpbrShadowTerm)`（原版只乘直射阳光，
     环境光占比高的表面阴影被冲淡）；
  2. 背阴面太阳反弹补光 `shadowFillStrength = 0.3`：在朝向门清零 SUNcolor **之前**
     保存 SUNcolorFill，补光 = diffcolor × SUNcolorFill × 0.3 × saturate(-sun_tilt)，
     **不乘阴影项**（背阴墙的自阴影读数恰为 0，乘它补光再次归零——此坑连咬两次）。

## 诊断链（供复用）
1. FX 是游戏启动时现场编译（改 `D:\...\Shaders\RA3\*.FX` + 重启即生效，无需重编 exe）。
2. 引擎有 shader 重映射表 `W3XShaderVariant`（W3XModelDraw.cpp）：mesh 的
   ShaderName 运行时被映射——buildings*→w3x_buildings.fx，**其余（含
   objectsgeneric.fx/G00）→ w3x_soviet.fx**；RA3 式入口 fx（buildingssoviet/
   objectsgeneric）是死文件，改它们无效。
3. 可视化二分法：PS 里临时 `return` 各阶段中间量（阴影项/反照率/光照分量），
   一次重启定位断点。注意 HLSL 坑：注释漏 `//`、float4 构造器超 4 分量
   → 编译失败 → 模型整体消失（无报错提示）。
