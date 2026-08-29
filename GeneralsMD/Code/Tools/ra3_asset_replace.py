#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ra3_asset_replace.py — RA3(红警3) 模型/素材通用替换工具（绝命时刻 W3X 管线）
================================================================================

把红警3 解压出来的 .w3x 模型(自包含 SKN)+贴图导入绝命时刻,并把单位 INI 的
W3D Draw 改成 W3XModelDraw。这是 2026-08-23/24 在 QWCSB MOD 上验证过的完整流程
（中方/GLA/美方 载具 + 飞机）的通用封装。

流程三步,对应三个子命令:

  1) import    — 拆分自包含 SKN 为 container+子网格,拷 SKL/动画/贴图到游戏 ART\\W3X
  2) convert   — 改写目标 INI 里单位的 Draw 为 W3XModelDraw(自动识别炮塔/开火点骨)
  3) validate  — 校验已导入模型(XML/子网格/骨骼索引/贴图/层级)

用法示例
--------
# 1) 导入模型+贴图
python ra3_asset_replace.py import ^
  --src "D:\\遗忘发来的红警3将军2里的资源\\ART" ^
  --game "E:\\!!!!!!!QWCSB" ^
  --model-sub AP --models "APATavBtMstrTech1,APATavOvrlrd" ^
  --tex-sub TA --tex "TavBtMstr2,TavOvrlrd2"

# 2) 改写 INI(自动识别骨骼,自动用 *_IDLA 动画)
python ra3_asset_replace.py convert ^
  --ini "E:\\!!!!!!!QWCSB\\Data\\INI\\Object\\ChinaVehicle.ini" ^
  --game "E:\\!!!!!!!QWCSB" ^
  --map "ChinaTankBattleMaster=APATAVBTMSTRTECH1,ChinaTankOverlord=APATAVOVRLRD"

# 3) 校验
python ra3_asset_replace.py validate --game "E:\\!!!!!!!QWCSB" ^
  --models "APATAVBTMSTRTECH1,APATAVOVRLRD"

映射=对象名→模型名(容器 id,如 APATAVBTMSTRTECH1)。骨骼/动画自动从模型 SKL 推断:
  Turret=BONE_TURRET, TurretPitch=BONE_TURRETEL(或 BONE_TURRTEL 拼写), 主武器=
  BONE_WEAPONA01(优先)/BONE_WEAPON_A01/WEAPONA01…, 副武器=BONE_WEAPONA02/B01…。
若模型无炮塔/武器骨(如卡车)会自动省略。可用 --turret/--pitch/--primary/--secondary
对单个单位强制指定,格式 "对象名:骨名"。

兼容性: Python 3.8+。核心模块只依赖标准库。
"""
import argparse
import os
import re
import shutil
import sys

NS = 'xmlns="uri:ea.com:eala:asset" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"'

# ----------------------------------------------------------------------------
# 拆分 & 导入
# ----------------------------------------------------------------------------

def extract_w3meshes(text):
    """从自包含 SKN 里取出所有 <W3DMesh id="..">...</W3DMesh>,返回 [(id, block)]。"""
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


def detect_muzzleflash(block):
    """检测 muzzleflash 旋翼/风扇子网格(<FXShader ShaderName="muzzleflash.fx">)。"""
    fx = re.search(r'<FXShader ShaderName="([^"]+)"', block)
    return bool(fx and "muzzle" in fx.group(1).lower())


def check_rotor_constants(mid, block):
    """校验旋翼/风扇子网格的 muzzleflash 常量(缺则旋翼不转/不可见)。"""
    warns = []
    if "<Float Name=\"TexCoordTransformAngle_0\"" not in block:
        warns.append("缺 TexCoordTransformAngle_0(旋翼不会转)")
    if "Fx_blades_G2" not in block:
        warns.append("缺 Fx_blades_G2 桨叶贴图(Texture_0)")
    if "<Bool Name=\"MultiTextureEnable\"" not in block:
        warns.append("缺 MultiTextureEnable(仅单次采样)")
    if warns:
        print("  [ROTOR] %s 警告: %s" % (mid, "; ".join(warns)))
    else:
        print("  [ROTOR] %s OK(旋翼常量齐全)" % mid)


def is_compressed_dds(path):
    """返回 True=DXT压缩 / False=未压缩(引擎已支持未压缩 DDS)/ None=非DDS。"""
    try:
        with open(path, "rb") as f:
            if f.read(4) != b"DDS ":
                return None
            f.seek(84)
            return f.read(4) in (b"DXT1", b"DXT2", b"DXT3", b"DXT4", b"DXT5")
    except OSError:
        return None


def build_container(container_id, hierarchy, mesh_ids, exclude=()):
    """构建 <W3DContainer> XML;exclude 里是待隐藏的子对象名(如旋翼 SKIN_SCREW*)。"""
    parts = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<AssetDeclaration %s>' % NS,
             '\t<W3DContainer id="%s" Hierarchy="%s">' % (container_id, hierarchy)]
    for mid in mesh_ids:
        sub = mid.rsplit(".", 1)[-1]
        if sub in exclude:
            continue
        parts.append('\t\t<SubObject SubObjectID="%s" BoneIndex="0">'
                     '<RenderObject><Mesh>%s</Mesh></RenderObject></SubObject>'
                     % (sub, mid))
    parts.append('\t</W3DContainer>')
    parts.append('</AssetDeclaration>')
    return "\n".join(parts) + "\n"


def split_model(model_dir, model, game_w3x, hide_subobjects=()):
    """拆分一个模型: SKN->container+子网格, 拷 SKL/动画。返回模型大写容器 id。"""
    skn_src = os.path.join(model_dir, "%s_SKN.W3X" % model)
    if not os.path.exists(skn_src):
        print("[MISS] %s_SKN" % model)
        return None
    with open(skn_src, encoding="utf-8", errors="replace") as f:
        text = f.read()
    meshes = extract_w3meshes(text)
    if not meshes:
        print("[WARN] %s: no W3DMesh" % model)
        return None
    # muzzleflash 旋翼/风扇子网格: 必须保留(不要隐藏)并带旋转常量,否则旋翼不转。
    rotors = [mid for mid, block in meshes if detect_muzzleflash(block)]
    if rotors:
        print("[ROTOR] %s: %d 个 muzzleflash 旋翼/风扇子网格: %s"
              % (model, len(rotors), ", ".join(mid.rsplit(".", 1)[-1] for mid in rotors)))
        for mid, block in meshes:
            if mid in rotors:
                check_rotor_constants(mid, block)
    container_id = meshes[0][0].rsplit(".", 1)[0]       # 如 APATAVBTMSTRTECH1_SKN
    model_upper = container_id[:-4]                      # 去 _SKN
    hierarchy = model_upper + "_SKL"
    for mid, block in meshes:
        out = os.path.join(game_w3x, mid + ".w3x")
        content = ('<?xml version="1.0" encoding="UTF-8"?>\n'
                   '<AssetDeclaration %s>\n%s\n</AssetDeclaration>\n' % (NS, block))
        with open(out, "w", encoding="utf-8", newline="") as f:
            f.write(content)
    cont = build_container(container_id, hierarchy,
                           [mid for mid, _ in meshes], hide_subobjects)
    with open(os.path.join(game_w3x, container_id + ".w3x"),
              "w", encoding="utf-8", newline="") as f:
        f.write(cont)
    skl_src = os.path.join(model_dir, "%s_SKL.W3X" % model)
    if os.path.exists(skl_src):
        shutil.copy(skl_src, os.path.join(game_w3x, hierarchy + ".w3x"))
    # 动画文件(跳过 SKN/SKL/COL)
    for fn in os.listdir(model_dir):
        m = re.match(r"^%s_([A-Za-z0-9]+)\.w3x$" % re.escape(model), fn, re.IGNORECASE)
        if not m:
            continue
        tag = m.group(1)
        if tag.lower() in ("skn", "skl", "col"):
            continue
        shutil.copy(os.path.join(model_dir, fn),
                    os.path.join(game_w3x, "%s_%s.w3x" % (model_upper, tag)))
    print("[OK] %s -> %s (submeshes=%d, hidden=%s)"
          % (model, container_id, len(meshes), sorted(hide_subobjects) or "-"))
    return container_id


def copy_textures(tex_dir, textures, game_w3x):
    """拷贝贴图(匹配前缀,含 _NRM/_SPM/_D/_K 变体)。"""
    for t in textures:
        n = 0
        for fn in os.listdir(tex_dir):
            if fn.lower().startswith(t.lower() + "_") or fn.lower().startswith(t.lower() + "."):
                shutil.copy(os.path.join(tex_dir, fn), os.path.join(game_w3x, fn))
                if fn.lower().endswith(".dds"):
                    comp = is_compressed_dds(os.path.join(tex_dir, fn))
                    if comp is False:
                        # Fx_blades_G2 等旋翼贴图是未压缩 A8R8G8B8 —— 引擎已支持,
                        # 但注意别用 DXT 工具压缩(会破坏桨叶 alpha)。
                        print("  [TEX] %s: 未压缩 DDS(非 DXT)——引擎支持,直接拷贝" % fn)
                    elif comp is None:
                        print("  [TEX] %s: 非标准 DDS 头,请检查" % fn)
                n += 1
        print("[TEX] %s: %d files" % (t, n))


# ----------------------------------------------------------------------------
# 骨骼自动识别 & Draw 改写
# ----------------------------------------------------------------------------

def parse_skl_bones(model, game_w3x):
    """读模型 SKL,返回小写骨名列表。"""
    for fn in os.listdir(game_w3x):
        if fn.lower() == model.lower() + "_skl.w3x":
            t = open(os.path.join(game_w3x, fn), encoding="utf-8-sig", errors="replace").read()
            return [b.lower() for b in re.findall(r'Pivot Name="([^"]+)"', t)]
    return []


def auto_bones(bones):
    """从骨名推断 Turret/TurretPitch/PRIMARY/SECONDARY。返回小写骨名或 None。"""
    def has(*kws):
        return [b for b in bones if all(k.lower() in b for k in kws)]
    turret = None
    for cand in ("bone_turret", "turret"):
        if cand in bones:
            turret = cand
            break
    if not turret and has("turret"):
        cands = [b for b in bones if "turret" in b and "el" not in b
                 and not re.search(r"_[grbs]\d*$", b)]
        turret = cands[0] if cands else None
    pitch = None
    for cand in ("bone_turretel", "bone_turrtel", "turretel"):
        if cand in bones:
            pitch = cand
            break
    if not pitch:
        # 炮塔带后缀(如 bone_turret_a)时,优先同后缀的俯仰骨(bone_turretel_a)
        if turret and turret != "bone_turret" and "_" in turret:
            suffix = turret.rsplit("_", 1)[-1]
            same = [b for b in bones if ("turretel" in b or "turrtel" in b)
                    and b.endswith("_" + suffix)]
            if same:
                pitch = same[0]
        if not pitch:
            # 带后缀的俯仰骨,如 bone_turretel_a / bone_turrtel 拼写变体
            telt = [b for b in bones if b.startswith("bone_turretel") or b.startswith("bone_turrtel")]
            if not telt:
                telt = [b for b in bones if b.startswith("turretel")]
            pitch = telt[0] if telt else None
    prim = None
    for cand in ("bone_weapona01", "bone_weapon_a01", "bone_weapon_a",
                 "weapona01", "bone_weapona", "weapona"):
        if cand in bones:
            prim = cand
            break
    if not prim and has("weapon"):
        cands = [b for b in has("weapon") if not re.search(r"0[2-9]|b\d", b)]
        prim = cands[0] if cands else None
    sec = None
    for cand in ("bone_weapona02", "bone_weapon_b01", "bone_weaponb01", "bone_weapon_b",
                 "bone_weaponb", "weaponb01", "bone_weapona03"):
        if cand in bones:
            sec = cand
            break
    if not sec and has("weaponb"):
        sec = has("weaponb")[0]
    return turret, pitch, prim, sec


def has_animation(model, game_w3x):
    """是否有 *_IDLA 动画文件。"""
    return any(f.lower() == model.lower() + "_idla.w3x" for f in os.listdir(game_w3x))


def w3x_draw_block(model, turret, pitch, primary, secondary, anim):
    """生成 W3XModelDraw INI 块。骨名为 None 则省略对应行。"""
    lines = ["  Draw = W3XModelDraw ModuleTag_01",
             "    DefaultModelName = %s_SKN" % model, ""]
    def state(name):
        b = ["    ConditionState = %s" % name, "      Model = %s_SKN" % model]
        if turret:
            b.append("      Turret = %s" % turret)
        if pitch:
            b.append("      TurretPitch = %s" % pitch)
        if primary:
            b.append("      WeaponFireFXBone = PRIMARY %s" % primary)
            b.append("      WeaponLaunchBone = PRIMARY %s" % primary)
        if secondary:
            b.append("      WeaponFireFXBone = SECONDARY %s" % secondary)
            b.append("      WeaponLaunchBone = SECONDARY %s" % secondary)
        if anim:
            b.append("      Animation = %s_IDLA" % model)
            b.append("      AnimationMode = LOOP")
        b.append("    End")
        return b
    lines += state("NONE")
    lines.append("")
    lines += state("REALLYDAMAGED")
    lines.append("  End")
    return "\n".join(lines)


DRAW_RE = re.compile(r"\n[ \t]*Draw[ \t]*=[ \t]*[A-Za-z_]\w*[ \t]+ModuleTag_\w+")


def convert_ini_draw(ini_path, mapping, game_w3x, force_bones=None):
    """改写 INI 里每个目标对象的首个 Draw 为 W3XModelDraw。

    mapping: dict 对象名->模型容器id。force_bones: dict 对象名->dict(bone名->值)。
    """
    with open(ini_path, encoding="utf-8-sig", errors="replace") as f:
        text = f.read()
    changed = []
    for obj, model in mapping.items():
        m = re.search(r"^Object %s\b" % re.escape(obj), text, re.M)
        if not m:
            print("[WARN] object not found: %s" % obj)
            continue
        obj_start = m.start()
        nxt = re.search(r"\nObject \w+", text[obj_start + len(obj) + 2:])
        body = text[obj_start: obj_start + len(obj) + 2 + (nxt.start() if nxt else len(text))]
        dm = DRAW_RE.search(body)
        if not dm:
            print("[WARN] no Draw in %s" % obj)
            continue
        draw_start = obj_start + dm.start()
        after = text[draw_start:]
        design = re.search(r"; \*{2,3}DESIGN", after)
        obj_end = re.search(r"\nEnd\s*\n(?:;|Object|\Z)", after)
        limit = draw_start + (design.start() if design else (obj_end.start() if obj_end else len(text)))
        region = text[draw_start:limit]
        ems = list(re.finditer(r"\n  End\n", region))
        if not ems:
            print("[WARN] no Draw end in %s" % obj)
            continue
        draw_end = draw_start + ems[-1].end()
        bones = parse_skl_bones(model, game_w3x)
        turret, pitch, prim, sec = auto_bones(bones)
        if force_bones and obj in force_bones:
            fb = force_bones[obj]
            turret = fb.get("turret", turret)
            pitch = fb.get("pitch", pitch)
            prim = fb.get("primary", prim)
            sec = fb.get("secondary", sec)
        anim = has_animation(model, game_w3x)
        block = w3x_draw_block(model, turret, pitch, prim, sec, anim)
        text = text[:draw_start] + "\n" + block + "\n" + text[draw_end:]
        changed.append("%s=%s" % (obj, model))
    with open(ini_path, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    print("convert: %s -> %d units (%s)" % (os.path.basename(ini_path), len(changed), ", ".join(changed)))
    return changed


# ----------------------------------------------------------------------------
# 校验
# ----------------------------------------------------------------------------

def validate_models(game_w3x, models):
    import xml.etree.ElementTree as ET
    bad = 0
    for model in models:
        base = os.path.join(game_w3x, model)
        cpath = base + "_SKN.w3x"
        if not os.path.exists(cpath):
            print("[FAIL] %s_SKN.w3x missing" % model); bad += 1; continue
        c = open(cpath, encoding="utf-8-sig", errors="replace").read()
        try:
            ET.fromstring(c)
        except Exception as e:
            print("[FAIL] %s container XML: %s" % (model, e)); bad += 1; continue
        refs = re.findall(r"<Mesh>([^<]+)</Mesh>", c)
        missing = [r for r in refs if not os.path.exists(os.path.join(game_w3x, r + ".w3x"))]
        hier = re.search(r'Hierarchy="([^"]+)"', c)
        skl_ok = hier and os.path.exists(os.path.join(game_w3x, hier.group(1) + ".w3x"))
        if missing or not skl_ok:
            print("[FAIL] %s refs missing=%s skl=%s" % (model, missing, skl_ok)); bad += 1; continue
        # 骨骼/三角形索引
        nbones = len(re.findall(r"<Pivot ", open(os.path.join(game_w3x, hier.group(1) + ".w3x"),
                                                 encoding="utf-8-sig", errors="replace").read()))
        ob = []
        for r in refs:
            t = open(os.path.join(game_w3x, r + ".w3x"), encoding="utf-8-sig", errors="replace").read()
            b = [int(x) for x in re.findall(r'<I Bone="(\d+)"', t)]
            if b and max(b) >= nbones:
                ob.append((r, max(b), nbones))
        if ob:
            print("[FAIL] %s bone OOB: %s" % (model, ob)); bad += 1; continue
        # muzzleflash 旋翼/风扇子网格常量校验(缺旋转角/桨叶贴图则旋翼不转)
        for r in refs:
            t = open(os.path.join(game_w3x, r + ".w3x"), encoding="utf-8-sig", errors="replace").read()
            if detect_muzzleflash(t):
                check_rotor_constants(r, t)
        print("[OK] %s: %d submeshes, %d bones, skl OK" % (model, len(refs), nbones))
    return bad == 0


# ----------------------------------------------------------------------------
# CLI
# ----------------------------------------------------------------------------

def _parse_list(s):
    return [x.strip() for x in s.split(",") if x.strip()]


def main(argv=None):
    ap = argparse.ArgumentParser(description="RA3 模型/素材通用替换工具（绝命时刻 W3X 管线）")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("import", help="拆分自包含 SKN + 拷 SKL/动画/贴图到游戏")
    p.add_argument("--src", required=True, help="源 ART 目录(含 AP/GL/EU/TA/TE/TG/FX 子目录)")
    p.add_argument("--game", required=True, help="游戏根目录(如 E:\\!!!!!!!QWCSB)")
    p.add_argument("--model-sub", required=True, help="模型子目录名(AP/GL/EU)")
    p.add_argument("--models", required=True, help="模型前缀列表,逗号分隔(如 APATavBtMstrTech1)")
    p.add_argument("--tex-sub", default="", help="贴图子目录名(TA/TE/TG),空则跳过贴图")
    p.add_argument("--tex", default="", help="贴图前缀列表,逗号分隔")
    p.add_argument("--hide", default="", help="隐藏子对象名(如 SKIN_SCREW01),逗号分隔")

    p = sub.add_parser("convert", help="改写目标 INI 的 Draw 为 W3XModelDraw")
    p.add_argument("--ini", required=True, help="目标 INI 文件(如 Data\\INI\\Object\\ChinaVehicle.ini)")
    p.add_argument("--game", required=True, help="游戏根目录")
    p.add_argument("--map", required=True, help="对象名=模型容器id 映射,逗号分隔")
    p.add_argument("--force", default="", help="强制骨,格式 对象:骨=骨名[,对象:骨=骨名...]")

    p = sub.add_parser("validate", help="校验已导入模型")
    p.add_argument("--game", required=True, help="游戏根目录")
    p.add_argument("--models", required=True, help="模型容器 id 列表,逗号分隔")

    a = ap.parse_args(argv)

    if a.cmd == "import":
        game_w3x = os.path.join(a.game, "ART", "W3X")
        os.makedirs(game_w3x, exist_ok=True)
        hide = set(_parse_list(a.hide))
        for model in _parse_list(a.models):
            split_model(os.path.join(a.src, a.model_sub), model, game_w3x, hide)
        if a.tex_sub:
            copy_textures(os.path.join(a.src, a.tex_sub), _parse_list(a.tex), game_w3x)
    elif a.cmd == "convert":
        mapping = {}
        for pair in _parse_list(a.map):
            obj, _, model = pair.partition("=")
            mapping[obj.strip()] = model.strip()
        force = {}
        for pair in _parse_list(a.force):
            obj, _, spec = pair.partition(":")
            k, _, v = spec.partition("=")
            force.setdefault(obj.strip(), {})[k.strip()] = v.strip()
        convert_ini_draw(a.ini, mapping, os.path.join(a.game, "ART", "W3X"), force)
    elif a.cmd == "validate":
        ok = validate_models(os.path.join(a.game, "ART", "W3X"), _parse_list(a.models))
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
