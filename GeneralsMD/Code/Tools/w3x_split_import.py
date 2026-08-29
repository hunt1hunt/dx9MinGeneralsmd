#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Split self-contained .w3x SKN files (inline <W3DMesh> blocks) into the
container + per-submesh file layout the W3X loader requires, and copy
skeleton / animation / texture assets into the game ART\\W3X folder.

Layout the loader needs (see W3XModelDraw.cpp / w3x_loader.cpp):
  Art/W3X/<MODEL>_SKN.w3x            <W3DContainer id=... Hierarchy=<MODEL>_SKL>
  Art/W3X/<MODEL>_SKN.<SUBID>.w3x    one <W3DMesh> per sub-object
  Art/W3X/<MODEL>_SKL.w3x            <W3DHierarchy>
  Art/W3X/<MODEL>_<ANIM>.w3x         <W3DAnimation>

Each job = (model_source_dir, [model_prefixes], texture_source_dir, [new_textures]).
"""
import os
import re
import shutil

GAME_W3X = r"E:\!!!!!!!QWCSB\ART\W3X"

SRC = r"D:\遗忘发来的红警3将军2里的资源\ART"

JOBS = [
    # AP (China) aircraft — models from AP, textures from TA
    ("AP", ["APATaaHelix", "APATaaMig"],
     "TA", ["taahelix2", "TapaJitFire"]),
    # EU (America) aircraft — models from EU, textures from TE
    ("EU", ["EUTeaRaptor", "EUTeaComancheS", "EUTeaAurora", "EUTeaStealth"],
     "TE", ["TeuJitFire", "tearaptor2", "TeaComanche2", "TeaAurora2", "teastealth2"]),
]

NS = 'xmlns="uri:ea.com:eala:asset" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"'


def extract_w3meshes(text):
    """Return [(mesh_id, block_text)] for each <W3DMesh>...</W3DMesh>."""
    results = []
    for m in re.finditer(r"<W3DMesh\b[^>]*>", text):
        idm = re.search(r'id="([^"]+)"', m.group(0))
        if not idm:
            continue
        endm = re.search(r"</W3DMesh>", text[m.end():])
        if not endm:
            continue
        end = m.end() + endm.end()
        results.append((idm.group(1), text[m.start():end]))
    return results


def build_container(container_id, hierarchy, mesh_ids):
    parts = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<AssetDeclaration %s>' % NS,
             '\t<W3DContainer id="%s" Hierarchy="%s">' % (container_id, hierarchy)]
    for mid in mesh_ids:
        sub = mid.rsplit(".", 1)[-1]
        parts.append('\t\t<SubObject SubObjectID="%s" BoneIndex="0">'
                     '<RenderObject><Mesh>%s</Mesh></RenderObject></SubObject>'
                     % (sub, mid))
    parts.append('\t</W3DContainer>')
    parts.append('</AssetDeclaration>')
    return "\n".join(parts) + "\n"


def process_models(model_dir, models):
    for model in models:
        skn_src = os.path.join(model_dir, "%s_SKN.W3X" % model)
        if not os.path.exists(skn_src):
            print("[MISS] %s_SKN" % model)
            continue
        with open(skn_src, encoding="utf-8", errors="replace") as f:
            text = f.read()
        meshes = extract_w3meshes(text)
        if not meshes:
            print("[WARN] %s: no W3DMesh found" % model)
            continue
        container_id = meshes[0][0].rsplit(".", 1)[0]
        model_upper = container_id[:-4]          # strip trailing "_SKN"
        hierarchy = model_upper + "_SKL"
        for mid, block in meshes:
            out = os.path.join(GAME_W3X, mid + ".w3x")
            content = ('<?xml version="1.0" encoding="UTF-8"?>\n'
                       '<AssetDeclaration %s>\n%s\n</AssetDeclaration>\n' % (NS, block))
            with open(out, "w", encoding="utf-8", newline="") as f:
                f.write(content)
        cont = build_container(container_id, hierarchy, [mid for mid, _ in meshes])
        with open(os.path.join(GAME_W3X, container_id + ".w3x"),
                  "w", encoding="utf-8", newline="") as f:
            f.write(cont)
        skl_src = os.path.join(model_dir, "%s_SKL.W3X" % model)
        if os.path.exists(skl_src):
            shutil.copy(skl_src, os.path.join(GAME_W3X, hierarchy + ".w3x"))
        for fn in os.listdir(model_dir):
            m = re.match(r"^%s_([A-Za-z0-9]+)\.w3x$" % re.escape(model), fn, re.IGNORECASE)
            if not m:
                continue
            tag = m.group(1)
            if tag.lower() in ("skn", "skl", "col"):
                continue
            shutil.copy(os.path.join(model_dir, fn),
                        os.path.join(GAME_W3X, "%s_%s.w3x" % (model_upper, tag)))
        print("[OK] %s -> %s (submeshes=%d, skl+anims copied)"
              % (model, container_id, len(meshes)))


def process_textures(tex_dir, new_tex):
    for t in new_tex:
        n = 0
        for fn in os.listdir(tex_dir):
            if fn.lower().startswith(t.lower() + "_") or fn.lower().startswith(t.lower() + "."):
                shutil.copy(os.path.join(tex_dir, fn), os.path.join(GAME_W3X, fn))
                n += 1
        print("[TEX] %s: %d files" % (t, n))


def main():
    os.makedirs(GAME_W3X, exist_ok=True)
    for model_sub, models, tex_sub, new_tex in JOBS:
        model_dir = os.path.join(SRC, model_sub)
        tex_dir = os.path.join(SRC, tex_sub)
        print("== models from %s ==" % model_dir)
        process_models(model_dir, models)
        print("== textures from %s ==" % tex_dir)
        process_textures(tex_dir, new_tex)


if __name__ == "__main__":
    main()
