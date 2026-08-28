# W3X 替换脚本/管线 全面反思与修复清单

> 日期：2026-08-28 ｜ 基于上午中方建筑测试的 10+ 问题, 逐项回溯到替换脚本/管线的根因漏洞。

---

## 一、漏洞总览(按类别)

### A. 模型转换(数据层面)
| # | 漏洞 | 现象 | 根因 |
|---|------|------|------|
| A1 | **骨骼名不规范** | 盖特坦克骨 `bone_turret`(小写)+ 哈希骨 `0xA5A545ED`; 兵营/电厂骨 `BONE_TURRET`(大写)。INI 引用难匹配 | 脚本原样保留源骨骼名, 无规范化/校验 |
| A2 | **网格骨骼绑定错** | 盖特坦克炮管网格绑 `bone_body`/`bone_turret`, 无网格绑 `bone_barrel01` → 炮管不随炮塔转 | 脚本不校验关键网格(炮管/炮塔)的绑定骨 |
| A3 | **alpha 贴图不处理** | 铁丝网(TasPL2_G 均匀灰无alpha)渲染实心; 旋翼(Fx_blades)不透明 | 脚本不检测 alpha 材质, 不保证贴图有 alpha + 网格 AlphaTestEnable |
| A4 | **未知着色器未映射** | 旋翼用 `muzzleflash.fx`(游戏无此shader)→ 走错渲染路径 | 脚本原样保留源 shader, 无映射到游戏可用 shader |
| A5 | **材质约定不统一** | 旋翼用 `Texture_0`(BASIC)但模型走 PBR → 贴图绑不上 | 脚本不归一化 Texture_0 → DiffuseTexture 等约定 |

### B. INI 生成(配置层面)
| # | 漏洞 | 现象 | 根因 |
|---|------|------|------|
| B1 | **建筑→源模型映射缺失** | 核弹基地误用载具 `APATavNukeCn`; 指挥中心漏替换 | 无完整建筑→源映射表 + 校验 |
| B2 | **冒烟粒子未自动加** | 电厂/兵工厂烟囱不冒烟 | 未检测骨架 `FX_SMOKE` 骨自动生成 ParticleSysBone |
| B3 | **碉堡开火偏移未生成** | 驻军从中心开火, 非射击窗口 | 未检测 `FIREPOINT` 骨自动生成 FiringOffset |
| B4 | **门模块手写** | 兵营/机场门状态需手动加 INI | 无自动生成门模块(DOOR_1 状态) |
| B5 | **炮塔骨名不匹配** | 盖特机炮炮塔骨名需手动匹配 | 未从模型骨架提取实际骨名生成 Turret/TurretPitch |
| B6 | **出口点不对齐** | 兵工厂坦克从侧墙出 | 未检测门位置自动算 UnitCreatePoint/NaturalRallyPoint |

### C. 校验(验证层面)
| # | 漏洞 | 现象 | 根因 |
|---|------|------|------|
| C1 | 校验不覆盖骨骼绑定 | 炮管错绑未查出 | check_w3x_conventions.py 只查约定 |
| C2 | 校验不覆盖建筑映射 | 核弹基地模型错未查出 | 无建筑→源映射校验 |
| C3 | 校验不覆盖 INI 状态 | 缺门/冒烟/开火偏移未查出 | 无 INI 完整性校验 |
| C4 | 校验不覆盖 alpha | 铁丝网实心未查出 | 无 alpha 材质校验 |

### D. 管线(流程层面)
| # | 漏洞 | 现象 | 根因 |
|---|------|------|------|
| D1 | 混合模型容器顺序 | 四管/Technical 士兵+车辆模型着色器路由错 | 脚本不保证车辆网格在前(引擎已修, 脚本应兜底) |
| D2 | 士兵网格贴图错配 | BODY03 士兵穿车辆贴图 | 脚本不校验士兵网格贴图约定 |

---

## 二、逐项修复计划

### A. 模型转换修复
- [ ] **A1 骨骼名规范**: w3x_convert 检测哈希骨(`0x`前缀)与大小写不统一, 输出警告; 提供骨名映射配置。
- [ ] **A2 网格绑定校验**: check_w3x_conventions 校验关键网格(含"barrel"/"turret"字样或 INI 引用的骨)的绑定骨; 炮管类网格应绑炮塔/炮管骨。
- [ ] **A3 alpha 贴图处理**: w3x_convert 检测 alpha 材质(AlphaTestEnable 或 alpha 纹理), 自动: 保证贴图 alpha + 网格 AlphaTestEnable + 纹理 RGB 有对比(非均匀灰)。均匀灰 alpha 贴图标记为"需人工生成 alpha 图案"。
- [ ] **A4 shader 映射**: w3x_convert 建立 shader 映射表(`muzzleflash.fx`→rotor 处理, `buildingssoviet.fx`→PBR, 等), 未知 shader 警告。
- [ ] **A5 材质约定归一**: w3x_convert 检测 `Texture_0` 网格, 若模型级走 PBR 则自动转 `DiffuseTexture`(如旋翼)。

### B. INI 生成修复
- [ ] **B1 建筑→源映射表**: 建立 `china_buildings_map.json`(全部中方建筑→源模型+动画+门), 生成 INI 时查表 + 校验。
- [ ] **B2 冒烟粒子自动生成**: 检测骨架 `FX_SMOKE*` 骨 → 自动在 NONE 状态加 `ParticleSysBone = FX_SMOKExx SteamVent`。
- [ ] **B3 碉堡开火偏移自动生成**: 检测 `FIREPOINT*` 骨 → 自动生成 GarrisonContain FiringOffset。
- [ ] **B4 门模块自动生成**: 检测 DOOR 模型 → 自动生成门模块(DOOR_1_OPENING/WAITING_OPEN/CLOSING)。
- [ ] **B5 炮塔骨自动匹配**: 从骨架提取实际骨名 → 生成 Turret/TurretPitch。
- [ ] **B6 出口点自动对齐**: 检测门骨位置 → 自动算 UnitCreatePoint/NaturalRallyPoint。

### C. 校验修复
- [ ] **C1-C4 扩展 check_w3x_conventions**: 加骨骼绑定校验、建筑映射校验、INI 状态完整性校验、alpha 材质校验。

### D. 管线修复
- [ ] **D1 容器自动排序**: w3x_convert 对混合模型(士兵+车辆)自动把车辆网格排前。
- [ ] **D2 士兵贴图约定校验**: 士兵网格(infantry.fx)必须 Texture_0+士兵贴图, 禁车辆贴图。

---

## 三、实施顺序
1. A1-A5(模型转换核心修复)—— 补 w3x_convert.py
2. C1-C4(校验扩展)—— 补 check_w3x_conventions.py
3. B1(建筑映射表)—— 建 china_buildings_map.json
4. B2-B6(INI 自动生成)—— 新增 patch 辅助
5. D1-D2(管线兜底)
