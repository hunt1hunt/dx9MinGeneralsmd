#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_w3x_conventions.py — validate RA3->Generals W3X model replacement conventions.

Scans W3X container models (ART/W3X) and verifies the per-mesh shader + texture
conventions the W3X renderer (W3XModelDraw / w3x_soviet.fx / w3x_infantry.fx)
depends on, so a model replacement that mixes soldiers and vehicles renders each
side with the correct texture.

Conventions checked per sub-mesh:
  * infantry.fx meshes (soldiers) must use the BASIC convention:
        single Texture_0 = <soldier texture>   (texture-ALPHA = faction mask)
        NO DiffuseTexture / NormalMap / SpecMap
  * objects*.fx meshes (vehicles/buildings) must use the PBR convention:
        DiffuseTexture / NormalMap / SpecMap = <vehicle texture>
        NO Texture_0
  * a mesh must never mix both conventions (Texture_0 + DiffuseTexture).

Also simulates the engine's model-wide shader selection (any PBR mesh forces
w3x_soviet.fx; only an all-BASIC model routes to w3x_infantry.fx) and flags the
classic bug where a soldier mesh wears the vehicle's diffuse texture.

Usage:
  python check_w3x_conventions.py [W3X_DIR] [MODEL_NAMES...]
    W3X_DIR      default: E:/!!!!!!!QWCSB/ART/W3X
    MODEL_NAMES  optional; if omitted, all <name>_SKN.w3x containers are scanned.
"""
import os
import re
import sys
import glob
import xml.etree.ElementTree as ET

# ---------------------------------------------------------------------------
# W3X XML parsing (files are RA3 XML; strip the default namespace first)
# ---------------------------------------------------------------------------
def parse_xml(path):
    data = open(path, 'rb').read().decode('utf-8', errors='replace')
    # Strip xmlns / xmlns:xsi default-namespace declarations so tags are plain.
    data = re.sub(r'\sxmlns(:\w+)?="[^"]*"', '', data)
    return ET.fromstring(data)


def children(node, name):
    return [c for c in node if c.tag == name]


def parse_container(path):
    """Return (id, hierarchy, [(subObjectID, meshName), ...]) in file order."""
    try:
        root = parse_xml(path)
    except ET.ParseError as e:
        return None, None, None, str(e)
    container = next((c for c in root if c.tag == 'W3DContainer'), None)
    if container is None:
        return None, None, None, 'no <W3DContainer>'
    subs = []
    for so in children(container, 'SubObject'):
        sid = so.get('SubObjectID')
        ro = next((c for c in so if c.tag == 'RenderObject'), None)
        mesh = None
        if ro is not None:
            for c in ro:
                if c.tag == 'Mesh':
                    mesh = (c.text or '').strip()
                    break
        if mesh:
            subs.append((sid, mesh))
    return (container.get('id'), container.get('Hierarchy'), subs, None)


def parse_mesh(path):
    """Return (meshID, fxShader, {constName: constValue}) for the texture constants."""
    try:
        root = parse_xml(path)
    except ET.ParseError as e:
        return None, None, None, str(e)
    mesh = next((c for c in root if c.tag == 'W3DMesh'), None)
    if mesh is None:
        return None, None, None, 'no <W3DMesh>'
    shader = None
    constants = {}
    for c in mesh:
        if c.tag == 'FXShader':
            shader = c.get('ShaderName')
            for consts in children(c, 'Constants'):
                for const in consts:
                    if const.tag == 'Texture':
                        name = const.get('Name')
                        val = const.find('Value')
                        if val is not None and val.text:
                            constants[name] = val.text.strip()
    return mesh.get('id'), shader, constants, None


# ---------------------------------------------------------------------------
# Classification + checks
# ---------------------------------------------------------------------------
SOLDIER_SHADERS = ('infantry.fx',)
VEHICLE_SHADER_HINTS = ('objects', 'tread')
PBR_CONSTS = ('DiffuseTexture', 'NormalMap', 'SpecMap')


def classify_mesh(shader, constants):
    """Return (role, hasBasic, hasPBR). role in SOLDIER/VEHICLE/OTHER/PLACEHOLDER."""
    hasBasic = 'Texture_0' in constants
    hasPBR = any(k in PBR_CONSTS for k in constants)
    s = (shader or '').lower()
    if not shader or shader == 'defaultw3d.fx':
        return 'PLACEHOLDER', hasBasic, hasPBR
    if any(h in s for h in SOLDIER_SHADERS):
        return 'SOLDIER', hasBasic, hasPBR
    if any(h in s for h in VEHICLE_SHADER_HINTS):
        return 'VEHICLE', hasBasic, hasPBR
    return 'OTHER', hasBasic, hasPBR


def check_model(model, w3x_dir):
    """Return (errors, warnings, info) for one container model."""
    errors, warnings, info = [], [], []

    cpath = os.path.join(w3x_dir, model + '.w3x')
    if not os.path.isfile(cpath):
        return ['container missing: %s' % cpath], [], []
    cid, hier, subs, cerr = parse_container(cpath)
    if cerr:
        return ['container parse error %s: %s' % (model, cerr)], [], []

    mesh_info = []          # (sid, role, shader, constants)
    for sid, mesh_name in subs:
        mpath = os.path.join(w3x_dir, mesh_name + '.w3x')
        if not os.path.isfile(mpath):
            errors.append('[%s] sub-mesh missing: %s' % (model, mpath))
            continue
        mid, shader, consts, merr = parse_mesh(mpath)
        if merr:
            errors.append('[%s] mesh parse error %s: %s' % (model, mesh_name, merr))
            continue
        role, hasBasic, hasPBR = classify_mesh(shader, consts)
        mesh_info.append((sid, role, shader, consts))

        if role == 'PLACEHOLDER':
            continue
        # 1) soldier mesh must be BASIC (Texture_0) only
        if role == 'SOLDIER':
            if not hasBasic:
                errors.append(
                    '[%s] soldier mesh %s (shader=%s) has NO Texture_0 - must use '
                    'the BASIC convention (Texture_0 + soldier texture).' % (model, sid, shader))
            if hasPBR:
                errors.append(
                    '[%s] soldier mesh %s declares PBR maps (DiffuseTexture/'
                    'NormalMap/SpecMap) - remove them; soldiers use single Texture_0.' % (model, sid))
        # 2) vehicle mesh must be PBR (DiffuseTexture)
        elif role == 'VEHICLE':
            if not hasPBR:
                errors.append(
                    '[%s] vehicle mesh %s (shader=%s) has NO DiffuseTexture - must '
                    'use the PBR convention (DiffuseTexture/NormalMap/SpecMap).' % (model, sid, shader))
            if hasBasic:
                errors.append(
                    '[%s] vehicle mesh %s declares Texture_0 - remove it; vehicles '
                    'use DiffuseTexture.' % (model, sid))
        # 3) never mix conventions on one mesh
        if hasBasic and hasPBR:
            errors.append(
                '[%s] mesh %s mixes conventions (Texture_0 + DiffuseTexture) - '
                'pick one.' % (model, sid))

    # 4) soldier wearing the vehicle texture (the QuadCannon BODY03 bug)
    vehicle_tex = set()
    for sid, role, shader, consts in mesh_info:
        if role == 'VEHICLE':
            vt = consts.get('DiffuseTexture')
            if vt:
                vehicle_tex.add(vt)
    for sid, role, shader, consts in mesh_info:
        if role == 'SOLDIER':
            st = consts.get('Texture_0')
            if st and st in vehicle_tex:
                errors.append(
                    '[%s] soldier mesh %s wears the VEHICLE texture "%s" - change '
                    'its Texture_0 to the soldier texture (e.g. TgiRifleS).' % (model, sid, st))

    # 5) simulate engine model-wide shader selection (any PBR -> soviet; all-BASIC -> infantry)
    hasPBR = any(role == 'VEHICLE' or (role == 'OTHER' and any(k in PBR_CONSTS for k in c))
                 for _, role, _, c in mesh_info)
    hasBasic = any(role == 'SOLDIER' for _, role, _, _ in mesh_info)
    if hasPBR:
        model_fx = 'w3x_soviet.fx'
    elif hasBasic:
        model_fx = 'w3x_infantry.fx'
    else:
        model_fx = '(none / defaultw3d)'
    info.append('[%s] model-wide shader = %s (Hierarchy=%s, %d sub-meshes)' %
                (model, model_fx, hier, len(mesh_info)))

    # mixed soldier+vehicle: engine fix guarantees model stays PBR; soldiers
    # are overridden per-sub-mesh to w3x_infantry.fx.
    if hasPBR and hasBasic:
        info.append('[%s] mixed soldier+vehicle model -> model stays PBR, '
                    'soldier meshes override to w3x_infantry.fx (correct).' % model)

    # 6) referenced texture .dds existence (warn)
    for sid, role, shader, consts in mesh_info:
        for k, v in consts.items():
            if os.path.isfile(os.path.join(w3x_dir, v + '.dds')):
                continue
            if os.path.isfile(os.path.join(w3x_dir, v + '.xml')):
                continue
            warnings.append('[%s] mesh %s references texture "%s" but no %s.dds/.xml found'
                            % (model, sid, v, v))

    return errors, warnings, info


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
def main():
    args = sys.argv[1:]
    w3x_dir = args[0] if args and os.path.isdir(args[0]) else r'E:/!!!!!!!QWCSB/ART/W3X'
    model_names = args[1:] if args and os.path.isdir(args[0]) else args

    if not os.path.isdir(w3x_dir):
        print('ERROR: W3X dir not found: %s' % w3x_dir)
        sys.exit(2)

    if not model_names:
        model_names = sorted(
            os.path.splitext(os.path.basename(f))[0]
            for f in glob.glob(os.path.join(w3x_dir, '*_SKN.w3x'))
            if not os.path.basename(f).startswith('_'))

    total_err = total_warn = 0
    for model in model_names:
        errors, warnings, info = check_model(model, w3x_dir)
        if not (errors or warnings or info):
            print('== %s : (no meshes parsed)' % model)
            continue
        print('\n== %s' % model)
        for line in info:
            print('   I: ' + line)
        for line in warnings:
            print('   W: ' + line)
        for line in errors:
            print('   E: ' + line)
        total_err += len(errors)
        total_warn += len(warnings)

    print('\n==== summary: %d model(s), %d error(s), %d warning(s) ====' %
          (len(model_names), total_err, total_warn))
    sys.exit(1 if total_err else 0)


if __name__ == '__main__':
    main()
