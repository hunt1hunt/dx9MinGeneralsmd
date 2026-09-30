#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
w3x_convert_corona.py -- convert a RA3 "Corona" (Corona MOD) source model into
the Generals W3X game format, written into a per-faction sub-directory under
Art/W3X/.

Why a second converter: w3x_convert.py targets the "Generals 2" source dump,
which kept one COMBINED <Model>_SKN.W3X holding the container plus every mesh.
The Corona dump (e.g. D:/rimian) is a per-asset-id dump instead:

    <Model>_CTR.w3x      the <W3DContainer>   (file name != container id)
    <Model>_HRC.w3x      the <W3DHierarchy>   (skeleton; _SKL also seen)
    <Model>.<SUB>.w3x    one <W3DMesh> per file
    <Model>_<ANIM>.w3x   one <W3DAnimation> per file (IDLA/ATKA/BLD/DYING/...)
    <Texture>.dds/.xml   RA3 texture asset (the engine reads the .xml, which
                         in turn names the .dds)

Two quirks of that dump drive the design:

  1. The Generals engine resolves every W3X asset by the id INSIDE the file -
     it opens Art/W3X/<id>.w3x (recursively, so a faction sub-directory is
     fine). The container file therefore has to be renamed from <Model>_CTR.w3x
     to <container id>.w3x.
  2. The container's Hierarchy attribute is MIXED CASE (CBSuperWeaponAdvanced)
     and may equal the container's own id (CUINFILTRATIONINFANTRYB_SKN), while
     the skeleton file is named <MODEL>_HRC.w3x and its <W3DHierarchy id> is the
     UPPERCASE model name. Lookups are therefore done case-insensitively and
     colliding elements are merged into one AssetDeclaration file - which is
     exactly what the engine wants for a self-skeleton model (its walls did the
     same under w3x_convert.py).

Usage:
  python w3x_convert_corona.py <src_dir> <model> <out_dir> [options]
    src_dir   Corona source folder holding <Model>* files (e.g. D:/rimian)
    model     model base name, e.g. CBSUPERWEAPONADVANCED. For a skinned model
              whose container is <Model>_SKN pass the base without _SKN
              (CUINFILTRATIONINFANTRYB) or with it - both resolve.
    out_dir   game sub-dir to write to, e.g. E:/!!!!!!!QWCSB/ART/W3X/CB

Options:
  --list            only report what would be converted (writes nothing)
  --anims A,B,C     keep only these animation id suffixes (default: every
                    <container|model>_<TAG> where TAG has no further '_')
  --no-anims        skip animations entirely
  --tex-dir DIR     extra directory to search for textures (repeatable)
  --quiet           print only warnings and errors
"""
import os
import re
import sys
import shutil

ELEMENT_TAGS = ('W3DContainer', 'W3DMesh', 'W3DHierarchy', 'W3DAnimation',
                'W3DCollisionBox')

HEADER = ('<?xml version="1.0" encoding="UTF-8"?>\n'
          '<AssetDeclaration xmlns="uri:ea.com:eala:asset" '
          'xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">\n')
FOOTER = '</AssetDeclaration>\n'


def read_utf8(path):
    with open(path, 'rb') as f:
        return f.read().decode('utf-8', errors='replace')


def attr(text, name):
    m = re.search(name + r'="([^"]*)"', text)
    return m.group(1) if m else None


def extract_elements(data, tag):
    """Raw text of every top-level <tag ...>...</tag>. Nested elements of the
    same name are skipped because scanning resumes past each match."""
    out = []
    pos = 0
    open_re = re.compile(r'<' + tag + r'\b')
    close_tok = '</' + tag + '>'
    while True:
        m = open_re.search(data, pos)
        if not m:
            break
        start = m.start()
        end = data.find(close_tok, start)
        if end < 0:
            break
        end += len(close_tok)
        out.append(data[start:end])
        pos = end
    return out


def elements_in(data):
    """[(tag, id, raw), ...] for every element we recognise, file order."""
    found = []
    for tag in ELEMENT_TAGS:
        for raw in extract_elements(data, tag):
            found.append((tag, attr(raw, 'id'), raw))
    return found


HASH_RE = re.compile(r'^0x[0-9A-Fa-f]+$')


def retarget_bone_names(container_raw, hierarchy_raw):
    """Restore readable bone names on a hierarchy that shipped hashed pivots.

    RA3 ships its pivots with hashed names (0x4E16B123) and Corona's dump only
    recovered some of them. The engine resolves both Turret/TurretPitch and
    WeaponFireFXBone/WeaponMuzzleFlashBone with getBoneIndexByName(), i.e. by
    NAME only - so a hashed pivot can never be wired from INI and its barrel or
    door silently stays static.

    The container's SubObject list names the same bones (SubObjectID, e.g.
    BONE_LAUNCHER) and gives their index (BoneIndex), so the hashed pivots can
    be renamed from it. Bone indices are untouched, so vertex skinning and the
    animation channels (both index-based) are unaffected.

    Guards: index 0 (roottransform) is never renamed, collision boxes are
    skipped (no <Mesh>), a name already present in the hierarchy is never
    duplicated (a duplicate would make Get_Bone_Index rotate the wrong bone),
    and for two sub-objects sharing one index the first one wins.

    Returns (new_hierarchy_raw, renamed_count).
    """
    fmap = {}
    for raw in extract_elements(container_raw, 'SubObject'):
        if '<Mesh>' not in raw:
            continue
        sid = attr(raw, 'SubObjectID')
        bi = attr(raw, 'BoneIndex')
        if not sid or bi is None:
            continue
        try:
            bi = int(bi)
        except ValueError:
            continue
        if bi > 0:
            fmap.setdefault(bi, sid)
    if not fmap:
        return hierarchy_raw, 0

    pivots = list(re.finditer(r'<Pivot\s+Name="([^"]*)"', hierarchy_raw))
    taken = set(m.group(1).lower() for m in pivots
                if not HASH_RE.match(m.group(1)))

    out = []
    last = 0
    renamed = 0
    for i, m in enumerate(pivots):
        name = m.group(1)
        text = m.group(0)
        if HASH_RE.match(name) and i in fmap:
            cand = fmap[i]
            if cand.lower() not in taken:
                taken.add(cand.lower())
                text = text.replace('Name="%s"' % name, 'Name="%s"' % cand)
                renamed += 1
        out.append(hierarchy_raw[last:m.start()])
        out.append(text)
        last = m.end()
    out.append(hierarchy_raw[last:])
    return ''.join(out), renamed


def strip_decl(raw):
    """Element text without a surrounding AssetDeclaration (source files that
    already carry one, e.g. combined container+animation files)."""
    start = raw.find('<')
    if '<AssetDeclaration' in raw:
        start = raw.find('>', raw.find('<AssetDeclaration')) + 1
    end = raw.rfind('</AssetDeclaration>')
    if end < 0:
        end = len(raw)
    return raw[start:end].strip()


def write_asset(path, elements):
    """Write one AssetDeclaration file holding `elements` (1 or more)."""
    body = '\n'.join(strip_decl(e) for e in elements)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(HEADER + '\t' + body.replace('\n', '\n\t').rstrip() + '\n'
                + FOOTER)


def find_file(src_dir, basename):
    """Case-insensitive lookup of `basename` (+ optional .w3x) in src_dir."""
    want = basename.lower()
    if not want.endswith('.w3x'):
        want += '.w3x'
    for fn in os.listdir(src_dir):
        if fn.lower() == want:
            return os.path.join(src_dir, fn)
    return None


def resolve_container(src_dir, model):
    """Locate the container file -> (path, container_id, hierarchy_ref, raw)."""
    for suffix in ('_CTR', '_SKN', '_SKN_CTR', '', '_BLD'):
        p = find_file(src_dir, model + suffix)
        if not p:
            continue
        conts = extract_elements(read_utf8(p), 'W3DContainer')
        if conts:
            return p, attr(conts[0], 'id'), attr(conts[0], 'Hierarchy'), conts[0]
    return None, None, None, None


def anim_suffix_ok(anim_id, model, cid, allow):
    """A plain animation tag carries no further '_' after the id it extends."""
    for base in (cid, model):
        if base and anim_id.upper().startswith(base.upper() + '_'):
            suffix = anim_id[len(base) + 1:]
            if allow:
                return suffix.upper() in allow
            return '_' not in suffix
    return False


def main():
    args = sys.argv[1:]
    if len(args) < 3:
        print(__doc__)
        sys.exit(2)

    src_dir, model, out_dir = args[0], args[1], args[2]
    opt = args[3:]
    do_list = '--list' in opt
    no_anims = '--no-anims' in opt
    quiet = '--quiet' in opt
    allow_anims = None
    if '--anims' in opt:
        allow_anims = set(x.upper() for x in
                          opt[opt.index('--anims') + 1].split(','))
    tex_dirs = [opt[i + 1] for i, a in enumerate(opt)
                if a == '--tex-dir' and i + 1 < len(opt)]

    if not os.path.isdir(src_dir):
        print('ERROR: src_dir not found: %s' % src_dir)
        sys.exit(1)
    src_dir = os.path.abspath(src_dir)
    out_dir = os.path.abspath(out_dir)

    cpath, cid, hierarchy, container = resolve_container(src_dir, model)
    if not cpath:
        print('ERROR: no container found for model %s in %s' % (model, src_dir))
        sys.exit(1)
    print('model=%s container=%s hierarchy=%s (from %s)'
          % (model, cid, hierarchy, os.path.basename(cpath)))

    # ----------------------------------------------------------------- index
    # Every model-prefixed file is parsed and indexed by element id. The index
    # is keyed lower-case (the dump mixes case between ids and references) and
    # holds a list, because a skeleton legitimately shares the container's id.
    index = {}          # lower(id) -> [(tag, id, raw, src), ...]
    src_of = {}         # lower(id) -> src path (for reporting)
    candidates = [cpath]
    prefix = model.lower()
    for fn in sorted(os.listdir(src_dir)):
        low = fn.lower()
        if low.endswith('.w3x') and low.startswith(prefix):
            p = os.path.join(src_dir, fn)
            if p not in candidates:
                candidates.append(p)

    def add_elements(src, data):
        n = 0
        for tag, eid, raw in elements_in(data):
            if not eid:
                continue
            key = eid.lower()
            index.setdefault(key, []).append((tag, eid, raw, src))
            src_of.setdefault(key, src)
            n += 1
        return n

    for src in candidates:
        add_elements(src, read_utf8(src))

    def lookup(ref):
        """Resolve an asset reference; True when something was found."""
        if ref.lower() in index:
            return True
        p = find_file(src_dir, ref)
        if not p:
            return False
        add_elements(p, read_utf8(p))
        return ref.lower() in index

    # ---------------------------------------------------------------- select
    refs = []
    for raw in extract_elements(container, 'SubObject'):
        m = (re.search(r'<Mesh>([^<]*)</Mesh>', raw)
             or re.search(r'<CollisionBox>([^<]*)</CollisionBox>', raw))
        if m:
            refs.append(m.group(1).strip())

    missing = [r for r in (refs + ([hierarchy] if hierarchy else []))
               if not lookup(r)]

    written = {}        # real id -> [(tag, raw), ...]
    written_src = {}    # real id -> src path

    def want(ref):
        key = ref.lower()
        if key not in index:
            return
        for tag, eid, raw, src in index[key]:
            # a skeleton that shares the container id must be merged in
            if tag == 'W3DAnimation' and not anim_ok(src, eid):
                continue
            lst = written.setdefault(eid, [])
            if not any(r is raw for _, r in lst):
                lst.append((tag, raw))
                written_src.setdefault(eid, src)

    anim_sel = {}       # src path -> set of animation ids to keep

    def anim_ok(src, eid):
        if no_anims:
            return False
        if src not in anim_sel:
            return False
        return eid in anim_sel[src]

    # pick animations first so want() can honour the selection
    if not no_anims:
        for src in candidates:
            for tag, eid, raw in elements_in(read_utf8(src)):
                if tag == 'W3DAnimation' and eid and \
                        anim_suffix_ok(eid, model, cid, allow_anims):
                    anim_sel.setdefault(src, set()).add(eid)

    want(cid)
    for r in refs:
        want(r)
    if hierarchy:
        want(hierarchy)

    n_anim = 0
    for src, ids in anim_sel.items():
        for eid in sorted(ids):
            before = len(written.get(eid, ()))
            want(eid)
            if len(written.get(eid, ())) > before:
                n_anim += 1
    # skinned animations often bind a different rig than the container's (the
    # build/pack "T" hierarchy), so pull those in as well
    extra_hist = []
    for eid, lst in list(written.items()):
        for tag, raw in lst:
            if tag != 'W3DAnimation':
                continue
            h = attr(raw, 'Hierarchy')
            if h and h.lower() not in set(x.lower() for x in written):
                extra_hist.append(h)
    for h in extra_hist:
        if h.lower() in set(x.lower() for x in written):
            continue
        if lookup(h):
            want(h)
        else:
            missing.append(h)

    # ---------------------------------------------------------------- textures
    tex_names = set()
    tex_re = re.compile(r'<Texture Name="[^"]*">\s*<Value>([^<]*)</Value>')
    inc_re = re.compile(r'<Include[^>]*source="ART:([^"]+)\.xml"')
    for eid, lst in written.items():
        for tag, raw in lst:
            if tag != 'W3DMesh':
                continue
            tex_names.update(x.strip() for x in tex_re.findall(raw))
            tex_names.update(x.strip() for x in inc_re.findall(raw))

    if missing:
        print('  WARN: %d referenced asset(s) missing from the source dump: %s'
              % (len(set(missing)), ', '.join(sorted(set(missing))[:12])))

    if do_list:
        print('  would write %d file(s), %d animation(s):'
              % (len(written), n_anim))
        for eid in sorted(written):
            tags = '+'.join(t for t, _ in written[eid])
            print('    %-58s %s' % (eid + '.w3x', tags))
        print('  textures: %s' % ', '.join(sorted(tex_names)))
        return

    # ----------------------------------------------------------------- write
    os.makedirs(out_dir, exist_ok=True)
    n_new = n_skip = n_bone = 0
    for eid, lst in written.items():
        dst = os.path.join(out_dir, eid + '.w3x')
        if os.path.exists(dst):
            n_skip += 1
            continue
        raws = []
        for tag, raw in lst:
            if tag == 'W3DHierarchy':
                raw, n = retarget_bone_names(container, raw)
                n_bone += n
            raws.append(raw)
        write_asset(dst, raws)
        n_new += 1

    search = [src_dir, os.path.dirname(src_dir)] + tex_dirs
    for d in (src_dir, os.path.dirname(src_dir)):
        if os.path.basename(d).upper() == 'ART':
            search.append(d)
    tex_map = {}
    for d in search:
        if not d or not os.path.isdir(d):
            continue
        for fn in os.listdir(d):
            base, ext = os.path.splitext(fn)
            if ext.lower() in ('.dds', '.tga', '.xml'):
                tex_map.setdefault(base.lower(), []).append(os.path.join(d, fn))

    n_tex = 0
    tex_missing = []
    for t in sorted(tex_names):
        hits = tex_map.get(t.lower())
        if not hits:
            tex_missing.append(t)
            continue
        for s in hits:
            d = os.path.join(out_dir, os.path.basename(s))
            if not os.path.exists(d):
                shutil.copy2(s, d)
                n_tex += 1
    if tex_missing:
        print('  WARN: texture(s) not found (mesh renders untextured): %s'
              % ', '.join(tex_missing))

    if not quiet:
        print('  wrote %d file(s), %d animation(s), skipped %d existing'
              % (n_new, n_anim, n_skip))
        if n_bone:
            print('  restored %d readable bone name(s) from the container'
                  % n_bone)
        print('  copied %d texture file(s) for %d texture(s)'
              % (n_tex, len(tex_names)))
        print('  out: %s' % out_dir)


if __name__ == '__main__':
    main()
