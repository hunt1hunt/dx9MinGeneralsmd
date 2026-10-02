#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ba_asset_import.py — Battle Archive 素材 -> 绝命时刻 W3X 管线 导入适配器
================================================================================

背景
----
`ra3_asset_replace.py import` 面向的是"自包含 SKN"(内联 <W3DMesh>)且按
AP/GL/EU 子目录组织的素材。而 `F:\\红警3MODBattle Archive...` 这套素材是
**已经拆分好**的,命名也不一样:

    <NAME>_CTR.w3x   容器   <W3DContainer id="NAME" Hierarchy="NAME">
    <NAME>_HRC.w3x   骨架   <W3DHierarchy id="NAME">
    <NAME>.<SUB>.w3x 子网格 <W3DMesh id="NAME.SUB">
    <NAME>_<TAG>.w3x 动画   <W3DAnimation ... Hierarchy="NAME">
    <Tex>.dds/.xml   贴图

所以 ra3_asset_replace.py 的 import 对它**空转**(找不到内联 <W3DMesh>)。

游戏侧的真实要求(W3XModelDraw::loadW3XModel / w3x_loader)
------------------------------------------------------------
    Art/W3X/<DefaultModelName>.w3x     容器(INI 的 DefaultModelName)
    Art/W3X/<Hierarchy 属性值>.w3x     骨架(字面文件名,由容器 Hierarchy 给出)
    Art/W3X/<Mesh 文本值>.w3x          子网格(字面文件名,由容器 <Mesh> 给出)
    Art/W3X/<Animation 名>.w3x         动画(INI 的 Animation = xxx)
    (贴图/XML 走 Art/W3X 递归查找)

关键点:容器里的 `Hierarchy` 和 `<Mesh>` 都是**字面文件名**。因此本脚本不需要
改写任何 XML —— 只要把 BA 的文件**改名/放到**游戏认得的位置即可:

    1) <NAME>_CTR.w3x  ->  <NAME>_SKN.w3x        (容器; Hierarchy 原样保留)
    2) <NAME>_HRC.w3x  ->  <Hierarchy 属性值>.w3x (骨架)
    3) <NAME>.*.w3x    ->  原样                   (子网格)
    4) <NAME>_<TAG>.w3x->  原样                   (动画)
    5) <Tex>.dds/.xml  ->  原样(空 xml 自动补 <Texture File=>)

用法
----
  python ba_asset_import.py --src "F:\\红警3MODBattle Archive..." ^
      --game "D:\\zerohour" --model ZTZ99A

  # 剔除某个子对象(如会渲染成不透明方块的光晕/FX 面片)
  python ba_asset_import.py ... --model SHANHAIJINGATINFANTRY --hide MAFIA_GENRYUMON_AR_HALO

  # 只看要做什么,不落盘
  python ba_asset_import.py ... --dry-run

兼容 Python 3.8+。只依赖标准库。
"""

import argparse
import os
import re
import shutil
import sys

# 容器 <SubObject> 块(含嵌套)
_SUBOBJ_RE = re.compile(r"<SubObject\b[^>]*>.*?</SubObject>", re.S)
# 贴图引用: <Texture Name="X"><Value>NAME</Value>
_TEX_RE = re.compile(
    r'<Texture\s+Name="[^"]*">\s*<Value>([^<]+)</Value>', re.S)
# 判断 <Value> 是不是贴图名(而不是数字/布尔/枚举)
_NUMISH_RE = re.compile(r"^[-+]?\d*\.?\d+$", re.I)


def _read_text(path):
    with open(path, "r", encoding="utf-8-sig", errors="ignore") as fh:
        return fh.read()


def _strip_bom_copy(src, dst):
    """原样拷贝,但去掉 UTF-8 BOM(BA 的文件带 BOM,golden 文件不带)。"""
    with open(src, "rb") as fh:
        data = fh.read()
    if data[:3] == b"\xef\xbb\xbf":
        data = data[3:]
    with open(dst, "wb") as fh:
        fh.write(data)


_SHADER_RE = re.compile(r'(FXShader\s+ShaderName=")([^"]+)(")')


def _lowercase_shader_names(path):
    """把网格里的 FXShader ShaderName 统一成小写。

    引擎的着色器路由用的是**大小写敏感**的 strstr —— `W3XShaderVariant()` 里
    `strstr(origShader, "buildings")` 判断"这是建筑 -> w3x_buildings.fx(不透明,
    FORBID_CLIPPING)"。若网格写的是 `BuildingsSoviet.fx`(大写 B),该判断落空,
    模型会被当成车辆走 `w3x_soviet.fx`(alpha 裁剪) —— 而建筑贴图(DXT1)的 alpha
    大多是 0,于是整片被裁掉,只剩黑漆漆的内部。**原版 RA3 素材本身大小写就乱**
    (BuildingsSoviet/ObjectsGeneric/ObjectsAlliedTread…),所以导入时必须归一化。
    """
    try:
        t = open(path, encoding="utf-8-sig", errors="ignore").read()
    except Exception:
        return 0
    n = [0]

    def repl(m):
        low = m.group(2).lower()
        if low != m.group(2):
            n[0] += 1
        return m.group(1) + low + m.group(3)

    t2 = _SHADER_RE.sub(repl, t)
    if t2 != t:
        open(path, "w", encoding="utf-8", newline="").write(t2)
    return n[0]


def _w3x_listing(src_dir):
    """返回 {basename_without_ext_lower: actual_filename} 供大小写无关查找。"""
    out = {}
    for fn in os.listdir(src_dir):
        base, ext = os.path.splitext(fn)
        if ext.lower() == ".w3x":
            out[base.lower()] = fn
    return out


def find_container(src_dir, model):
    """定位容器文件,返回 (filename, convention)。"""
    for suffix, conv in (("_CTR", "A"), ("_SKN", "B")):
        fn = "%s%s.w3x" % (model, suffix)
        if os.path.isfile(os.path.join(src_dir, fn)):
            return fn, conv
    return None, None


def parse_container(path):
    """返回 (hierarchy_name, [subobject_block_text, ...])。"""
    text = _read_text(path)
    m = re.search(r'<W3DContainer\b[^>]*Hierarchy="([^"]+)"', text)
    hierarchy = m.group(1) if m else None
    subs = _SUBOBJ_RE.findall(text)
    return hierarchy, subs


def subobject_id(block):
    m = re.search(r'SubObjectID="([^"]+)"', block)
    return m.group(1) if m else None


def mesh_ref(block):
    m = re.search(r"<(?:Mesh|CollisionBox)>([^<]+)</(?:Mesh|CollisionBox)>", block)
    return m.group(1).strip() if m else None


def build_container_text(container_id, hierarchy, sub_blocks):
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<AssetDeclaration xmlns="uri:ea.com:eala:asset" '
             'xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">',
             '\t<W3DContainer id="%s" Hierarchy="%s">' % (container_id, hierarchy)]
    for b in sub_blocks:
        # 统一缩进,保留原有内容
        lines.append("\t\t" + b.strip().replace("\n", "\n\t\t"))
    lines.append("\t</W3DContainer>")
    lines.append("</AssetDeclaration>")
    return "\n".join(lines) + "\n"


def collect_textures(model_files, src_dir):
    """扫描一组 .w3x,收集被引用的贴图名。"""
    names = set()
    for fn in model_files:
        text = _read_text(os.path.join(src_dir, fn))
        for t in _TEX_RE.findall(text):
            t = t.strip()
            if not t or _NUMISH_RE.match(t) or t.lower() in ("true", "false"):
                continue
            names.add(t)
    return names


def ensure_texture_xml(src_dir, dst_dir, tex):
    """拷贝 <tex>.xml;若源 xml 缺 <Texture File=> 则补一个正确的。"""
    xml_src = os.path.join(src_dir, tex + ".xml")
    xml_dst = os.path.join(dst_dir, tex + ".xml")
    ok = False
    if os.path.isfile(xml_src):
        txt = _read_text(xml_src)
        if re.search(r"<Texture\b[^>]*File=", txt):
            _strip_bom_copy(xml_src, xml_dst)
            return True
    # 源缺 xml / xml 是空壳 -> 自己写一个(引擎 ResolveTextureDDS 要求 <Texture File=>)
    with open(xml_dst, "w", encoding="utf-8", newline="\n") as fh:
        fh.write('<?xml version="1.0" encoding="UTF-8"?>\n'
                 '<AssetDeclaration xmlns="uri:ea.com:eala:asset" '
                 'xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">\n'
                 '  <Texture id="%s" File="%s.dds"/>\n'
                 '</AssetDeclaration>\n' % (tex, tex))
    return ok


def main():
    ap = argparse.ArgumentParser(description="Battle Archive -> 绝命时刻 W3X 导入适配器")
    ap.add_argument("--src", required=True, help="Battle Archive 根目录")
    ap.add_argument("--game", required=True, help="游戏根目录(如 D:\\zerohour)")
    ap.add_argument("--model", required=True, help="模型名(容器前缀),如 ZTZ99A / SHANHAIJINGATINFANTRY")
    ap.add_argument("--hide", default="", help="要剔除的子对象名(逗号分隔)")
    ap.add_argument("--dst-sub", default="BA", help="游戏 ART/W3X 下的子目录名(默认 BA)")
    ap.add_argument("--dry-run", action="store_true", help="只打印计划,不落盘")
    a = ap.parse_args()

    src = a.src
    game = a.game
    model = a.model
    dst = os.path.join(game, "ART", "W3X", a.dst_sub)
    # 默认额外剔除"损伤填充"子网格：RA3 建筑的 `*_FILL` 用
    # `buildingsgenericdamagefill.fx` + 一张暗金属贴图，本意只在被打出的破洞处露出来。
    # 但引擎只按 `strstr(..."buildings")` 把它当普通建筑面**不透明整片画出来** →
    # 整栋楼被暗金属色盖住，看起来"大面积发黑"。原版 神州(CB) 建筑压根没有 _FILL 子网格，
    # 所以只有照搬原版苏军(SB)建筑时才会中招。用 --hide 显式保留时可在 --hide 里写回。
    hide = set(x.strip().upper() for x in a.hide.split(",") if x.strip())

    if not os.path.isdir(src):
        print("[FAIL] 源目录不存在: %s" % src)
        return 2
    if not os.path.isdir(game):
        print("[FAIL] 游戏目录不存在: %s" % game)
        return 2

    listing = _w3x_listing(src)
    cfn, conv = find_container(src, model)
    if not cfn:
        print("[FAIL] 找不到容器 %s_CTR.w3x / %s_SKN.w3x" % (model, model))
        return 2
    print("[OK] 容器: %s (Convention %s)" % (cfn, conv))

    hierarchy, sub_blocks = parse_container(os.path.join(src, cfn))
    if not hierarchy:
        print("[FAIL] 容器里没有 Hierarchy 属性")
        return 2
    print("[OK] Hierarchy=%s, 子对象 %d 个" % (hierarchy, len(sub_blocks)))

    # --- 需要拷贝的 w3x: 容器/骨架/子网格/动画 ---
    plan_files = []
    out_container_name = "%s_SKN" % model

    # 骨架: <NAME>_HRC.w3x -> <hierarchy>.w3x
    hrc = listing.get(("%s_HRC" % model).lower())
    if hrc:
        plan_files.append((hrc, "%s.w3x" % hierarchy))
    else:
        # convention B: 骨架就是 <NAME>_SKL.w3x(或 <NAME>_SKN_HRC.w3x)
        for cand in ("%s_SKL" % model, "%s_SKN_HRC" % model):
            f = listing.get(cand.lower())
            if f:
                plan_files.append((f, "%s.w3x" % hierarchy))
                break
        else:
            print("[WARN] 找不到骨架文件(_HRC/_SKL),模型将无骨骼")

    # 子网格 + 动画: 所有 <model>. 前缀 与 <model>_ 前缀 的 w3x
    kept_subs, dropped = [], []
    for b in sub_blocks:
        sid = subobject_id(b)
        ref = mesh_ref(b)
        # 默认剔除"损伤填充"子网格(*_FILL),见 main() 里的说明
        if sid and (sid.upper() in hide or "FILL" in sid.upper()):
            dropped.append(sid)
            continue
        kept_subs.append(b)
        if ref:
            f = listing.get(ref.lower())
            if f:
                plan_files.append((f, "%s.w3x" % ref))
            else:
                print("[WARN] 子网格文件缺失: %s.w3x" % ref)

    anims = []
    for key, fn in listing.items():
        if key.startswith(model.lower() + "_") and not key.endswith(("_ctr", "_hrc")):
            if fn == cfn:
                continue
            anims.append(fn)
            plan_files.append((fn, fn))
    if dropped:
        print("[OK] 剔除子对象: %s" % ", ".join(dropped))

    # 骨架名撞车:Convention B 的容器 Hierarchy 属性有时正好等于 "<MODEL>_SKN",
    # 而本脚本把骨架拷成 "<Hierarchy>.w3x" -> 会**覆盖刚写好的容器文件**
    # (2026-10-02 在 日冕 ABAIRCRAFTBUNKER / ALLIEDAEGISLARGEDEFENSEBASE 上踩到)。
    # 解决:骨架改用源文件名,并把容器的 Hierarchy 指过去。
    for _k, (_sfn, _dfn) in enumerate(list(plan_files)):
        if _dfn == out_container_name + ".w3x":
            _newname = os.path.splitext(_sfn)[0] + ".w3x"
            plan_files[_k] = (_sfn, _newname)
            hierarchy = os.path.splitext(_newname)[0]
            print("[OK] 骨架名与容器撞车 -> 骨架用 %s, 容器 Hierarchy 指向它" % _newname)

    # --- 贴图 ---
    model_files = [p[0] for p in plan_files]
    textures = collect_textures(model_files, src)
    print("[OK] 引用贴图: %s" % ", ".join(sorted(textures)))

    print("\n计划:")
    print("  容器 %s -> ART/W3X/%s/%s.w3x" % (cfn, a.dst_sub, out_container_name))
    print("  其余 %d 个 .w3x(骨架/子网格/动画) + %d 组贴图" %
          (len(plan_files), len(textures)))
    if a.dry_run:
        print("\n[dry-run] 未落盘。")
        return 0

    os.makedirs(dst, exist_ok=True)

    # 1) 容器
    with open(os.path.join(dst, out_container_name + ".w3x"), "w",
              encoding="utf-8", newline="\n") as fh:
        fh.write(build_container_text(out_container_name, hierarchy, kept_subs))

    # 2) 骨架/子网格/动画(拷贝后把着色器名统一小写 —— 见 _lowercase_shader_names)
    shader_fixed = 0
    for src_fn, dst_fn in plan_files:
        dp = os.path.join(dst, dst_fn)
        _strip_bom_copy(os.path.join(src, src_fn), dp)
        shader_fixed += _lowercase_shader_names(dp)
    if shader_fixed:
        print("[OK] 着色器名小写归一化: %d 处" % shader_fixed)

    # 3) 贴图
    missing = []
    for tex in sorted(textures):
        dds = os.path.join(src, tex + ".dds")
        if os.path.isfile(dds):
            shutil.copy(dds, os.path.join(dst, tex + ".dds"))
            ensure_texture_xml(src, dst, tex)
        else:
            # 可能大小写不同
            hit = None
            for fn in os.listdir(src):
                if fn.lower() == (tex + ".dds").lower():
                    hit = fn
                    break
            if hit:
                shutil.copy(os.path.join(src, hit),
                            os.path.join(dst, hit))
                ensure_texture_xml(src, dst, os.path.splitext(hit)[0])
            else:
                missing.append(tex)
    if missing:
        print("[WARN] 贴图缺失(游戏会退到品红): %s" % ", ".join(missing))

    print("\n[DONE] 导入完成 -> %s" % dst)
    print("  INI 里这样接:")
    print("    Draw = W3XModelDraw ModuleTag_01")
    print("      DefaultModelName = %s" % out_container_name)
    print("      ConditionState = NONE")
    print("        Model = %s" % out_container_name)
    if anims:
        print("        Animation = %s" % os.path.splitext(anims[0])[0])
        print("        AnimationMode = LOOP")
    print("      End")
    print("    End")
    return 0


if __name__ == "__main__":
    sys.exit(main())
