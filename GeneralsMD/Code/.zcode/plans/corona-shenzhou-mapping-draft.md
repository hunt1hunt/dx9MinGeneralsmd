# 日冕·神州 → 绝命时刻核武将军 单位映射表（草案 v1）

> 状态：**待用户审定**。日期 2026-09-30。作者：Claude（会话 Sess-Corona-Pilot）
> 依据：`D:\rimian` 素材实测 + `D:\zerohour` 现有 INI 实测 + 已通行「将军2」素材作对照基准

---

## 一、素材命名规范（已实测确认）

| 项 | 规范 | 备注 |
|---|---|---|
| 神州前缀（三套并用） | `CU` = 单位、`CB` = 建筑、**`CELESTIAL*` = 基地/大型建筑** | 见下表 |
| 容器 | `<模型>_CTR.w3x`，**内含 `id` 不含 `_CTR`** | 引擎按文件内 id 找文件 → 转换后须改名成 `<容器id>.w3x` |
| 骨架 | 多数**内嵌**在同一容器文件内（`<W3DHierarchy>`） | 部分带独立 `_SKN_CTR` / `_HRC` / `_BONE` |
| 尺寸基准 | 已通行的「将军2」素材 | `D:\zerohour\ART\W3X\AP\`、根目录 |

### 神州素材分布（实测文件数）

| 前缀 | 文件数 | 内容 | 是否神州 |
|---|---|---|---|
| `CELESTIAL*` | 3418 | 基地车、战车工厂、轨道炮、龙船、无人机、港口道具 | ✅ |
| `CU*` | 2239 | 单位（步兵/载具/飞机/舰船） | ✅ |
| `CB*` | 1334 | 建筑（兵营/电厂/机场/超武/围墙…） | ✅ |
| `CO*` | 1029 | `CONTAINER_STACK` / `CORMAP` —— **港口集装箱道具** | ❌ 非单位 |
| `SU*` `AU*` `JU*`/`JA*`/`JAP*` | — | 苏/盟/日阵营 | ❌ 其它阵营 |

> **教训1**：第一版草案只扫了 `CU*`/`CB*`，漏掉 `CELESTIAL*` 这个更大的命名空间，
> 导致指挥中心误判为「无候选」。**全面替换前必须三套前缀一起扫。**
>
> **教训2（2026-09-30 更正）**：本文档所有「动画数」列**不可靠**。
> 计数脚本用的是 `glob("NAME_*.w3x")`，但日冕素材**两种分隔符混用**：
> 网格 `RAILGUNBASE_SKN.SUB.w3x`（点）vs `CELESTIALMCV_SUB.w3x`（下划线）。
> 后者会被误当成动画数进去。实例：`CELESTIALMCV` 实为 **4** 个动画而非 64。
> **正确做法**：只认 `<W3DAnimation id=...>` 元素，不要按文件名猜。
> 已知受影响的：`CELESTIALMCV`(64→4)、`CBNAVALYARD`(146)、`CBAIRFIELD`(115)、
> `CELESTIALWARFACTORY`(112)、`CBTECHSTRUCTURE`(73) 等所有下划线命名的模型。
> **动手前必须用转换器的 `--list` 复核真实动画数。**

---

## 二、尺度合格线（这是第一道硬约束）

上次 CUDF41 栽在尺寸上，所以先立基准。**已通行」将军2」素材的尺寸 = 本项目已验证可接受的尺度**。

### 载具车体（单网格 `SKIN_BODY` 对比）

| | 尺寸 (X,Y,Z) |
|---|---|
| 已通行 将军2 车体 `APATAVBTMSTRTECH1_SKN.SKIN_BODY01` | 49.6 × 31.1 × 10.4 |
| 新素材 神州 `CUANTIVEHICLEVEHICLETECH1.SKIN_BODY` | 50.1 × 30.3 × 10.7 |
| 已作废 `CUDF41`（223.1 × 45.9 × 75.3） | **约 3 倍 → 不可用** |

**结论：日冕与将军2 同为 RA3 素材，车体尺度一致，不需要缩放。**

### 建筑（全网格并集对比）

| 功能 | 将军2（已通行） | 神州候选 | 倍率 | 判定 |
|---|---|---|---|---|
| 超级武器 | `APASUPERWEAPONADVANCED` 89.1×89.3×111.2 | `CBSUPERWEAPONADVANCED` 105.8×111.3×121.7 | 1.19/1.25/1.09 | ✅ 已试点成功 |
| 电厂 | `APAPOWERPLANT_BLD` 77.6×90.0×65.6 | `CBPOWERPLANT` 70.4×151.6×117.4 | 0.91/1.68/1.79 | ⚠ 偏高 |
| 兵营 | `APABARRACKS` 90.0×114.5×60.1 | `CBBARRACKS` 93.5×81.6×89.9 | 1.04/0.71/1.50 | ⚠ 偏高 |
| 机场 | `APAAIRFIELD_BLD` 120.6×180.5×71.2 | `CBAIRFIELD` 172.3×229.1×119.1 | 1.43/1.27/1.67 | ⚠ 偏高 |
| 碉堡 | `APABUNKER` 59.9×83.0×30.6 | `CBBATTERY` 52.4×52.7×44.3 | 0.87/0.63/1.45 | ~ |
| 指挥中心 | `APACONSTRUCTIONYARD` 120.0×125.6×93.1 | **`CELESTIALMCV`** 147.3×121.1×104.8 | **1.23/0.96/1.13** | ✅ 好（64 动画） |
| 战车工厂 | `APAWARFACTORY` 147.6×89.9×71.0 | **`CELESTIALWARFACTORY`** 128.2×127.4×84.9 | 0.87/1.42/1.20 | ✅ 好（112 动画） |
| 加特林炮 | — | **`CELESTIALENERGYGATLINGTOWER`** 61.9×46.6×52.0 | — | ✅ 好（32 动画） |
| 超武（备选） | `APASUPERWEAPONADVANCED` 89.1×89.3×111.2 | `CELESTIALENERGYRAILGUNBASE` 128.8×116.0×127.0 | 1.45/1.30/1.14 | ✅ 好（157 动画） |

> `CBCORE` 实测仅 26.2×25.4×15.4，其子件是 `B_01..06`/`PAN_01..06`/`ROLL_01..02`/`YANG`/`YING`
> —— 是个**阴阳能量核心装饰件**，不是指挥中心。已排除。

> **注**：上表「倍率 >1.5」不等于不可用——神州建筑普遍比将军2 大一圈但同量级，真正不可用的是 CUDF41 那种 3 倍级。
> **用户已裁定：1.5× 级别建筑（电厂 1.7×、机场 1.7×、兵营 1.5×）接受。**

---

## 三、映射表草案

### 建筑（11 待换）

| # | ZH 对象 | 现 W3D 模型 | 神州候选 | 尺寸 | 契合度 | 备注 |
|---|---|---|---|---|---|---|
| B1 | `Nuke_ChinaCommandCenter` | `NBConYardN` | **`CELESTIALMCV`**（或 `_PLANTFORM`） | 147×121×105 | ✅ 好 | **用户裁定用神州基地中心**；实测只有 4 个动画（BLD/IDLA/TRANS/TRANS_2）——TRANS 是"展开成基地"。待定：用移动态 MCV 还是展开态 `CELESTIALMCV_PLANTFORM`(177×107×98) |
| B2 | `Nuke_ChinaPowerPlant` | `NBPwrPtI` | `CBPOWERPLANT` | 70×152×117 | 高 1.7×（已接受） | 另有 `CBPOWERPLANTADVANCED`(83×124×138) |
| B3 | `Nuke_ChinaBarracks` | `NBBarracks` | `CBBARRACKS` | 94×82×90 | 好 1.5×（已接受） | 直接对应 |
| B4 | `Nuke_ChinaWarFactory` | `NBWarFact` | **`CELESTIALWARFACTORY`** | 128×127×85 | ✅ 好 | 112 动画，比 `CBWARFACTORY` 更完整 |
| B5 | `Nuke_ChinaAirfield` | `NBAirfield` | `CBAIRFIELD` | 172×229×119 | 中 | 机场最大，1.4-1.7× |
| B6 | `Nuke_ChinaSupplyCenter` | `NBSupCent` | `CBREFINERY` | 124×102×127 | 好 | 补给=精炼 |
| B7 | `Nuke_ChinaInternetCenter` | `NBINTCNT` | `CBTECHSTRUCTURE` | 109×91×135 | 好 | 科技建筑 |
| B8 | `Nuke_ChinaPropagandaCenter` | `NBPCenter` | **`CBTECHSTRUCTURE`** | 109×91×135 | ✅ 好 | 73 动画；科技建筑对位"超武前置" |
| B9 | `Nuke_ChinaSpeakerTower` | `NBPTower` | `CBLASERTOWER` | 31×31×28 | 中 | 功能相远（扩音→激光） |
| B10 | `Nuke_ChinaGattlingCannon` | `NBGattling` | **`CELESTIALENERGYGATLINGTOWER`** | 62×47×52 | ✅ 好 | 32 动画；备选 `CBBASEDEFENSEADVANCED`(58×58×71) / `...TOWERB`(43×43×36) |
| B11 | `Nuke_ChinaBunker` | `NBBunker` | **`CBBATTERY`** | 52×53×44 | ✅ 好 | 33 动画；防御炮台对位碉堡 |
| B12 | `Nuke_ChinaNuclearMissileLauncher` | 🔄 **改轨道炮** | **`CELESTIALENERGYRAILGUNBASE_SKN`** | 129×116×127 | ✅ 好 | **用户裁定改轨道炮**，替换试点的 `CBSUPERWEAPONADVANCED`；规格见 §七 |

### V10 推土机的缺位问题（必须单独裁定）

神州**没有工程车**——因为它的基地是 `CELESTIALMCV` **展开**而成，不需要推土机建造。
实测所有疑似候选：

| 候选 | 尺寸 | 动画 | 为什么不行 |
|---|---|---|---|
| `CUENGINEER` | 7.6×13.4×25.5 | 24 | **这是工兵（士兵），不是车** |
| `CUENGINEERDRONE` | 7.7×5.0×9.9 | 3 | 无人机，太小 |
| `CUENGINEERWATERWHEELS` | 5.4×2.1×5.1 | 4 | 是部件 |
| `CUWHEELEDASSAULTVEHICLE` | 46×20×18 | **2** | 尺寸最贴合，但只有 2 个动画（几乎静止），且是突击车 |
| `CUMINER` | 97×61×33 | 56 | 矿车（后勤车），偏大，但职能最接近"非战斗工程车" |

**建议二选一**：① 用 `CUMINER`（后勤车，56 动画，至少能动）；② 像黑客/黑莲花一样**不换保留原版**。
**这条需要你定**——硬套一个突击车会看着别扭。

> ✅ **用户已裁定（2026-09-30）：保留不换，原版 W3D 不动。**

### 载具（10 待换）

| # | ZH 对象 | 现 W3D 模型 | 神州候选 | 尺寸 | 契合度 | 备注 |
|---|---|---|---|---|---|---|
| V1 | `Nuke_ChinaTankBattleMaster` | ✅ **已完成** | `CUANTIVEHICLEVEHICLETECH1` | 70×30×18 | — | 试点（开火点待修） |
| V2 | `Nuke_ChinaTankOverlord` | `NVOvrlrd` | `CUANTIVEVEHICLEARMORTECH4_SKN` | 待测 | 好 | 超重型，有 `_UP`/`_DOWN`/`_SWORD01` 形态 |
| V3 | `Nuke_ChinaTankDragon` | `NVDragon` | `CUMBT99B`（74×29×30） | | 中 | 99式主战坦克 |
| V4 | `Nuke_ChinaTankGattling` | `NVGattTank` | `CUANTIVEVEHICLEINFANTRY`（51×30×45） | | 好 | 反步兵载具，40 个动画 |
| V5 | `Nuke_ChinaTankECM` | `NVBANSHEE` | **`CUANTIVEVEHICLEVEHICLETECH3`** | 68×38×20 | ✅ 好 | 8 动画；反载具载具对位 ECM 的反载具职能 |
| V6 | `Nuke_ChinaVehicleInfernoCannon` | `NVInferno` | **`CUANTISTRUCTUREVEHICLE`** | 89×34×24 | ✅ 好 | 反建筑载具 = 攻城炮职能；备选 `CUAVVT4UP`(101×51×64，有 UP/DOWN 展开态) |
| V7 | `Nuke_ChinaVehicleNukeLauncher` | `NVNukeCn` | `CULONGRANGEMISSILEVEHICLE_B` | 54×30×31 | ✅ 好 | 远程导弹车，21 动画 |
| V8 | `Nuke_ChinaVehicleTroopCrawler` | `NVTCrawler` | **`CELESTIALWAVERIDERIFV`** | 72×36×42 | ✅ 好 | **IFV = 步兵战车**，职能完全对位；20 动画 |
| V9 | `Nuke_ChinaVehicleSupplyTruck` | `NVSSUPPLYTK` | `CUMINER` | 97×61×33 | 中 | 矿车=后勤车；56 动画；偏大 |
| V10 | `Nuke_ChinaVehicleDozer` | `NVCONSTDOZ_A` | ⚠ **见下注** | — | 缺位 | **神州没有工程车** |
| V11 | `Nuke_ChinaVehicleListeningOutpost` | `NVLOUTPOST` | `CUOUTPOST`（55×29×28） | | 好 | 名字直接对应 |

### 飞机（3 待换）

| # | ZH 对象 | 现 W3D 模型 | 神州候选 | 尺寸 | 契合度 |
|---|---|---|---|---|---|
| A1 | `Nuke_ChinaJetMIG` | `NVMIGN` | `CUFIGHTERAIRCRAFT`（79×25×12） | | 好 |
| A2 | `Nuke_ChinaJetCargoPlane` | `NVCargoPln` | `CUSUPPORTAIRCRAFT`（57×38×20） | | 好 |
| A3 | `Nuke_ChinaVehicleHelix` | `NVHELIX` | **`CUANTIAIRSHIP`** | 69×32×21 | 中 | 43 动画；**神州无直升机**，重型飞艇最接近"重型运输/炮艇"；备选 `CUADVANCEAIRCRAFTTECH4`(64×39×20，**83 动画**) |

### 步兵（3 待换）

| # | ZH 对象 | 现 W3D 模型 | 神州候选 | 尺寸 | 契合度 | 备注 |
|---|---|---|---|---|---|---|
| I1 | `Nuke_ChinaInfantryRedguard` | ✅ **已完成** | `CUINFILTRATIONINFANTRYB_SKN` | 高 26.7 | — | 试点 |
| I2 | `Nuke_ChinaInfantryTankHunter` | `NIMSST_SKN` | `CUANTIINFANTRYVEHICLE_B`（54×30×27） | | 好 | 反载具步兵 |
| I3 | `Nuke_ChinaInfantryHacker` | `NIHCKR_SKN` | ⛔ **不换** | — | — | **用户裁定**：神州无功能对位，保留原版 W3D |
| I4 | `Nuke_ChinaInfantryBlackLotus` | `NIHERO_SKN` | ⛔ **不换** | — | — | **用户裁定**：英雄单位无对位，保留原版 W3D |

### 系统/弹药/子对象（10，**暂不替换**）

`Nuke_BattleMasterTankShell`、`Nuke_OverlordTankShell`、`Nuke_ChinaCarpetBomb`、`Nuke_RadiationFieldSmall`，
以及 6 个挂载子对象（`Nuke_ChinaHelixGattlingCannon` / `HelixPropagandaTower` / `HelixBattleBunker` /
`TankOverlordBattleBunker` / `TankOverlordGattlingCannon` / `TankOverlordPropagandaTower`）。

> 子对象是挂在父模型上的**独立模型**（如 Overlord 炮塔挂件）。父车换了模型后，挂件位置会对不上。
> **用户已裁定：这 6 个随父车一起重新定位。**
>
> 这意味着 `Nuke_ChinaTankOverlord` / `Nuke_ChinaVehicleHelix` 替换时，
> 必须同时在**新模型上找到挂载点骨骼**（`WeaponFireFXBone` / `ParticleSysBone` / 挂载骨），
> 并把 6 个挂件的 `Draw` 位置改到新骨骼上——**不是换完父车就完事**。
> 这是映射表里工作量最大的一块，单独立项。

---

## 四、必须逐对象复核的项（不可用统一脚本一把梭）

1. **炮塔骨骼名**：日冕命名不统一。试点坦克是 `turret`（小写真实 pivot），
   而将军2 体系用 `bone_turret` / `Turret01`。**每个载具都要实地 dump pivot 名**。
2. **开火点骨骼名**：试点是 `fx_weapon`；将军2 用 `fx01` / `b_weapona_fx` / `bone_weapona01`。同样要实地核。
3. **哈希骨骼名**：日冕 pivot 大量是 `0x2493AF2B` 这类哈希名。
   `Turret=` / `WeaponFireFXBone=` 只按骨骼名解析 → **必须用容器 `SubObjectID`+`BoneIndex` 回填**（试点已验证此法可行）。
4. **动画清单**：每个模型要列出实际有哪些动画，才能决定挂哪些 ConditionState。
   试点坦克只有 `_TRANS`（无 IDLA），所以完全没有动画。
   **建议映射表定稿后，逐个模型产出「动画清单 + 骨骼清单」再动手。**
5. **§ 与 § 的尺寸复核**：本表的尺寸是用「容器内全网格并集」量的，
   与载具基线的「单 `SKIN_BODY`」口径不同 → **>1.5 倍的项要用同口径复测**再定论。

---

## 五、疑问卡点清单

### 已裁定

| # | 问题 | 裁定 |
|---|---|---|
| 1 | 指挥中心用什么 | ✅ **用神州基地中心 `CELESTIALMCV`** |
| 3 | 6 个挂载子对象 | ✅ **随父车重定位**（见 §三末，需单独立项） |
| 5 | 尺寸 >1.5× 的建筑 | ✅ **接受**（电厂 1.7×、机场 1.7×、兵营 1.5×） |

### 仍待裁定

1. **B8 宣传中心、B11 碉堡、V5/V6/V8/V10、A3 这些「待定/中」的项**要不要换？
   功能对不上的换上去可能比不换更别扭。
2. **I3 黑客 / I4 黑莲花**是英雄与特殊单位，神州素材里没有功能对位的。
   要不要干脆**不换**（保留原版 W3D）？混搭会不会更难看？
3. **B12 超武是否改用 `CELESTIALENERGYRAILGUNBASE`**（157 动画，但 1.45×）？
   试点用的 `CBSUPERWEAPONADVANCED` 已跑通且尺寸更贴合（1.19×）。
   **建议保持现状**，除非你想要轨道炮的观感。

---

## 六、试点已确认正确的机制（后续可直接复用）

1. 容器→骨架→骨骼解析链（内嵌 `W3DHierarchy` 或独立 `_SKN_CTR`/`_HRC`）
2. 哈希骨骼名回填（`SubObjectID` + `BoneIndex`，索引不变、蒙皮/动画零影响）
3. `FilenameList` 是 `std::set`（忽略大小写）→ loose 文件**整文件替换**，必须带完整原文
4. 建筑超武的开门动画必须挂 `DOOR_1_*`（由 `MissileLauncherBuildingUpdate` 驱动），
   **不能**挂 `PREATTACK_A`/`FIRING_A`——发射井没有普通武器
5. 建筑遗留的 W3D 子绘制（围栏/脚手架/吊车/灯）**必须整块禁用**，否则叠在新模型上
6. `W3XModelDraw` **不支持** `Flags` / `OkToChangeModelColor` / `IgnoreConditionStates`
   （字段表实测）；构造期抬升 `ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT` 只在 `W3DModelDraw` 有

---

## 七、超武改轨道炮 —— 替换规格（待实施）

用户裁定把 `Nuke_ChinaNuclearMissileLauncher` 从试点的 `CBSUPERWEAPONADVANCED` 换成
**`CELESTIALENERGYRAILGUNBASE`**。**这等于重做建筑试点。**

### 为什么轨道炮比发射井好

| | 发射井 `CBSUPERWEAPONADVANCED` | 轨道炮 `CELESTIALENERGYRAILGUNBASE` |
|---|---|---|
| 骨骼 | 22 根，几乎全哈希 | **59 根，多数可读** |
| 炮塔 | ❌ 无 | ✅ `turret` (pivot 5) |
| 俯仰 | ❌ 无 | ✅ `pitch` (22) + `pitch_l` (23) |
| 炮管 | ❌ 无 | ✅ `barrel` (26) |
| 开火点 | `fx_weapon` (pivot 3) | ✅ **`fx_fire` (54，barrel 子骨 X=+40 = 炮口)** |
| 动画 | 只有 IDLA/IDLB/RDY，且 IDLB 是全静止 | **IDLA/IDLB/PREATTACK/FIRE/RELOAD/TRNS/BLD.01-03** |

### 实测的动画用途（按关键骨骼运动幅度判定，非猜测）

| 动画 | 帧 | `pitch` | `barrel` | hash37/48 | 判读 |
|---|---|---|---|---|---|
| `IDLA` | 21 | — | — | **0.0** | 静止定格（**不能当待机循环**） |
| `IDLB` | 11 | 0.0 | 0.0 | 0.0 | 动的是电容/装载臂（43 根骨） |
| `PREATTACK` | 41 | — | — | **24.7** | 充能（可视） |
| `FIRE` | 11 | — | — | **24.7** | **真开火动画** |
| `RELOAD` | 156 | 4.8 | — | 0.7 | 小幅装填 |
| **`TRNS`** | 161 | **90.0°** | **54.2°** | 24.7/50.0 | **炮管展开瞄准（巨幅）** |

> ⚠️ **注意**：`IDLA` 和发射井的 `_IDLB` 一样是**静止定格**，直接用来当 `NONE` 的 LOOP 会没有呼吸感。
> 建议 `NONE` 用 `IDLB` LOOP（动电容），或接受静止。

### 骨骼映射

```
Turret          = turret      ; pivot 5
TurretPitch     = pitch       ; pivot 22
WeaponFireFXBone  = PRIMARY fx_fire   ; pivot 54 = 炮口
WeaponMuzzleFlash = PRIMARY fx_fire
WeaponLaunchBone  = PRIMARY fx_fire
```

### 资产清单（需转换）

| 文件 | 作用 |
|---|---|
| `CELESTIALENERGYRAILGUNBASE_SKN_CTR.w3x` | 主容器（id=`CELESTIALENERGYRAILGUNBASE_SKN`，Hierarchy=`..._SKL`） |
| `CELESTIALENERGYRAILGUNBASE_SKL.w3x` | 骨架（59 pivots） |
| `CELESTIALENERGYRAILGUNBASE_SKN.*.w3x` | 55 个子网格（TURRET/BARREL/BASE/CAPACITOR_*/RAIL_01..04/…） |
| `CELESTIALENERGYRAILGUNBASE_{IDLA,IDLB,PREATTACK,FIRE,RELOAD,TRNS}.w3x` | 6 个动画 |
| `CELESTIALENERGYRAILGUNBASE_{,NRM,SPM}.dds` + `.xml` | 贴图 |

### 遗留疑问

1. **`fx_fire` 是否是唯一炮口？** barrel 链上还有 pivot 27/38 等哈希骨。若特效位置不对，备选是 `barrel`(26) 或报 `0x97C10ADD`(37)。
2. **`MissileLauncherBuildingUpdate` 还要不要留？** 它驱动 `DOOR_1_*`，而轨道炮**没有门**。
   选项：留着（无害，DOOR_1_* 无匹配则回退默认态）／改成普通炮塔建筑（但会失去超武发射流程）。
   **建议先留着**，只换 Draw。
3. **`TRNS` 要不要挂到构造态？** 161 帧的展开动作很适合做建造动画，
   但 W3XModelDraw 不支持 `ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT`（只放动画、不抬升）。
