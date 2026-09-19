#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
spd_analyzer.py — SagePerfDiag 分帧轨迹分析器（P0/T8 最小版）
设计文档: Tools/PERF_DIAG_DESIGN.md

用法:
  python spd_analyzer.py <file.spd>                     单份报告
  python spd_analyzer.py <old.spd> <new.spd>            A/B 对比
  python spd_analyzer.py <file.spd> --budget-ms 33.3    指定帧预算(默认30Hz逻辑帧33.3ms)
  python spd_analyzer.py <file.spd> --drop-first auto   首行处理: auto(默认,自动识别
                                                        ring-slot-0 累加污染并剔除) /
                                                        always / never

输出: 终端统计 + <名>_report.md
"""
import sys, os, csv, statistics as st

BUDGET_MS = 33.3
STAGE_COLS = [  # t_ 开头的阶段列(排除 t_total)
    "t_radar","t_audio","t_client","t_msg","t_net","t_logic",
    "t_net_wait","t_render","t_present","t_postfx","t_fps_spin",
    "t_logic_script","t_logic_terrain","t_logic_create",
    "t_logic_ai","t_logic_pathfind","t_logic_destroy",
    # T11 (2026-09-17): split of the ~105ms non-DRAW remainder of
    # GameClient::update() plus the pre-RENDER half of W3DDisplay::draw().
    # Absent from .spd written before this stage set existed; `if c in r` guards.
    "t_client_input","t_client_window","t_client_ghost",
    "t_client_drawables","t_client_terrain","t_client_displupd",
    "t_client_strmgr","t_client_shell","t_client_ingameui",
    "t_draw_views","t_draw_rttex",
    # T12 (2026-09-18): split of t_draw_rttex into water vs shadow
    "t_draw_rttex_water","t_draw_rttex_shadow",
    # T13 (2026-09-18): split of t_postfx
    "t_postfx_ui","t_postfx_debug","t_postfx_misc",
]

# FrameProbe ring-slot-0 accumulation bug (fixed 2026-09-17, FrameProbe.cpp:190).
# Every .spd written by an older build has a FIRST data row that is the running
# sum of that window's first frame plus every earlier window's first frame, so it
# sits at a near-integer multiple of the file's own steady state (measured
# 0.00 / 0.98 / 1.98 / 2.98 / 3.97 across the 2026-09-15 windows = exactly the
# cumulative window count). Detect it rather than blindly dropping row 0, because
# for post-fix files row 0 is a perfectly good sample.
FIRST_ROW_CONTAM_RATIO = 1.5


def steady_state(vals):
    """Robust steady-state estimate for files where most rows are 0 (the game sat
    in a menu / loading state): a plain median yields 0 there and silently skips
    the check. Measured on frameprobe_921168308_419 (1359 rows, median 0) whose
    first row is still the accumulation artifact (objects 610160 vs real ~2335)."""
    pos = [v for v in vals if v > 0]
    if len(pos) >= 2:
        return st.median(pos)
    return 0.0


def detect_contaminated_first_row(rows):
    """True if rows[0] carries the ring-slot-0 accumulation artifact.

    Uses only the bounded steady-state counters (objects / draw_calls); t_total is
    deliberately excluded because a genuine load spike in a clean file's first
    frame would false-positive. A ratio near 0 (menu window, nothing preceding it)
    is NOT flagged -- correctly, since such a row carries no accumulated total.
    """
    if len(rows) < 3:
        return False
    for key in ("objects", "draw_calls"):
        vals = [r[key] for r in rows[1:] if key in r]
        if len(vals) < 2:
            continue
        base = steady_state(vals)
        if base <= 0:
            continue
        if rows[0].get(key, 0) / base > FIRST_ROW_CONTAM_RATIO:
            return True
    return False

def load(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    out = []
    for r in rows:
        d = {"frame": int(r["frame"]), "wall_ms": int(r["wall_ms"])}
        for k, v in r.items():
            if k.startswith("t_"):
                d[k] = float(v)
            elif k and k[0].isalpha() and k not in ("frame","wall_ms"):
                d[k] = int(v)
        out.append(d)
    return out

def pct(vals, p):
    vals = sorted(vals)
    if not vals: return 0.0
    i = min(len(vals)-1, int(len(vals)*p/100.0))
    return vals[i]

def analyze(path, budget=BUDGET_MS, drop_first="auto"):
    """drop_first: 'auto' (detect the ring-slot-0 artifact), 'always', 'never'."""
    rows = load(path)
    n_raw = len(rows)
    dropped = False
    if rows and drop_first != "never":
        dropped = True if drop_first == "always" else detect_contaminated_first_row(rows)
        if dropped:
            rows = rows[1:]
    n = len(rows)
    rep = {"path": path, "n": n, "n_raw": n_raw, "first_row_dropped": dropped}
    if n == 0:
        return rep
    totals = [r["t_total"] for r in rows if "t_total" in r]
    rep["fps_med"] = 1000.0/st.median(totals) if totals and st.median(totals)>0 else 0
    rep["total_med"], rep["total_p95"] = st.median(totals), pct(totals,95)
    rep["over_budget"] = sum(1 for t in totals if t > budget)
    rep["over_budget_pct"] = 100.0*rep["over_budget"]/n

    # 阶段瀑布(排除 t_total)
    stages = []
    for c in STAGE_COLS:
        vals = [r[c] for r in rows if c in r]
        if vals:
            stages.append((c, st.median(vals), pct(vals,95)))
    grand = sum(s[1] for s in stages) or 1.0
    stages.sort(key=lambda s: -s[1])
    rep["stages"] = [(c, m, p95, 100.0*m/grand) for c, m, p95 in stages]

    # 卡顿尖峰: >滑动均值3倍 且 >50ms
    spikes = []
    win = 30
    for i, r in enumerate(rows):
        lo, hi = max(0,i-win), min(n,i+win)
        loc = st.median([rows[j]["t_total"] for j in range(lo,hi)])
        t = r.get("t_total",0)
        if t > 50 and t > 3*max(loc,0.001):
            worst = max(((c, r[c]) for c in STAGE_COLS if c in r), key=lambda x: x[1], default=("?",0))
            spikes.append((r["frame"], round(t,1), worst[0], round(worst[1],1),
                           r.get("objects",0), r.get("draw_calls",0)))
    rep["spikes"] = spikes[:50]
    rep["spike_count"] = len(spikes)
    return rep

def fmt_report(rep, budget=BUDGET_MS):
    L = []
    L.append(f"# spd 报告: {os.path.basename(rep['path'])}")
    if rep.get("first_row_dropped"):
        L.append(f"> 已剔除污染首帧（ring-slot-0 累加伪影）—— 计入 {rep['n']} 帧 / 原始 {rep['n_raw']} 帧")
    elif rep.get("n_raw") and rep.get("n_raw") != rep.get("n"):
        L.append("> 含首帧（未检出累加污染），首行计入统计")
    L.append(f"帧数 {rep['n']} | 中位FPS {rep.get('fps_med',0):.1f} | "
             f"帧时间中位 {rep.get('total_med',0):.2f}ms / P95 {rep.get('total_p95',0):.2f}ms | "
             f"超预算({budget}ms) {rep.get('over_budget_pct',0):.1f}%")
    L.append("\n## 阶段瀑布(按中位耗时排序)")
    L.append("| 阶段 | 中位ms | P95 ms | 占比 |")
    L.append("|---|---|---|---|")
    for c, m, p95, share in rep["stages"]:
        L.append(f"| {c} | {m:.3f} | {p95:.3f} | {share:.1f}% |")
    L.append(f"\n## 卡顿尖峰({rep['spike_count']}个, >50ms且>3x局部中位)")
    if rep["spikes"]:
        L.append("| 帧 | 总ms | 最重阶段 | 阶段ms | objects | draw_calls |")
        L.append("|---|---|---|---|---|---|")
        for s in rep["spikes"]:
            L.append(f"| {s[0]} | {s[1]} | {s[2]} | {s[3]} | {s[4]} | {s[5]} |")
    return "\n".join(L)

def fmt_ab(old, new):
    L = ["# spd A/B 对比", "", "| 指标 | 旧 | 新 | 变化 |", "|---|---|---|---|"]
    for k, label in [("total_med","帧时间中位ms"),("total_p95","P95 ms"),
                     ("fps_med","中位FPS"),("over_budget_pct","超预算%"),("spike_count","卡顿数")]:
        o, nw = old.get(k,0), new.get(k,0)
        d = (nw-o)/o*100 if o else 0
        L.append(f"| {label} | {o:.2f} | {nw:.2f} | {d:+.1f}% |")
    L.append("\n## 阶段中位ms对比")
    L.append("| 阶段 | 旧 | 新 | 变化 |")
    L.append("|---|---|---|---|")
    om = {c:m for c,m,_,_ in old["stages"]}
    nm = {c:m for c,m,_,_ in new["stages"]}
    for c in sorted(set(om)|set(nm)):
        o, nw = om.get(c,0), nm.get(c,0)
        d = (nw-o)/o*100 if o else 0
        L.append(f"| {c} | {o:.3f} | {nw:.3f} | {d:+.1f}% |")
    return "\n".join(L)

def main():
    global BUDGET_MS
    budget = BUDGET_MS
    drop_first = "auto"
    files = []
    argv = sys.argv[1:]
    # NOTE: the old parser collected flag *values* as file arguments
    # (`--budget-ms 33.3` left "33.3" in args). Consume values explicitly.
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--budget-ms" and i + 1 < len(argv):
            budget = float(argv[i + 1]); i += 2
        elif a == "--drop-first" and i + 1 < len(argv):
            drop_first = argv[i + 1]; i += 2
        elif a.startswith("--"):
            i += 1
        else:
            files.append(a); i += 1

    if drop_first not in ("auto", "always", "never"):
        print(f"--drop-first 取值非法: {drop_first!r}（应为 auto/always/never）")
        sys.exit(2)

    if not files:
        print(__doc__); sys.exit(1)
    if len(files) == 1:
        rep = analyze(files[0], budget, drop_first)
        txt = fmt_report(rep, budget)
        out = os.path.splitext(files[0])[0] + "_report.md"
    else:
        old = analyze(files[0], budget, drop_first)
        new = analyze(files[1], budget, drop_first)
        txt = fmt_report(old, budget) + "\n\n" + fmt_report(new, budget) + "\n\n" + fmt_ab(old, new)
        out = os.path.splitext(files[1])[0] + "_ab_report.md"
    with open(out, "w", encoding="utf-8") as f:
        f.write(txt)
    print(txt); print(f"\n已写出 {out}")

if __name__ == "__main__":
    main()
