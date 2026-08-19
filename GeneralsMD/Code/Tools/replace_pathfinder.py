#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
replace_pathfinder.py - Replace the US Pathfinder with the RA3 Allied Sniper.

1. Copy the RA3 sniper assets (model EUTEISINPER_SKN + skeleton
   GU_SNPRSH_SKL + 30 GU_SNPRSH animations + TeiSinper textures) from the
   RA3 resource dir into the game Art\\W3X dir.
2. Patch Data\\INI\\Object\\AmericaInfantry.ini: replace the
   AmericaInfantryPathfinder W3DModelDraw block with a W3XModelDraw
   (model EUTEISINPER_SKN, GU_SNPRSH animations per condition state).
   A .bak backup is made first. Idempotent (safe to re-run).

Run via Bash:  python Tools/replace_pathfinder.py
"""
import os
import re
import shutil
import sys

SRC = r"E:\红警3解压后资源文件\其他资源文件"
DST = r"E:\!!!!!!!QWCSB\!!!!!!!QWCSB\Art\W3X"
INI = r"E:\!!!!!!!QWCSB\!!!!!!!QWCSB\Data\INI\Object\AmericaInfantry.ini"

CORE = [
    "EUTEISINPER_SKN.w3x",
    "EUTEISINPER_SKN.SKIN_BODY01.w3x",
    "EUTEISINPER_SKN.SKIN_BODY02.w3x",
    "EUTEISINPER_SKN.SKIN_WEAPON_A.w3x",
    "EUTEISINPER_SKN.XBOX01.w3x",
    "GU_SNPRSH_SKL.w3x",
    "TeiSinper.dds",
    "TeiSinper.xml",
    "TeiSinper_NRM.dds",
    "TeiSinper_NRM.xml",
]


def step_copy():
    print("=" * 60)
    print("replace_pathfinder.py - US Pathfinder -> RA3 Allied Sniper")
    print("=" * 60)
    if not os.path.isdir(SRC):
        print("[ERROR] source dir not found:", SRC)
        return False
    if not os.path.isdir(DST):
        print("[ERROR] target dir not found:", DST)
        return False
    if not os.path.isfile(INI):
        print("[ERROR] ini not found:", INI)
        return False

    # verify core files present
    missing = [f for f in CORE if not os.path.isfile(os.path.join(SRC, f))]
    if missing:
        print("[ERROR] missing source files:", ", ".join(missing))
        return False
    print(f"[Step 1/3] all {len(CORE)} core source files present.")

    # copy core files
    ok = fail = 0
    for f in CORE:
        try:
            shutil.copy2(os.path.join(SRC, f), os.path.join(DST, f))
            ok += 1
        except Exception as e:  # noqa: BLE001
            print(f"[FAIL] {f}: {e}")
            fail += 1
    print(f"[Step 2/3] core copied: {ok} ok, {fail} fail.")

    # copy all GU_SNPRSH animations
    n = 0
    for name in sorted(os.listdir(SRC)):
        if re.fullmatch(r"GU_SNPRSH_[\w]*\.w3x", name):
            try:
                shutil.copy2(os.path.join(SRC, name), os.path.join(DST, name))
                n += 1
            except Exception as e:  # noqa: BLE001
                print(f"[FAIL] {name}: {e}")
    print(f"[Step 2/3] GU_SNPRSH animations copied: {n}.")
    return True


W3X_BLOCK = """  Draw = W3XModelDraw ModuleTag_01
    DefaultModelName = EUTEISINPER_SKN

    ConditionState = NONE
      Model = EUTEISINPER_SKN
      Animation = GU_SNPRSH_AIDA
      AnimationMode = LOOP
      WeaponFireFXBone = PRIMARY b_weapona_fx
      WeaponMuzzleFlash = PRIMARY b_weapona_fx
    End

    ConditionState = MOVING
      Model = EUTEISINPER_SKN
      Animation = GU_SNPRSH_SMVA
      AnimationMode = LOOP
      ParticleSysBone = None InfantryDustTrails
    End

    ConditionState = FIRING_A
      Model = EUTEISINPER_SKN
      Animation = GU_SNPRSH_ATKA
      AnimationMode = ONCE
    End

    ConditionState = BETWEEN_FIRING_SHOTS_A
      Model = EUTEISINPER_SKN
      Animation = GU_SNPRSH_AIDA
      AnimationMode = LOOP
    End

    ConditionState = DYING
      Model = EUTEISINPER_SKN
      Animation = GU_SNPRSH_DIEA
      AnimationMode = ONCE
    End

    ConditionState = DYING EXPLODED_FLAILING
      Model = EUTEISINPER_SKN
      Animation = GU_SNPRSH_DIEA
      AnimationMode = LOOP
    End

    ConditionState = DYING EXPLODED_BOUNCING
      Model = EUTEISINPER_SKN
      Animation = GU_SNPRSH_DIEB
      AnimationMode = ONCE
    End

    ConditionState = FREEFALL
      Model = EUTEISINPER_SKN
      Animation = GU_SNPRSH_FLYA
      AnimationMode = LOOP
    End

    ConditionState = PARACHUTING
      Model = EUTEISINPER_SKN
      Animation = GU_SNPRSH_FLYA
      AnimationMode = LOOP
    End
  End"""


def step_patch_ini():
    with open(INI, "r", encoding="utf-8", newline="") as fh:
        lines = fh.read().split("\n")
    # strip any \r so we work with clean logical lines
    lines = [l.rstrip("\r") for l in lines]

    obj = "AmericaInfantryPathfinder"
    obj_pat = re.compile(r"^\s*Object\s+" + re.escape(obj) + r"\s*$")
    obj_start = next((i for i, l in enumerate(lines) if obj_pat.match(l)), -1)
    if obj_start < 0:
        print(f"[ERROR] object '{obj}' not found in ini")
        return False

    # collect "  Draw = ..." blocks until object's column-0 End
    blocks = []
    i = obj_start + 1
    while i < len(lines):
        ln = lines[i]
        if re.match(r"^End\s*$", ln):
            break
        if re.match(r"^  Draw\s*=", ln):
            j = i + 1
            while j < len(lines) and not re.match(r"^  End\s*$", lines[j]):
                j += 1
            if j >= len(lines):
                print(f"[WARNING] unterminated Draw block at line {i + 1}; skipping")
                i += 1
                continue
            blocks.append((i, j))
            i = j + 1
        else:
            i += 1

    if not blocks:
        print(f"[ERROR] no '  Draw = ' block found for '{obj}'")
        return False
    print(f"   [OK] found {len(blocks)} Draw block(s) in '{obj}'")

    tag = "ModuleTag_01"
    m = re.search(r"ModuleTag_\w+", lines[blocks[0][0]])
    if m:
        tag = m.group(0)
    w3x_lines = W3X_BLOCK.replace("ModuleTag_01", tag).split("\n")

    out = lines[: blocks[0][0]] + w3x_lines
    cursor = blocks[0][1] + 1
    for k in range(1, len(blocks)):
        start, end = blocks[k]
        out += lines[cursor:start]
        head = lines[start].strip()
        out.append(f"  ; DISABLED by replace_pathfinder (W3D draw replaced by RA3 sniper): {head}")
        out.append(f"  ;   (block lines {start + 1}-{end + 1} removed; see .bak for original)")
        cursor = end + 1
    out += lines[cursor:]

    # backup
    bak = INI + ".replace_pathfinder.bak"
    shutil.copy2(INI, bak)
    print(f"   [OK] backed up to {bak}")

    # write back CRLF + UTF-8 no BOM.
    # newline="" => no implicit translation; we emit explicit \r\n ourselves.
    # (newline="\r\n" would translate every \n again -> \r\r\n corruption.)
    with open(INI, "w", encoding="utf-8", newline="") as fh:
        fh.write("\r\n".join(out) + "\r\n")
    print(f"   [OK] '{obj}' -> W3XModelDraw (EUTEISINPER_SKN), {len(blocks) - 1} other Draw(s) disabled")
    return True


def main():
    if not step_copy():
        sys.exit(1)
    print("\n[Step 3/3] patching ini:", INI)
    if not step_patch_ini():
        sys.exit(1)
    print("\nDone! Restart the game to verify the Pathfinder is now a sniper.")


if __name__ == "__main__":
    main()
