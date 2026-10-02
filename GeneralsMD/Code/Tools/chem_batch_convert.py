#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
chem_batch_convert.py — 化学将军(ChemicalGeneral.ini) 批量 Draw 改写(2026-10-02)

载具/飞机/步兵 -> 红警3MOD Battle Archive 中方模型; 建筑 -> 日冕+原版RA3盟军。
用法: python lazr_batch_convert.py
"""
import re, os, shutil, sys

F = r"D:\zerohour\Data\INI\Object\ChemicalGeneral.ini"
BA = r"D:\zerohour\ART\W3X\BA"

OPEN = re.compile(r"^(DefaultConditionState|ConditionState|TransitionState)\b", re.I)


def balanced_end(text, start):
    """从 Draw 行起按语法配平找块尾(INI 里 End/END 混用,必须大小写无关)。"""
    i = text.index("\n", start) + 1
    depth = 1
    while i < len(text):
        j = text.find("\n", i)
        if j < 0:
            j = len(text)
        s = text[i:j].strip()
        if OPEN.match(s):
            depth += 1
        elif s.lower() == "end":
            depth -= 1
            if depth == 0:
                return j + 1
        i = j + 1
    raise RuntimeError("unterminated draw block")


def has(f):
    return os.path.isfile(os.path.join(BA, f))


# object -> (model, turret, fire, anim, note)
VEH = {
 "Chem_GLATankScorpion":              ("JUANTIVEHICLEVEHICLETECH1_SKN", "BONE_TURRET", "FX_WEAPONA", None, "Scorpion->JU tank"),
 "Chem_GLAVehicleRocketBuggy":        ("JUANTIVEHICLEVEHICLETECH3_SKN", "TURRET", None, None, "RocketBuggy->JU tech3"),
 "Chem_GLAVehicleCombatBike":         ("JUANTIINFANTRYVEHICLE_SKN", None, "FX_WEAPON01", None, "CombatBike->JU anti-infantry"),
 "Chem_GLAVehicleQuadCannon":         ("JUANTIAIRVEHICLETECH1_SKN", "TURRET", None, None, "QuadCannon->JU AA"),
 "Chem_GLAVehicleToxinTruck":         ("JUANTISTRUCTUREVEHICLE_SKN", "TURRET", "FX_WEAPON01", "JUANTISTRUCTUREVEHICLE_IDLA", "ToxinTruck->JU anti-structure"),
 "Chem_GLAVehicleBombTruck":          ("JUEGG_SKN", None, None, None, "BombTruck->JU egg"),
 "Chem_GLAVehicleScudLauncher":       ("JUANTIVEHICLESHIP_SKN", None, "FX_WEAPON_01", "JUANTIVEHICLESHIP_IDLA", "ScudLauncher->JU anti-vehicle ship"),
 "Chem_GLAVehicleTechnical":          ("JUTRANSPORTVEHICLE2_SKN", None, None, None, "Technical->JU transport"),
 "Chem_GLAVehicleTechnicalChassisOne":("JUTRANSPORTVEHICLE2_SKN", None, None, None, "TechnicalChassis->JU transport"),
 "Chem_GLATankMarauder":              ("JUANTIVEHICLEINFANTRYTECH3_SKN", None, "FX_WEAPON05", None, "Marauder->JU anti-vehicle-infantry"),
 "Chem_GLAVehicleRadarVan":           ("JUANTIVEHICLEINFANTRY_SKN", None, None, None, "RadarVan->JU anti-vehicle infantry"),
 "Chem_GLAVehicleBattleBus":          ("JUANTISTRUCTURESHIP_SKN", None, None, None, "BattleBus->JU anti-structure ship"),
}
AIR = {
 "AirF_AmericaJetRaptor":         ("AUFIGHTERAIRCRAFT_SKN", None, None, "AUFIGHTERAIRCRAFT_MOVA", None, "Raptor->AU fighter"),
 "AirF_AmericaJetB52":            ("AUBOMBERAIRCRAFT_SKN", None, "FX_WEAPON_01", None, "B52->AU bomber"),
 "AirF_AmericaJetAurora":         ("AUSUPERSONICBOMBER_SKN", None, "FXCON02", None, None, "Aurora->AU supersonic bomber"),
 "AirF_AmericaJetStealthFighter": ("AUANTIAIRSHIP_SKN", None, "FX_WEAPON_01", None, None, "Stealth->AU airship"),
 "AirF_AmericaJetA10Thunderbolt": ("AUANTIGROUNDAIRCRAFT_SKN", None, "FX_WEAPON_01", None, None, "A10->AU anti-ground aircraft"),
 "AirF_AmericaVehicleComanche":   ("AUANTIAIRSHIP_SKN", None, "FX_WEAPON_01", None, None, "Comanche->AU airship"),
 "AirF_AmericaVehicleChinook":    ("AUMCV_SKN", None, None, None, "Chinook->AU MCV"),
 "AFG_AmericaVehicleChinook":     ("AUMCV_SKN", None, None, None, "Chinook(AG)->AU MCV"),
 "AirF_AmericaJetCargoPlane":     ("AUBOMBERAIRCRAFT_SKN", None, "FX_WEAPON_01", None, "CargoPlane->AU bomber"),
 "AirF_AmericaJetB3":             ("AUSUPERSONICBOMBER_SKN", None, "FXCON02", None, None, "B3->AU supersonic bomber"),
 "AirF_AmericaJetSpectreGunship1":("AUANTIGROUNDAIRCRAFT_SKN", None, "FX_WEAPON_01", None, None, "Spectre1->AU anti-ground"),
 "AirF_AmericaJetSpectreGunship2":("AUANTIGROUNDAIRCRAFT_SKN", None, "FX_WEAPON_01", None, None, "Spectre2->AU anti-ground"),
 "AirF_AmericaJetSpectreGunship3":("AUANTIGROUNDAIRCRAFT_SKN", None, "FX_WEAPON_01", None, None, "Spectre3->AU anti-ground"),
}
# object -> (model, animPrefix, fire)
INF = {
 "Chem_GLAInfantryRebel":               ("JUANTIINFANTRYINFANTRY_SKN", "JUANTIINFANTRYINFANTRY", None),
 "Chem_GLAInfantryJarmenKell":          ("JUCOMMANDOTECH1_SKN", "JUCOMMANDOTECH1", None),
 "Chem_GLAInfantryTunnelDefender":      ("JUANTIVEHICLEINFANTRY_SKN", "JUANTIVEHICLEINFANTRY", None),
 "Chem_GLAInfantryStingerSoldier":      ("JUSCOUTINFANTRY_SKN", "JUSCOUTINFANTRY", None),
 "Chem_GLAInfantryTerrorist":           ("JUINFILTRATIONINFANTRY_SKN", "JUINFILTRATIONINFANTRY", None),
 "Chem_GLAInfantryWorker":              ("JUENGINEER_SKN", "JUENGINEER", None),
 "Chem_GLAInfantryAngryMobPistol01":    ("JUANTIINFANTRYINFANTRY_SKN", "JUANTIINFANTRYINFANTRY", None),
 "Chem_GLAInfantryAngryMobRock02":      ("JUANTIINFANTRYINFANTRY_SKN", "JUANTIINFANTRYINFANTRY", None),
 "Chem_GLAInfantryAngryMobMolotov02":   ("JUANTIINFANTRYINFANTRY_SKN", "JUANTIINFANTRYINFANTRY", None),
}
# object -> (model, fire)
BLD = {
 "Chem_GLACommandCenter":             ("JBCONSTRUCTIONYARD_SKN", None),
 "Chem_FakeGLACommandCenter":         ("JBCONSTRUCTIONYARD_SKN", None),
 "Chem_GLABarracks":                  ("JBBARRACKS_SKN", None),
 "Chem_FakeGLABarracks":              ("JBBARRACKS_SKN", None),
 "Chem_GLAArmsDealer":                ("JBWARFACTORY_SKN", None),
 "Chem_FakeGLAArmsDealer":            ("JBWARFACTORY_SKN", None),
 "Chem_GLABlackMarket":               ("JBTECHSTRUCTURE_SKN", None),
 "Chem_FakeGLABlackMarket":           ("JBTECHSTRUCTURE_SKN", None),
 "Chem_GLAScudStorm":                 ("JBSUPERWEAPONADVANCED_SKN", None),
 "Chem_GLAPalace":                    ("JBSUPERWEAPON_SKN", "FX_WEAPON_01"),
 "Chem_GLAStingerSite":               ("JBBASEDEFENSE_SKN", "FX_WEAPON_01"),
 "Chem_GLASupplyStash":               ("JBREFINERY_SKN", None),
 "Chem_FakeGLASupplyStash":           ("JBREFINERY_SKN", None),
 "Chem_GLADemoTrap":                  ("JBBASEDEFENSEADVANCED_SKN", None),
 "Chem_GLASneakAttackTunnelNetwork":  ("JBWALLHUB_SKN", None),
 "Chem_GLATunnelNetwork":             ("JBWALL_SKN", None),
}


def make_simple(mdl, turret, fire, anim, note):
    L = ["  Draw = W3XModelDraw ModuleTag_01", "    DefaultModelName = %s" % mdl, "",
         "    ConditionState = NONE", "      Model = %s" % mdl]
    if turret:
        L.append("      Turret = %s" % turret)
    if anim and has(anim + ".w3x"):
        L += ["      Animation = %s" % anim, "      AnimationMode = LOOP"]
    if fire:
        L += ["      WeaponFireFXBone = PRIMARY %s" % fire,
              "      WeaponMuzzleFlash = PRIMARY %s" % fire,
              "      WeaponLaunchBone = PRIMARY %s" % fire]
    L += ["    End", "    ConditionState = REALLYDAMAGED", "      Model = %s" % mdl]
    if turret:
        L.append("      Turret = %s" % turret)
    L += ["    End", "    ConditionState = RUBBLE", "      Model = %s" % mdl, "    End", "  End"]
    return "\n".join(L)


def _pick(pre, tags):
    for t in tags:
        if has("%s_%s.w3x" % (pre, t)): return t
    return None


def _atk(pre):
    """挑这个骨架实际存在的攻击动画标签(AU 用 ATKA,山海经用 ATKZ,...)。"""
    for t in ("ATKZ", "ATKA", "ATKB", "ATKC", "ATKD"):
        if has("%s_%s.w3x" % (pre, t)): return t
    return "BIDA"   # 连攻击动画都没有(如 AUINFILTRATIONINFANTRY) -> 退待机


def make_inf(mdl, pre, fire):
    L = ["  Draw = W3XModelDraw ModuleTag_01",
         "    ; RA3 步兵动画标签: BIDA=待机 RUNA=移动 ATKZ=攻击 DTB*/DTF*/DTP*=死亡 FLYA=坠落",
         "    DefaultModelName = %s" % mdl, "",
         "    ConditionState = NONE", "      Model = %s" % mdl,
         "      Animation = %s_BIDA" % pre, "      AnimationMode = LOOP"]
    if fire:
        for s in ("PRIMARY", "SECONDARY"):
            L += ["      WeaponFireFXBone = %s %s" % (s, fire),
                  "      WeaponMuzzleFlash = %s %s" % (s, fire)]
    L += ["    End", "    ConditionState = MOVING", "      Model = %s" % mdl,
          "      Animation = %s_RUNA" % pre, "      AnimationMode = LOOP",
          "      ParticleSysBone = None InfantryDustTrails", "    End"]
    if fire:
        L += ["    ConditionState = FIRING_A", "      Model = %s" % mdl,
              "      Animation = %s_%s" % (pre, _atk(pre)), "      AnimationMode = ONCE",
              "      WeaponLaunchBone = PRIMARY %s" % fire, "      UseWeaponTiming = Yes", "    End",
              "    ConditionState = BETWEEN_FIRING_SHOTS_A", "      Model = %s" % mdl,
              "      Animation = %s_%s" % (pre, _atk(pre)), "      AnimationMode = MANUAL", "    End"]
    for cond, tag, mode in (("DYING", "_DTBA", "ONCE"),
                            ("DYING EXPLODED_FLAILING", "_DTFA", "LOOP"),
                            ("DYING EXPLODED_BOUNCING", "_DTPA", "ONCE"),
                            ("FREEFALL", "_FLYA", "LOOP")):
        L += ["    ConditionState = %s" % cond, "      Model = %s" % mdl,
              "      Animation = %s%s" % (pre, _pick(pre, [tag[1:], "DIEA", "DIE", "DTHA", "DTFA"]) or tag[1:]), "      AnimationMode = %s" % mode, "    End"]
    L += ["    ConditionState = REALLYDAMAGED", "      Model = %s" % mdl,
          "      Animation = %s_BIDA" % pre, "      AnimationMode = LOOP"]
    if fire:
        L += ["      WeaponFireFXBone = PRIMARY %s" % fire,
              "      WeaponMuzzleFlash = PRIMARY %s" % fire]
    L += ["    End", "  End"]
    return "\n".join(L)


def make_bld(mdl, fire):
    base = mdl.replace("_SKN", "")
    bld = base + "_BLD" if has(base + "_BLD.w3x") else None
    # ⚠️ 空动画(0 通道)绝不能挂 —— 会让 Skin 网格塌陷
    idla = base + "_IDLA" if (has(base + "_IDLA.w3x") and
        len(re.findall(r"<Channel(?:Quaternion|Scalar|Vector)\b", open(os.path.join(BA, base + "_IDLA.w3x"), encoding="utf-8-sig", errors="ignore").read())) > 0) else None
    L = ["  Draw = W3XModelDraw ModuleTag_01", "    DefaultModelName = %s" % mdl, "",
         "    ConditionState = NONE", "      Model = %s" % mdl]
    if idla:
        L += ["      Animation = %s" % idla, "      AnimationMode = LOOP"]
    if fire:
        L += ["      WeaponFireFXBone = PRIMARY %s" % fire,
              "      WeaponMuzzleFlash = PRIMARY %s" % fire]
    L += ["    End",
          "    ConditionState = AWAITING_CONSTRUCTION PARTIALLY_CONSTRUCTED ACTIVELY_BEING_CONSTRUCTED",
          "      Model = %s" % mdl]
    if bld:
        L += ["      Animation = %s" % bld, "      AnimationMode = ONCE"]
    L += ["    End", "    ConditionState = REALLYDAMAGED RUBBLE", "      Model = %s" % mdl,
          "    End", "  End"]
    return "\n".join(L)


def main():
    shutil.copy(F, F + ".bak_before_ba_batch_20261002")
    src = open(F, encoding="utf-8", newline="").read()
    objs = list(re.finditer(r"(?m)^Object[ \t]+(\S+)", src))
    done = 0
    for i in range(len(objs) - 1, -1, -1):
        name = objs[i].group(1)
        if name in VEH or name in AIR:
            c = (VEH | AIR)[name]
            if len(c) == 4: c = (c[0], None) + c[1:]   # 本表没写炮塔 -> 补 None
            new = make_simple(c[0], c[1], c[2], c[3], c[4])
        elif name in INF:
            c = INF[name]
            new = make_inf(c[0], c[1], c[2])
        elif name in BLD:
            c = BLD[name]
            new = make_bld(c[0], c[1])
        else:
            continue
        s = objs[i].start()
        e = objs[i + 1].start() if i + 1 < len(objs) else len(src)
        body = src[s:e]
        ds = list(re.finditer(r"(?m)^[ \t]*Draw[ \t]*=[ \t]*W3D\w+", body))
        if not ds:
            print("  [SKIP] %s (no active W3D draw)" % name)
            continue
        for d in reversed(ds[1:]):
            en = balanced_end(body, d.start())
            blk = body[d.start():en]
            body = body[:d.start()] + "\n".join(
                ("; " + l) if l.strip() else l for l in blk.split("\n")) + body[en:]
        d = re.search(r"(?m)^[ \t]*Draw[ \t]*=[ \t]*W3D\w+", body)
        en = balanced_end(body, d.start())
        last = body[d.start():en].rstrip().splitlines()[-1].strip().lower()
        if last != "end":
            print("  [ABORT] %s: bad slice (last=%r)" % (name, last))
            sys.exit(1)
        body = body[:d.start()] + new + "\r\n" + body[en:]
        src = src[:s] + body + src[e:]
        done += 1
        print("  [OK] %-38s -> %s" % (name, new.splitlines()[1].strip() if len(new.splitlines()) > 1 else "?"))
    open(F, "wb").write(src.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
    d = open(F, "rb").read()
    print("\nconverted %d objects" % done)
    print("bytes:", len(d), "CRLF:", d.count(b"\r\n"),
          "bareLF:", d.count(b"\n") - d.count(b"\r\n"))
    print("Draw = W3XModelDraw:", d.count(b"Draw = W3XModelDraw"))


if __name__ == "__main__":
    main()
