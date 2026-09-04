# 支奴干（AVChinook）旋翼不可见故障 — 完整因果链与修复总结

> 日期：2026-09-03 ｜ 状态：已解决（游戏内验证通过）
> 涉及提交：`7b15f2a0`（早期）→ `22c3d548`（诊断仪器）→ `0fb2413a`（状态raw直推）→ `ed80fd7a`（SORT标志清除）→ 收尾清理

---

## 一、故障现象

美军运输直升机支奴干（AVCHINOOK / AVCHINOOKAG，旧 W3D 模型）顶部前后两副旋翼的叶桨
完全不可见。期望表现：各三片半透明深色叶桨随动画旋转。

## 二、资产数据（离线解析实证，零编译）

| 项 | 事实 |
|---|---|
| 网格 | `PROPS01/02` 叶桨（12 tri，CAST_SHADOW）；`PROPELLER01/02` 桨毂（不透明） |
| Shader | `SRC_ALPHA / INV_SRC_ALPHA`，深度 LEQUAL、**不写深**；vmtl Opacity=1.0，无顶点色 |
| SORT | `SORT_LEVEL=0` 但 `meshmdlio.cpp` 对 DestBlend≠ZERO 自动 `Set_Flag(SORT)` |
| 贴图 | `Textures.big\Art\Textures\avcomanche_p.dds`（64×64 **DXT5**，73% 像素 α<16） |
| **关键** | 按 PROPS UV 逐面采样：顶点角 α=**0/35/27**，面均值 **45.6/255≈18% 不透明度** |

⇒ 资产本身"天生极淡"：即使绘制链路完美，也只是把背景压暗 ~18%。

工具：`E:\tmp\rotor_alpha_probe.py`（BIG 大端目录@16 紧凑无对齐；W3dTriStruct=32B；
UV=0x4A in 0x48 in 0x38；可视化 `E:\tmp\_rotor_uv_alpha.png`）。

## 三、渲染管线结构（读码实证）

```
RTS3DScene::Render 延迟路径:
①ShadowMap Pass(每对象强制 WORLD=identity) → ②G-Buffer(g_gbufferActive=true)
→ ③延迟光照(+HDR/ToneMap) → ④Forward Transparent Pass(整场景重画)
→ ⑤aoComposite(乘法混合)/iblComposite(加法混合)
每 pass 的 RTS3DScene::Flush 末尾: SortingRendererClass::Flush()  ← 排序(半透明)多边形最后画
```

游戏内同时存在**两个模型**：`AVCHINOOKAG`（玩家色版，玩家看到的）与 `AVCHINOOK`。

## 四、五层根因（按发现与修复顺序）

### ① 延迟 G-Buffer 深度污染
半透明叶桨被当作不透明写入 G-Buffer，其深度使前向半透明绘制被深度拒绝。
**修复**：`mesh.cpp` — `g_gbufferActive && Is_Translucent()` → 不提交基 passes。

### ② 分支链抢占：玩家色 USER_DATA
玩家色系统给 AG 模型网格挂 `USER_DATA_MATERIAL_OVERRIDE`，dx8renderer 分支链里
alpha-override 分支排在旋翼分支之前 → 所有修复从未作用于**玩家看见的** AG 模型。
（日志铁证：分支在 `AVCHINOOK` 上全屏生效，AG 却毫无变化。）
**修复**：`if (!rotorBlade && (alpha_override || USER_DATA...))` — 旋翼优先。

### ③ PBR 顶点着色器裁剪（遮挡查询实锤 pixels=0）
支奴干是 PBR 单位，`PBR_BindVS()` 绑定可编程 VS；固定管线绘制（PS=NULL+TSS）在
VS 绑定下用 VS 自己的常量变换顶点 → **12 个三角形全部被裁剪，GPU 光栅化 0 像素**。
**修复**：绘制前 `SetPixelShader(NULL)+SetVertexShader(NULL)`，走固定变换
（WORLD/VIEW/PROJ 寄存器已被证明是真实矩阵），绘制后还原。
⇒ 该层修复后叶桨首次上屏（呈网格自身"亮纹理×18%"外观）。

### ④ 状态所有权：wrapper 差量推送不可靠
`[ROTOR8]` 绘制前读回：`blend=5/6 aop=4`（网格值）而 `tf=00FF0000`（我的）幸存。
机制：`Apply_Render_State_Changes` **无通用 RS 段、无 TSS 段**；
`ShaderClass::Apply` 用 `diff=CurrentShader^ShaderBits` 差量推送；
`Set_DX8_Render_State` 即时生效、`Set_Shader/Set_DX8_Texture_Stage_State` 只进缓存。
**修复（攻防合一）**：wrapper 设置（缓存=override ⇒ Draw 内 flush 无脏可推）
+ `renderer->Render()` 前 **raw 直推**相同状态（设备=override）。读回验证全绿。

### ⑤ 排序容器劫持（视向差异真凶）
SORT 标志 → 注册进**排序型 FVF 容器**（VB=DYNAMIC_SORTING）→ `Draw()` 把叶桨
劫持进 `SortingRendererClass` → **帧末用网格自身 shader 重放**（正面剔除+18%亮纹理）
且最后画 ⇒ 从上看盖掉修复绘制（"亮"），从下背面剔除（露出正确暗桨）。
**修复**：`mesh.cpp` — 注册前对 PROPS/PROPELLER 半透明网格
`Model->Set_Flag(MeshGeometryClass::SORT, false)`，彻底脱离排序管线。

## 四b、后续两问（2026-09-04，验证中发现的回归与泄漏）

### ⑥ 清 SORT 后的绘制时机回归：桨叶被后方岩石"遮挡"
SORT 清除后叶桨改为提交序直绘（不再有排序池"顺带最后画"的保护），后画的岩石/机身
以 LEQUAL 对自身 G-Buffer 深度必过 → 重画像素盖掉不写深度的桨叶。
**修复**：叶桨开启深度写入（`Set_Depth_Mask(DEPTH_WRITE_ENABLE)` + raw
`ZWRITEENABLE=TRUE`）→ 后画几何在桨叶像素深度更远必失败，无法覆盖；
机身重叠区缺角问题同源同修。

### ⑦ TSS 恢复只进缓存不进设备：岩石间歇性全白
绘制后经 wrapper 恢复 4 个纹理级状态，但 `Apply` 无 TSS 推送段 → 设备残留
`SELECTARG1 + TFACTOR`；TFACTOR 此时已还原为 0xFFFFFFFF（白）→ 之后未完整
重推纹理级状态的网格（水面岩石）输出纯 TFACTOR = 全白，某次完整材质重放后恢复。
叶桨每帧绘制 ⇒ 泄漏反复 ⇒ "多次变白"。
**修复**：绘制后用 **raw 设备调用**对称还原 4 个 TSS 值（缓存、设备同时还原）。

## 五、最终实现（dx8renderer.cpp 旋翼分支）

对可见前向 pass 的叶桨绘制：
1. G-Buffer 跳过；阴影/选框 pass（COLORWRITE 无 RGB）保持原状；world=identity 保持原状。
2. `PS=NULL + VS=NULL`（固定管线）。
3. 外观：`SRCBLEND_ZERO / DSTBLEND_ONE_MINUS_SRC_ALPHA` + `TFACTOR=0x8C000000`
   （恒定 α=0.55，RGB=黑）+ 纹理级 alpha/color 取 TFACTOR + 双面。
   ⇒ **背景压暗 45% 的黑色半透明桨叶**，亮暗背景均可见，不受资产弱 α 影响。
4. 状态"wrapper 设置 + raw 直推"双重保险，绘制后经 wrapper 还原保持缓存一致。
5. 浓淡调节：仅改 `0x8C000000` 一个常数（0x66≈40% 淡 / 0xB3≈70% 浓）。

## 六、方法论教训（已固化记忆）

1. **诊断先于修复**：前两次迭代"按推测修→期望→失败"；引入分级日志+raw 设备读回+
   遮挡查询后才逐层定位。遮挡查询注意 `S_FALSE`（数据未就绪≠零像素）。
2. **pass 判别必须用原始设备状态**：`Get_Transform` 缓存会被污染；COLORWRITE 不能
   作为 pass 身份（D24X8 目标可能仍是 7）；预算/日志必须按 pass 分类防烧穿。
3. **本项目 wrapper 的 `Set_Shader/Set_DX8_Texture_Stage_State` "缓存可靠、设备不可靠"**
   —— 最终防线必须在 draw 前 raw 直推，同时保持 wrapper 缓存一致防 flush 反噬。
4. **PBR 单位上做固定管线特殊绘制必须同时清 VS 和 PS**。
5. **特殊绘制分支必须放在分支链最前**（先于 alpha-override/USER_DATA），
   否则在带玩家色的"主角模型"上永远无效。
6. **视向相关外观差异（上亮下暗）= 排序重放带背面剔除的签名特征**；
   只堵 `Render_Sorted` 分支不够，必须清 SORT 标志（容器/VB 类型在注册时决定）。
7. 违例教训：子代理/技能 fork 会继承写权限——约束必须随 prompt 传递，返回后必查
   `git status` 核账（见记忆 `no-unauthorized-edit-via-subagent`）。
