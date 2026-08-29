#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Replace the primary Draw block of target aircraft in ChinaAir.ini / AmericaAir.ini
with a W3XModelDraw referencing imported APATaa*/EUTea* models.
"""
import re

FILES = [r"E:\!!!!!!!QWCSB\Data\INI\Object\ChinaAir.ini",
         r"E:\!!!!!!!QWCSB\Data\INI\Object\AmericaAir.ini"]


def w3x_draw(model, turret=None, pitch=None, primary=None, secondary=None):
    lines = ["  Draw = W3XModelDraw ModuleTag_01",
             "    DefaultModelName = %s_SKN" % model,
             ""]
    def state(name):
        b = ["    ConditionState = %s" % name,
             "      Model = %s_SKN" % model]
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
        b.append("      Animation = %s_IDLA" % model)
        b.append("      AnimationMode = LOOP")
        b.append("    End")
        return b
    lines += state("NONE")
    lines.append("")
    lines += state("REALLYDAMAGED")
    lines.append("  End")
    return "\n".join(lines)


# object -> new W3X draw
REPLACEMENTS = {
    # China
    "ChinaVehicleHelix": w3x_draw("APATAAHELIX", turret="bone_turret", pitch="bone_turretel",
                                  primary="bone_weapona01", secondary="bone_weaponr01"),
    "ChinaJetMIG": w3x_draw("APATAAMIG", primary="bone_weapona01", secondary="bone_weapona02"),
    # America
    "AmericaJetRaptor": w3x_draw("EUTEARAPTOR", primary="bone_weapona01", secondary="bone_weapona02"),
    "AmericaJetAurora": w3x_draw("EUTEAAURORA"),
    "AmericaJetStealthFighter": w3x_draw("EUTEASTEALTH", turret="bone_turret", pitch="bone_turretel",
                                         primary="weaponc01"),
    "AmericaVehicleComanche": w3x_draw("EUTEACOMANCHES", turret="bone_turret", pitch="bone_turretel",
                                       primary="bone_weapona01", secondary="bone_weaponb01"),
}

# Robust Draw-line match: tolerates double spaces ("W3DOverlordAircraftDraw  ModuleTag_01;")
# and trailing comments after ModuleTag.
DRAW_RE = re.compile(r"\n[ \t]*Draw[ \t]*=[ \t]*[A-Za-z_]\w*[ \t]+ModuleTag_\w+")

for INI in FILES:
    with open(INI, encoding="utf-8-sig", errors="replace") as f:
        text = f.read()
    changed = []
    for obj, newblock in REPLACEMENTS.items():
        m = re.search(r"^Object %s\b" % re.escape(obj), text, re.M)
        if not m:
            continue  # object may live in the other file
        obj_start = m.start()
        # object body ends at the next top-level Object line
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
        text = text[:draw_start] + "\n" + newblock + "\n" + text[draw_end:]
        changed.append(obj)
    with open(INI, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    print("%s: replaced %d (%s)" % (INI.split("\\")[-1], len(changed), ", ".join(changed)))
