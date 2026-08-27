# W3X 通用替换管线方案与全面回顾 (RA3 → 绝命时刻)

> 日期：2026-08-27 ｜ 范围：美军载具 / 美军兵工厂 / 三个美军士兵 / 中方载具 / GLA 载具 的全面回顾 +
> 通用"载具 / 建筑 / 士兵"替换脚本方案计划（用户批准后从**中方建筑**开始分步实施）

---

## 一、全面回顾：已完成的替换（39 个单位，全部通过约定校验）

模型均用 `W3XModelDraw` 渲染，容器文件位于 `E:\!!!!!!!QWCSB\ART\W3X\`，
INI 位于 `E:\!!!!!!!QWCSB\Data\INI\Object\`。**`check_w3x_conventions.py` 扫描 40 个容器 = 0 错误**。

### 1.1 美军载具（13）— `AmericaVehicle.ini` / `AmericaAir.ini`
`EUTEAAURORA`(曙光) / `EUTEACOMANCHES`(科曼奇) / `EUTEARAPTOR`(猛禽) /
`EUTEASTEALTH`(幽灵) / `EUTEVAVENGER`(复仇者) / `EUTEVCRUSADER`(十字军) /
`EUTEVHUMVEE`(悍马, 内嵌士兵) / `EUTEVMICROWAVE`(微波) / `EUTEVPALADIN`(帕拉丁) /
`EUTEVSENTRY`(岗哨) / `EUTEVTOMAHAWK`(战斧)

### 1.2 美军建筑（1）— `FactionBuilding.ini`
`AmericaWarFactory` → 模型 `EUWARFACTORY_SKN`（+`EUWARFACTORY_DOOR_SKN` 门）

### 1.3 美军士兵（3）— `AmericaInfantry.ini`
`EUTEIROCKETS`(导弹兵) / `EUTEIRIFLES`(游骑兵) / `EUTeiSinper`(尖兵)，骨架分别复用
`AUANTIVEHICLEINFANTRY_SKL` / `JUANTIINFANTRYINFANTRY_SKL` / `GU_SNPRSH_SKL`。

### 1.4 中方载具（9）— `ChinaVehicle.ini` / `ChinaAir.ini`
`APATAAHELIX`(母舰) / `APATAAMIG`(米格) / `APATAVBTMSTRTECH1`(炎黄) /
`APATAVFIREDRAGON`(火龙) / `APATAVGATTTANK`(盖特) / `APATAVLOUTPOST`(防空炮) /
`APATAVNUKECN`(核子加农炮) / `APATAVOVRLRD`(神农) / `APATAVTROOPC`(运兵车)

### 1.5 GLA 载具（12）— `GLAVehicle.ini`
`GLATGBROCKET`(火箭车) / `GLATGVBUS`(巴士) / `GLATGVDIABLO`(魔鬼) /
`GLATGVMARAUDER`(掠夺者) / `GLATGVNUKETRUCK`(核卡车) / `GLATGVQUADCANN`(四管, 内嵌士兵) /
`GLATGVRADARVAN`(雷达车) / `GLATGVSCORPION`(天蝎) / `GLATGVSCUDCANN`/`GLATGVSCUDCANN2`(飞毛腿) /
`GLATGVTECHNICAL`(皮卡, 内嵌士兵) / `GLATGVTOXINTANK`(毒素卡车)

### 1.6 待替换（下一步）
- **中方建筑（15）**：CommandCenter / Airfield / NuclearMissileLauncher / SpeakerTower /
  PowerPlant / SupplyCenter / Barracks / WarFactory / Wall / WallHub / Moat / Bunker /
  PropagandaCenter / GattlingCannon / InternetCenter（现仍用旧 `W3DModelDraw`）
- GLA 建筑、美军其他建筑（CommandCenter/PowerPlant/StrategyCenter/Airfield 等）——后续

---

## 二、根因修复（本次四管贴图问题的真正根因）

### 2.1 症状回顾
四管载具的士兵穿车辆迷彩（BODY03 士兵网格材质 = 车辆贴图 `TgvQuadCann2`）。

### 2.2 真正根因（两层）
1. **替换脚本的纹理映射漏洞**：把士兵网格 BODY03 按名字/惯性错配成车辆贴图，且
   士兵网格（`infantry.fx`/BASIC）与车辆网格（`objects*.fx`/PBR）**两种着色器约定混在一个模型**。
2. **引擎"一模型只选一个着色器"的机制**：模型级着色器由"容器第一个非默认网格"决定，
   导致混合约定模型必有一侧错——四管（士兵在前→PBR→士兵错）、Technical（士兵在前→步兵→车身错）。

### 2.3 修复（三层，已全部完成）
| 层 | 改动 | 状态 |
|----|------|------|
| 引擎-渲染兜底 | `W3XModelDraw.cpp` createRenderObject 加 **per-submesh 步兵 override**：`infantry.fx` 子网格 → `w3x_infantry.fx` | ✅ 已提交 `bf341beb` |
| 引擎-路由健壮 | `loadW3XModel` 模型级着色器改为**"只要有任何 PBR 网格就选 `w3x_soviet.fx`，只有全 BASIC 才选 `w3x_infantry.fx`"**（从全部网格判定，不再看首位） | ✅ 本次 |
| 数据 | 四管 BODY03 材质 → `Texture_0 = TgiRifleS`；容器 BODY01(车辆) 置首 | ✅ 游戏根 |
| 校验 | `check_w3x_conventions.py` 通用约定校验脚本 | ✅ 本次 |

**效果**：四管士兵贴图正确；Technical 卡车（士兵首位、模型级曾走步兵）被引擎改动**自动修复**（模型级回 PBR、车辆贴图正常），无需改数据；未来任何混合模型都按"每网格各自着色器"渲染。

---

## 三、通用替换管线方案（载具 / 建筑 / 士兵）

> 目标：一个可复用的半自动管线，输入 RA3 源素材，输出合格的绝命时刻 W3X 模型 + INI，
> 并由校验脚本兜底。**核心原则：每个网格的"角色 → 着色器 → 贴图约定"必须一致。**

### 3.1 网格约定总表（一切的核心）
| 网格角色 | 判定依据 | FXShader | 贴图约定 | 渲染着色器 |
|----------|----------|----------|----------|------------|
| 士兵 | 蒙皮到士兵骨骼（HIPS/SPINE/HEAD/四肢）| `infantry.fx` | `Texture_0`=士兵贴图（alpha=阵营色）| `w3x_infantry.fx`（引擎 per-submesh override）|
| 载具/建筑 | 蒙皮到车辆/建筑骨骼（BONE_*）| `objectsjapan.fx`/`objectsgeneric.fx` | `DiffuseTexture`/`NormalMap`/`SpecMap`=车辆贴图 | `w3x_soviet.fx`（PBR）|
| 履带 | — | `objectsalliedtread.fx` | PBR + 滚动 UV | `w3x_tread.fx`（引擎 per-submesh override）|

**铁律**：
1. 士兵网格**必须** `Texture_0`，**禁止**出现 `DiffuseTexture`（否则走 PBR 泄漏）。
2. 载具/建筑网格**必须** PBR 三件套，**禁止** `Texture_0`。
3. 同一网格不可同时存在 `Texture_0` 与 `DiffuseTexture`。
4. 混合模型（内嵌士兵的载具）**容器首个网格必须是车辆网格**——模型级着色器= PBR，
   车辆贴图才正确；士兵由引擎 per-submesh override 走步兵着色器。
5. 士兵贴图与车辆贴图**不得相同**（四管 BODY03 教训）。

### 3.2 管线步骤（每一类通用）
```
1. 素材定位      D:\ra3红警3原版建筑载具士兵飞机完整素材 / D:\遗忘发来的红警3将军2里的资源
2. W3X 转换      RA3 .w3x/模型 → ART/W3X 容器+SKL+网格XML+贴图DDS+材质XML
3. 骨骼对齐      骨架复用或按父链递归；bind 旋转非单位时合成必须 R(bindQuat)·animTrans
4. 贴图+着色器    按 §3.1 总表逐网格赋值；士兵 Texture_0/士兵贴图，车辆 PBR/车辆贴图
5. 容器排序      混合模型车辆网格在前
6. 动画映射      W3DAnimation → INI ModelConditionState（NONE/MOVING/FIRING_A/BETWEEN…）
                帧0归一化 + UseWeaponTiming + FrameForPristineBonePositions
7. INI patch     Object → W3XModelDraw + ConditionState + Turret/WeaponFXBone
8. 校验          python Tools/check_w3x_conventions.py ART/W3X <模型>  → 0 错误
9. 构建          （纯数据替换无需编译；引擎改动才需重编译 GameEngineDevice→RTS.exe）
```

### 3.3 校验脚本用法
```bash
python GeneralsMD/Code/Tools/check_w3x_conventions.py "E:/!!!!!!!QWCSB/ART/W3X" [模型名...]
# 不写模型名则扫描全部 *_SKN.w3x；0 错误退出码 0，有错误退出码 1
```
检查项：士兵网格约定 / 车辆网格约定 / 约定不混用 / 士兵不穿车辆贴图 / 模型级着色器路由 / 贴图文件存在。

---

## 四、分步实施计划（用户批准后，从中方建筑开始）

### 第 1 步：中方建筑摸底
- 列出 15 个中方建筑的 Object 名、当前 `W3DModelDraw` 的 `DefaultModelName`（旧模型名）。
- 从源素材定位对应 RA3 中方建筑模型/贴图（如 `ChinaWarFactory` ↔ RA3 `CHMWARFACTORY` 等）。

### 第 2 步：首个建筑试点（建议 `ChinaWarFactory` 中方兵工厂）
- 按 §3.2 管线转换一个建筑 → 生成容器/SKL/网格/贴图/材质 XML。
- 建筑网格全部 PBR（`objects*.fx` + `DiffuseTexture` 三件套），无士兵。
- INI：`Draw = W3XModelDraw` + ConditionState（NONE/REALLYDAMAGED）+ 门动画（如有）。
- 跑 §3.3 校验 → 0 错误。
- 进游戏目视确认：模型 / 贴图 / 开门动画 / 阴影 / 出口点。

### 第 3 步：其余中方建筑逐个替换
- 按批次（生产建筑→防御→指挥/其他），每个完成后跑校验 + 目视确认。
- 含玩家颜色（team color）、损坏状态（REALLYDAMAGED）、粒子/灯（FX bones）适配。

### 第 4 步：扩展
- GLA 建筑 → 美军其他建筑 → 其他阵营单位（按 §3.1 约定复用管线）。

---

## 五、工程产物（本次新增）
| 文件 | 说明 |
|------|------|
| `GameEngineDevice/.../W3XModelDraw.cpp` | 引擎：per-submesh 步兵 override + 模型级"有PBR就PBR" |
| `GameEngineDevice/.../w3x_loader.cpp` / `W3DFileSystem.cpp` | 引擎：Art/W3X 子目录递归搜索（支持按源分类子目录组织资源）|
| `GeneralsMD/Code/Tools/check_w3x_conventions.py` | 通用替换管线校验脚本 |
| `GeneralsMD/Code/Tools/w3x_convert.py` | RA3 源模型 → 游戏 W3X 转换器（拆分容器/网格/SKL/动画 + 贴图收集，输出到分类子目录）|
| `docs/w3x-quadcannon-soldier-texture-fix.md` | 四管修复专项记录 |
| 本文件 | 全面回顾 + 通用方案 + 分步计划 |

## 六、中方建筑试点状态（ChinaWarFactory → APAWarFactory）
**已完成并验证通过**（2026-08-27）：
- 转换工具 `w3x_convert.py` 落地，产物输出到 `E:\!!!!!!!QWCSB\ART\W3X\AP\`（按源分类 AP 子目录）。
- 模型：`APAWARFACTORY_SKN`（8 主网格 + SKL + IDLE + DOOR(模型+动画一体) + BLD/CON）。
- INI：`ChinaWarFactory` 主块 → `W3XModelDraw`（DefaultModelName + NONE/REALLYDAMAGED + IDLE 循环动画）；新增门 `ModuleTag_09`（DOOR_1_OPENING/WAITING_OPEN/CLOSING）；旧 W3D 子块全部禁用。
- 校验 0 错误（PBR 路由）。
- **用户验证通过**：模型/贴图/开门动画/风扇(ANIM01骨)正常。两问题已修（INI 运行时读取，无需重编译）：
  1. 烟囱冒烟：NONE 状态加 `ParticleSysBone = FX_SMOKE01/02/03 SteamVent`。
  2. 出口方向：门在世界 X=57（Y 中心 7.5），`UnitCreatePoint/NaturalRallyPoint` 改为 (54,7.5)/(57,7.5)。

## 六之二、中方其余建筑批量转换（8 个，2026-08-27）
**已完成**：转换工具输出到 `Art/W3X/AP/` + INI patch（`FactionBuilding.ini.bak_china_buildings_batch_20260827` 备份）。

| 建筑 | 模型 | 动画/门 |
|------|------|---------|
| ChinaAirfield | APAAIRFIELD_SKN | IDLE + 4 门(DOOR_01-04_SKN, DOOR_1 同时开) |
| ChinaBarracks | APABARRACKS_SKN | IDLE + 门(APABARRACKS_DOOR, X=29/Y=-25 已对齐出口) |
| ChinaPowerPlant | APAPOWERPLANT_SKN | IDLA(涡轮) |
| ChinaBunker | APABUNKER_SKN | 无 |
| ChinaWall | APAWALL_SKN | 无(容器+骨架同id合并文件) |
| ChinaWallHub | APAWALLHUB_SKN | 无(同上) |
| ChinaGattlingCannon | APABASEDEFENSEGATLIN_SKN | IDLE + Turret=BONE_TURRET + PREATTACK/FIRING/BETWEEN(ATA LOOP) |
| ChinaNuclearMissileLauncher | APATAVNUKECN_SKN | IDLA + Turret=BONE_TURRET + PREATTACK(PREA)/FIRING(ATKA) |

校验：13 模型 0 错误（4 个 FX2ndPassA 特效贴图警告为既有灯网格问题）。
**待用户目视验证**：8 个建筑逐个进游戏确认模型/贴图/动画/门/出口。
