#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
w3x_gen_ini.py — generate the W3XModelDraw INI block additions for a building
from its model skeleton, automatically. Implements the B2-B6 fixes:

  B2  smoke particles:   detect FX_SMOKE* skeleton bones -> ParticleSysBone SteamVent
  B3  bunker firepoints: detect FIREPOINT* bones -> GarrisonContain FiringOffset
  B4  door module:       detect a DOOR model -> DOOR_1_OPENING/WAITING_OPEN/CLOSING
  B5  turret bones:      detect BONE_TURRET / BONE_TURRETEL / BONE_WEAPON* ->
                         Turret / TurretPitch / WeaponFireFXBone
  B6  exit points:       door position (from the door skeleton) ->
                         UnitCreatePoint / NaturalRallyPoint

Usage:
  python w3x_gen_ini.py <model> [W3X_DIR]
    model     e.g. APAPOWERPLANT_SKN  (reads <model> + <Hierarchy> skeleton)
    W3X_DIR   default E:/!!!!!!!QWCSB/ART/W3X
Prints the generated INI lines to stdout (ready to paste into the object).
"""
import os
import re
import sys

DEFAULT_W3X = r'E:/!!!!!!!QWCSB/ART/W3X'


def read(p):
    with open(p, 'rb') as f:
        return f.read().decode('utf-8', errors='replace')


def pivot_translations(skl_data):
    """Return {pivotName: (x,y,z)} for every <Pivot ...> with a Translation."""
    out = {}
    for m in re.finditer(
            r'<Pivot Name="([^"]*)" Parent="([^"]*)">\s*'
            r'<Translation X="([^"]*)" Y="([^"]*)" Z="([^"]*)">?', skl_data):
        out[m.group(1)] = (float(m.group(3)), float(m.group(4)), float(m.group(5)))
    return out


def pivot_parents(skl_data):
    return {m.group(1): int(m.group(2))
            for m in re.finditer(r'<Pivot Name="([^"]*)" Parent="([^"]*)"', skl_data)}


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    model = sys.argv[1]
    w3x_dir = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_W3X

    cpath = os.path.join(w3x_dir, model + '.w3x')
    if not os.path.isfile(cpath):
        print('ERROR: %s not found' % cpath); sys.exit(1)
    cdata = read(cpath)
    hier_m = re.search(r'Hierarchy="([^"]*)"', cdata)
    if not hier_m:
        print('ERROR: no Hierarchy in %s' % cpath); sys.exit(1)
    hier = hier_m.group(1)

    skl_path = os.path.join(w3x_dir, hier + '.w3x')
    if not os.path.isfile(skl_path):
        print('ERROR: skeleton %s not found' % skl_path); sys.exit(1)
    skl = read(skl_path)
    trans = pivot_translations(skl)
    parents = pivot_parents(skl)

    lines = []
    # ---- B2: smoke particles ----
    smoke = sorted(k for k in trans if re.match(r'^FX_SMOKE', k))
    if smoke:
        lines.append('    ; B2 smoke (auto-detected FX_SMOKE bones)')
        for b in smoke:
            lines.append('      ParticleSysBone = %s SteamVent' % b)

    # ---- B3: bunker firepoints ----
    fps = sorted(k for k in trans if re.match(r'^FIREPOINT', k))
    if fps:
        lines.append('    ; B3 garrison firepoints (auto-detected FIREPOINT bones)')
        # use the first few (ContainMax) spread around the windows
        for b in fps[:8]:
            x, y, z = trans[b]
            lines.append('      FiringOffset = X: %.1f Y: %.1f Z: %.1f' % (x, y, z))

    # ---- B5: turret / weapon bones ----
    turret = next((k for k in trans if k.upper() == 'BONE_TURRET' or 'TURRET' in k and 'EL' not in k), None)
    pitch = next((k for k in trans if 'TURRETEL' in k), None)
    weapon = next((k for k in trans if re.match(r'^BONE_WEAPON', k)), None)
    if turret or weapon:
        lines.append('    ; B5 turret/weapon (auto-detected)')
        if turret:
            lines.append('      Turret = %s' % turret)
        if pitch:
            lines.append('      TurretPitch = %s' % pitch)
        if weapon:
            lines.append('      WeaponFireFXBone = PRIMARY %s' % weapon)
            lines.append('      WeaponLaunchBone = PRIMARY %s' % weapon)

    # ---- B4/B6: door module + exit points from the door model ----
    # door model = <model with _DOOR> container(s); read door bone translations
    door_cands = []
    base = model.replace('_SKN', '')
    for fn in os.listdir(w3x_dir):
        if re.match(base + r'_DOOR\w*\.w3x', fn, re.I):
            door_cands.append(fn)
    for door_file in sorted(door_cands):
        ddoor = read(os.path.join(w3x_dir, door_file))
        door_id = re.search(r'<W3DContainer id="([^"]*)"', ddoor)
        if not door_id:
            continue
        door_model = door_id.group(1)
        lines.append('')
        lines.append('  ; ----------------- RA3 door (animated, opens/closes) -------------------')
        lines.append('  Draw = W3XModelDraw ModuleTag_09')
        lines.append('    DefaultModelName = %s' % door_model)
        for state, mode in (('NONE', None), ('DOOR_1_OPENING', 'ONCE'),
                            ('DOOR_1_WAITING_OPEN', 'MANUAL'), ('DOOR_1_CLOSING', 'ONCE_BACKWARDS')):
            lines.append('    ConditionState = %s' % state)
            lines.append('      Model = %s' % door_model)
            if mode:
                lines.append('      Animation = %s' % door_model)
                lines.append('      AnimationMode = %s' % mode)
            lines.append('    End')
        lines.append('  End')
        # B6 exit from the FIRST door's position
        dtrans = pivot_translations(ddoor)
        door_bones = {k: v for k, v in dtrans.items() if re.match(r'^SKIN_DOOR|^DOOR', k)}
        if door_bones and door_file == door_cands[0]:
            dx = max(v[0] for v in door_bones.values())
            dy = sum(v[1] for v in door_bones.values()) / len(door_bones)
            lines.append('    ; B6 exit (door at X=%.0f, Y=%.1f)' % (dx, dy))
            lines.append('      UnitCreatePoint   = X: %.1f Y: %.1f Z: 0.0 ; door at X=%.0f, exit toward +X' % (dx - 3, dy, dx))
            lines.append('      NaturalRallyPoint = X: %.1f Y: %.1f Z: 0.0' % (dx, dy))

    if not lines:
        print('# no auto-detectable additions for %s' % model)
        return
    print('# Generated INI additions for %s (paste into the W3XModelDraw block):' % model)
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
