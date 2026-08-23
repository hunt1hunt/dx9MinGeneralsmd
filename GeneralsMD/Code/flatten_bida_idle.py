#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
flatten_bida_idle.py — 压平 AUAntiVehicleInfantry_BIDA 空闲动画的过量身体摆动。

背景(2026-08-23 诊断确认):
  BIDA 空闲 286 帧, 髋部(hips, p2)旋转相对 frame0 最大 27.4°、Z 平移起伏 1.55 单位、
  脊椎(spine, p3)侧倾 13.7° —— 远超正常空闲的 2-5°, 造成"站立不稳, 老是在晃动"。

本脚本把 hips(p2)与 spine(p3)的旋转和平移通道向各自的 frame0 压缩(保留 20% 的
相对运动 = 轻微呼吸感), 使士兵空闲时身体基本站稳。手臂/腿/筒等其余通道不动。

用法:
  python flatten_bida_idle.py --dir <Art/W3X 路径>
"""
import re, os, sys, shutil, math

DEFAULT_DIR = r"E:\!!!!!!!QWCSB\ART\W3X"
TARGET = "AUAntiVehicleInfantry_BIDA.W3X"
# 压平的身体骨骼: p2=hips(旋转+平移), p3=spine(旋转)
COMPRESS_PIVOTS = (2, 3)
COMPRESS_TRANS = (2,)   # hips 的 X/Y/Z 平移
RETAIN = 0.2            # 保留 20% 相对运动 (相对 frame0 压缩 80%)

def slerp(q0, q1, t):
    x0, y0, z0, w0 = q0
    x1, y1, z1, w1 = q1
    dot = x0*x1 + y0*y1 + z0*z1 + w0*w1
    if dot < 0.0:
        x1, y1, z1, w1, dot = -x1, -y1, -z1, -w1, -dot
    if dot > 0.9995:
        ox, oy, oz, ow = x0 + (x1-x0)*t, y0 + (y1-y0)*t, z0 + (z1-z0)*t, w0 + (w1-w0)*t
    else:
        th = math.acos(min(1.0, max(-1.0, dot)))
        s0 = math.sin((1.0-t)*th)/math.sin(th)
        s1 = math.sin(t*th)/math.sin(th)
        ox, oy, oz, ow = s0*x0+s1*x1, s0*y0+s1*y1, s0*z0+s1*z1, s0*w0+s1*w1
    L = math.sqrt(ox*ox+oy*oy+oz*oz+ow*ow)
    if L > 1e-9: return (ox/L, oy/L, oz/L, ow/L)
    return (0.0, 0.0, 0.0, 1.0)

def fix_file(fn):
    s = open(fn, 'r', encoding='utf-8', errors='replace').read()
    changed = 0
    # Rebuild whole channel bodies via regex on the whole file.
    # 1) Quaternion channels for COMPRESS_PIVOTS
    def rebuild_quat(m):
        pivot = int(re.search(r'Pivot="(\d+)"', m.group(1)).group(1))
        body = m.group(2)
        frames = re.findall(r'<Frame X="([-0-9.eE]+)"\s+Y="([-0-9.eE]+)"\s+Z="([-0-9.eE]+)"\s+W="([-0-9.eE]+)"/>', body)
        if pivot not in COMPRESS_PIVOTS or len(frames) < 2:
            return m.group(0)
        f0 = tuple(float(x) for x in frames[0])
        outs = []
        for i, f in enumerate(frames):
            q = tuple(float(x) for x in f)
            outs.append('<Frame X="%.6f" Y="%.6f" Z="%.6f" W="%.6f"/>' % slerp(f0, q, RETAIN))
        return '<ChannelQuaternion%s>%s</ChannelQuaternion>' % (m.group(1), ''.join(outs))
    s2, n1 = re.subn(
        r'(<ChannelQuaternion [^>]*Pivot="[23]"[^>]*>)(.*?)(</ChannelQuaternion>)',
        lambda m: rebuild_quat(m), s, flags=re.S)
    changed += n1
    s = s2
    # 2) Scalar (translation) channels for COMPRESS_TRANS pivots
    def rebuild_scalar(m):
        pivot = int(re.search(r'Pivot="(\d+)"', m.group(1)).group(1))
        body = m.group(2)
        vals = [float(x) for x in re.findall(r'<Frame>([-0-9.eE]+)</Frame>', body)]
        if pivot not in COMPRESS_TRANS or len(vals) < 2:
            return m.group(0)
        v0 = vals[0]
        outs = ''.join('<Frame>%.6f</Frame>' % (v0 + RETAIN*(v - v0) if i > 0 else v) for i, v in enumerate(vals))
        return '<ChannelScalar%s>%s</ChannelScalar>' % (m.group(1), outs)
    s, n2 = re.subn(
        r'(<ChannelScalar [^>]*Pivot="[2]"[^>]*>)(.*?)(</ChannelScalar>)',
        lambda m: rebuild_scalar(m), s, flags=re.S)
    changed += n2
    if changed == 0:
        print('OK   %s: no channels to compress' % fn)
        return False
    bak = fn + '.bak_bob'
    if not os.path.exists(bak):
        shutil.copy2(fn, bak)
        print('BACKUP -> %s' % bak)
    with open(fn, 'w', encoding='utf-8') as f:
        f.write(s)
    print('FIXED %s: %d channels compressed (retain %g of frame0 deviation)' % (fn, changed, RETAIN))
    return True

def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', default=DEFAULT_DIR)
    args = ap.parse_args()
    fn = os.path.join(args.dir, TARGET)
    if not os.path.isfile(fn):
        print('ERROR: missing %s' % fn); sys.exit(1)
    fix_file(fn)
    print('DONE')

if __name__ == '__main__':
    main()
