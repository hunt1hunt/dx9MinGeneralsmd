# 交接：各将军阵营模型替换（2026-10-02 收工）

> 目标游戏安装目录：**`D:\zerohour`**（`E:\!!!!!!!QWCSB`、`D:\!!!!!!!QWCSB\!!!!!!!QWCSB` 是**旧镜像**，已确认不是当前目标）。
> 所有改动均为**游戏侧数据**（`D:\zerohour\Data\INI\*`、`D:\zerohour\ART\W3X\BA\*`），
> **不涉及 C++ 重编**。仓库里提交的是**工具脚本**。

## 一、今天做完的阵营（6 个）

| 将军 | INI | 来源 | 载具数 | 翻页 |
|---|---|---|---|---|
| 步兵将军 | `Infa_` | 红警3MOD **Battle Archive 中方** | 13 | 2 页 |
| 激光将军 | `Lazr_` | Battle Archive 中方 + 日冕/原版盟军建筑 | 13 | 2 页 |
| 坦克将军 | `Tank_` | BattleField **三家混用** | 21 | 3 页 |
| 空军将军 | `AirF_` | BattleField **盟军 AB/AU** | 21 | 3 页 |
| 化学将军 | `Chem_` | BattleField **升日 JB/JU** | 21 | 3 页 |
| 隐匿将军 | `Slth_` | **日冕苏军 SU/SB** + BattleField 苏军补缺 | 21 | 3 页 |

（`TankGeneral.ini` / `AirforceGeneral.ini` / `ChemicalGeneral.ini` / `StealthGeneral.ini` 原本**只存在于 INI.big**，已提取成散装。）

## 二、工具链（`GeneralsMD/Code/Tools/`）

| 脚本 | 作用 |
|---|---|
| `big_extract.py` | EA BIG 归档读取/提取（`list/cat/get/getall`）。格式抄自引擎 `Win32BIGFileSystem`：`@0 "BIGF"`、`@4 u32 大小(小端)`、`@8 u32 文件数(大端)`、`@0x10` 起 `偏移(大端)+大小(大端)+NUL结尾路径` |
| `ba_asset_import.py` | **模型导入适配器**：已拆格式(`_CTR`/`_HRC` 或 `_SKN`/`_SKL`) → 游戏 `ART\W3X\<dst-sub>\`；自动处理骨架撞名、着色器名转小写、剔除 `*FILL*` 子网格 |
| `<faction>_batch_convert.py` | 逐阵营的 Draw 批量改写（配置表驱动） |
| `<faction>_warfactory_3page.py` | 兵工厂多页翻页 + 新载具克隆 |

## 三、★改 INI 的铁律（今天被咬过多次）

1. **`End` 与 `END` 大小写混用** → 找块尾必须 `line.strip().lower() == "end"`。
2. **切割块要按语法配平**（`ConditionState`/`DefaultConditionState`/`TransitionState` 是真块；
   **`AliasConditionState` 是单行**，不是块）。切完**断言块尾那行是 end**。
3. **CRLF 文件用 `newline=""` 读入后行尾是 `\r`** → 任何"整行锚定"的正则
   （`^Object X$`、字符串 `"...\n"` 匹配）**必须先容忍 `\r`**。今天为此踩了 3 次。
4. **空动画(0 通道)绝不能挂** —— 会让 Skin 网格塌陷。挂之前数 `<Channel*>` 个数。
5. **批量删子网格前先打印完整清单人工过目** —— 曾把 `JUEGG` 的 12 个实体件当特效删了。
6. **改完必查**：① 顶层键 diff 备份（有没有丢内容）② 遍历活跃容器的子网格查贴图是否存在
   ③ 命令集每槽 → 按钮 → 对象/升级 全链引用。

## 四、★素材判据（这些都踩过）

- **"紫色/品红" = 贴图缺失**。导入脚本**只从 `--src` 一个目录找贴图**；从 A 源取模型时，
  它可能引用只在 B 源里的贴图（**大小写还常不同**）→ 必须补拷。
- **"不透明长方形面" = 特效面片**（`defaultw3d.fx`/`lightning.fx` + FX 贴图：护盾/喷嘴辉光/激光/
  尾焰/灯光）。W3X 管线不做特效混合 → 整片被不透明画出。判据要**叠加名字/贴图**（含
  FX|GLOW|SHIELD|FLARE|LASER|PLASMA|EXHAUST|TRAIL|HALO|BLADE|ROTOR），并**排除**
  `EGG|PLANE|UPGRADE|NEWSKIN`（这些常是正常实体件）。
- **日冕素材残缺**：① 盟军建筑缺机场/部分 ② 苏军建筑**只有**防空塔/高科技/海军/特斯拉墙，
  没有兵营/兵工厂/电厂/矿厂/指挥中心 ③ 苏军步兵骨架基本没有 ④ `ABCONYARD`/`ABWARFACTORY` 的
  骨架文件缺失。遇缺就从 **BattleField** 同阵营补。

## 五、下个窗口可以做的

- **其余将军**：`DemoGeneral`(GLA 爆破)、`BossGeneral`、`SuperWeaponGeneral`(超武)、`GC_Slth_*`(隐匿 GLA 单位包)…
  流程与今天完全一致：先 `big_extract.py get` 出 INI → 看对象 → 选素材 → `*_batch_convert.py` 改写 → 多页翻页 → 三项校验。
- **建筑缺口补素材**：如需日冕占更大比重，请把日冕的苏军/盟军兵营·兵工厂·电厂等 `_SKL`+网格补齐。
- **引擎侧可选优化**：`W3XModelDraw.cpp` 的 `W3XShaderVariant()` 用的是**大小写敏感**的 `strstr(..., "buildings")`；
  改成大小写无关可从根上免疫"着色器名大小写"问题（当前靠导入时统一转小写规避）。
