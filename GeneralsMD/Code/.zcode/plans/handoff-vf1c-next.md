# 交接文档：VF-1c 开工（扇面案已结案）

> 2026-09-14 深夜交接。上承 plan-sess_4712a02b（VF总计划）+ vf2-fog-reference-digest.md（VF-2配方）。

## 一、两案结案状态（勿重查）

| 案 | 结论 | 证据 |
|----|------|------|
| **扇面案**（P1d遗留） | **结案**：四个地形twin像素着色器 `return float4(result, base0.a)` 把底图alpha当地形输出alpha→地形半透明透出裙边几何（透视拉伸草地）=放射巨楔。修复=四变体默认alpha=1.0 | PBRDebugMode=20 A/B铁证；提交 57540f3f |
| **白影案** | **结案**：=雾因（UseDistanceFog=No即无白影），VF-0a修复链有效，等VF-2体积雾整体埋葬 | 用户A/B实测 |

两笔提交：`f0d464ab`（VF-0a白影修复）、`57540f3f`（扇面结案+探针基建+VS路线INI化）。工作树干净。

## 二、下窗口任务：VF-1c 地形MRT写GB深度（VS路线已放行）

1. **VF-1c**：地形 ps_3_0 twin 写 NDC z 到 GB/rt2（引擎 g_gbufferPS 同域；MRT与MSAA互斥但我们不用MSAA——pizi0475文献实证MRT是DX9标准解）
2. **VF-1d**：W3X深度（去掉GB早退+深度仅技术，借鉴W3X ShadowDepth已有VS）
3. **VF-1e**：StretchRect场景深度副本（阴影解析同款模式）
4. **顺手修**：s4采样器撞车（twin的detail项tex2D(s4,tex0*8.0)把阴影图当detail纹理采样——非扇面但属画质瑕疵，detail挪空闲sampler）
5. **VF-2体积雾**（压轴主菜）：配方三件套见 vf2-fog-reference-digest.md——豆包密度函数(exp(-z*invH)*groundDensity,高度尺度~12.5)+pizi深度门控三分支+KW PostFX_LightRays raymarch骨架与太阳in-scattering；挂AO合成前；INI全参数(UseVolumetricFog/FogDensity/FogVolumeHeight/FogGroundDensity/SunScatterStrength)默认关

## 三、探针基建（全部默认关，INI可开，勿删）

- `WaterProbeMode`：1=跳战雾通道 2=跳主绘制 3=整个水体对象全关
- `TerrainProbeMode`（位叠加）：1迷雾pass/2车辙/4桥/8云/16仅PS关阴影/32阴影矩阵原样/64高光/128点光/512法线/1024 s4解绑/2048额外混合瓦片/4096海岸线/8192道具/65536跳stage7/131072 vs_2_0+ps_2_a档/262144截屏dump(600/1200帧存E:\fan_dump_N.png)
- `TerrainVSRoute`：地形vs_3_0路线开关（转置修复+TEXCOORD2云UV补写+防泄漏守卫+末尾铁锤都在）
- PBRDebugMode 20/21/26/28 调试位已随结案清理（twin里已删）

## 四、构建与部署铁律（实战验证）

- VC6在 `C:\Program Files (x86)\Microsoft Visual Studio\`；批处理：`E:\...\GeneralsMD\Build\build_vf1b.bat`（GameEngine→GameEngineDevice→RTS三工程Release增量）
- **msdev退出码不可信**——验收看日志 error 计数；**msdev单实例**（并发互删obj）
- 构建产物 `GeneralsMD\Run\RTS.exe` → **必须部署**：备份 `D:\!!!!!!!QWCSB\!!!!!!!QWCSB\RTS.exe`→.bak，拷贝，cmp字节校验
- 用户INI：`D:\!!!!!!!QWCSB\!!!!!!!QWCSB\Data\INI\GameData.ini`——用户测试时会自行改值/覆盖注释行，改它只用Edit工具或字节级python（**assert find!=-1**，编码往返会损坏）
- 改INI别用gbk往返脚本（出过事故，教训在宫殿）

## 五、宫殿状态（已迁移）

- **新宫** cbec6b73-71a8-4f3a-96fd-3fb7ba0c0931；admin `gk_43e30996eda77d8776a97a4622368f63`（config.json里）；zcode-agent写钥匙 `gk_de3ddcb68e15a5d20ceb3b2a1364330b`
- 归档通道（已验证）：`GET https://m.cuer.ai/api/ingest?auth=<写钥匙>&data=<urlsafe_b64(json)>`（payload字段：session_name/agent/status/outcome/repo/branch/built/decisions/next_steps/files/blockers/conversation_context/roster/metadata）
- 旧宫15608374钥匙全吊销，记忆链经公开胶囊 `m.cuer.ai/q/<short_id>` 可读（关键：wa1pwzs画质计划/5mk7lri红指纹/56dl04w截图定形/lzdqk5b雾文献/uxot9jm结案档）
- 宫殿钥匙若再失联：先考古会话JSONL（`C:\Users\Administrator\.zcode\cli\rollout\`），再POST /api/palace重建（同public_key不去重会开新宫）

## 六、开局动作

1. 触发 min-generals 技能 → 直连宫殿读 uxot9jm（结案档）
2. 读 W3DShaderManager.cpp 的 g_gbufferPS/延迟渲染基建 + HeightMap 地形twin选择（ST_TERRAIN_PBR×4变体）
3. VF-1c 动工：给地形ps_3_0 twin加MRT深度输出版本（新增变体勿改现有签名——宫殿事故2教训）
