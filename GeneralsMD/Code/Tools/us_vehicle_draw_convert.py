#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Replace the Draw block(s) of target America vehicles in AmericaVehicle.ini
with a single W3XModelDraw referencing imported EUTev* models.
"""
import re

INI = r"E:\!!!!!!!QWCSB\Data\INI\Object\AmericaVehicle.ini"


def w3x_draw(model, turret="bone_turret", pitch="bone_turretel",
             primary="bone_weapona01", anim=True):
    lines = ["  Draw = W3XModelDraw ModuleTag_01",
             "    DefaultModelName = %s_SKN" % model,
             ""]
    def state(name):
        b = ["    ConditionState = %s" % name,
             "      Model = %s_SKN" % model,
             "      Turret = %s" % turret,
             "      TurretPitch = %s" % pitch,
             "      WeaponFireFXBone = PRIMARY %s" % primary,
             "      WeaponLaunchBone = PRIMARY %s" % primary]
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


REPLACEMENTS = {
    "AmericaVehicleTomahawk": w3x_draw("EUTEVTOMAHAWK", primary="weapona01"),
    "AmericaTankMicrowave": w3x_draw("EUTEVMICROWAVE", primary="bone_weapona", anim=False),
    "AmericaTankAvenger": w3x_draw("EUTEVAVENGER", pitch="bone_turretel_a", primary="bone_weapona01"),
    "AmericaVehicleSentryDrone": w3x_draw("EUTEVSENTRY", primary="bone_weapona01"),
    "AmericaTankPaladin": w3x_draw("EUTEVPALADIN", primary="weapona01"),
    "AmericaTankCrusader": w3x_draw("EUTEVCRUSADER", pitch="bone_turrtel", primary="bone_weapona01"),
}

with open(INI, encoding="utf-8-sig", errors="replace") as f:
    text = f.read()

changed = []
for obj, newblock in REPLACEMENTS.items():
    m = re.search(r"^Object %s\b" % re.escape(obj), text, re.M)
    if not m:
        print("[WARN] object not found: %s" % obj)
        continue
    obj_start = m.start()
    # find first Draw within the object body
    dm = re.search(r"\n\s*Draw\s*=\s*\w+ ModuleTag_\w+", text[obj_start:])
    if not dm:
        print("[WARN] no Draw in %s" % obj)
        continue
    draw_start = obj_start + dm.start()
    # DESIGN comment after the draw region, else object end
    after = text[draw_start:]
    design = re.search(r"; \*{2,3}DESIGN", after)
    obj_end = re.search(r"\nEnd\s*\n(?:;|Object|\Z)", after)
    limit = draw_start + (design.start() if design else (obj_end.start() if obj_end else len(text)))
    region = text[draw_start:limit]
    # last 2-space-indent End closes the final Draw module
    ems = list(re.finditer(r"\n  End\n", region))
    if not ems:
        print("[WARN] no Draw end in %s" % obj)
        continue
    draw_end = draw_start + ems[-1].end()
    text = text[:draw_start] + "\n" + newblock + "\n" + text[draw_end:]
    changed.append(obj)

with open(INI, "w", encoding="utf-8", newline="") as f:
    f.write(text)

print("Replaced Draw for: %s" % ", ".join(changed))
print("Total: %d" % len(changed))
