#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
spd_rescan.py — 历史 .spd 批量重扫（SagePerfDiag 优先级 2）

背景：FrameProbe 的 ring-slot-0 累加缺陷（2026-09-17 修复，FrameProbe.cpp:190）
让旧构建写出的每份 .spd **首行** = 本窗口首帧 + 之前所有窗口首帧之和。于是
一切基于 max / 峰值 / 卡顿聚类的历史结论都作废（中位数受影响很小）。

本工具对整目录重扫：逐份检测污染首帧并剔除，给出「剔除前 / 剔除后」的峰值对照，
同时标出**加速模式**（m_TiVOFastMode）录制的文件 —— 那类文件只渲染 1/30 帧
（W3DDisplay.cpp:1894 的门控），与普通模式不可比，B2 重复性验收必须排除。

用法:
  python spd_rescan.py <目录> [--glob frameprobe_*.spd]
                              [--out spd_rescan_report.md]
                              [--budget-ms 33.3]

输出: 终端汇总 + Markdown 报告
"""
import sys
import os
import glob
import csv
import statistics as st

BUDGET_MS = 33.3
FIRST_ROW_CONTAM_RATIO = 1.5

# 加速模式判定：渲染门（W3DDisplay.cpp:1894）只在 m_TiVOFastMode ON 且未暂停时
# 退化成 frame % 30 == 1，即 ~29/30 的帧 t_render 为 0。低于该比例的文件不判。
#
# 原「已知局限」已修（2026-09-18）：只凭零渲染占比，会把【停在菜单/空图的窗口】一并
# 判成加速模式——那里同样不渲染世界。区分点用稳态 objects：加速模式下世界是存在的，
# 菜单/空图窗口 objects 恒为 0。在 1046 份历史 .spd 上实测：当前判据选出 241 份，
# 其中 231 份 objects >= 100、10 份 objects == 0，而【0 < objects < 100 的样本数为 0】
# ——两组之间没有灰带，切得很干净，所以这个判据是可靠的而不是拍脑袋定的阈值。
FASTMODE_ZERO_RENDER_FRAC = 0.5
FASTMODE_MIN_ROWS = 20


def is_fastmode_recording(rows, zero_render):
    """True if this window was recorded under m_TiVOFastMode.

    rows 用原始行（不剔首行），zero_render 由调用方数好以免重复遍历。
    需要同时满足：（a）不渲染的帧占比达标；（b）稳态 objects > 0，即世界确实存在。
    只看 (a) 会把菜单/空图窗口误判进来。
    """
    n = len(rows)
    if n < FASTMODE_MIN_ROWS:
        return False
    if zero_render / float(n) < FASTMODE_ZERO_RENDER_FRAC:
        return False
    return steady_state([r.get("objects", 0) for r in rows]) > 0

# 2026-09-17 存档基准白名单（来自 _palace_handoff_20260917.json）。
# 12 份是基准；另 5 份是空图验证轮（无 AI 无战斗），不属于基准数据。
BASELINE_SAVE_12 = [
    "93909772_0", "93940344_1", "93989976_2", "94020321_3", "94050832_4",
    "94081642_5", "94112466_6", "94142549_7", "94173576_8", "94203654_9",
    "94233745_10", "94241284_11",
]
NON_BASELINE_VERIFY_5 = [
    "93453892_0", "93484290_1", "93600279_0", "93630983_1", "93661388_2",
]


def load(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    out = []
    for r in rows:
        d = {"frame": int(r["frame"]), "wall_ms": int(r["wall_ms"])}
        for k, v in r.items():
            if k.startswith("t_"):
                try:
                    d[k] = float(v)
                except (TypeError, ValueError):
                    pass
            elif k and k[0].isalpha() and k not in ("frame", "wall_ms"):
                try:
                    d[k] = int(v)
                except (TypeError, ValueError):
                    pass
        out.append(d)
    return out


def steady_state(vals):
    """对「大部分行为 0」的文件（游戏长期停在菜单/载入态）依然稳健的稳态估计。
    直接用中位数会在这种文件上得 0，从而漏判伪影 —— 实测 frameprobe_921168308_419
    这类 1359 行、median=0 的文件首行同样是累加值（objects 610160 vs 真实 ~2335）。"""
    pos = [v for v in vals if v > 0]
    if len(pos) >= 2:
        return st.median(pos)
    return 0.0


def detect_contaminated_first_row(rows):
    """与 spd_analyzer.py 同源的检测：ratio = 首行 / 本文件稳态 > 1.5 判污染。
    比值接近 0（菜单窗口，之前无窗口）不判 —— 该行本就不含累加量。"""
    if len(rows) < 3:
        return False, {}
    detail = {}
    hit = False
    for key in ("objects", "draw_calls"):
        vals = [r[key] for r in rows[1:] if key in r]
        if len(vals) < 2:
            continue
        base = steady_state(vals)
        if base <= 0:
            continue
        ratio = rows[0].get(key, 0) / base
        detail[key] = ratio
        if ratio > FIRST_ROW_CONTAM_RATIO:
            hit = True
    return hit, detail


def peak(rows, key):
    vals = [r[key] for r in rows if key in r]
    return max(vals) if vals else 0


def scan_file(path):
    rows = load(path)
    n = len(rows)
    if n == 0:
        return None
    contaminated, detail = detect_contaminated_first_row(rows)
    body = rows[1:] if contaminated else rows

    zero_render = sum(1 for r in rows if r.get("t_render", 0.0) == 0.0
                      and r.get("draw_calls", 0) == 0)
    fastmode = is_fastmode_recording(rows, zero_render)

    base = os.path.basename(path)
    stem = base.replace("frameprobe_", "").replace(".spd", "")
    return {
        "path": path,
        "name": base,
        "stem": stem,
        "n": n,
        "contaminated": contaminated,
        "detail": detail,
        # 无对照基线可比 = 判不了。典型是 n<3 的单行文件（整份就是那一个累加槽），
        # 以及 body 全为 0 的窗口。如实计数，别让报告静默掩盖覆盖缺口。
        "ambiguous": (not contaminated) and (not detail),
        "fastmode": fastmode,
        "zero_render_frac": zero_render / float(n),
        "is_baseline": stem in BASELINE_SAVE_12,
        "is_verify": stem in NON_BASELINE_VERIFY_5,
        # 剔除前（旧结论口径）
        "raw_peak_objects": peak(rows, "objects"),
        "raw_peak_draw": peak(rows, "draw_calls"),
        "raw_peak_total": peak(rows, "t_total"),
        # 剔除后（修正口径）
        "fix_peak_objects": peak(body, "objects"),
        "fix_peak_draw": peak(body, "draw_calls"),
        "fix_peak_total": peak(body, "t_total"),
        "fix_n": len(body),
    }


def fmt_table(recs):
    L = ["| 文件 | 帧 | 污染首帧 | 加速模式 | 峰值 draw_calls 剔除前→后 | 峰值 t_total 剔除前→后 | 标记 |",
         "|---|---|---|---|---|---|---|"]
    for r in sorted(recs, key=lambda x: x["stem"]):
        mark = []
        if r["is_baseline"]:
            mark.append("基准")
        if r["is_verify"]:
            mark.append("空图验证轮")
        L.append("| `%s` | %d | %s | %s | %d → **%d** | %.0f → **%.0f** | %s |" % (
            r["name"], r["n"],
            "是" if r["contaminated"] else "否",
            "**是**" if r["fastmode"] else "否",
            r["raw_peak_draw"], r["fix_peak_draw"],
            r["raw_peak_total"], r["fix_peak_total"],
            " ".join(mark) if mark else ""))
    return L


def main():
    args = [a for a in sys.argv[1:]]
    if not args or args[0] in ("-h", "--help"):
        print(__doc__)
        return 2

    src = args[0]
    pattern = "frameprobe_*.spd"
    out = "spd_rescan_report.md"
    budget = BUDGET_MS
    i = 1
    while i < len(args):
        if args[i] == "--glob" and i + 1 < len(args):
            pattern = args[i + 1]; i += 2
        elif args[i] == "--out" and i + 1 < len(args):
            out = args[i + 1]; i += 2
        elif args[i] == "--budget-ms" and i + 1 < len(args):
            budget = float(args[i + 1]); i += 2
        else:
            i += 1

    if not os.path.isdir(src):
        print("不是目录: %s" % src)
        return 1

    files = sorted(glob.glob(os.path.join(src, pattern)))
    if not files:
        print("目录下无匹配 %s 的文件: %s" % (pattern, src))
        return 1

    recs = []
    for f in files:
        try:
            r = scan_file(f)
        except Exception as e:
            print("跳过 %s: %s" % (os.path.basename(f), e))
            continue
        if r:
            recs.append(r)

    n_all = len(recs)
    n_contam = sum(1 for r in recs if r["contaminated"])
    n_ambig = sum(1 for r in recs if r["ambiguous"])
    n_fast = sum(1 for r in recs if r["fastmode"])
    baseline = [r for r in recs if r["is_baseline"]]
    baseline_fast = [r for r in baseline if r["fastmode"]]

    L = []
    L.append("# 历史 .spd 重扫报告（剔除 ring-slot-0 累加污染首帧）")
    L.append("")
    L.append("> 目录: `%s`  匹配: `%s`  预算: %.1f ms" % (src, pattern, budget))
    L.append("> 检测规则: 首行 counters / 本文件稳态(非零值中位) > %.1f 判为累加污染并剔除；"
             "加速模式: t_render 与 draw_calls 同时为 0 的帧占比 >= %.0f%%（n>=%d）"
             "**且稳态 objects > 0**（世界存在）——只凭占比会把菜单/空图窗口误判进来"
             % (FIRST_ROW_CONTAM_RATIO, FASTMODE_ZERO_RENDER_FRAC * 100, FASTMODE_MIN_ROWS))
    L.append("")
    L.append("## 总览")
    L.append("")
    L.append("| 项 | 值 |")
    L.append("|---|---|")
    L.append("| 扫描文件数 | %d |" % n_all)
    L.append("| 检出累加污染首帧（已剔除） | **%d** |" % n_contam)
    L.append("| 无法判定（无对照基线，**未剔除**） | **%d** |" % n_ambig)
    L.append("| 加速模式录制（B2 必须排除） | **%d** |" % n_fast)
    L.append("| 存档基准白名单命中 | %d / 12 |" % len(baseline))
    L.append("| 其中为加速模式 | **%d** |" % len(baseline_fast))
    L.append("")

    L.append("## 修正后的历史峰值（全目录）")
    L.append("")
    L.append("这是「旧结论 vs 剔除污染后」的直接对照 —— 旧结论里的峰值是窗口累加伪影。")
    L.append("")
    worst_raw_draw = max(r["raw_peak_draw"] for r in recs)
    worst_fix_draw = max(r["fix_peak_draw"] for r in recs)
    worst_raw_obj = max(r["raw_peak_objects"] for r in recs)
    worst_fix_obj = max(r["fix_peak_objects"] for r in recs)
    worst_raw_tot = max(r["raw_peak_total"] for r in recs)
    worst_fix_tot = max(r["fix_peak_total"] for r in recs)
    L.append("| 指标 | 剔除前峰值 | 剔除后峰值 |")
    L.append("|---|---|---|")
    L.append("| draw_calls | %d | **%d** |" % (worst_raw_draw, worst_fix_draw))
    L.append("| objects | %d | **%d** |" % (worst_raw_obj, worst_fix_obj))
    L.append("| t_total (ms) | %.0f | **%.0f** |" % (worst_raw_tot, worst_fix_tot))
    L.append("")

    if baseline_fast:
        L.append("## ⚠️ 存档基准白名单内的加速模式文件")
        L.append("")
        L.append("这些文件只渲染 1/30 帧（`W3DDisplay.cpp:1894` 的 `frame % 30 == 1` 门控），")
        L.append("`draw_calls`/`t_render` 大量为 0，**与普通模式文件不可比**。")
        L.append("任何跨全部基准的聚合都会被它们拉低；B2 三遍重复性验收必须确认加速模式关闭。")
        L.append("")
        L.append("| 文件 | 帧 | 渲染帧占比 |")
        L.append("|---|---|---|")
        for r in sorted(baseline_fast, key=lambda x: x["stem"]):
            L.append("| `%s` | %d | %.1f%% |" % (
                r["name"], r["n"], (1 - r["zero_render_frac"]) * 100))
        L.append("")

    L.append("## 逐份明细")
    L.append("")
    L += fmt_table(recs)
    L.append("")

    txt = "\n".join(L)
    with open(out, "w", encoding="utf-8") as f:
        f.write(txt)

    print("扫描 %d 份 | 污染首帧 %d | 无法判定 %d | 加速模式 %d | 基准命中 %d/12（其中加速模式 %d）"
          % (n_all, n_contam, n_ambig, n_fast, len(baseline), len(baseline_fast)))
    print("全目录峰值 draw_calls: %d → %d ; objects: %d → %d ; t_total: %.0f → %.0f ms"
          % (worst_raw_draw, worst_fix_draw, worst_raw_obj, worst_fix_obj,
             worst_raw_tot, worst_fix_tot))
    print("已写出 %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
