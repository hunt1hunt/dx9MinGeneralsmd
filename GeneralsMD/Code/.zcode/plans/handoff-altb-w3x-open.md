# 交接：ALT+B 免费建造仍扣费 + W3X 贴图阴影仍丢失（两案未闭环）

> 2026-09-19 傍晚交接。上一窗口（VF-2 雾+悬崖+ALT+B 马拉松）尾部状态混乱，用户裁定换窗。
> 本文档 = 下窗开局唯一必读。配合宫殿胶囊（见文末）。

## 一、当前部署态（事实，勿重查）

| 项 | 状态 |
|---|---|
| 代码 HEAD | `a051907d`（已推送）：三修合一 = MetaEvent 摘 DEMO 双绑定 + CommandXlat 500ms 去抖 + Money::withdraw 判定复原 + 地形 c15 清除/衰减无条件化 |
| D: 游戏目录 | **`D:\!!!!!!!QWCSB\!!!!!!!QWCSB\`（用户钦定游戏目录）**，exe=15:40 新版(6758479B, LAA✔, cmp✔) |
| E: 游目录 | `E:\!!!!!!!QWCSB\`，exe=15:54 同版已部署（旧版备份 `RTS.EXE.bak_20260919_155420`） |
| INI | D:/E: 已同步（体积雾面板+地形凹凸面板+乱码修复版），E: 备份链齐全 |
| 其他 | D 盘另有 6 个休眠旧安装（Program Files (x86)/绝命时刻繁体中文硬盘版/*），全部 0 活动勿碰 |
| 铁律 | EXE+资源双目录部署（E: 同名先时间戳备份）+ LAA——已入 SKILL.md |

## 二、两案现状（用户最终报告：两个都未修复）

### 案1：ALT+B 首按仍扣费
**已修三层**（均有自测证据）：①Money::withdraw 判定反（47d9db16 引入，已复原原始语义）；②MetaEvent.cpp:785 DEMO 老路与 :862 release 路双绑 ALT+B（已摘 DEMO）；③500ms 去抖。
**我方实测**（D: 和 E: 双实例）：ALT+B 单次执行、`ALTB enable=1 player[2/4] freeNow=1` 稳定不再自反转。
**缺口**：从未做过端到端建建筑验证！诊断日志里用户的 withdraw 一条都没出现过（只见 AI idx=0 free=0）。
**下窗第一步**：跑游戏→ALT+B→**亲手建一座建筑**（UI 自动化或请用户配合一次）→读 `E:\freebuild_diag.log` 的 WITHDRAW 行：`idx=? free=?` 一行定案——
- idx=2/4 且 free=1 还扣钱 → 扣款另有通路（查生产渐进扣款/其他 withdraw 调用点）
- idx=别的号 free=0 → 用户的玩家号没拿到 flag（PLAYER_HUMAN 过滤问题，本图 human=2/4，确认用户是几号）
- 没有 WITHDRAW 行 → 扣款根本不走这个函数（全库再排查 m_money 直改点）

### 案2：W3X 贴图阴影仍丢失
**已知因果**：悬崖修复 e36b79dd 的 `register(c15)` 触发 X4500 → terrain_pbr_nm(ps_2_a) 编译失败 → 连锁砸 W3X 接收（用户 A/B 实锤：前版 7dfa4c67 有阴影）。
**已修**：c15 全清、衰减无条件化；**编译已恢复**（pbr_compile.log:17231 `terrain_pbr_nm (ps_2_a) hr=0`）。
**矛盾**：用户在 a051907d 版仍报阴影没了。两种可能：①用户那次实测落在 15:47-15:53 窗口（当时可能玩的是被我 rm 掉证据前的实例/或 E: 旧 exe 时段）；②W3X 阴影丢失另有第二根因（c15 编译失败只是其一）。
**下窗第二步**：当前版跑图截图 W3X 单位，与保存的参照图 `E:\vf2_w3xA.png`（7dfa4c67 前版实拍）对比；若确丢，读 W3XRenderObj 接收链（:1320-1360 已知入口）+ gate 条件逐项核（isShadowMapAvailable/isShadowMapFresh/receiveShadow）。

## 三、关键证据文件（下窗直接用，勿删）

- `E:\freebuild_diag.log`：ALTB 行（每次按键）+ WITHDRAW 行（前10次扣款，含 idx/free/playerType）——**本案唯一实时真相源；禁止 rm，只能追加读**
- `E:\vf2_w3xA.png`：前版（有阴影）W3X 实拍参照
- 游戏目录 `pbr_compile.log`：17231 行=当前版地形编译 OK；16830/16977 的 X4500=历史（已修）
- `E:\GeneralsMD_DeferredRT.log`：VF-2/阴影链日志

## 四、教训（本窗口踩的，已入宫）

1. **诊断日志禁止清空**——15:53 我 rm 了 freebuild_diag.log，恰好可能销毁了用户 15:47-15:53 测试的按键证据，导致后续全部推理失去锚点
2. 诊断行**必须带 GetTickCount 时间戳**（下窗补上）
3. SendInput 游戏按键前必须 SetForegroundWindow（失焦=按键黑洞，曾误判为回归）
4. "用户测到修复失败" 先查**他运行的是哪个 exe**（本日两次翻车：E: 旧 exe 疑云 + 多安装目录）——现在定案 D:\!!!!!!!QWCSB\!!!!!!!QWCSB\，但测试前仍建议 wmic 验进程路径

## 五、下窗开局动作

1. 触发 min-generals 技能 → 宫殿（本胶囊）→ 本文档
2. 跑 D: 游戏 → ALT+B → 建筑端到端测试 → 读 WITHDRAW 行定案扣费通路
3. W3X 截图 vs vf2_w3xA.png 定案阴影
4. 修复 → 铁律部署（D:+E:+LAA+备份）→ 用户验收
