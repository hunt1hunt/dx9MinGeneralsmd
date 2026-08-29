#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Replace the first Draw block of each target GLA vehicle in GLAVehicle.ini
with a W3XModelDraw referencing the imported GLATgv*/GLATgb* models.
"""
import re

INI = r"E:\!!!!!!!QWCSB\Data\INI\Object\GLAVehicle.ini"


def w3x_draw(model, turret="bone_turret", pitch="bone_turretel",
             primary="bone_weapona01", secondary=None, firepoint=None):
    lines = ["  Draw = W3XModelDraw ModuleTag_01",
             "    DefaultModelName = %s_SKN" % model,
             ""]
    def state(name):
        b = ["    ConditionState = %s" % name,
             "      Model = %s_SKN" % model]
        if firepoint:
            b.append("      WeaponFireFXBone = PRIMARY %s" % firepoint)
            b.append("      WeaponLaunchBone = PRIMARY %s" % firepoint)
        else:
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


# object -> new W3X draw block
REPLACEMENTS = {
    "GLATankScorpion": w3x_draw("GLATGVSCORPION", secondary="bone_weaponb01"),
    "GLAVehicleRocketBuggy": w3x_draw("GLATGBROCKET", turret="bone_turret_a",
                                      pitch="bone_turretel_a", primary="bone_weapona01"),
    "GLAVehicleQuadCannon": w3x_draw("GLATGVQUADCANN", primary="weapona01"),
    "GLAVehicleToxinTruck": w3x_draw("GLATGVTOXINTANK", primary="bone_weapona01"),
    "GLAVehicleBombTruck": w3x_draw("GLATGVNUKETRUCK", turret=None, pitch=None, primary=None),
    "GLAVehicleScudLauncher": w3x_draw("GLATGVSCUDCANN", primary="bone_weapona01"),
    "GLAVehicleTechnical": w3x_draw("GLATGVTECHNICAL", primary="bone_weapon_a"),
    "GLALightTank": w3x_draw("GLATGVDIABLO", primary="bone_weapona01"),
    "GLATankMarauder": w3x_draw("GLATGVMARAUDER", primary="bone_weapon_a01", secondary="bone_weapon_b01"),
    "GLAVehicleRadarVan": w3x_draw("GLATGVRADARVAN", turret=None, pitch=None, primary=None),
    "GLAVehicleBattleBus": w3x_draw("GLATGVBUS", turret=None, pitch=None, firepoint="firepoint01"),
    "GLAVehicleScudLauncherHiDef": w3x_draw("GLATGVSCUDCANN2", primary="bone_weapona01"),
}

with open(INI, encoding="utf-8-sig", errors="replace") as f:
    text = f.read()

changed = []
for obj, newblock in REPLACEMENTS.items():
    # find object body
    m = re.search(r"^Object %s\b" % re.escape(obj), text, re.M)
    if not m:
        print("[WARN] object not found: %s" % obj)
        continue
    obj_start = m.start()
    # find next Object line (end of this object)
    nxt = re.search(r"\nObject \w+", text[obj_start + len(obj) + 10:], re.M)
    body_end = obj_start + len(obj) + 10 + (nxt.start() if nxt else len(text))
    body = text[obj_start:body_end]
    # find first Draw line within body
    dm = re.search(r"\n\s*Draw\s*=\s*\w+ ModuleTag_\w+", body)
    if not dm:
        print("[WARN] no Draw in %s" % obj)
        continue
    draw_start = obj_start + dm.start()
    # first 2-space-indent End after draw start (closes the Draw module)
    em = re.search(r"\n  End\n", text[draw_start:obj_start + len(body)])
    if not em:
        print("[WARN] no Draw end in %s" % obj)
        continue
    draw_end = draw_start + em.end()  # include the "\n  End\n"
    text = text[:draw_start] + "\n" + newblock + "\n" + text[draw_end:]
    changed.append(obj)

with open(INI, "w", encoding="utf-8", newline="") as f:
    f.write(text)

print("Replaced Draw blocks for: %s" % ", ".join(changed))
print("Total objects changed: %d" % len(changed))
