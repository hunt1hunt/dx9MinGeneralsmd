# VF-2 体积雾参考资料消化

> 来源1: pizi0475《体积雾(dx9)》 https://blog.csdn.net/pizi0475/article/details/6913049
> 来源2: 豆包分享的 VC6+DX9c 完整源码（固定管线顶点雾 + PS_2_0 解析式高度雾）——用户粘贴正文
> 消化日期: 2026-09-14。本文件是 VF 计划(plan-sess_4712a02b)的 VF-2 段参考资料增补。

## 来源2（豆包）：两个 VC6 可编译方案

### 方案A：固定管线顶点雾（零 Shader）
纯渲染状态：FOGENABLE/FOGCOLOR/FOGVERTEXMODE(LINEAR|EXP|EXP2)/FOGDENSITY/FOGSTART/FOGEND。
**与我们关系**：= VF-0a 已做的 FF 雾（shader.cpp 全局覆盖+设备地板）。已实践，非 VF-2 目标。

### 方案B：PS_2_0 解析式高度雾（每像素闭式公式，非 raymarch）
```hlsl
viewDist    = distance(worldPos, cameraPos);
heightFactor = exp(-max(worldPos.y - fogBaseHeight, 0) * 0.08);  // 高度衰减
fogAmount   = 1 - exp(-viewDist * fogDensity * heightFactor);    // 指数浓度
finalColor  = lerp(diff.rgb, fogColor, fogAmount);
```
C++ 侧：D3DXCompileShaderFromFile + ID3DXConstantTableSetValue 喂参（我们惯用内嵌字符串+D3DXCompileShader，等价）。

**与我们关系（重要辨析）**：
- 这条公式 = 在**像素终点**取密度的一次乘积近似——数学上等于 P4 材质雾已实现的"距离×高度双因子"思路（c30/c31 那套），用户已裁定"非想要的体积雾"而挂起
- **但高度衰减因子 exp(-Δh×0.08)（高度尺度≈12.5 单位）直接采进我们 VF-2 的 raymarch 密度函数**：每步密度 = exp(-z_step×invH) × groundDensity，沿射线积分才是真体积感
- 即：来源2给了"密度函数"，pizi0475给了"深度门控+MRT深度获取"，KW LightRays给了"屏幕空间 raymarch+太阳散射"——三者拼起来就是 VF-2 完整方案

## 来源1（pizi0475）：雾体前后深度法 + MRT 深度（保持原消化）

1. 场景 MRT 渲染: COLOR0=场景色, COLOR1=场景深度（DX9 无法直接读 ZBuffer → MRT 或二次渲染；PS2.0+ 支持，NumSimultaneousRTs 一般=4，**MRT 与 MSAA 互斥**）
2. 雾体模型背面深度 → 纹理A（D3DCULL_CW 反剔 + ZFUNC GREATER 技巧）
3. 雾体模型正面深度 → 纹理B
4. 全屏后处理 (vs_3_0 过屏 + ps_3_0 四采样: 场景色/场景深度/雾前/雾后):
   ```
   dis = 0                                // sceneDepth <= fogFront 物体在雾前
   dis = sceneDepth - fogFront            // 物体在雾内
   dis = fogBack - fogFront               // sceneDepth >= fogBack 物体在雾后
   factor = max(dis - fogStart, 0) / (fogEnd - fogStart)
   finalColor = rgb + factor * (fogColor - rgb)   // 提公因式省一次乘法
   ```
5. 深度纹理 Clear 黑可减少分支；场景色用 StretchRect 拷到动态纹理

## 采纳进 VF-2 的点

| 采纳 | 说明 |
|------|------|
| MRT 深度路线确认 | = VF-1c（地形 ps_3_0 twin 写 NDC z 到 GB/rt2），文章实证 DX9 下这是标准解 |
| 合成公式 | `rgb + factor*(fogColor-rgb)`，加法提公因式版本 |
| 深度门控三分支 | 我们的版本: raymarch 终点 = min(场景深度对应距离, FogEnd)；近景物体自动无雾 |
| StretchRect 场景色副本 | 与阴影解析同款模式（VF-1e 直接复用） |
| 纹理清黑/清白技巧 | 深度 RT clear 值统一（远平面=1.0），省分支 |

## 不采纳（保持我们路线）

- **雾体网格法**：需要为雾建模几何+两次额外 pass，适合"形状受限的局部雾"。
  我们要的是全图统一高度雾空气感 → **高度雾密度场 raymarch**（密度 = exp(-z/H) 高度衰减 × 地面浓度，16-32 步积分，深度门控终点），无需雾几何。
- 文章无太阳 in-scattering（我们的可选加强来自 KW PostFX_LightRays 思路：沿射线朝太阳方向加权）。

## VF-2 参数面（与 P4 材质雾 INI 共存）

- UseVolumetricFog / FogDensity / FogVolumeHeight / FogGroundDensity / SunScatterStrength（INI 全参数，默认关）
- 挂 AO 合成前（P3 Bloom 之前），全屏 pass；近景清晰=门控，远景统一溶进雾色

## 当前依赖链状态（2026-09-14）

- VF-1a 扇面案进行中（截图定形=草地纹理巨楔=地形混合几何错投影；探针位 2048/4096/8192 待用户测）
- VF-1b 已就位（TerrainVSRoute INI 化）
- VF-1c 地形 MRT 深度 ← 依赖 1a 结案 + VS 路线放行
- 白影案已定案（=雾因，fog 关即无白影），由 VF-2 整体埋葬
