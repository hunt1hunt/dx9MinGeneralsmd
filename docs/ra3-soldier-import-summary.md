# RA3 士兵导入绝命时刻 — 替换工作总结与经验教训

> 日期：2026-08-25 ｜ 范围：美军三兵种（导弹兵 / 游骑兵 / 尖兵）模型·骨骼·蒙皮·动画·动作的完整替换
> 本文重点记录 **昨天到今天的分析故障与处理经验**，供后续步兵（及类似 RA3 模型）导入参考。

---

## 1. 最终配置总览

| 单位 | 模型 (W3X) | 骨架 (SKL) | 动画家族 | 武器蒙皮骨 | 炮口/发射骨 |
|---|---|---|---|---|---|
| **导弹兵** `AmericaInfantryMissileDefender` | `EUTEIROCKETS_SKN` | `AUANTIVEHICLEINFANTRY_SKL` | AU (BIDA/RUNA/ATKZ/ATKY/AIDA…) | props1 (骨20) | fx_laser (骨21) |
| **游骑兵** `AmericaInfantryRanger` | `EUTEIRIFLES_SKN` | `JUANTIINFANTRYINFANTRY_SKL` | JUANTI (BIDA/RUNA/ATKZ/ATRA…) | ENERGYRIFLE (骨19) | fx01 (骨21) |
| **尖兵** `AmericaInfantryPathfinder` | `EUTeiSinper_SKN` | `GU_SNPRSH_SKL` | GU (BIDA/CRNA/ATKA…) | PROPS0 (骨20) | b_weapona_fx (骨21) |

**注意**：三者的 `Hierarchy=` 声明直接指向 RA3 原版骨架（`AUANTIVEHICLEINFANTRY_SKL` / `JUANTIINFANTRYINFANTRY_SKL` / `GU_SNPRSH_SKL`），骨骼索引天然对齐 —— 这就是"模型没改骨骼"的原因，骨架是共享的。

### 导弹兵动作映射（含开火时序，已验正常）
```
NONE=BIDA(待机) / MOVING=RUNA / MOVING FIRING_A=ATRA(边动边射)
PREATTACK_A=ATKZ(MANUAL,前置瞄准) / FIRING_A=ATKZ(ONCE, FrameForPristineBonePositions=4, UseWeaponTiming)
BETWEEN_FIRING_SHOTS_A=ATKZ(MANUAL)   ← 持续攻击持瞄准
PREATTACK_B=AIDA / FIRING_B=ATKY(激光制导)
USER_1=UPGD(武器切换) / DYING=DTBA / FREEFALL=MCNA / PARACHUTING=MCNC
STUNNED=LNDA / STUNNED_FLAILING=FLYA / FRONT/BACKCRUSHED=CDTA/CDTB
OVER_WATER=WADA / MOVING OVER_WATER=SWMB
```

### 游骑兵动作映射（2026-08-25 补 BETWEEN 状态后）
```
NONE=JUANTI BIDA(待机) / MOVING=JUANTI RUNA
FIRING_A=JUANTI ATKZ(ONCE) / BETWEEN_FIRING_SHOTS_A=ATKZ(MANUAL)   ← 本次补
FIRING_B=JUANTI ATRA(ONCE) / BETWEEN_FIRING_SHOTS_B=ATRA(MANUAL)   ← 本次补
DYING=DTBA / DYING EXPLODED_FLAILING=DTFA / DYING EXPLODED_BOUNCING=DTPA
FREEFALL=FLYA / PARACHUTING=SFLYA
```

### 尖兵动作映射（2026-08-25 修正 BETWEEN 后）
```
NONE=GU BIDA / MOVING=GU CRNA(匍匐)
FIRING_A=GU ATKA(ONCE) / BETWEEN_FIRING_SHOTS_A=GU ATKA(MANUAL)   ← 原为 BIDA(待机)，本次改
DYING=DIEA / DYING EXPLODED_FLAILING=DIEA / DYING EXPLODED_BOUNCING=DIEB
FREEFALL=FLYA / PARACHUTING=FLYA
```

---

## 2. 引擎关键机制（W3X 数据语义）

1. **W3X = RA3 XML 格式**：`W3DHierarchy`(Pivot 骨骼) + `W3DAnimation`(Channel 关键帧) + `W3DMesh`(Skin 蒙皮)。引擎用 pugixml 解析（`w3x_loader.cpp`）。
2. **骨骼是相对父节点的**：动画位移/旋转都是**相对父骨骼**的，世界位姿必须沿父链递归合成（父先于子，`parentIndex < i`）。
3. **通道约定（关键）**：RA3 导出 channel = **bind⁻¹ × animLocal**，所以还原姿势必须 `compose = bind × channel`。
   - `localQuat  = bindQuat × animQuat`
   - `localTrans = bindTrans + R(bindQuat) · animTrans`  ← **anim 位移增量必须乘 bind 旋转**
   - `world = parentWorld × local`（父链递归）
4. **骨骼序号即通道索引**：引擎按 pivot 序号应用动画通道（不是按名字），所以**只有同骨架的动画能播**（`Hierarchy` 声明绑定）。
5. **开火时序**：引擎 `adjustModelConditionForWeaponStatus` 只在开枪那一帧设 `WSF_FIRING → FIRING_A`（**天生 1 帧**），持久的攻击状态是 **`BETWEEN_FIRING_SHOTS_A`**。所以持续射击必须配 `BETWEEN=开火动画(MANUAL)` 才能保持瞄准姿势。

---

## 3. 主要故障与经验教训（昨天 → 今天）

### 故障 A：武器离手（核心，2026-08-25 解决）
- **现象**：游骑兵武器在右手外 ~17 单位漂浮；导弹兵正常。
- **排查链**：模型蒙皮正确（SKIN_WEAPON_A 100% 蒙到 ENERGYRIFLE 骨19）→ 动画有骨19通道 → 骨架共享索引对齐 → **锁定引擎合成公式**。
- **根因**：`W3XRenderObj.cpp` 局部平移 `localTrans = bindTrans + animTrans` 漏了 `R(bindQuat)`。ENERGYRIFLE 骨 bind 旋转 = **Z 轴 90°**（非单位），通道位移 (+11.97, +1.27, +1.25) 应旋成 (-1.27, +11.97, +1.25) 落手边，没旋就沿 +X 飞出 17 单位。
- **为什么导弹兵没事**：AU props1 骨 bind 旋转 = **单位四元数**，`R(bind)=I` → `bind+anim` 天然正确。**单位 bind 旋转的骨不受此 bug 影响**。
- **为什么身体不散架**：body 骨（hip/肩）也非单位 bind，但**通道位移很小**，缺失旋转的误差被吸收；只有武器骨通道位移大(+11.97)+90° 旋转才暴露。
- **修复**：三处合成（composeControlledBones controlled/animated 分支 + Get_Bone_Transform_Model_Anim）加 `R(bindLocalQuat)·animTrans`。
- **影响面验证**：40+ 骨架全量扫描非单位 bind 旋转；载具/建筑全部单位 → 零影响；导弹兵武器骨单位 → 零影响；导弹兵身体（hips 118°）校正到 RA3 姿势（脚更接地，典型 <1 单位）。
- **数值验证**：武器距手 17→3.5 单位；用已知正常的导弹兵做校准对照。

### 故障 B：开火 1 帧后弹回待机（2026-08-25 解决）
- **现象**：游骑兵/尖兵开火后立即恢复待机姿势。
- **根因**：`FIRING_A` 天生 1 帧，持久状态是 `BETWEEN_FIRING_SHOTS_A`；游骑兵**没定义**该状态（回落 NONE 待机），尖兵定义成了 **BIDA(待机)**。
- **修复（INI 数据，无需编译）**：游骑兵补 `BETWEEN_FIRING_SHOTS_A/B = ATKZ/ATRA (MANUAL)`；尖兵 `BETWEEN = ATKA (MANUAL)`。MANUAL 模式把动画停在瞄准帧。
- **教训**：**新单位必须连带定义全部武器时序状态**（PREATTACK / FIRING / BETWEEN / RELOADING），否则状态机切回默认。

### 故障 C：AU 动画是坏导出（2026-08-20，历史）
- AUAntiVehicleInfantry `SBIDA/SBIDB/SATEA/SATKZ` 腿通道恒定折叠、四个动画腿全同、"跑"只有筒动。数据 100% 忠实对比后确认**数据本身坏**，不是引擎。
- **解法**：用同骨架族的 JUANTI 真实动画 remap（`w3x_anim_merge.py`，骨索引 +1 平移）。

### 故障 D：帧0归一化归零武器通道（2026-08-19，历史）
- `updateAnimation` 的帧0归一化（trans: current−frame0；quat: frame0⁻¹×current）把**恒定武器通道归零** → 武器停在 bind。
- **解法**：去掉帧0归一化，用原始插值 + 父链递归。

### 故障 E：合成约定之争（2026-08-21，历史）
- 试遍所有约定（inc/abs/bind×anim/共轭/swapXY），AU ATKZ hips 绕 X 滚 54° —— **数据本身编码了侧倾**，不是约定问题。
- OpenSAGE 行约定（`offset × bind`）对 AU/JUANTI 身体下沉/埋地，**列约定（bind × channel）才是 RA3 W3X 的正确约定**，且匹配游戏内日志。

### 故障 F：软绑定未蒙皮（2026-08-19，历史）
- RA3 步兵网格带**双套顶点/法线/骨骼影响**（软绑定）；dgVoodoo 误读软声明 → 未蒙皮。硬回退（block0）成人形/行走/发射全正常。

### 故障 G：开火点错误（2026-08-21，历史）
- `getProjectileLaunchOffset` 取当前状态（待机）姿势 → 导弹从错误位置发射。解法：用开火状态动画的姿势解析发射骨。

---

## 4. 方法论经验（可直接复用）

1. **数据 100% 忠实**：先逐字节对比游戏数据与权威源（`D:\ra3步兵` / 遗忘文件），确认数据真伪再怀疑引擎。
2. **数值实证仲裁**：对每个假设写脚本算数（骨架 bind + 动画通道 → 世界位姿），用数字判定，不靠猜。
3. **校准对照**：先用已知正确的对象（导弹兵 props1）验证分析脚本/公式，再相信它对异常对象（游骑兵 ENERGYRIFLE）的结论。
4. **影响面扫描**：改引擎公式前，全量扫描所有骨架的非单位 bind 旋转，量化受影响对象。
5. **连带全状态**：修一个状态必须连带修移动/开火/间隙，否则状态切换后一帧正常然后还原。
6. **单骨蒙皮检查**：先查武器网格的 `BoneInfluences`（哪个骨、权重），再查动画对那个骨有无通道。
7. **工具留档**：分析脚本（`_*.py`，gitignored）保留在 `GeneralsMD/Code/`，可复算。

---

## 5. 关键资源位置

- 权威 XML（作者遗忘提供）：`C:\Users\hjzhhzc\Downloads\`（SovietAntiInfantryInfantry.xml / AlliedAntiVehicleInfantry.xml 等）
- RA3 步兵数据：`D:\ra3步兵\`（EUTEIRIFLES / AUANTIVEHICLEINFANTRY / JUANTIINFANTRYINFANTRY / GU_SNPRSH）
- 原始素材：`D:\遗忘发来的红警3将军2里的资源`、`D:\ra3红警3原版建筑载具士兵飞机完整素材`
- 本机游戏根目录：`E:\!!!!!!!QWCSB`（数据在 `ART\W3X`，主单位 INI 在 `Data\INI\Object\AmericaInfantry.ini`）
- 引擎代码：`W3XRenderObj.cpp`（合成公式）、`W3XModelDraw.cpp`（loadHierarchy/updateAnimation）、`w3x_loader.cpp`（解析）

## 6. 工具清单（GeneralsMD/Code/，均 `_*.py` 不入库）

| 工具 | 用途 |
|---|---|
| `_ranger_weapon_analyze.py` | 复现引擎合成公式，算骨架+动画世界位姿 |
| `_ranger_calib_au.py` | 用导弹兵(AU)校准公式正确性 |
| `_ranger_test_fix.py` | 验证修复方向（OPT1: bind旋转 anim delta） |
| `_skl_bind_scan.py` | 全量扫描所有骨架非单位 bind 旋转（影响面） |
| `_au_body_delta.py` | 量化修复对导弹兵身体的位移 |
| `_gu_fire_state.py` | 分析 GU 尖兵 ATKA/AIDA 瞄准姿势 |
| `_bind_feet_check.py` | 坐标系校准（bind 脚底 Z） |
| `w3x_anim_merge.py` | JUANTI 动画 remap 到 AU 骨架（历史） |

## 7. 后续待办

- [ ] 游骑兵/尖兵开火持续状态游戏内验证（重启游戏后）
- [ ] 闪雷(B)/狙击开火点 fx01 / b_weapona_fx 位置确认
- [ ] 若导弹兵身体校正幅度不满意，评估 hips 通道局部处理
- [ ] 其他阵营步兵（盟军标枪/Peacekeeper、GLA 征召兵）如需导入，套用同一骨架映射规则
