# post_process() 中 shininess/specular 修改位置清单

**文件：** `Libraries/Source/WWVegas/WW3D2/meshmdlio.cpp:1784-1867`

## 块A：Material[0]（第1786-1793行）

```cpp
if (DefMatDesc->Material[0] != NULL) {
    Vector3 spec;
    DefMatDesc->Material[0]->Get_Specular(&spec);
    if ((spec.X > 0.86f && spec.Y > 0.86f && spec.Z > 0.86f) ||
        (spec.X == 0.0f && spec.Y == 0.0f && spec.Z == 0.0f)) {
         DefMatDesc->Material[0]->Set_Specular(0.80f, 0.80f, 0.80f);
         DefMatDesc->Material[0]->Set_Shininess(50.0f);
    }
}
```
→ spec=0.80 → metalness=(0.80-0.04)/0.5=1.0（饱和）

## 块B：MaterialArray[0] 遍历（第1796-1814行）

```cpp
if (mtl->Get_Opacity() >= 1.0f) {
    Vector3 spec;
    mtl->Get_Specular(&spec);
    if ((spec.X > 0.86f && spec.Y > 0.86f && spec.Z > 0.86f) ||
        (spec.X == 0.0f && spec.Y == 0.0f && spec.Z == 0.0f)) {
       mtl->Set_Specular(0.80f, 0.80f, 0.80f);
       mtl->Set_Shininess(50.0f);
    }
}
```
→ spec=0.80 → metalness=1.0

## 块C：Peek_Single_Material(0) ← 影响最广（第1820-1839行）

```cpp
if (vmat->Get_Opacity() >= 1.0f) {
    Vector3 spec;
    vmat->Get_Specular(&spec);
    if (spec.X == 0.0f && spec.Y == 0.0f && spec.Z == 0.0f) {
        vmat->Set_Specular(100.0f/255.0f, 100.0f/255.0f, 100.0f/255.0f);
    }
    if (vmat->Get_Shininess() == 0.0f)
        vmat->Set_Shininess(50.0f);
    vmat->Set_Opacity(1.0f);
} else {  // 透明物体
    if (vmat->Get_Shininess() == 0.0f)
        vmat->Set_Shininess(30.0f);
}
```
→ spec=100/255=0.392 → metalness≈0.704 ← **绝大部分不透明模型走此分支**

## 块D：MaterialArray[pass] 遍历（第1844-1866行）

```cpp
if (vmat->Get_Opacity() >= 1.0f) {
    Vector3 spec;
    vmat->Get_Specular(&spec);
    if (spec.X == 0.0f && spec.Y == 0.0f && spec.Z == 0.0f) {
        vmat->Set_Specular(100.0f/255.0f, 100.0f/255.0f, 100.0f/255.0f);
    }
    if (vmat->Get_Shininess() == 0.0f)
        vmat->Set_Shininess(50.0f);
    vmat->Set_Opacity(1.0f);
} else {
    if (vmat->Get_Shininess() == 0.0f)
        vmat->Set_Shininess(30.0f);
}
```
→ spec=0.392 → metalness≈0.704

## 影响汇总

| 块 | 行号 | 设置 spec | metalness | 触发条件 |
|:--:|:----:|:---------:|:---------:|----------|
| A | 1790-1791 | 0.80 | 1.0 | Material[0] spec=0或>0.86 |
| B | 1807-1808 | 0.80 | 1.0 | MaterialArray spec=0或>0.86 + opacity>=1 |
| **C** | **1830,1833** | **0.392** | **0.704** | **Peek_Single_Material spec=0 + opacity>=1 ← 绝大多数模型** |
| **D** | **1855,1858** | **0.392** | **0.704** | **MaterialArray spec=0 + opacity>=1** |

透明物体（opacity<1）进入 else 分支只设 shininess=30，不清除 specular（保持为 0），PBR 推导时 specLum=0 → metalness=0。
