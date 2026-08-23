#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
fix_euteirockets_soft.py — 修正 EUTEIROCKETS 软蒙皮骨骼权重格式。

背景(2026-08-23 诊断确认):
  RA3 W3X 软蒙皮网格带两个 <BoneInfluences> 块,格式要求互补:
    block0 weight = w0 (主骨权重)
    block1 weight = w1 = 1 - w0   (次骨权重, 与主骨互补, w0+w1 == 1)
  对照正确网格 AUANTIVEHICLEINFANTRY01: block0={0.502,0.749,0.8,1.0},
  block1={0.0,0.2,0.251,0.498}, 每个顶点 w0+w1 == 1.0。
  EUTEIROCKETS(今天重新生成)把 block0 权重全写成 1.0, block1=0.05~0.5,
  w0+w1=1.2~1.4 —— 不互补。引擎 blendWeight = w1/(w0+w1) 因此偏低
  (0.31 而非 0.45), 膝关节混合不足 → 移动迈步时"纸片"拉伸。

本脚本把 block0 的 Weight 改为 1 - block1 的 Weight(同骨顶点则 w0=1,w1=0),
使 w0+w1 == 1, 与 AUANTIVEHICLEINFANTRY01 一致。骨骼索引不动。

用法:
  python fix_euteirockets_soft.py --dir <Art/W3X 路径>
处理网格: EUTEIROCKETS_SKN.SKIN_BODY01 / BODY02 / UP01 / WEAPON_A
"""
import re, os, sys, shutil

DEFAULT_DIR = r"E:\!!!!!!!QWCSB\ART\W3X"
MESHES = [
    "EUTEIROCKETS_SKN.SKIN_BODY01.w3x",
    "EUTEIROCKETS_SKN.SKIN_BODY02.w3x",
    "EUTEIROCKETS_SKN.SKIN_UP01.w3x",
    "EUTEIROCKETS_SKN.SKIN_WEAPON_A.w3x",
]

def fix_file(fn):
    s = open(fn, 'r', encoding='utf-8', errors='replace').read()
    # Find all <BoneInfluences> blocks
    blocks = list(re.finditer(r'<BoneInfluences>(.*?)</BoneInfluences>', s, re.S))
    if len(blocks) < 2:
        print('SKIP %s: only %d BoneInfluences block(s), not soft-skin' % (fn, len(blocks)))
        return False
    b0_text, b1_text = blocks[0].group(1), blocks[1].group(1)
    b0 = list(re.finditer(r'<I\s+Bone="(\d+)"\s+Weight="([-0-9.eE]+)"/>', b0_text))
    b1 = list(re.finditer(r'<I\s+Bone="(\d+)"\s+Weight="([-0-9.eE]+)"/>', b1_text))
    if len(b0) != len(b1):
        print('SKIP %s: block lengths differ (%d vs %d)' % (fn, len(b0), len(b1)))
        return False
    changed = 0
    out_entries = []
    for i in range(len(b0)):
        bone0 = int(b0[i].group(1)); w0 = float(b0[i].group(2))
        bone1 = int(b1[i].group(1)); w1 = float(b1[i].group(2))
        if bone0 == bone1:
            new_w0, new_w1 = 1.0, 0.0
        else:
            new_w0 = 1.0 - w1
            new_w1 = w1
        if abs(new_w0 - w0) > 1e-4 or abs(new_w1 - w1) > 1e-4:
            changed += 1
        out_entries.append((bone0, new_w0, bone1, new_w1))
    if changed == 0:
        print('OK   %s: weights already complementary (no change)' % fn)
        return False
    # Backup + rebuild block0 (keep block1 entries as-is; block0 weights recomputed)
    bak = fn + '.bak_softfix'
    if not os.path.exists(bak):
        shutil.copy2(fn, bak)
        print('BACKUP -> %s' % bak)
    new_b0 = ''.join('<I Bone="%d" Weight="%.6f"/>' % (e[0], e[1]) for e in out_entries)
    new_s = s[:blocks[0].start(1)] + new_b0 + s[blocks[0].end(1):]
    with open(fn, 'w', encoding='utf-8') as f:
        f.write(new_s)
    print('FIXED %s: %d/%d vertices' % (fn, changed, len(b0)))
    return True

def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', default=DEFAULT_DIR)
    args = ap.parse_args()
    if not os.path.isdir(args.dir):
        print('ERROR: dir not found: %s' % args.dir); sys.exit(1)
    any_fixed = False
    for m in MESHES:
        fn = os.path.join(args.dir, m)
        if not os.path.isfile(fn):
            print('MISSING %s' % fn); continue
        if fix_file(fn): any_fixed = True
    print('DONE. fixed=%s' % any_fixed)

if __name__ == '__main__':
    main()
