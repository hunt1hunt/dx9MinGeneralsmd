# dgVoodoo A/B 实验清单（可执行 + 含回滚）

> 背景：T15 实测查明 `t_postfx` 的 ~254ms 不是引擎的 CPU 成本，而是**主线程在等
> dgVoodoo 翻译层**（同期进程内有 ~3 个线程满跑，而全引擎只有一处 `CreateThread`，
> 且仅联机 DNS 用）。详见看板 `EXECUTION_STATUS.md` 的「T15 查明」一节。
>
> 本清单的三个实验**都不改引擎代码、都可逆**，目的是把"dgVoodoo 是主因"从
> **高置信推断**推到**因果证明**，顺带找有没有现成的加速空间。

---

## 0. 通用约定（三个实验都适用，先读）

**固定 exe**：`E:\!!!!!!!QWCSB\RTSI.exe` = **`169c4e45`**（Internal + LAA）。
实验期间**不要重新构建**——exe 一变，A/B 就不成立。

```powershell
Get-FileHash 'E:\!!!!!!!QWCSB\RTSI.exe' -Algorithm MD5   # 跑每个实验前后各核一次
```

**固定场景与参数**（三个实验都用同一套，否则不可比）：

```
存档 00000036.sav（游戏内名 Golden Oasis12345，开局档）
-Map 无 / -ManualLoad -WarmupSeconds 120 -Seconds 120 -Runs 1
```

**每轮采样口径**：只取稳态窗口（`objects >= 0.9 x 本轮峰值`），见下面第 5 节的判读脚本。

**基线（每轮实测，务必以你自己那轮为准重算）**：

| 指标 | 基线值 |
|---|---|
| `t_total` 总帧时 | **867.8 ms** |
| `t_render` 渲染 | 541.1 ms |
| `t_postfx_ui` HUD | **254.5 ms** |
| `t_draw_rttex` 水面 RT | 76.2 ms |
| `cpu_postfx_ui_us` HUD 内**主线程** CPU | **0.0 ms** |
| `postfx_pcpu_us` HUD 内**进程** CPU | **781.0 ms** |
| `vb_lock_count` 缓冲锁次数/帧 | 137 |
| `draw_calls`/帧 | 1715 |

**关键判据**：`postfx_pcpu_us / t_postfx_ui`。基线 = 781.0/254.5 = **3.07**（≈3 线程并行）。
**这个比值掉到 ≈0 就是因果坐实**——说明那 254ms 确实是翻译层造成的。

---

## 实验 1（最先做）：停用包装层，直接用系统 D3D9

**最便宜、信息量最大**。无论结果如何都能定性。

### 前置
**游戏必须完全退出**——`d3d9.dll` 被加载时改名会失败。

```powershell
if (Get-Process -Name RTSI,RTS -ErrorAction SilentlyContinue) { '仍在运行，先退出游戏' } else { 'OK，可以开始' }
```

### 步骤
```powershell
# 1) 记录原文件指纹（万一改名失败/被覆盖，可核对是不是原文件）
Get-FileHash 'E:\!!!!!!!QWCSB\d3d9.dll' -Algorithm MD5

# 2) 改名停用（不要删；原地改名，随时改回）
Rename-Item 'E:\!!!!!!!QWCSB\d3d9.dll' 'd3d9.dll.dgvoodoo-off'
```

### 跑一轮
```powershell
powershell -File Tools\bench_capture.ps1 -ManualLoad -SaveName "Golden Oasis12345" `
           -WarmupSeconds 120 -Seconds 120 -Tag exp1_no_wrapper -Runs 1
```

### 判读（第 5 节脚本）
- **`t_postfx_ui` 大幅下降 + `postfx_pcpu_us` 掉到接近 0** → **因果坐实**，dgVoodoo 是主因。
- **帧时没变** → dgVoodoo 不是主因，回头查别的（但 T15 的线程证据仍指向它）。
- **游戏黑屏 / 崩溃 / 起不来** → 这本身是有效结论：**说明包装层是必需的**，直接回滚。

### 回滚（任何异常立刻执行）
```powershell
Rename-Item 'E:\!!!!!!!QWCSB\d3d9.dll.dgvoodoo-off' 'd3d9.dll'
```

### ⚠️ 注意事项
- 同目录还有 **`d3d9.dll.z`（3.83 MB）**——那是另一份备份，**不要动、不要用它替换**。
- 系统 `C:\Windows\System32\d3d9.dll` 是否存在不影响判断：DLL 搜索顺序里**应用目录优先**，
  改名后自然回落到系统那份；若系统那份也没有/不被驱动支持，游戏会直接失败（见上面的判读）。

---

## 实验 2：换 DXVK 的 `d3d9.dll`

**前提**：显卡驱动支持 Vulkan（近十年的独显/集显基本都支持）。

### 步骤
1. 从官方发布页取 DXVK（`doitsujin/dxvk` 的 Releases），解压后取 **`x32\d3d9.dll`**
   —— 必须是 **32 位**那份（本游戏是 32 位进程）。
2. 先备份 dgVoodoo 那份（实验 1 若已在"停用"状态，先把文件改回原名再备份）：
   ```powershell
   Copy-Item 'E:\!!!!!!!QWCSB\d3d9.dll' 'E:\!!!!!!!QWCSB\d3d9.dll.dgvoodoo-backup' -Force
   ```
3. 把 DXVK 的 `d3d9.dll` 复制进 `E:\!!!!!!!QWCSB\`（覆盖）。

### 跑**两**轮
```powershell
powershell -File Tools\bench_capture.ps1 -ManualLoad -SaveName "Golden Oasis12345" `
           -WarmupSeconds 120 -Seconds 120 -Tag exp2_dxvk_r1 -Runs 1
# 再跑一轮
powershell -File Tools\bench_capture.ps1 -ManualLoad -SaveName "Golden Oasis12345" `
           -WarmupSeconds 120 -Seconds 120 -Tag exp2_dxvk_r2 -Runs 1
```

### ⚠️ 只取第二轮
DXVK **首次运行要编译/缓存 shader**，第一轮的帧时会被摊入编译开销，**不能当数据**。

### 回滚
```powershell
Copy-Item 'E:\!!!!!!!QWCSB\d3d9.dll.dgvoodoo-backup' 'E:\!!!!!!!QWCSB\d3d9.dll' -Force
```

---

## 实验 3：加 `dgVoodoo.conf` 调参

**当前游戏目录没有任何 dgVoodoo 配置文件**（`dgVoodoo.conf` 在游戏目录、`%APPDATA%\dgVoodoo\`
都没有）—— 也就是**纯默认配置**在跑。

### ⚠️ 不要手写配置键
本清单**故意不给出具体配置键和取值**。理由：dgVoodoo 的 conf 没有公开稳定的 schema，
我实测查证过 `GeneralExt` 里**没有**"多线程"开关这类项；手写臆造的键轻则无效、
重则触发未定义行为，而且**无效时你会以为"调过了没用"**，得出一半的结论。

**正确做法**：
1. 下载 dgVoodoo2 官方包（含 `dgVoodooCpl.exe` 控制面板）
2. **用控制面板**生成/编辑配置 —— 面板里能看到的项才是真实存在的项
3. 把面板生成的 `dgVoodoo.conf` 放到 `E:\!!!!!!!QWCSB\`（与 `d3d9.dll` 同级）
4. 逐项试，**一次只改一项**，每改一项跑一轮

### 值得先试的方向（在面板 UI 里找得到才改）
- **输出 API / DirectX 版本**：d3d11 vs d3d12，两条后端开销差别可能很大
- **缩放 / 重采样**（`imageXScaleFactor` / `Resampling` 一类）：默认若开了放大，会显著加开销
- **FPSLimit**：确认不是限制器在制造等待

### 回滚
```powershell
Remove-Item 'E:\!!!!!!!QWCSB\dgVoodoo.conf'   # 删掉即回到默认
```

---

## 5. 判读脚本（每轮跑完执行）

```bash
cd E:/Source/repos/MinGeneralsfreebuild2ok/GeneralsMD/Code/Tools
python - <<'PY'
import csv, glob, os, statistics as st
fs = sorted(glob.glob('E:/!!!!!!!QWCSB/frameprobe_*.spd'), key=os.path.getmtime)[-14:]
rows = []
for f in fs:
    r = list(csv.DictReader(open(f)))
    if r and 'postfx_pcpu_us' in r[0]:      # 只取带 T15 列的本轮文件
        rows += r
if not rows:
    raise SystemExit('没有带 T15 列的文件——这轮用的不是 169c4e45？')
peak = max(int(r['objects']) for r in rows)
S = [r for r in rows if int(r['objects']) >= 0.9*peak]
m = lambda k: st.median([float(x[k]) for x in S])
w, mc, pc = m('t_postfx_ui'), m('cpu_postfx_ui_us')/1000, m('postfx_pcpu_us')/1000
print('稳态 %d 帧, objects 中位 %d' % (len(S), st.median([int(x['objects']) for x in S])))
print('  t_total        %8.1f ms' % m('t_total'))
print('  t_postfx_ui    %8.1f ms   <- 墙钟' % w)
print('  cpu_postfx_ui  %8.1f ms   <- 主线程 CPU' % mc)
print('  postfx_pcpu_us %8.1f ms   <- 进程 CPU' % pc)
print('  进程CPU/墙钟    %8.2f      <- 基线 3.07；掉到 ~0 即因果坐实' % (pc/w if w else 0))
PY
```

---

## 6. 回滚总表（出事照这张表做）

| 改了什么 | 回滚命令 |
|---|---|
| `d3d9.dll` 改名停用 | `Rename-Item 'E:\!!!!!!!QWCSB\d3d9.dll.dgvoodoo-off' 'd3d9.dll'` |
| 换成了 DXVK 的 d3d9.dll | `Copy-Item 'E:\!!!!!!!QWCSB\d3d9.dll.dgvoodoo-backup' 'E:\!!!!!!!QWCSB\d3d9.dll' -Force` |
| 加了 dgVoodoo.conf | `Remove-Item 'E:\!!!!!!!QWCSB\dgVoodoo.conf'` |

**一条总原则**：所有改动都是**游戏目录里加/改名文件**，没有一处动到引擎代码或构建产物，
所以回滚永远是"把文件改回来/删掉"，不会污染仓库。

---

## 7. 做完之后

- 把结果（三个实验各自的一轮表格）记进看板 `EXECUTION_STATUS.md` 的 T15 一节
- 若实验 1 证实因果：**引擎侧优化的天花板就清楚了**，重点转向
  「HUD 的 draw call 数量」（引擎侧可控，见看板"新的着力点"）
