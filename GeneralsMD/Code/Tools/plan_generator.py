#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
plan_generator.py — SagePerfDiag 诊断计划生成器（P2/T9）
设计文档: Tools/PERF_DIAG_DESIGN.md §5 + §8 规则库

用法:
  python plan_generator.py <file.spd> [--budget-ms 33.3] [--out DIAG_PLAN.md]

输入: FrameProbe 落盘的 .spd 分帧轨迹(CSV)，与 spd_analyzer.py 同格式。
输出: DIAG_PLAN_<日期>.md —— 问题描述、证据(.spd 统计摘要)、嫌疑点(file:line)、
      修改方案、风险、验证方法。这份计划直接是下一轮代码修改的任务书。

规则库来源: PERF_DIAG_DESIGN.md §5 初版规则表 + §8 业界映射新增规则。
"""
import sys
import os
import csv
import statistics as st
import datetime

BUDGET_MS = 33.3

# 与 spd_analyzer.py 保持一致的阶段列（t_total 单独处理）
STAGE_COLS = [
    "t_radar", "t_audio", "t_client", "t_msg", "t_net", "t_logic",
    "t_net_wait", "t_render", "t_present", "t_postfx", "t_fps_spin",
    # T5 逻辑细分（P2 上线后存在）
    "t_logic_ai", "t_logic_pathfind", "t_logic_script",
    "t_logic_terrain", "t_logic_create", "t_logic_destroy",
    # T11 (2026-09-17): GameClient::update() 非 DRAW 余量 + W3DDisplay::draw()
    # 预渲染段。旧 .spd 无这些列，col() 取不到自然跳过。
    "t_client_input", "t_client_window", "t_client_ghost",
    "t_client_drawables", "t_client_terrain", "t_client_displupd",
    "t_client_strmgr", "t_client_shell", "t_client_ingameui",
    "t_draw_views", "t_draw_rttex",
    # T12 (2026-09-18): split of t_draw_rttex into water vs shadow
    "t_draw_rttex_water", "t_draw_rttex_shadow",
    # T13 (2026-09-18): split of t_postfx
    "t_postfx_ui", "t_postfx_debug", "t_postfx_misc",
]

# FrameProbe ring-slot-0 累加缺陷（2026-09-17 修复，见 FrameProbe.cpp:190）。
# 旧构建写出的每份 .spd，其**首行**是本窗口首帧 + 之前所有窗口首帧的累加和，
# 因而落在本文档稳态值的近似整数倍上（实测 0.00/0.98/1.98/2.98/3.97 = 累计窗口数）。
# 做的是检测而非无脑丢弃 —— 修复后文件的合法首行不该被丢掉。
FIRST_ROW_CONTAM_RATIO = 1.5


def steady_state(vals):
    """对「大部分行为 0」的文件（游戏长期停在菜单/载入态）依然稳健的稳态估计。
    直接用中位数会在这种文件上得 0，从而漏判伪影 —— 实测 frameprobe_921168308_419
    这类 1359 行、median=0 的文件首行同样是累加值（objects 610160 vs 真实 ~2335）。"""
    pos = [v for v in vals if v > 0]
    if len(pos) >= 2:
        return st.median(pos)
    return 0.0


def detect_contaminated_first_row(rows):
    """首行是否携带 ring-slot-0 累加伪影。只用 objects/draw_calls 两个有界稳态
    计数器；t_total 刻意排除，否则干净文件首帧的真实载入尖峰会误报。比值接近 0
    （菜单窗口，之前无任何窗口）不判污染 —— 正确，因为该行本就不含累加量。"""
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

COUNTER_COLS = ["objects", "drawables", "particles", "draw_calls", "state_changes"]


def load(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    out = []
    for r in rows:
        d = {"frame": int(r.get("frame", 0)), "wall_ms": int(r.get("wall_ms", 0))}
        for k, v in r.items():
            if k.startswith("t_"):
                try:
                    d[k] = float(v)
                except (TypeError, ValueError):
                    pass
            elif k in COUNTER_COLS:
                try:
                    d[k] = int(v)
                except (TypeError, ValueError):
                    pass
        out.append(d)
    return out


def pct(vals, p):
    vals = sorted(vals)
    if not vals:
        return 0.0
    i = min(len(vals) - 1, int(len(vals) * p / 100.0))
    return vals[i]


def col(rows, name):
    return [r[name] for r in rows if name in r]


def stats_of(vals):
    if not vals:
        return None
    return {
        "med": st.median(vals),
        "p95": pct(vals, 95),
        "max": max(vals),
        "n": len(vals),
    }


def spike_count(vals, times=3.0, floor_ms=50.0):
    """卡顿事件: > 滑动均值*times 且 > floor_ms。简化版按全局中位数*times。"""
    if len(vals) < 10:
        return 0
    base = st.median(vals)
    if base <= 0:
        base = 1.0
    return sum(1 for v in vals if v > base * times and v > floor_ms)


# ---------------------------------------------------------------------------
# 规则库。每条: (名称, 条件(evidence)->bool, 嫌疑点/动作)
# ---------------------------------------------------------------------------
RULES = [
    {
        "id": "R1",
        "name": "渲染段 P95 高 + draw_calls 高",
        "test": lambda e: e["t_render"] and e["t_render"]["p95"] > e["budget_ms"] * 0.5
                          and e["draw_calls"] and e["draw_calls"]["med"] > 3000,
        "suspects": [
            "GameEngineDevice/Source/W3DDevice/GameClient/Drawable/Draw/W3DDrawModule.cpp — 逐 Drawable 循环提交",
            "GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DShadow.cpp — 阴影逐 pass 重绘",
            "GameEngineDevice/Source/W3DDevice/GameClient/W3DScene.cpp — 排序/提交循环",
        ],
        "action": "合批与剔除审查；阴影 pass 频率（参照 commit 809f3845 先例：无消费者的 pass 直接关）。",
    },
    {
        "id": "R2",
        "name": "阴影 pass 突增（若列存在）",
        "test": lambda e: e.get("t_shadow_pass") and e["t_shadow_pass"]["p95"] > 5.0,
        "suspects": [
            "GameEngineDevice/Source/W3DDevice/GameClient/W3XRenderObj.cpp:1581-1856 _CreateShadowMap / 相机跟随窗口",
        ],
        "action": "复用阴影图、降低重绘频率。注意：W3XRenderObj.cpp 是活跃热区，动手前先与 W3X 任务协调（看板硬规则 #6）。",
    },
    {
        "id": "R3",
        "name": "逻辑段高",
        "test": lambda e: e["t_logic"] and e["t_logic"]["p95"] > e["budget_ms"] * 0.6,
        "suspects": [
            "GameEngine/Source/GameLogic/System/GameLogic.cpp:3947 起各 update",
            "GameEngine/Source/GameLogic/AI/AIGuard.cpp / Pathfind.cpp",
        ],
        "action": "AI 节流、寻路缓存；若 t_logic_ai/t_logic_pathfind 列存在，按其占比先打细分再动手。",
    },
    {
        "id": "R4",
        "name": "锁步等待高",
        "test": lambda e: e["t_net_wait"] and e["t_net_wait"]["med"] > e["budget_ms"] * 0.3,
        "suspects": [
            "GameEngine/Source/Common/Network/Network.cpp:807 isFrameDataReady",
        ],
        "action": "非本机性能问题 → 转网络诊断轨道，不要优化本地渲染。",
    },
    {
        "id": "R5",
        "name": "粒子突变伴随尖峰",
        "test": lambda e: e["particles"] and e["t_total"]
                          and (max(e["particles"]["max"], 1) >= max(e["particles"]["med"], 1) * 2)
                          and spike_count(e["t_total"]["vals"], 3.0, 50.0) > 0,
        "suspects": [
            "GameEngineDevice/Source/W3DDevice/GameClient/ParticleSys.cpp",
        ],
        "action": "粒子上限/LOD；检查爆炸/烟雾叠加场景的粒子预算。",
    },
    {
        "id": "R6",
        "name": "固定开销大（与场景无关）",
        "test": lambda e: e["t_fps_spin"] and e["t_fps_spin"]["med"] > 5.0,
        "suspects": [
            "GameEngine/Source/Common/GameEngine.cpp:905-918 自旋 Sleep(0) 帧率限制",
        ],
        "action": "改 waitable timer 释放 CPU。低风险、收益直接。",
    },
    {
        "id": "R7",
        "name": "wrapper 状态变更计数高",
        "test": lambda e: e["state_changes"] and e["state_changes"]["med"] > 2000,
        "suspects": [
            "GameEngineDevice/Source/W3DDevice/GameClient/DX8Wrapper 差量推送失效点（参照 0fb2413a 攻防合一套路）",
        ],
        "action": "用计数数据定位\"每帧全量重推\"热点；修差量推送失效。",
    },
    {
        "id": "R8",
        "name": "诊断残留自污染（周期性停顿）",
        "test": lambda e: e["t_total"] and spike_count(e["t_total"]["vals"], 3.0, 50.0) > 0
                          and e["t_total"]["med"] < e["budget_ms"],
        "suspects": [
            "热路径残留 fopen 打点（terrain_diag.log / W3XShadowDiag 打点，历史已发生两次）",
        ],
        "action": "grep 热路径 fopen；拔除或迁入 FrameProbe 内存写。验收: grep fopen=0（热路径）。",
    },
    {
        "id": "R9",
        "name": "对象/可绘制数随负载缩放差",
        "test": lambda e: e["objects"] and e["t_logic"]
                          and e["objects"]["med"] > 20000 and e["t_logic"]["med"] > e["budget_ms"] * 0.8,
        "suspects": [
            "每对象逻辑开销（T5 细分后定位具体子系统）；模块链遍历、字符串查找",
        ],
        "action": "26 万对象时 t_logic 独占 99% 是已知数据点；按 T5 细分先定位，再考虑编译期固化决策。",
    },
]


def build_evidence(rows, budget_ms):
    e = {"budget_ms": budget_ms, "n": len(rows)}
    totals = col(rows, "t_total")
    e["t_total"] = stats_of(totals)
    if totals:
        e["t_total"]["vals"] = totals
        e["over_budget_pct"] = 100.0 * sum(1 for t in totals if t > budget_ms) / len(totals)
    for c in STAGE_COLS:
        v = col(rows, c)
        if v:
            s = stats_of(v)
            s["vals"] = v
            e[c] = s
    for c in COUNTER_COLS:
        v = col(rows, c)
        if v:
            e[c] = stats_of(v)
    return e


def generate_plan(path, budget_ms, drop_first="auto"):
    """drop_first: 'auto'（检测 ring-slot-0 伪影）/ 'always' / 'never'。"""
    rows = load(path)
    n_raw = len(rows)
    dropped = False
    if rows and drop_first != "never":
        dropped = True if drop_first == "always" else detect_contaminated_first_row(rows)
        if dropped:
            rows = rows[1:]
    e = build_evidence(rows, budget_ms)
    e["first_row_dropped"] = dropped
    e["n_raw"] = n_raw

    lines = []
    lines.append("# DIAG_PLAN %s" % datetime.date.today().isoformat())
    lines.append("")
    lines.append("> 自动生成: `python plan_generator.py %s`" % path)
    lines.append("> 设计文档: `Tools/PERF_DIAG_DESIGN.md` §5/§8")
    if dropped:
        lines.append("> ⚠️ 已剔除污染首帧（ring-slot-0 累加伪影）：计入 %d 帧 / 原始 %d 帧，"
                     "max/峰值/卡顿聚类结论已不含该伪影。" % (len(rows), n_raw))
    else:
        lines.append("> 含首帧（未检出累加污染）：计入 %d 帧。" % len(rows))
    lines.append("")
    lines.append("## 1. 问题描述")
    lines.append("")
    lines.append("基于 `.spd` 统计自动命中规则，逐条列出嫌疑点与建议动作。")
    lines.append("")
    lines.append("## 2. 证据摘要（%s，%d 帧，预算 %.1f ms）" % (os.path.basename(path), e["n"], budget_ms))
    lines.append("")
    lines.append("| 指标 | 中位 | P95 | 最大 |")
    lines.append("|---|---|---|---|")
    for name in ["t_total"] + STAGE_COLS:
        if e.get(name):
            s = e[name]
            lines.append("| %s | %.2f ms | %.2f ms | %.2f ms |" % (name, s["med"], s["p95"], s["max"]))
    for name in COUNTER_COLS:
        if e.get(name):
            s = e[name]
            lines.append("| %s | %d | %d | %d |" % (name, int(s["med"]), int(s["p95"]), int(s["max"])))
    if "over_budget_pct" in e:
        lines.append("")
        lines.append("超预算帧占比: %.1f%%" % e["over_budget_pct"])
    lines.append("")

    hit = 0
    for rule in RULES:
        try:
            ok = rule["test"](e)
        except (KeyError, TypeError, ZeroDivisionError):
            ok = False
        if not ok:
            continue
        hit += 1
        lines.append("## %d. [%s] %s" % (hit + 2, rule["id"], rule["name"]))
        lines.append("")
        lines.append("嫌疑点:")
        for s in rule["suspects"]:
            lines.append("- `%s`" % s)
        lines.append("")
        lines.append("建议动作: %s" % rule["action"])
        lines.append("")

    if hit == 0:
        lines.append("## 3. 未命中规则")
        lines.append("")
        lines.append("当前 .spd 未命中预置规则。若仍觉得卡顿，检查: ①预算参数 `--budget-ms`；")
        lines.append("②是否缺少列（T5/T7 未上线前无逻辑细分/GPU 列）；③人工看 spd_analyzer 瀑布图。")
        lines.append("")

    lines.append("## 修改方案与验证")
    lines.append("")
    lines.append("1. 按嫌疑点顺序，优先处理**低风险、可 A/B 验证**的项（如 R6 waitable timer、R8 拔打点）。")
    lines.append("2. 每项修改前用 CodeGraph（codegraph_explore）复核影响面。")
    lines.append("3. 验证: 同一地图/同一固定种子场景，`bench_capture.ps1` 录改前/改后 .spd，")
    lines.append("   `spd_analyzer.py old.spd new.spd` 出 A/B 表，确认无回归后再提交。")
    lines.append("4. 性能相关提交附 .spd 摘要（看板硬规则 #7）。")
    lines.append("")
    lines.append("## 风险")
    lines.append("")
    lines.append("- 只做诊断指认的优化，不做画质/玩法有损改动。")
    lines.append("- 避开活跃热区 `W3XRenderObj.cpp`（看板硬规则 #6）。")
    lines.append("- 遵守 VC6 约束与单管线构建纪律。")
    lines.append("")
    return "\n".join(lines)


def main():
    args = [a for a in sys.argv[1:]]
    if not args or args[0] in ("-h", "--help"):
        print(__doc__)
        return 2
    path = args[0]
    budget = BUDGET_MS
    out = None
    drop_first = "auto"
    i = 1
    while i < len(args):
        if args[i] == "--budget-ms" and i + 1 < len(args):
            budget = float(args[i + 1]); i += 2
        elif args[i] == "--drop-first" and i + 1 < len(args):
            drop_first = args[i + 1]; i += 2
        elif args[i] == "--out" and i + 1 < len(args):
            out = args[i + 1]; i += 2
        else:
            i += 1
    if drop_first not in ("auto", "always", "never"):
        print("--drop-first 取值非法: %r（应为 auto/always/never）" % drop_first)
        return 2
    if not os.path.isfile(path):
        print("找不到 .spd 文件: %s" % path)
        return 1
    md = generate_plan(path, budget, drop_first)
    if not out:
        out = "DIAG_PLAN_%s.md" % datetime.datetime.now().strftime("%Y%m%d_%H%M")
    with open(out, "w", encoding="utf-8") as f:
        f.write(md)
    print("已生成: %s" % out)
    print(md[:800])
    return 0


if __name__ == "__main__":
    sys.exit(main())
