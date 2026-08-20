#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
w3x_anim_merge.py — 把 JUANTI 家族的真实腿部动画 remap 到 AU AntiVehicleInfantry 骨架,
保留 AU 自己的手臂/火箭筒通道(火箭筒握在手), 输出合法的 W3DAnimation .w3x。

背景(2026-08-20 诊断确认):
  AUAntiVehicleInfantry_SBIDA/SBIDB/SATEA/SATKZ 是坏导出 —— 腿通道恒定折叠(右腿折到腰高 Z≈9.8),
  4 个动画腿完全相同, "跑"其实是定格姿势只有火箭筒在动。
  引擎合成(权威 raw channel×bind)已被 JUANTIINFANTRYINFANTRY 真实动画证明正确, 不能动。
  同一单位族 JUANTIINFANTRYINFANTRY_SKL 的 body 骨骼与 AU 逐骨一致(仅 AU 多一个 rocket 骨, 索引差 1),
  其 SBIDB/RUNA 等有真实腿部运动(448/48 帧, 脚保持落地)。

本脚本把 JUANTI 动画的腿骨通道(索引-1)合并进 AU 动画:
  腿(14-19)       <- JUANTI (remap: AU_idx = JUANTI_idx + 1), 保留全部关键帧
  躯干/手臂/火箭筒 <- AU 自身通道(帧0冻结, 保持筒握在手、零漂移)

用法:
  python w3x_anim_merge.py --juanim JUANTIINFANTRYINFANTRY_RUNA.w3x \
      --auanim AUAntiVehicleInfantry_SBIDB.W3X \
      --out AUAntiVehicleInfantry_SBIDB.W3X \
      --dir <Art/W3X 路径> --bak

输出: 合法 W3DAnimation XML(ChannelQuaternion + ChannelScalar), 引擎 ParseAnimation 可直接读。
"""
import re, sys, os, shutil

DEFAULT_DIR = r"E:\!!!!!!!QWCSB\!!!!!!!QWCSB\ART\W3X"
HIERARCHY = "AUANTIVEHICLEINFANTRY_SKL"
AU_LEG_PIVOTS = (14, 15, 16, 17, 18, 19)      # AU leftupleg..rightfoot
# 每项: (AU pivot, JUANTI pivot) —— JUANTI 索引 +1 (AU 有 rocket 骨在索引1)
AU_LEG_MAP = {14: 13, 15: 14, 16: 15, 17: 16, 18: 17, 19: 18}
# 从 AU 冻结的通道(保持火箭筒握在手 + 躯干姿势)。AU SBIDB 实际有通道的 pivot。
AU_FROZEN_PIVOTS = (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 20)

def parse_anim(fn):
    """返回 {pivot: {'q':[frames..], 'X':[..],'Y':[..],'Z':[..]}}, numFrames, frameRate"""
    s = open(fn, 'r', encoding='utf-8', errors='replace').read()
    ch = {}
    m = re.search(r'<W3DAnimation\b([^>]*)>', s)
    attrs = m.group(1) if m else ''
    nf = int(re.search(r'NumFrames="(\d+)"', attrs).group(1))
    fr = int(re.search(r'FrameRate="(\d+)"', attrs).group(1)) if 'FrameRate' in attrs else 30
    for om in re.finditer(r'<Channel(?:Quaternion|Scalar)\b([^>]*)>(.*?)</Channel\w+>', s, re.S):
        attrs, body = om.group(1), om.group(2)
        p = int(re.search(r'Pivot="(\d+)"', attrs).group(1))
        ty = re.search(r'Type="([^"]*)"', attrs).group(1)
        if 'Quaternion' in om.group(0):
            fs = re.findall(r'X="([-0-9.eE]+)"\s+Y="([-0-9.eE]+)"\s+Z="([-0-9.eE]+)"\s+W="([-0-9.eE]+)"', body)
            ch.setdefault(p, {})['q'] = [[float(x) for x in f] for f in fs]
        else:
            axis = {'XTranslation': 'X', 'YTranslation': 'Y', 'ZTranslation': 'Z'}.get(ty, ty)
            ch.setdefault(p, {})[axis] = [float(x) for x in re.findall(r'<Frame>([-0-9.eE]+)</Frame>', body)]
    return ch, nf, fr

def val_at(frames, t):
    """t in [0,1] -> 采样帧值(循环/最近)"""
    if not frames:
        return None
    i = int(t * (len(frames) - 1))
    return frames[i]

def build_merged(ju, au, out_nf, src_nf):
    """返回 {AU_pivot: {'q':[N..], 'X':[N..], 'Y':[N..], 'Z':[N..]}}
    out_nf = 输出帧数; src_nf = JUANTI 源帧数(用于时间归一化; 若 --frames 给出则重采样)。"""
    merged = {}
    def put(ap, key, val):
        merged.setdefault(ap, {}).setdefault(key, []).append(val)
    for t_i in range(out_nf):
        t = t_i / (out_nf - 1) if out_nf > 1 else 0.0
        # 腿: 来自 JUANTI (remap), 按源帧数归一化采样
        for ap, jp in AU_LEG_MAP.items():
            jc = ju.get(jp)
            if not jc:
                continue
            qv = val_at(jc.get('q'), t)
            if qv:
                put(ap, 'q', qv)
            for ax in 'XYZ':
                v = val_at(jc.get(ax), t)
                if v is not None:
                    put(ap, ax, v)
        # 躯干/手臂/火箭筒: AU 帧0冻结
        for ap in AU_FROZEN_PIVOTS:
            ac = au.get(ap)
            if not ac:
                continue
            qv = ac.get('q')
            if qv:
                put(ap, 'q', qv[0])
            for ax in 'XYZ':
                v = ac.get(ax)
                if v:
                    put(ap, ax, v[0])
    return merged

def fmt_q(v):
    return '<Frame X="%.6f" Y="%.6f" Z="%.6f" W="%.6f"/>' % (v[0], v[1], v[2], v[3])

def write_anim(fn, anim_id, merged, out_nf, frame_rate=30):
    # 结构与原 RA3 文件完全一致: AssetDeclaration > Includes > W3DAnimation > Channels。
    # 引擎 ParseAnimation 用 pugi 按此结构遍历 (assetDecl.child("W3DAnimation").child("Channels"))。
    lines = []
    lines.append('<?xml version="1.0" encoding="UTF-8"?>')
    lines.append('<AssetDeclaration xmlns="uri:ea.com:eala:asset" '
                 'xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">')
    lines.append('\t<Includes>')
    lines.append('\t\t<Include type="all" source="ART:%s.w3x"/>' % HIERARCHY.lower())
    lines.append('\t</Includes>')
    lines.append('\t<W3DAnimation id="%s" Hierarchy="%s" NumFrames="%d" FrameRate="%d">'
                 % (anim_id, HIERARCHY, out_nf, frame_rate))
    lines.append('\t\t<Channels>')
    for ap in sorted(merged.keys()):
        c = merged[ap]
        if 'q' in c:
            lines.append('\t\t\t<ChannelQuaternion Pivot="%d" Type="Orientation" FirstFrame="0">' % ap)
            for q in c['q']:
                lines.append('\t\t\t\t' + fmt_q(q))
            lines.append('\t\t\t</ChannelQuaternion>')
        for ax, atag in (('X', 'XTranslation'), ('Y', 'YTranslation'), ('Z', 'ZTranslation')):
            if ax in c:
                lines.append('\t\t\t<ChannelScalar Pivot="%d" Type="%s" FirstFrame="0">' % (ap, atag))
                for v in c[ax]:
                    lines.append('\t\t\t\t<Frame>%.6f</Frame>' % v)
                lines.append('\t\t\t</ChannelScalar>')
    lines.append('\t\t</Channels>')
    lines.append('\t</W3DAnimation>')
    lines.append('</AssetDeclaration>')
    with open(fn, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print('WROTE %s (%d frames, %d bones)' % (fn, out_nf, len(merged)))

def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('--juanim', required=True, help='JUANTI leg-source animation (.w3x)')
    ap.add_argument('--auanim', required=True, help='AU torso/weapon animation (.W3X)')
    ap.add_argument('--out', required=True, help='output .w3x filename')
    ap.add_argument('--dir', default=DEFAULT_DIR)
    ap.add_argument('--frames', type=int, default=0,
                    help='output frame count (default = JUANTI source frame count; '
                         'legs are resampled by ratio, so this speeds up/slows the cycle)')
    ap.add_argument('--bak', action='store_true', help='backup existing output to .bak')
    args = ap.parse_args()
    if not os.path.isdir(args.dir):
        print('ERROR: dir not found: %s' % args.dir); sys.exit(1)
    ju_path = os.path.join(args.dir, args.juanim)
    au_path = os.path.join(args.dir, args.auanim)
    out_path = os.path.join(args.dir, args.out)
    for p in (ju_path, au_path):
        if not os.path.isfile(p):
            print('ERROR: missing %s' % p); sys.exit(1)
    ju, ju_nf, ju_fr = parse_anim(ju_path)
    au, au_nf, au_fr = parse_anim(au_path)
    out_nf = args.frames if args.frames > 0 else ju_nf
    print('JUANTI %s: %d frames' % (args.juanim, ju_nf))
    print('AU     %s: %d frames' % (args.auanim, au_nf))
    print('OUT    %s: %d frames%s' % (args.out, out_nf,
        ' (resampled from %d)' % ju_nf if out_nf != ju_nf else ''))
    merged = build_merged(ju, au, out_nf, ju_nf)
    anim_id = re.sub(r'\.w3x$', '', args.out, flags=re.I)
    if args.bak and os.path.isfile(out_path):
        bak = out_path + '.bak_merge'
        shutil.copy2(out_path, bak)
        print('BACKUP -> %s' % bak)
    write_anim(out_path, anim_id, merged, out_nf, ju_fr)
    # 校验: 重新解析输出, 检查每骨帧数一致性
    ch2, nf2, fr2 = parse_anim(out_path)
    assert nf2 == out_nf, 'frame count mismatch'
    for p, c in merged.items():
        n = len(c.get('q', []))
        if n: assert n == out_nf, 'bone %d quat frames %d != %d' % (p, n, out_nf)
    print('OK: output re-parses cleanly (%d frames, %d channels)' % (nf2, len(ch2)))

if __name__ == '__main__':
    main()
