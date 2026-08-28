#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
w3x_convert.py — convert an RA3 / "将军2" source model into the Generals W3X
game format, written into a per-faction sub-directory under Art/W3X/.

The RA3 source uses combined files:
    <Model>_SKN.W3X   holds the W3DContainer AND every W3DMesh in ONE file
    <Model>_SKL.W3X   the skeleton (W3DHierarchy)
    <Model>_<ANIM>.W3X  animations (W3DAnimation), doors, build/produce, etc.
The Generals format needs one file per mesh / container / skeleton / animation,
with the texture .dds + .xml files beside them. The source mesh/container ids
are already the uppercase game names (e.g. APAWARFACTORY_SKN.SKIN_BODY03), so
the conversion is: extract each top-level element verbatim, wrap it in an
AssetDeclaration, name the file by its id, and copy textures.

Usage:
  python w3x_convert.py <src_dir> <model> <out_dir>
    src_dir   source ART folder holding <Model>_SKN.W3X etc. (e.g. D:/.../ART/AP)
    model     model base name, e.g. APAWarFactory
    out_dir   game sub-dir to write to, e.g. E:/!!!!!!!QWCSB/ART/W3X/AP
"""
import os
import re
import sys
import shutil

DECL = ('<?xml version="1.0" encoding="UTF-8"?>\n'
        '<AssetDeclaration xmlns="uri:ea.com:eala:asset" '
        'xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">\n')


def read_utf8(path):
    with open(path, 'rb') as f:
        return f.read().decode('utf-8', errors='replace')


def extract_tags(data, tag):
    """Return the raw text of every top-level <tag ...>...</tag> element."""
    out = []
    for m in re.finditer(r'<' + tag + r'\b', data):
        start = m.start()
        em = re.search(r'</' + tag + r'>', data[start:])
        if em:
            out.append(data[start:start + em.end()])
    return out


def wrap(content):
    return DECL + '\t' + content.replace('\n', '\n\t').rstrip() + '\n</AssetDeclaration>\n'


def merge_element(path, content):
    """Insert a top-level XML element into an existing AssetDeclaration file
    (used when a skeleton/animation id collides with the container id, e.g.
    walls whose hierarchy id == container id: one file serves both roles)."""
    with open(path, 'r', encoding='utf-8') as f:
        data = f.read()
    close_tag = '</AssetDeclaration>'
    if close_tag in data:
        data = data.replace(close_tag,
                            '\t' + content.replace('\n', '\n\t').rstrip() + '\n' + close_tag)
    else:
        data = data.rstrip() + '\n\t' + content.replace('\n', '\n\t').rstrip() + '\n' + close_tag
    with open(path, 'w', encoding='utf-8') as f:
        f.write(data)


def attr(text, name):
    m = re.search(name + r'="([^"]*)"', text)
    return m.group(1) if m else None


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(2)
    src_dir = sys.argv[1]
    model = sys.argv[2]
    out_dir = sys.argv[3]

    os.makedirs(out_dir, exist_ok=True)

    skn = os.path.join(src_dir, model + '_SKN.W3X')
    if not os.path.isfile(skn):
        print('ERROR: no %s (looking for %s)' % (model + '_SKN.W3X', skn))
        sys.exit(1)

    data = read_utf8(skn)

    # ---- meshes + container ----
    meshes = extract_tags(data, 'W3DMesh')
    containers = extract_tags(data, 'W3DContainer')
    if not containers:
        print('ERROR: no <W3DContainer> in %s' % skn)
        sys.exit(1)
    container = containers[0]

    game_model = attr(container, 'id')
    if not game_model:
        print('ERROR: container has no id'); sys.exit(1)
    hierarchy = attr(container, 'Hierarchy')
    print('model=%s hierarchy=%s meshes=%d' % (game_model, hierarchy, len(meshes)))

    # container file
    cpath = os.path.join(out_dir, game_model + '.w3x')
    with open(cpath, 'w', encoding='utf-8') as f:
        f.write(wrap(container))
    print('  wrote %s' % cpath)

    # mesh files (by mesh id, e.g. APAWARFACTORY_SKN.SKIN_BODY03)
    mesh_textures = set()
    for mesh in meshes:
        mid = attr(mesh, 'id')
        if not mid:
            continue
        mpath = os.path.join(out_dir, mid + '.w3x')
        with open(mpath, 'w', encoding='utf-8') as f:
            f.write(wrap(mesh))
        print('  wrote %s' % mpath)
        for m in re.finditer(r'<Texture Name="[^"]*">\s*<Value>([^<]*)</Value>', mesh):
            mesh_textures.add(m.group(1).strip())

    # collect every texture name the model uses (mesh constants + <Includes>)
    all_tex = set(mesh_textures)
    for m in re.finditer(r'<Include[^>]*source="ART:([^"]+)\.xml"', data):
        all_tex.add(m.group(1))

    # ---- skeleton ----
    skl_path = None
    skl_id = None
    if hierarchy:
        # find <model>_SKL.W3X (case-insensitive) in src_dir
        for fn in os.listdir(src_dir):
            if fn.upper() == (hierarchy + '.W3X').upper() or fn.upper() == (hierarchy + '.w3x').upper():
                skl_path = os.path.join(src_dir, fn); break
        if skl_path is None:
            cand = os.path.join(src_dir, model + '_SKL.W3X')
            if os.path.isfile(cand):
                skl_path = cand
        if skl_path and os.path.isfile(skl_path):
            sdata = read_utf8(skl_path)
            hier = extract_tags(sdata, 'W3DHierarchy')
            if hier:
                skl_id = attr(hier[0], 'id') or hierarchy
            else:
                skl_id = hierarchy
            skl_file = os.path.join(out_dir, skl_id + '.w3x')
            if os.path.exists(skl_file):
                # skeleton id collides with the container (e.g. walls: hierarchy
                # id == container id) - merge into the same file.
                merge_element(skl_file, hier[0])
                print('  merged skeleton into %s.w3x' % skl_id)
            else:
                with open(skl_file, 'w', encoding='utf-8') as f:
                    f.write(wrap(hier[0]))
                print('  wrote skeleton %s.w3x' % skl_id)

    # ---- animations / doors / build / produce (other <Model>_*.W3X) ----
    for fn in sorted(os.listdir(src_dir)):
        if not fn.upper().startswith(model.upper() + '_'):
            continue
        if fn.upper() == (model + '_SKN.W3X').upper() or fn.upper() == (model + '_SKL.W3X').upper():
            continue
        if not fn.upper().endswith('.W3X'):
            continue
        fpath = os.path.join(src_dir, fn)
        adata = read_utf8(fpath)

        # inline meshes (door/build models carry their own meshes)
        for mesh in extract_tags(adata, 'W3DMesh'):
            mid = attr(mesh, 'id')
            if not mid:
                continue
            mpath = os.path.join(out_dir, mid + '.w3x')
            if not os.path.exists(mpath):
                with open(mpath, 'w', encoding='utf-8') as f:
                    f.write(wrap(mesh))
                print('  wrote %s' % mpath)
            for m in re.finditer(r'<Texture Name="[^"]*">\s*<Value>([^<]*)</Value>', mesh):
                all_tex.add(m.group(1).strip())

        # container present? then this file is a full model+animation (e.g. the
        # door, whose animation id == container id). Write the WHOLE source file
        # so the same file serves as both the container and the animation.
        conts = extract_tags(adata, 'W3DContainer')
        if conts:
            cid = attr(conts[0], 'id')
            cpath2 = os.path.join(out_dir, cid + '.w3x')
            if not os.path.exists(cpath2):
                with open(cpath2, 'w', encoding='utf-8') as f:
                    f.write(adata)
                print('  wrote (combined model+anim) %s' % cpath2)
            continue

        # pure animation
        for a in extract_tags(adata, 'W3DAnimation'):
            aid = attr(a, 'id')
            if not aid:
                continue
            apath = os.path.join(out_dir, aid + '.w3x')
            if os.path.exists(apath):
                merge_element(apath, a)
                print('  merged animation into %s.w3x' % aid)
            else:
                with open(apath, 'w', encoding='utf-8') as f:
                    f.write(wrap(a))
                print('  wrote animation %s.w3x' % aid)

    # ---- textures (.dds/.tga + .xml) ----
    # all_tex was collected above (mesh constants + <Includes>); copy each found.

    # locate the ART root (folder holding the per-faction sub-folders)
    art_root = None
    for d in (src_dir, os.path.dirname(src_dir),
              os.path.dirname(os.path.dirname(src_dir))):
        if (os.path.basename(d).upper() == 'ART'
                or any(os.path.isdir(os.path.join(d, x))
                       for x in ('AP', 'EU', 'GL', 'TA', 'TU'))):
            art_root = d
            break
    if art_root is None:
        art_root = os.path.dirname(src_dir)

    # recursive texture map: lower(basename) -> [abs paths] (case-insensitive)
    tex_map = {}
    for root, dirs, files in os.walk(art_root):
        for fn in files:
            base, ext = os.path.splitext(fn)
            if ext.lower() in ('.dds', '.tga', '.xml'):
                tex_map.setdefault(base.lower(), []).append(os.path.join(root, fn))

    copied = 0
    for t in sorted(all_tex):
        hits = tex_map.get(t.lower())
        if not hits:
            print('  WARN: texture %s not found under %s' % (t, art_root))
            continue
        for src in hits:
            dst = os.path.join(out_dir, os.path.basename(src))
            if not os.path.exists(dst):
                shutil.copy2(src, dst)
                copied += 1
    print('copied %d texture files (names: %s)' % (copied, ', '.join(sorted(all_tex))))

    # ---- post-conversion validation (catches the known script/pipeline holes) ----
    print('== 转换后校验 ==')
    skl_out = os.path.join(out_dir, (hierarchy or game_model) + '.w3x')
    if os.path.isfile(skl_out):
        sdata = read_utf8(skl_out)
        hash_bones = re.findall(r'<Pivot Name="(0x[0-9A-Fa-f]{6,})"', sdata)
        if hash_bones:
            print('  WARN: 骨架有哈希骨 %s - INI 无法用这些骨名引用 Turret/WeaponFireFXBone; '
                  '炮管/炮塔网格绑哈希骨不会随炮塔转。' % ', '.join(sorted(set(hash_bones))[:6]))
    # per-mesh shader availability + convention check
    known_shaders = ('infantry.fx', 'objectsjapan.fx', 'objectsgeneric.fx',
                     'objectssoviet.fx', 'buildingssoviet.fx', 'objectsalliedtread.fx',
                     'defaultw3d.fx')
    for m in meshes:
        mid = attr(m, 'id')
        sh = re.search(r'ShaderName="([^"]*)"', m)
        shader = sh.group(1) if sh else ''
        has_t0 = 'Texture_0' in m
        has_diff = 'DiffuseTexture' in m
        if shader and shader != 'defaultw3d.fx' and shader not in known_shaders:
            print('  WARN: 网格 %s 用未知着色器 "%s" - 游戏无此shader, 会走错渲染路径。' % (mid, shader))
        if has_t0 and not has_diff:
            print('  WARN: 网格 %s 用 Texture_0(BASIC) - 若模型级走 PBR 会绑不上贴图, '
                  '需改 DiffuseTexture(如旋翼)。' % mid)
    print('DONE: %s -> %s' % (game_model, out_dir))


if __name__ == '__main__':
    main()
