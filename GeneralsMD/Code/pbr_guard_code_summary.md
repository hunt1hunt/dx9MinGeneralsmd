# PBR 排除守卫体系 + Set_Legacy_PBR 触发链

## 一、守卫体系（3层）

### 第1层：post_process() 根源阻断

**文件：** `Libraries/Source/WWVegas/WW3D2/meshmdlio.cpp:1919-1934`

在 PBR 参数推导前检查模型名（`ContainerName.` 前缀），匹配则跳过 `Set_Legacy_PBR()`，使 `Has_Legacy_PBR()` 保持 false。

```cpp
{	bool skipPBR = false;
    const char *modelName = Get_Name();
    if (modelName) {
        const char *excludedPrefixes[] = {
            "0qsnwateryy1.", "bloombox_r.", "bloombox_rx.",
            "qingwaddskybox.", "qsnboxmorning.",
            "bloomboxa.", "bloomboxb.", "bloomboxc.",
        };
        for (...) {
            if (_strnicmp(modelName, excludedPrefixes[pi], ...) == 0) {
                skipPBR = true; break;
            }
        }
    }
    if (!skipPBR) {  /* 原有的 PBR 参数推导代码 */ }
}
```

### 第2层：渲染管线三层守卫

**文件：** `Libraries/Source/WWVegas/WW3D2/dx8renderer.cpp`

| 阶段 | 行号 | 守卫条件 | 作用 |
|:----:|:----:|----------|------|
| Phase 3.5 | 1827 | `Has_Legacy_PBR() && !PBR_IsMeshExcluded(name)` | 跳过 PBR 常量 c3 设置 |
| Phase 4c | 1842 | `Has_Legacy_PBR() && !PBR_IsMeshExcluded(name)` | 跳过 PBR 像素着色器绑定 |
| Phase 3.7 | 2079 | `!PBR_IsMeshExcluded(name)` | 跳过 PBR 顶点着色器绑定 |

### 第3层：排除名单 + PBR_IsMeshExcluded()

**文件：** `GameEngineDevice/Source/W3DDevice/GameClient/W3DShaderManager.cpp:4495-4521`

```cpp
static const char *s_pbrExcludedMeshes[32] = {
    "0qsnwateryy1",     // 水面
    "bloombox_r",       // bloom
    "bloombox_rx",      // bloom 变体
    "qingwaddskybox",   // 天空盒
    "qsnboxmorning",    // 天空盒
};
```

比较函数 `PBR_IsMeshExcluded()`（第4514行）：`_stricmp` 精确匹配，忽略大小写。

---

## 二、Set_Legacy_PBR → Has_Legacy_PBR()=true 触发链

### 2.1 定义

**文件：** `Libraries/Source/WWVegas/WW3D2/meshmdl.h:281-284`

```cpp
void Set_Legacy_PBR(float roughness, float metalness) {
    m_legacyPBRRoughness = roughness;
    m_legacyPBRMetalness = metalness;
}
bool Has_Legacy_PBR(void) const {
    return (m_legacyPBRRoughness >= 0.0f && m_legacyPBRMetalness >= 0.0f);
}
```

### 2.2 构造函数默认值 = false（不触发 PBR）

**文件：** `Libraries/Source/WWVegas/WW3D2/meshmdl.cpp:77-78, 97-98`

```cpp
MeshModelClass() : m_legacyPBRRoughness(-1.0f), m_legacyPBRMetalness(-1.0f) {}
// Has_Legacy_PBR() = false：因为 -1.0f < 0.0f
```

### 2.3 Set_Legacy_PBR() 调用位置（仅有 2 处）

**均在 `meshmdlio.cpp` 的 `post_process()` 函数内：**

| 调用 | 行号 | 条件分支 | 触发条件 |
|:----:|:----:|----------|----------|
| 第1处 | **1953** | `if (DefMatDesc->Material[0] != NULL)` | 当 `pass 0` 的单材质不为空时 |
| 第2处 | **1974** | `else if (!Has_Material_Array(0)) { if (vmat) }` | 当没有材质数组但 `Peek_Single_Material(0)` 不为空时 |

**两个分支调用的推导逻辑相同：**
```cpp
// 从材质的 shininess 推导 roughness
roughness = sqrt(sqrt(2.0 / (shininess + 2.0)));
// 从材质的 specular 亮度推导 metalness
specLum = spec.R*0.299 + spec.G*0.587 + spec.B*0.114;
metalness = (specLum - 0.04) / 0.5;
Set_Legacy_PBR(roughness, metalness);  // ← 触发 Has_Legacy_PBR()=true
```

### 2.4 post_process() 提前修改材质（确保所有不透明材质都获得 PBR 参数）

**同一函数内，在 PBR 推导之前**（第1784-1867行）：

对所有不透明材质（opacity>=1.0f）且 spec=(0,0,0) 或 shininess=0 的：
```cpp
mtl->Set_Specular(100/255, 100/255, 100/255);  // = 0.39
mtl->Set_Shininess(50.0f);
```

这确保后续 PBR 推导总能计算出一个非零的 roughness 和 metalness。

### 2.5 Has_Legacy_PBR() 的消费方（6 处）

| 文件 | 行号 | 用途 |
|------|:----:|------|
| dx8renderer.cpp | 762 | FVF 修改：添加 TEX3（法线编码到 TEXCOORD2） |
| dx8renderer.cpp | 1151 | 法线编码到 TEXCOORD2 |
| dx8renderer.cpp | 1827 | Phase 3.5 守卫（排除检查） |
| dx8renderer.cpp | 1842 | Phase 4c 守卫（排除检查） |
| dx8renderer.cpp | 2078 | Phase 3.7 守卫（排除检查） |

---

## 三、关于"阵营色、树木、岩石、建筑"

这些类别**不在排除名单中**。它们获得 `Has_Legacy_PBR()=true` 的原因：

1. `post_process()` 对所有不透明材质（`opacity >= 1.0f`）设 `spec=(0.39,0.39,0.39)` + `shininess=50`
2. 紧接着 PBR 推导从这些值算出 `roughness≈0.37` + `metalness≈0.59`
3. `Set_Legacy_PBR()` 被调用 → `Has_Legacy_PBR()=true`

**自然避开 PBR 的只有：** 透明/半透明物体（opacity < 1.0），例如带 alpha 测试的树木，因为 post_process 第1836行的条件 `if (vmat->Get_Opacity() >= 1.0f)` 不满足，材质不被修改，后续 PBR 推导也跳过。
