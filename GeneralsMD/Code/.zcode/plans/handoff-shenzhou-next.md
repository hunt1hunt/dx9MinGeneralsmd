# 交接：日冕神州模型替换（下一个对话窗口从这里开始）

> 日期 2026-09-30 起。上一窗口做了 27 个对象的替换 + 2 项引擎改动。
> 本文是**唯一权威的进度与规则索引**；逐项映射细节另见 `corona-shenzhou-mapping-draft.md`。

---

## 一、当前进度

### 已换成神州 W3X：**27 个对象**（改的是 `D:\zerohour\Data\INI\Object\NukeGeneral.ini`）

| 类别 | 数量 | 对象 → 模型 |
|---|---|---|
| 建筑 | 12 | CommandCenter→CELESTIALMCV、NuclearMissileLauncher→**CELESTIALENERGYRAILGUNBASE_SKN**、PowerPlant→CBPOWERPLANT、Barracks→CBBARRACKS、WarFactory→CELESTIALWARFACTORY、Airfield→CBAIRFIELD、SupplyCenter→CBREFINERY、InternetCenter/PropagandaCenter→CBTECHSTRUCTURE、SpeakerTower→CBLASERTOWER、GattlingCannon→CELESTIALENERGYGATLINGTOWER、Bunker→CBBATTERY |
| 载具 | 10 | TankOverlord→CUANTIVEHICLEARMORTECH4_SKN、TankDragon→CUMBT99B、TankGattling→CUANTIVEHICLEINFANTRY、TankECM→CUANTIVEHICLEVEHICLETECH3、InfernoCannon→CUANTISTRUCTUREVEHICLE、NukeLauncher→CULONGRANGEMISSILEVEHICLE_B、TroopCrawler→CELESTIALWAVERIDERIFV_SKN、SupplyTruck→CUMINER、ListeningOutpost→CUOUTPOST、TankBattleMaster→CUANTIVEHICLEVEHICLETECH1 |
| 飞机 | 3 | JetMIG→CUFIGHTERAIRCRAFT、JetCargoPlane→CUSUPPORTAIRCRAFT、VehicleHelix→CUANTIAIRSHIP |
| 步兵 | 2 | InfantryRedguard→CUINFILTRATIONINFANTRYB_SKN、InfantryTankHunter→CUANTIINFANTRYINFANTRY |

### 仍为 W3D：**13 个（有意保留）**

- **6 个挂载子对象**：`HelixGattlingCannon`/`HelixPropagandaTower`/`HelixBattleBunker`、
  `TankOverlordGattlingCannon`/`TankOverlordPropagandaTower`/`TankOverlordBattleBunker`
  （都是 `W3DDependencyModelDraw`）。**用户裁定"随父车重定位"——尚未做，是剩余工作的主体。**
- **3 个用户裁定不换**：`InfantryBlackLotus`、`InfantryHacker`、`VehicleDozer`（神州无功能对位）
- **4 个弹药/特效**：`BattleMasterTankShell`、`OverlordTankShell`、`ChinaCarpetBomb`、`RadiationFieldSmall`

---

## 二、未完成事项（按优先级）

### 1. 6 个挂载子对象的重定位（用户已裁定要做）
父车已换模型，挂件的挂载骨对不上。做法：在新父模型上找挂载点骨骼，把 6 个挂件的
`Draw` 改绑过去。Helix 父车是 `CUANTIAIRSHIP`（有 `door_01`/`turret`/`gun`/`fx_weapon`），
Overlord 父车是 `CUANTIVEHICLEARMORTECH4_SKN`。

### 2. 炎黄机甲（任务 #16，用户裁定暂停）
`CUANTIVEVECHICLEARMORTECH4_SKN` 是**上下半身分离绑定**：
`ARMORDOWN` 用骨 1~20（腿）、`ARMORTOP` 用骨 22~71（上半身），
`DOWN_*` 动画只驱动前者、`UP_*` 只驱动后者。**RA3 同时播放两条动画层，SAGE 只能有一条。**
所以任何单一动画都会让另一半停在绑定姿态（现象：身体倾斜、只有右臂完整、装甲板飘着）。
- ⚠ 摘掉 `ARMORDOWN` 会**腿全没**——腿在其中，不要再试这条
- 出路：① 找同时覆盖上下身的合并动画（`DIEA` 覆盖 47 骨，最多）
  ② 把两套烘焙成一条 ③ 改引擎支持双动画层
- 容器已还原（5 个子件齐全）；工具 `_w3x_drop_subobject.py` 可摘子件（带备份/还原）

### 3. 脚本路径硬编码（任务 #7）
`us_vehicle_draw_convert.py` / `gla_vehicle_draw_convert.py` / `air_draw_convert.py` /
`us_building_extras.py` / `gla_building_extras.py` / `w3x_gen_ini.py` /
`check_w3x_conventions.py` 都硬编码 `E:\!!!!!!!QWCSB`。
另建议给桌面 `①增量构建Release.bat` 补 `apply_laa.py` 步骤（现在缺）。
`Tools/w3x_convert_corona.py` 无硬编码，无需改。

### 4. 转换器缺陷（任务 #14）
`w3x_convert_corona.py` 的 `anim_suffix_ok` 要求动画后缀**不含下划线**，
所以 `MODEL_DOWN_ATKA` 这类复合标签会被静默丢弃（实测 ARMORTECH4 的 53 个动画只转了 2 个）。
绕过：显式 `--anims X,Y,Z`。

---

## 三、🚨 八条硬教训（每条都付出过代价，务必遵守）

1. **`Turret=` / `TurretPitch=` 只能写给"本来就有 `AIUpdateInterface`"的对象。**
   炮塔角度由 AI 提供（`W3XModelDraw::handleClientTurretPositioning()` 开头 `if (!ai) return;`）。
   给原本没 AI 的对象**新增** `AIUpdateInterface` 会在**放置瞬间崩溃**（轨道炮已复现）。
2. **`ConditionState` 的条件名必须对照引擎的表**（`GameEngine/Source/Common/BitFlags.cpp`
   里 `"PARACHUTING"` 附近，共 **125** 个）。生造名字直接让 INI 解析失败、**启动即崩**
   （实测 `MOVING FAST_MOVING` —— SAGE 没有 `FAST_MOVING`）。
   注意 `TransitionState` 用的是**过渡键名**（`TRANS_*` 自定义标识符），不是模型条件。
3. **替换 Draw 块时块边界不能靠缩进判断。** SAGE 的 `End` 关闭最近的开块，与缩进无关。
   实测 Helix 的 `Draw` 是 4 空格缩进、`ConditionState` 也是 4 空格 → 按缩进找 End 会切半截
   → 旧内容残留 + **多一个 `End`** → 启动崩。
   **每次批量替换后必须复扫：该对象里是否还有旧 W3D 模型名（`NV*`/`NB*`/`NI*`）**
   —— 这是唯一能可靠发现"半截替换"的检查。
4. **动画要量「绝对姿态」而非「运动幅度」。** 轨道炮的 `IDLB`/`IDLA` 就是靠 pitch 骨的
   绝对旋转（-90° vs 绑定 0°）才分出"展开/折叠"；只看幅度会得出错误结论。
5. **避开空动画**（0 条通道）：`CBBARRACKS_IDLA`、`CELESTIALENERGYGATTLINGTOWER_IDLE`、
   `CELESTIALMCV_IDLA`、`CUANTISTRUCTUREVEHICLE_IDLA` 都是空的。
6. **超武建筑永远不进 `PREATTACK_A`/`FIRING_A`**（`SpecialPowerModule` 不设模型条件），
   必须挂 `DOOR_1_*`；且 `MissileLauncherBuildingUpdate` 的"就绪捷径"跳过
   `DOOR_1_OPENING`，展开动画要挂在 `DOOR_1_WAITING_OPEN` 上。
7. **换主模型后要整块禁用遗留 W3D 子绘制**（施工围栏/脚手架/吊车/灯），否则叠在新模型上。
   连旗帜（`ModuleTag_OfficersClub`）也一并禁用（先例如此）。
8. **"动画数"别按文件名数**——日冕素材两种分隔符混用（`M.SUB` 用点、`MCV_SUB` 用下划线），
   会数进网格。必须扫 `<W3DAnimation>` 元素或用转换器 `--list`。

---

## 四、工程环境

### 构建 → LAA → 部署（改引擎后必须三步走完）
```bash
cd E:/Source/repos/MinGeneralsfreebuild2ok/GeneralsMD/Code
bash build.sh Release -inc                      # 增量构建
# ⚠ 判据不是退出码！看 GeneralsMD/Build/msdev_output.log 里的 "RTS.exe - N error(s)"
python Tools/apply_laa.py ".../GeneralsMD/Run/RTS.exe"   # VC6 的 /LARGEADDRESSAWARE 不生效，每次必须重打
cp .../GeneralsMD/Run/RTS.exe D:/zerohour/RTS.exe        # ⚠ 脚本默认拷 E:\!!!!!!!QWCSB，要手动拷 D 盘
```
- PE `Characteristics` 应为 `0x012F`，`Machine` 必须仍是 `0x014C`（误写成 AMD64 会 WinError 193）
- **VC6 的 for 循环变量会泄漏到外层作用域** —— 同作用域两个循环同名会 `error C2374`

### 素材源
- **`D:\rimian`**（20550 文件，真数据）
- ⚠ `D:\日冕模型贴图和蒙皮动画和XML和shader文件\` 是**坏解压**（15311 个 145 字节空桩），勿用
- 神州三套前缀：`CU*`(单位) / `CB*`(建筑) / `CELESTIAL*`(基地·大型)
- 容器 `<模型>_CTR.w3x`，**内含 id 不含 `_CTR`**；转换后须改名成 `<容器id>.w3x`

### 磁盘上的工具（`_*.py` 被 `.gitignore` 排除，未入库，但可用）
| 工具 | 用途 |
|---|---|
| `Tools/w3x_convert_corona.py` | 日冕素材转换（按 id 落盘 + 贴图收集 + 哈希骨名回填），**已入库** |
| `Tools/big_extract.py` | BIGF 归档 list/extract，**已入库** |
| `_building_batch_run.py` | 建筑批处理（换主模型 + 禁用旧子绘制） |
| `_vehicle_batch_run.py` / `_aircraft_batch_run.py` | 载具/飞机批处理 |
| `_infantry_tankhunter.py` | 反坦克步兵 |
| `_w3x_drop_subobject.py` | 从 W3X 容器摘子件（带备份/还原） |
| `_toggle_turret_ai.py` | 炮塔 AI 开关（`--off/--on/--status`） |
| `_overlord_pose_test.py` | 机甲姿态二分（`--restore` 可回滚） |

---

## 五、引擎改动（本窗口的核心代码成果）

| 文件 | 改动 |
|---|---|
| `W3XRenderObj.cpp` | **A**：`BindW3XBones` 栈越界修复——原来只填 64 根骨却上传 `boneCount*8`，>64 骨的模型会读栈垃圾；现在填满整个数组、只上传拥有的部分，超出槽位填单位变换 |
| `W3XRenderObj.cpp/.h` | **B**：骨骼上限拆成 `kMaxBones=64`（单次上传，对齐着色器 `MaxSkinningBones`）与 `kMaxRigBones=128`（整骨架）；合成/控制/动画数组与栈暂存全部放大到 128；两个 enum 移到 public |
| `W3XRenderObj.h/.cpp` | **B**：`SubMesh` 加 `boneRemap`（紧凑槽→全局骨索引）+ `SetSubMeshBoneRemap` |
| `W3XModelDraw.cpp/.h` | **B**：加载期为超限网格建立紧凑映射并**重写顶点骨索引**（软/硬蒙皮两条路径）；`SubMeshBuffer` 加 `boneRemap` 并传递到渲染对象；绘制期对带 `boneRemap` 的网格 gather 出紧凑骨表再上传 |

**设计要点**：只有网格骨索引**真的超过 64** 才启用，`boneRemap` 留空即走原路 →
**所有 ≤64 骨的模型行为逐字节不变**，回归风险压到最低。

---

## 六、备份链（全部在 `D:\zerohour\Data\INI\Object\`）

```
NukeGeneral.ini.bak_jetfix        ← FAST_MOVING 修复前
NukeGeneral.ini.bak_helixleftover ← Helix 残留删除前
NukeGeneral.ini.bak_helixfix      ← Helix 第一次（没修对的）
NukeGeneral.ini.bak_tankhunter    ← 反坦克步兵前
NukeGeneral.ini.bak_aircraft      ← 飞机前
NukeGeneral.ini.bak_vehicles      ← 载具前
NukeGeneral.ini.bak_buildings     ← 建筑前
NukeGeneral.ini.bak_toggle / .bak_batch / .bak_turretai
NukeGeneral.ini.bak_railgun / .bak_fix2 / .bak_corona_pilot
D:\zerohour\RTS.exe.bak_before_bonefix   ← A 之前的引擎
```

---

## 七、下一个窗口的第一件事

1. **确认当前版本能正常启动**（上一窗口最后修的是 `FAST_MOVING` 非法条件名，
   用户还没回报结果；若再崩，`ReleaseCrashInfo.txt` 现在会带行号+对象名，定位很快）
2. 按用户优先级选：**6 个挂载子对象** / 机甲 / 脚本参数化
3. 每批替换后**必跑那两道检查**：条件名对照 125 表 + 扫旧 W3D 模型残留
