#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
assert_reader.py — 断言弹窗自动读取与分析工具 (SagePerfDiag 配套)
用法:
  python assert_reader.py            读取当前所有弹窗(断言/崩溃/异常), 解析并给出源码定位
  python assert_reader.py --watch    持续监控, 每2秒扫一次, 发现新弹窗即分析
  python assert_reader.py --shot out.png   同时截全屏存档
原理: Win32 弹窗(Assertion Failure / Technical Difficulties 等)是标准对话框,
      用 ctypes 枚举顶层窗口+子控件 WM_GETTEXT 提取文字, 无需 OCR, 100% 精准。
解析: 提取 "ASSERTION FAILURE: <msg>, <path>, <line>" 三元组, 自动:
      1) 打印源码上下文(该行前后各6行, 若文件存在)
      2) 记录到 E:\\!!!!!!!QWCSB\\assert_log.txt 供事后分析
"""
import ctypes, ctypes.wintypes as wt
import os, re, sys, time, subprocess

GAME_DIR = r"E:\!!!!!!!QWCSB"
LOG_PATH = os.path.join(GAME_DIR, "assert_log.txt")
DIALOG_TITLES = ("Assertion Failure", "Technical Difficulties", "Assertion",
                 "错误", "Microsoft Visual C++ Runtime Library")

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

WM_GETTEXT = 0x000D
WM_GETTEXTLENGTH = 0x000E
GW_CHILD = 5

SMTO_ABORTIFHUNG = 0x0002

def _send_msg_timeout(hwnd, msg, wparam, lparam, timeout_ms=300):
    # SendMessageW can block forever on a hung window; always use the timeout variant
    res = ctypes.c_ulong()
    ok = user32.SendMessageTimeoutW(hwnd, msg, wparam, lparam,
                                    SMTO_ABORTIFHUNG, timeout_ms, ctypes.byref(res))
    return ok, res.value

def get_wtext(hwnd):
    ok, n = _send_msg_timeout(hwnd, WM_GETTEXTLENGTH, 0, 0)
    if not ok or n <= 0:
        return ""
    buf = ctypes.create_unicode_buffer(n + 1)
    ok, _ = _send_msg_timeout(hwnd, WM_GETTEXT, n + 1, buf)
    return buf.value if ok else ""

def enum_child_texts(hwnd):
    texts = []
    child = user32.GetWindow(hwnd, GW_CHILD)
    # GW_HWNDFIRST=5 is GW_CHILD; iterate GW_HWNDNEXT=2
    GW_HWNDNEXT = 2
    while child:
        t = get_wtext(child)
        if t.strip():
            texts.append(t.strip())
        child = user32.GetWindow(child, GW_HWNDNEXT)
    return texts

def _class_of(hwnd):
    buf = ctypes.create_unicode_buffer(64)
    user32.GetClassNameW(hwnd, buf, 64)
    return buf.value

results = []
@ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
def _cb(hwnd, lparam):
    if not user32.IsWindowVisible(hwnd):
        return True
    title = get_wtext(hwnd)
    cls = _class_of(hwnd)
    # 2026-09-14: 标准Win32对话框的窗口类是 #32770 — 有些断言确认框(如
    # "Ignore this crash from now on?")没有标题, 按类识别才不会漏。
    is_dialog_class = (cls == "#32770")
    is_titled_dialog = bool(title) and any(k in title for k in DIALOG_TITLES)
    if is_dialog_class or is_titled_dialog:
        results.append((hwnd, title or "(无标题对话框)", enum_child_texts(hwnd)))
    return True

def scan_dialogs():
    global results
    results = []
    user32.EnumWindows(_cb, 0)
    return results

ASSERT_RE = re.compile(
    r"ASSERTION FAILURE:\s*(?P<msg>.+?),\s*(?P<path>[A-Za-z]:\\[^,]+?),\s*(?P<line>\d+)", re.S)

def show_source_context(path, line, ctx=6):
    if not os.path.isfile(path):
        # try repo-relative
        alt = os.path.join(r"E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code",
                           path.replace("\\", "/").split("GeneralsMD/Code/")[-1])
        path = alt if os.path.isfile(alt) else path
    if not os.path.isfile(path):
        print(f"      (源文件不存在: {path})")
        return
    lo, hi = max(1, line - ctx), line + ctx
    try:
        lines = open(path, encoding="latin-1", errors="replace").read().splitlines()
    except OSError as e:
        print(f"      (读失败: {e})"); return
    print(f"      --- {path}:{line} 上下文 ---")
    for i in range(lo, min(hi, len(lines)) + 1):
        mark = ">>" if i == line else "  "
        print(f"      {mark}{i:5d}: {lines[i-1].rstrip()}")

def analyze(dialogs, log=True):
    entries = []
    for hwnd, title, texts in dialogs:
        body = "\n".join(texts)
        print(f"\n=== 弹窗: {title} (hwnd={hwnd}) ===")
        print(f"    正文: {body[:400]}")
        m = ASSERT_RE.search(body)
        if m:
            msg, path, line = m.group("msg").strip(), m.group("path").strip(), int(m.group("line"))
            print(f"    [解析] 断言: {msg}")
            print(f"    [解析] 位置: {path}:{line}")
            show_source_context(path, line)
            entries.append((time.strftime("%F %T"), title, msg, path, line))
        else:
            entries.append((time.strftime("%F %T"), title, body[:200], "", 0))
    if log and entries:
        with open(LOG_PATH, "a", encoding="utf-8") as f:
            for e in entries:
                f.write(" | ".join(str(x) for x in e) + "\n")
        print(f"\n已记录到 {LOG_PATH}")
    return entries

def screenshot(path):
    try:
        subprocess.run(["powershell", "-NoProfile", "-Command",
            "Add-Type -AssemblyName System.Windows.Forms,System.Drawing;"
            "$b=[System.Windows.Forms.SystemInformation]::VirtualScreen;"
            "$bmp=New-Object System.Drawing.Bitmap $b.Width,$b.Height;"
            "$g=[System.Drawing.Graphics]::FromImage($bmp);"
            "$g.CopyFromScreen($b.X,$b.Y,0,0,$bmp.Size);"
            f"$bmp.Save('{path}');Write-Output ok"], capture_output=True, timeout=20)
    except Exception as e:
        print("截图失败:", e)

def click_ignore(hwnd):
    """点击弹窗按钮, 覆盖断言弹窗的完整三段流程:
      1) 点两次 忽略/Ignore (内部状态机需要第二下确认)
      2) 弹出'继续将导致不可预测的行为'确认框 -> 点 是(&Y)
      3) 崩溃弹窗(Technical Difficulties) -> 点 确定/OK
    每轮扫描都会重试, 上一轮未生效的按钮下一轮补点。"""
    BM_CLICK = 0xF5
    clicked = False
    child = user32.GetWindow(hwnd, GW_CHILD)
    GW_HWNDNEXT = 2
    while child:
        t = get_wtext(child)
        if t and ("忽略" in t or "Ignore" in t):
            _send_msg_timeout(child, BM_CLICK, 0, 0)
            time.sleep(0.2)
            _send_msg_timeout(child, BM_CLICK, 0, 0)
            clicked = True
        elif t and ("是" in t or "Yes" in t):
            _send_msg_timeout(child, BM_CLICK, 0, 0)
            clicked = True
        elif t and ("确定" in t or t.strip() == "OK"):
            _send_msg_timeout(child, BM_CLICK, 0, 0)
            clicked = True
        child = user32.GetWindow(child, GW_HWNDNEXT)
    return clicked

def main():
    args = sys.argv[1:]
    if "--shot" in args:
        screenshot(args[args.index("--shot") + 1])
    auto_ignore = "--auto-ignore" in args
    if "--watch" in args or auto_ignore:
        seen = set()
        print("监控中 (Ctrl+C 退出)..." + (" [自动点击忽略x2]" if auto_ignore else ""))
        while True:
            for hwnd, title, texts in scan_dialogs():
                key = (title, "\n".join(texts)[:120])
                if key not in seen:
                    seen.add(key)
                    analyze([(hwnd, title, texts)])
                if auto_ignore and click_ignore(hwnd):
                    print("    -> 已自动点击 [忽略]x2")  # 每轮都点, 防第一轮未生效
            time.sleep(2)
    else:
        ds = scan_dialogs()
        if not ds:
            print("当前无断言/错误弹窗。")
        analyze(ds)
        if auto_ignore:
            for hwnd, title, texts in ds:
                if click_ignore(hwnd):
                    print(f"    -> 已自动点击 [{title}] 的 [忽略]")

if __name__ == "__main__":
    main()
