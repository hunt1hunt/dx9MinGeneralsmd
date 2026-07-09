# PBRDebugMode 调试可视化系统

## INI 配置入口

**文件：** `GameEngine/Source/Common/GlobalData.cpp:94`
```cpp
{ "PBRDebugMode", INI::parseInt, NULL, offsetof( GlobalData, m_pbrDebugMode ) },
```

**声明：** `GameEngine/Include/Common/GlobalData.h:145`
```cpp
Int m_pbrDebugMode;
// 0=off, 1=metalness, 2=roughness, 3=AO, 4=normals,
// 5=diffIBL, 6=specIBL, 7=direct
```

## 全局变量

**文件：** `W3DShaderManager.cpp:135`
```cpp
Int g_pbrDebugMode = 0;
```

**同步：** `W3DShaderManager.cpp:3292`
```cpp
g_pbrDebugMode = TheGlobalData ? TheGlobalData->m_pbrDebugMode : 0;
```

## PS 常量 c11 设置（两处）

### 位置 A：dx8renderer.cpp Phase 4c
**文件：** `Libraries/Source/WWVegas/WW3D2/dx8renderer.cpp:1870-1873`
```cpp
float dbg[4] = { (float)TheGlobalData->m_pbrDebugMode, 0.0f, 0.0f, 0.0f };
DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(11, dbg, 1);
```

### 位置 B：W3DPBRShader::set()
**文件：** `GameEngineDevice/Source/W3DDevice/GameClient/W3DShaderManager.cpp:3497-3499`
```cpp
float dbg[4] = { (float)TheGlobalData->m_pbrDebugMode, 0.0f, 0.0f, 0.0f };
DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(11, dbg, 1);
```

## 着色器中读取 c11.x

| 文件 | 行号 | HLSL 声明 | 调试变体 |
|------|:----:|-----------|:--------:|
| W3DShaderManager.cpp | 2814 | `float4 c11 : register(c11);` | src30IBL (ps_3_0 IBL 纹理) |
| W3DShaderManager.cpp | 2862-2866 | `float dbg = c11.x;` | 1=金属度 2=粗糙度 3=AO 4=法线 |
| W3DShaderManager.cpp | 2922 | `float4 c11 : register(c11);` | src30SpecIBL (ps_3_0 SpecIBL 纹理) |
| W3DShaderManager.cpp | 2977-2983 | `float dbg = c11.x;` | 1-4同上+5=diffIBL 6=specIBL 7=direct |
| W3DShaderManager.cpp | 3019 | `float4 c11 : register(c11);` | src30SpecIBL alpha |
| W3DShaderManager.cpp | 3170 | `float4 c11 : register(c11);` | srcNT_30_IBLSpec |

## 模式映射

| dbg 范围 | 可视化内容 |
|:--------:|-----------|
| < 0.5 | 正常 PBR 渲染 |
| 0.5 - 1.5 | metalness |
| 1.5 - 2.5 | roughness |
| 2.5 - 3.5 | AO |
| 3.5 - 4.5 | 法线 (N*0.5+0.5) |
| 4.5 - 5.5 | envDiffuse (IBL 漫反射) |
| 5.5 - 6.5 | envSpecular (IBL 镜面反射) |
| > 6.5 | result (直接光照) |

## 你的修复

两处 `dbg[4]` 已从硬编码 `{0,0,0,0}` 恢复为 INI 驱动：
- ✅ dx8renderer.cpp:1871-1872
- ✅ W3DShaderManager.cpp:3497-3498
