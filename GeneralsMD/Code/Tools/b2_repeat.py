#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
b2_repeat.py — B2 重复性验收（SagePerfDiag P0 余项：同场景 3 遍重复性 < 5%）

为什么不用 spd_analyzer.py 直接测 B2：它是单文件/双文件口径，且中位数取全文件，
会被"载入早期窗口"（objects 291 vs 稳态 719）拉低 —— 看板 2026-09-18 方法论里
踩过这个坑，跨场景比数字前必须先对齐 objects。本工具按轮聚合、只在稳态窗口上取中位。

判定口径：
  1. 【按轮聚合】一轮 = 一次进程运行，会落多份 .spd（每 30s 一份），必须合并后再算。
  2. 【先剔加速模式，再算阈值】m_TiVOFastMode ON 时渲染门退化成 frame%30==1，只渲染
     1/30 帧（W3DDisplay.cpp:1864），与普通模式不可比。热键 F 会切换（MetaEvent.cpp:757
     把 MSG_META_TOGGLE_FAST_FORWARD_REPLAY 绑到 F，CommandXlat.cpp:3316 循环四档）。
     判定沿用 spd_rescan.py 的「t_render 与 draw_calls 同为 0 的帧占比 >= 50%」。
     ⚠️ 顺序不能反：加速模式帧的 objects 与普通模式**完全一样**（实测同一存档档
     654-683 vs 649-654），所以"对齐 objects"挡不住它。必须按文件先剔快模式，
     再算阈值 —— 否则中位会被 1/30 渲染的帧拉走（实测把 1095ms 拉成 140ms）。
  3. 【对齐 objects】在剩余帧上只取 objects >= 稳态阈值 的帧（默认 0.9 x 峰值 objects），
     把载入/菜单窗口排除掉。剔除的帧数如实报出，不静默丢弃。
  4. 【轮间极差】(max-min)/min，在三轮的稳态中位 t_total 上算。

轮次切分：文件名形如 frameprobe_<tick>_<窗口序号>.spd，序号在同一进程内 0,1,2,... 递增、
新进程重置为 0。⚠️ 定序必须用**文件 mtime**而**不是 tick** —— tick 是 GetTickCount()（开机
毫秒），系统重启归零，旧开机周期的文件 tick 反而更大，按 tick 排序会把它们排到最新一轮之后
（实测踩过：`--last 3` 选到几天前的旧会话文件，判定报告冒出 21548ms 的累加伪影值）。

用法:
  python b2_repeat.py <目录>                       # 扫目录下全部 frameprobe_*.spd
  python b2_repeat.py a.spd b.spd ...              # 显式给文件
  python b2_repeat.py <目录> --last 3              # 只验最近 3 轮（B2 常规用法）
  python b2_repeat.py <目录> --min-objects 600     # 手动画稳态阈值（默认 auto）
  python b2_repeat.py <目录> --out r.md
"""
import sys
import os
import glob
import csv
import statistics as st

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# 复用 spd_rescan.py 已实测过的三件套：CSV 读入 / 稳态估计 / 累加污染首帧检测。
# 同一目录下的兄弟脚本，直接导入而非第三次复制（此前 spd_analyzer 与 spd_rescan
# 已各存一份）。
from spd_rescan import (  # noqa: E402
    load,
    steady_state,
    detect_contaminated_first_row,
    is_fastmode_recording,
)

BUDGET_MS = 33.3
STEADY_OBJ_FRAC = 0.9      # 稳态阈值 = 本轮峰值 objects * 该系数
MIN_STEADY_FRAMES = 10     # 稳态帧太少则结论无效，只能看，不能判
B2_TARGET_PCT = 5.0        # 验收线：轮间极差 < 5%
MAX_RUN_GAP_MS = 300000    # 同轮内相邻窗口约 30s；超过这个跳变即判为新一轮


def parse_name(path):
    """frameprobe_<tick>_<idx>.spd -> (tick, idx)；不符合则 (None, None)。"""
    stem = os.path.basename(path).replace("frameprobe_", "").replace(".spd", "")
    parts = stem.rsplit("_", 1)
    if len(parts) != 2:
        return None, None
    try:
        return int(parts[0]), int(parts[1])
    except ValueError:
        return None, None


def group_runs(files):
    """切分轮次。返回 [(tick, idx, path), ...] 的列表，**按时间先后排列**。

    ⚠️ 不能按 tick 排序后再切：tick 是 GetTickCount()（开机毫秒），**系统重启会归零**，
    于是上一开机周期的文件 tick 反而更大，排序后被排到最新一轮之后。实测踩过：
    按 tick 取 `--last 3` 会选到几天前旧会话的文件（那些还带 ring-slot-0 累加伪影，
    于是判定报告里冒出 21548ms 这种值）。故一律按**文件 mtime** 定序。

    切轮条件（任一）：
      - 序号回绕（idx <= 上一个 idx）—— 同一次进程运行内序号严格递增，新进程从 0 起
      - tick 不增（跨重启，tick 归零）
      - tick 跳变 > MAX_RUN_GAP_MS（同轮内相邻窗口约 30s，留足冗余）
    """
    tagged = []
    for f in files:
        tick, idx = parse_name(f)
        if tick is None:
            continue
        try:
            mt = os.path.getmtime(f)
        except OSError:
            continue
        tagged.append((mt, tick, idx, f))
    tagged.sort(key=lambda t: t[0])          # 按 mtime 定序，不用 tick

    runs = []
    cur = []
    prev_idx = None
    prev_tick = None
    for mt, tick, idx, f in tagged:
        if cur:
            if idx <= prev_idx or tick <= prev_tick or (tick - prev_tick) > MAX_RUN_GAP_MS:
                runs.append(cur)
                cur = []
        cur.append((tick, idx, f))
        prev_idx = idx
        prev_tick = tick
    if cur:
        runs.append(cur)
    return runs


def pct(vals, p):
    vals = sorted(vals)
    if not vals:
        return 0.0
    return vals[min(len(vals) - 1, int(len(vals) * p / 100.0))]


def run_stats(files, min_objects):
    """合并一轮所有 .spd，返回整体与稳态窗口两套统计。

    加速模式文件在这里就被剔除，不进入 rows —— 它们的 objects 与普通模式相同，
    放到阈值之后再剔就晚了（阈值和中位都已被污染）。
    """
    rows = []
    fast_rows = []
    per_file = []
    for f in files:
        try:
            r = load(f)
        except Exception as e:
            per_file.append({"name": os.path.basename(f), "err": str(e)})
            continue
        if not r:
            continue
        contaminated, _ = detect_contaminated_first_row(r)
        if contaminated:
            r = r[1:]
        zero_render = sum(1 for x in r
                          if x.get("t_render", 0.0) == 0.0 and x.get("draw_calls", 0) == 0)
        n = len(r)
        # 共享判据（零渲染占比 + 稳态 objects > 0），避免与 spd_rescan 规则漂移。
        is_fast = is_fastmode_recording(r, zero_render)
        per_file.append({
            "name": os.path.basename(f),
            "n": n,
            "contaminated": contaminated,
            "fastmode": is_fast,
            "zero_render_frac": (zero_render / float(n)) if n else 0.0,
        })
        (fast_rows if is_fast else rows).extend(r)

    if not rows:
        return None

    peak_obj = max((x.get("objects", 0) for x in rows), default=0)
    thr = min_objects if min_objects else int(peak_obj * STEADY_OBJ_FRAC)
    steady = [x for x in rows if x.get("objects", 0) >= thr] if thr > 0 else rows

    def summarize(rs):
        if not rs:
            return {"n": 0}
        tot = [x["t_total"] for x in rs if "t_total" in x]
        return {
            "n": len(rs),
            "t_total_med": st.median(tot) if tot else 0.0,
            "t_total_p95": pct(tot, 95),
            "t_render_med": st.median([x["t_render"] for x in rs if "t_render" in x] or [0.0]),
            "t_postfx_med": st.median([x["t_postfx"] for x in rs if "t_postfx" in x] or [0.0]),
            "objects_med": st.median([x.get("objects", 0) for x in rs]),
            "draw_calls_med": st.median([x.get("draw_calls", 0) for x in rs]),
            "over_budget_pct": 100.0 * sum(1 for t in tot if t > BUDGET_MS) / len(rs),
        }

    fast_files = [p for p in per_file if p.get("fastmode")]
    return {
        "files": per_file,
        "n_all": len(rows),
        "n_fast_rows": len(fast_rows),
        "peak_objects": peak_obj,
        "threshold": thr,
        "overall": summarize(rows),
        "steady": summarize(steady),
        "any_fastmode": bool(fast_files),
        "fastmode_files": [p["name"] for p in fast_files],
        # 快模式那部分单独给个数，只为说明"剔掉了多少"，不参与判定。
        "fastmode_t_total_med": st.median([x["t_total"] for x in fast_rows
                                           if "t_total" in x]) if fast_rows else 0.0,
    }


def spread_pct(vals):
    vals = [v for v in vals if v > 0]
    if len(vals) < 2:
        return None
    return 100.0 * (max(vals) - min(vals)) / min(vals)


def main():
    # Windows 控制台默认 cp936，中文会被编成 GBK 字节流，在 UTF-8 终端里显示成乱码。
    # 报告 .md 是 UTF-8 写的，终端也统一成 UTF-8。
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass

    argv = sys.argv[1:]
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 2

    srcs, last, min_objects, out, pattern = [], 0, 0, "b2_repeat_report.md", "frameprobe_*.spd"
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--last" and i + 1 < len(argv):
            last = int(argv[i + 1]); i += 2
        elif a == "--min-objects" and i + 1 < len(argv):
            v = argv[i + 1]
            min_objects = 0 if v == "auto" else int(v); i += 2
        elif a == "--out" and i + 1 < len(argv):
            out = argv[i + 1]; i += 2
        elif a == "--glob" and i + 1 < len(argv):
            pattern = argv[i + 1]; i += 2
        elif a.startswith("--"):
            i += 1
        else:
            srcs.append(a); i += 1

    files = []
    for s in srcs:
        if os.path.isdir(s):
            files.extend(glob.glob(os.path.join(s, pattern)))
        else:
            files.append(s)
    if not files:
        print("没有可分析的文件（目录下无 %s？）" % pattern)
        return 1

    runs = group_runs(files)
    if last > 0:
        runs = runs[-last:]

    L = ["# B2 重复性验收报告", ""]
    L.append("> 文件 %d 份 → 切分出 %d 轮；稳态阈值 %s；验收线：轮间极差 < %.1f%%"
             % (len(files), len(runs),
                ("自动（0.9 × 本轮峰值 objects）" if not min_objects else "objects >= %d" % min_objects),
                B2_TARGET_PCT))
    L.append("")

    detail, medians = [], []
    for ri, run in enumerate(runs, 1):
        st_ = run_stats([f for _, _, f in run], min_objects)
        if not st_:
            L.append("## 第 %d 轮 — 无有效数据（%d 份 .spd 全空）" % (ri, len(run)))
            L.append("")
            continue
        s = st_["steady"]
        detail.append((ri, st_))
        if s["n"] >= MIN_STEADY_FRAMES and not st_["any_fastmode"]:
            medians.append(s["t_total_med"])

        L.append("## 第 %d 轮（tick %d..%d, %d 份 .spd）"
                 % (ri, run[0][0], run[-1][0], len(run)))
        L.append("")
        if st_["any_fastmode"]:
            L.append("> ⚠️ **本轮剔除了加速模式文件**（`%s`，共 %d 帧，其中位 t_total %.2f ms）"
                     "—— 只渲染 1/30 帧，与普通模式不可比，已整体排除在统计之外。"
                     % (", ".join(st_["fastmode_files"]), st_["n_fast_rows"],
                        st_["fastmode_t_total_med"]))
            L.append("")
        L.append("| 项 | 全帧 | 稳态窗口（objects>=%d） |" % st_["threshold"])
        L.append("|---|---|---|")
        for key, label, fmt in [("n", "帧数", "%d"), ("objects_med", "objects 中位", "%.0f"),
                                ("draw_calls_med", "draw_calls 中位", "%.0f"),
                                ("t_total_med", "**t_total 中位 ms**", "%.2f"),
                                ("t_total_p95", "t_total P95 ms", "%.2f"),
                                ("t_render_med", "t_render 中位 ms", "%.2f"),
                                ("t_postfx_med", "t_postfx 中位 ms", "%.2f"),
                                ("over_budget_pct", "超预算(33.3ms) %", "%.1f")]:
            L.append("| %s | " % label + fmt % st_["overall"][key] + " | "
                     + fmt % st_["steady"][key] + " |")
        L.append("")
        if st_["steady"]["n"] < MIN_STEADY_FRAMES:
            L.append("> ⚠️ 稳态帧仅 %d 帧（< %d），本轮结论无效。"
                     % (st_["steady"]["n"], MIN_STEADY_FRAMES))
            L.append("")
        dropped = st_["n_all"] - st_["steady"]["n"]
        L.append("（普通模式 %d 帧，其中 %d 帧被判为载入/菜单窗口而剔除；本轮峰值 objects = %d）"
                 % (st_["n_all"], dropped, st_["peak_objects"]))
        L.append("")

    L.append("## 轮间重复性（验收判定）")
    L.append("")
    if len(medians) < 2:
        L.append("⚠️ 可比较的轮次不足 2 轮（需稳态帧 >= %d 且无加速模式），**无法判定**。"
                 % MIN_STEADY_FRAMES)
        verdict = "不可判定"
    else:
        L.append("| 轮 | 稳态 t_total 中位 ms |")
        L.append("|---|---|")
        for (ri, st_), m in zip([d for d in detail if d[1]["steady"]["n"] >= MIN_STEADY_FRAMES], medians):
            L.append("| 第 %d 轮 | %.2f |" % (ri, m))
        sp = spread_pct(medians)
        mean = st.mean(medians)
        L.append("")
        L.append("| 指标 | 值 |")
        L.append("|---|---|")
        L.append("| 参与判定的轮数 | %d |" % len(medians))
        L.append("| 中位 | %.2f ms |" % st.median(medians))
        L.append("| 极差 (max-min)/min | **%.2f%%** |" % sp)
        L.append("| 极差 (max-min)/mean | %.2f%% |" % (100.0 * (max(medians) - min(medians)) / mean))
        L.append("")
        if sp < B2_TARGET_PCT:
            verdict = "✅ 通过（%.2f%% < %.1f%%）" % (sp, B2_TARGET_PCT)
        else:
            verdict = "❌ 未通过（%.2f%% >= %.1f%%）" % (sp, B2_TARGET_PCT)
        L.append("**判定：%s**" % verdict)

    L.append("")
    txt = "\n".join(L)
    with open(out, "w", encoding="utf-8") as f:
        f.write(txt)

    print("切分出 %d 轮；判定：%s" % (len(runs), verdict))
    for ri, st_ in detail:
        print("  第 %d 轮: 稳态 %d 帧, t_total 中位 %.2f ms, P95 %.2f ms%s"
              % (ri, st_["steady"]["n"], st_["steady"]["t_total_med"], st_["steady"]["t_total_p95"],
                 "  [含加速模式]" if st_["any_fastmode"] else ""))
    print("已写出 %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
