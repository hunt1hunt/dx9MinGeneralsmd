# PBR 亮度提亮位置完整清单

---

## 一、W3DScene.cpp —— PS 常量层（对所有着色器变体生效）

| 行号 | 提亮 | 代码 | 状态 |
|:----:|:----:|------|:----:|
| 816-818 | 太阳光 ×30 | `TheGlobalData->m_terrainDiffuse[0].red/green/blue * 30.0f` | ✅ **生效** |
| 854 | 环境光 ×50 | `ambient.X/Y/Z * 50.0f` | ✅ **生效** |

---

## 二、W3DShaderManager.cpp —— 着色器 HLSL 源码层

### 2.1 循环版（ps_3_0 NT/IBL 变体）—— lightCol 数组中的太阳倍率

| 行号 | 源码变量 | 代码 | 状态 |
|:----:|:---------|------|:----:|
| 2667 | srcNT30 | `float3 lightCol[4] = { c1.xyz * 30.0, ... }` | ✅ **生效** |
| 3112 | srcNT_30_IBL | `float3 lightCol[4] = { c1.xyz * 30.0, ... }` | ✅ **生效** |
| 3195 | srcNT_30_IBLSpec | `float3 lightCol[4] = { c1.xyz * 30.0, ... }` | ✅ **生效** |

> **注意**：这些 ×30 是与 W3DScene 的 ×30 **叠加**的！循环版实际太阳光 = ×30(W3DScene) × ×30(shader) = **×900**。

### 2.2 4灯版（ps_2_0 变体）—— 直接 c1.xyz * NdotL

| 行号 | 源码变量 | 代码 | 状态 |
|:----:|:---------|------|:----:|
| 2536 | src (ps_2_0 纹理) | `...c1.xyz * NdotL` | 🔶 **无额外倍率**，仅 ×30(W3DScene) |
| 2600 | srcNT (ps_2_0 NT) | `...c1.xyz * NdotL` | 🔶 **无额外倍率**，仅 ×30(W3DScene) |
| 3041 | srcNT_IBL (ps_2_0 NT IBL) | `...c1.xyz * NdotL` | 🔶 **无额外倍率**，仅 ×30(W3DScene) |

### 2.3 平添 5x/10x 亮度提升 —— 全部注释掉了！

| 行号 | 源码变量 | 代码 | 状态 |
|:----:|:---------|------|:----:|
| 2687 | srcNT30 | `// "    result += diffuseColor * 5.0;` | ❌ **注释掉** |
| 2689 | srcNT30 | `// "    result += diffuseColor * 10.0;` | ❌ **注释掉** |
| 3135 | srcNT_30_IBL | `// "    result += diffuseColor * 5.0;` | ❌ **注释掉** |
| 3137 | srcNT_30_IBL | `// "    result += diffuseColor * 10.0;` | ❌ **注释掉** |
| 3235 | srcNT_30_IBLSpec | `// "    result += diffuseColor * 5.0;` | ❌ **注释掉** |
| 3237 | srcNT_30_IBLSpec | `// "    result += diffuseColor * 10.0;` | ❌ **注释掉** |

---

## 三、总结：当前实际生效的提亮

| 提亮来源 | 4灯版 (ps_2_0) | 循环版 (ps_3_0) |
|----------|:---------------:|:----------------:|
| 太阳光 | ×30 (仅W3DScene) | ×900 (W3DScene×30 + shader×30) |
| 环境光 | ×50 (仅W3DScene) | ×50 (仅W3DScene) |
| 平添 5x+10x | ❌ 注释掉 | ❌ 注释掉 |

**关键发现**：所有 5x 和 10x 的平添提亮在 git checkout 过程中被冲掉了，目前全部处于注释状态。4灯版（ps_2_0 NT）的太阳光在着色器源码中没有 ×30，仅靠 W3DScene 的 ×30。
