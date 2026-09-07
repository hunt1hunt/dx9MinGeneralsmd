---
name: causal-debug
description: 因果链深度调试方法论 — 6 阶段系统化故障排查协议。当用户遇到复杂 bug、崩溃、渲染异常、不工作等问题需要系统化排查时自动触发。基于 MinGenerals D3D8→D3D9 水渲染倒影修复实战经验提炼。
context: fork
allowed-tools: Read,Bash,Write,Edit,Grep,Glob,WebSearch,WebFetch
argument-hint: "[症状描述]"
---

# 因果链深度调试方法论

> **Causal Chain Deep Debugging Protocol**
>
> 本技能总结了一套经过实战验证的复杂问题系统化排查方法。核心思想：**不靠猜测盲目修改，而是通过因果链逆向追踪 + 诊断仪表 + 逐项排除，让证据引导解决方案。**
>
> 方法论源于 MinGeneralsmd 项目的 D3D8→D3D9 迁移过程中 Water Type 2 倒影渲染故障排查实战。

---

## 触发条件

当用户遇到以下情况时自动激活本技能：
- **复杂 bug / 崩溃**：游戏崩溃、渲染异常、不工作、不显示
- **调试请求**：包含"调试"、"排查"、"找 bug"、"debug"、"troubleshoot"、"fix"
- **非确定性故障**：时好时坏、依赖特定条件、只在特定硬件出现
- **回归问题**：之前能用、现在不能用（特别关注近期变更）

---

## 核心理念

```
忙改瞎练 → 效率归零
因果链分析 + 诊断仪表 + 逐项排除 → 根因必现
```

**三个不能违反的原则：**

| # | 原则 | 说明 |
|---|------|------|
| 1 | **不猜测** | 每次修改前必须有诊断证据支持，拒绝"试一下会不会好" |
| 2 | **一次只改一个变量** | 同时改多个东西等于没改——你不知道哪个起了作用 |
| 3 | **每个阶段可验证** | 编译通过 ≠ 修复成功。每个阶段必须有独立的验证标准 |

---

## 6 阶段协议

### 阶段 0 — 问题精确定义（Problem Articulation）

**目标：** 将模糊的"出问题了"转化为精确的、可验证的技术描述。

**操作：**
1. 写出 **症状（Symptom）** — 观察到的实际行为
2. 写出 **期望（Expectation）** — 应该发生的行为
3. 写出 **验收标准（Acceptance Criteria）** — 修复完成时满足什么条件
4. 记录 **环境信息** — 操作系统、硬件、运行时上下文
5. 记录 **复现步骤** — 稳定复现还是概率性出现

**实战案例（水渲染倒影）：**
```
症状: Water Type 2 水面一片漆黑，无倒影
期望: 水面显示天空/云层/地形的倒影
验收标准: 进入含 Type 2 水的地图后，水面正确显示反射内容
环境: Windows 11 + D3D9On12，全屏模式
复现: 100% 复现，所有含 Type 2 水的地图
```

---

### 阶段 1 — 因果链逆向追踪（Causal Chain Analysis）

**目标：** 从症状出发，逆向追溯完整调用链，找到"第一个出错的决策点"。

**操作：**
1. **阅读代码**：找到症状出现处的代码（渲染/处理/输出）
2. **逆向追踪**：逐层向上追溯调用栈，在每个函数检查：
   - 输入参数是否有效？
   - 前置条件是否满足？
   - 资源是否已分配？
   - 状态是否正确？
3. **标记决策点**：每个 if/else、switch、资源检查都是潜在的断裂点
4. **检查近期变更**：`git log --oneline -20` + `git diff HEAD~5..HEAD`，标记所有改动过的区域
5. **区分"因"与"果"**：
   - 果：最终症状（如"画面漆黑"）
   - 中间因：函数返回 NULL / 跳过渲染 / 资源未分配
   - 根因：初始状态错误 / 条件判断错误 / API 调用错误

**实战案例（水渲染倒影）：**
```
症状: 水面漆黑无倒影
  ↑
调用链:
  Render() → renderWater() → drawSea()
    ↑ drawSea 中:
      检查 m_pReflectionTexture → NULL → 提前 return
        ↑ ReAcquireResources 中:
          Create_Render_Target() 返回了有效的纹理
            但是! updateRenderTargetTextures() 从未被调用
              ↑ GameClient::update() 中渲染循环:
                updateRenderTargetTextures() 被条件守卫跳过了
                  ↑ 条件判断依赖于一个从未设置的状态变量

根因: D3D9 设备创建过程中状态变量初始化顺序错误，
      导致渲染循环认为"不需要更新反射纹理"
```

---

### 阶段 2 — 诊断仪表设计（Diagnostic Instrumentation）

**目标：** 在因果链的关键决策点插入定向日志，将程序的黑盒行为可视化。

**操作：**
1. **确定需要观察的数据**：
   - 函数是否被调用？（entry/exit 标记）
   - 决策分支走哪边？（条件判断结果）
   - 关键变量的值是什么？（指针、状态、计数）
2. **选择日志方式**：
   - 文件日志（`fprintf` + 文件）— 适合游戏、服务端
   - 控制台输出（`printf`/`OutputDebugString`）— 适合 CLI 工具
   - 回调注入 — 适合插件模块
3. **日志格式规范**：
   - 每个日志点有唯一标签（如 `CP1_xxx`, `CP2_xxx`）
   - 标签分级：`CP1_` = 函数入口，`CP2_` = 逻辑决策点，`CP8_` = 渲染操作
   - 方便 `grep` 过滤分析
4. **日志分级输出**：
   ```
   [时间] CP1_DRAWSEA: entered
   [时间] CP2_UPDATERTT: m_waterType = 2
   [时间] CP5_DRAWSEA: polygon found, height = 42.5
   [时间] CP5_DRAWSEA: reflTex = 0x00000000  ← 根因线索!
   ```

**实战案例（水渲染：water_diag.log）：**
```cpp
// 诊断日志宏
static void WaterDiag(const char *msg) {
    FILE *f = fopen("water_diag.log", "a");
    if (f) { fprintf(f, "[%d] %s\n", timeGetTime(), msg); fclose(f); }
}
// 输出:
// [123456] CP4_DRAWSEA: SKIP - resource NULL
// [123456] CP4:  m_dwWaveVertexShader NULL? 0
// [123456] CP4:  m_dwWavePixelShader NULL? 0
// [123456] CP4:  m_waveShaderNoBump NULL? 0
// → 排除了着色器问题，锁定到其他资源
```

---

### 阶段 3 — 假设检验与逐项排除（Hypothesis Elimination）

**目标：** 将所有可能的根因列为可测试假设，设计诊断实验逐一确认或排除。

**操作：**
1. **列出假设**：基于因果链分析，列出所有可能的根因
2. **为每个假设设计观测信号**：如果此假设成立，诊断日志中应该出现什么？
3. **确定检验顺序**：从最可能 → 最不可能，从最简单 → 最复杂
4. **一次检验一个**：除非检验结果相互独立，否则不要并行
5. **每个假设结束时更新状态**：✅ 确认 / ❌ 排除 / ⏳ 待定

**实战案例（水渲染假设表）：**
```
假设 H1: 像素着色器编译失败，导致水面无法渲染
  观测信号: m_dwWavePixelShader == NULL 但 m_waveShaderNoBump != NULL
  → CP4 日志: 两者都 NULL → ❌ 排除（着色器正常）

假设 H2: m_pReflectionTexture 纹理未创建
  观测信号: m_pReflectionTexture == NULL
  → CP5 日志: reflTex = 0x00000000 → ✅ 确认！
  → 根因: updateRenderTargetTextures 从未被调用

假设 H3: updateRenderTargetTextures 被条件守卫跳过
  观测信号: CP2_UPDATERTT 日志从未出现
  → water_diag.log 无 CP2_ 输出 → ✅ 确认！
  → 根因: 调用链上游状态变量错误
```

---

### 阶段 4 — 分步修复 + 验证关卡（Incremental Fix）

**目标：** 将修复拆分为独立阶段，每阶段有明确验证标准，避免一次做太多。

**操作：**
1. **设计修复分阶段方案**：
   - 阶段 A：头文件 + 声明（编译验证）
   - 阶段 B：核心逻辑修复（功能验证）
   - 阶段 C：清理 + 加固（稳定性验证）
2. **每个阶段有验证关卡**：
   - 关卡 1：编译通过，0 error
   - 关卡 2：程序启动不崩溃
   - 关卡 3：功能基本正常
   - 关卡 4：功能完全正确
3. **使用编译工具自动验证**：
   ```bash
   cd GeneralsMD/Code && bash build.sh Release -inc
   ```
4. **记录每个阶段的改动和验证结果**

**实战案例（水渲染修复分阶段）：**
```
阶段 A: 头文件声明（MAX_WATER_HEIGHT_LEVELS, renderMirror 签名等）
  验证: ✅ 编译通过

阶段 B: updateRenderTargetTextures 重写（PolygonTrigger 遍历）
  验证: ✅ 编译通过，日志显示纹理已创建

阶段 C: renderMirror 多参数版本
  验证: ✅ 倒影可见，但高山区域无反射

阶段 D: drawSea 重建（高山 + 海平面分支）
  验证: ✅ 所有区域倒影正确

阶段 E-G: 状态清理 + 设备丢失 + 性能
  验证: ✅ 完整构建 0 error
```

---

### 阶段 5 — 纵深防御加固（Defense-in-Depth）

**目标：** 在因果链的多层同时设防，每层独立足以阻止故障，防止同类型问题复发。

**操作：**
1. **识别因果链的所有断裂点**
2. **在每个断裂点添加防护**：
   - 外层：调用前检查（caller validates preconditions）
   - 中层：函数入口检查（early return on invalid state）
   - 内层：资源分配时的保护（NULL check, fallback）
3. **每层防护添加注释说明**为什么需要这层保护

**实战案例（水渲染三层防护）：**
```
因果链: 设备创建 → 初始化状态 → 渲染循环 → update → draw

外层防护 (WinMain.cpp):
  // D3D9On12 全屏模拟可能干扰窗口激活消息
  // 在设备创建前主动设置激活状态
  if (GetForegroundWindow() == hWnd) isWinMainActive = TRUE;

中层防护 (GameClient.cpp):
  // D3D9 设备创建可能内部发送 WM_ACTIVATEAPP
  // 确保引擎在主菜单可见时处于激活状态
  if (GetForegroundWindow() == ApplicationHWnd && !TheGameEngine->isActive())
      TheGameEngine->setIsActive(TRUE);

内层防护 (W3DWater.cpp):
  // drawSea 入口: 设备丢失则静默返回
  if (FAILED(hr = m_pDev->TestCooperativeLevel())) {
      if (hr == D3DERR_DEVICELOST) return;
  }
```

---

### 阶段 6 — 回归验证（Regression Validation）

**目标：** 确认修复未引入新问题，并记录经验以供将来参考。

**操作：**
1. **验证核心功能**：修复的目标功能在所有场景下正常工作
2. **验证周边功能**：修改波及的周边功能未退化
3. **验证边缘情况**：
   - 空数据（没有水的地图）
   - 极端值（最高/最低高度）
   - 资源不足（显存不足、设备丢失）
4. **验证多配置**：Release / Debug / Internal 均编译通过
5. **记录经验教训**：写入项目文档或 skill，供未来参考

**实战案例（水渲染回归验证）：**
```
✅ 海平面地图: 倒影正常
✅ 高山地图: 高海拔水面独立反射正确
✅ 多高度地图: 每层水面各自纹理更新
✅ Alt+Tab: 设备恢复后水面重新出现（无崩溃）
✅ 编译: Release 0 error
```

---

## 实战案例完整演示（水渲染倒影修复）

以下是本次方法论各阶段在实际项目中的对应成果物：

| 阶段 | 成果物 | 文件 |
|------|--------|------|
| P0 问题定义 | 症状+期望+验收标准 | 对话记录 |
| P1 因果链 | 调用链逆向追踪 | W3DWater.cpp 分析 |
| P2 诊断仪表 | water_diag.log | W3DWater.cpp 第 57-79 行 |
| P3 假设检验 | 着色器/纹理/状态排除 | 日志分析 |
| P4 分步修复 | 13 项变更，5 个阶段 | Stage A-G 实施 |
| P5 纵深防御 | 3 层激活状态防护 | WinMain.cpp + GameClient.cpp + W3DWater.cpp |
| P6 回归验证 | 多地图 + Alt+Tab + 编译 | 构建验证 |

---

## 反模式（Anti-Patterns）

| 反模式 | 问题 | 正确做法 |
|--------|------|---------|
| **"瞎猫碰死耗子"** | 随机改代码看结果 | 先加诊断日志，有了证据再改 |
| **"一次改三样"** | 改了 A、B、C，问题好了，不知道是谁的功劳 | 一次只改一个变量，每步验证 |
| **"只修表面"** | 只是 catch 了异常/返回了默认值 | 追踪到根因，在源头修复 |
| **"不加日志靠推理"** | 觉得"应该不会走到这里" | 用日志证伪你的假设 |
| **"修完就跑"** | 验证了 happy path 就结束 | 测试 edge cases + Alt+Tab + 资源不足 |
| **"不回滚"** | 改错了方向还在继续改 | 修改前 commit/stash，方向不对立刻回滚 |

---

## 诊断日志模式速查

| 场景 | 日志模式 | 标签规范 |
|------|---------|---------|
| 函数入口 | `CP1_xxx: entered` + 关键参数 | `CP1_` |
| 决策分支 | `CP2_xxx: deciding: val=%d → branch %s` | `CP2_` |
| 资源检查 | `CP3_xxx: ptr=%p NULL? %d` | `CP3_` |
| 资源分配 | `CP4_xxx: Create_X result=%d` | `CP4_` |
| 循环迭代 | `CP5_xxx: iter %d of %d` | `CP5_` |
| 渲染操作 | `CP8_xxx: SetRenderTarget done` | `CP8_` |
| 错误/警告 | `CPE_xxx: ERROR - %s` | `CPE_` |

---

## 快速启动清单

遇到 bug 时，按此清单操作：

```
[ ] P0: 明确写出症状/期望/验收标准
[ ] P1: 逆向追踪调用链，标记所有决策点
[ ] P1: git log 检查近期变更
[ ] P2: 在因果链决策点插入诊断日志
[ ] P2: 编译 + 运行，收集日志
[ ] P3: 列出假设，设计观测信号
[ ] P3: 根据日志逐一确认/排除假设
[ ] P4: 定位根因，设计分阶段修复方案
[ ] P4: 每阶段有验证关卡
[ ] P5: 评估是否需要多层防御
[ ] P6: 完整回归验证 + 记录经验
```

---

## 参考

- **MinGeneralsmd 项目**：`E:\Source\repos\MinGeneralsfreebuild2ok`
- **Water Type 2 修复完整变更**：W3DWater.h + W3DWater.cpp
- **鼠标菜单点击修复**：WinMain.cpp + GameClient.cpp
- **构建工具**：`/build` — 增量/全量构建
