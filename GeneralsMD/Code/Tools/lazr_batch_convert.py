#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
lazr_batch_convert.py — 激光将军(LaserGeneral.ini) 批量 Draw 改写(2026-10-02)

载具/飞机/步兵 -> 红警3MOD Battle Archive 中方模型; 建筑 -> 日冕+原版RA3盟军。
用法: python lazr_batch_convert.py
"""
import re, os, shutil, sys

F = r"D:\zerohour\Data\INI\Object\LaserGeneral.ini"
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
 "Lazr_AmericaVehicleHumvee":        ("LYT2021_SKN", "TURRET", None, None, "Humvee->LYT2021"),
 "Lazr_AmericaVehicleTomahawk":      ("SR5GMLRS_SKN", "04C4B72D", "FX_WEAPON01", None, "Tomahawk->SR5 MLRS"),
 "Lazr_AmericaVehicleMedic":         ("ZFB05_SKN", "TURRET", None, None, "Medic->ZFB05"),
 "Lazr_AmericaTankCrusader":         ("ZTQ15B_SKN", "TURRETA", "FX_WEAPONA", None, "Crusader->ZTQ15B"),
 "Lazr_AmericaTankPaladin":          ("WZ122_SKN", None, "FX_WEAPON", None, "Paladin->WZ122"),
 "Lazr_AmericaTankMicrowave":        ("SH16_SKN", "BONE_TURRET", "FX_WEAPON", None, "Microwave->SH16"),
 "Lazr_AmericaTankAvenger":          ("TYPE625AA_SKN", None, "FX_CANNON", None, "Avenger->TYPE625AA"),
 "Lazr_AmericaVehicleSentryDrone":   ("LD2000TURRET_SKN", None, "GUN", None, "SentryDrone->LD2000"),
 "Lazr_AmericaVehicleDozer":         ("82CCV_SKN", None, None, None, "Dozer->82CCV"),
 "Lazr_AmericaVehicleBattleDrone":   ("ZLT11_SKN", "TURRET", "FX_WEAPONA", None, "BattleDrone->ZLT11"),
 "Lazr_AmericaVehicleScoutDrone":    ("87SCOUTCAR_SKN", "BONE_TURRET", None, None, "ScoutDrone->87ScoutCar"),
 "Lazr_AmericaVehicleHellfireDrone": ("AFT09MISSLELUNCHER_SKN", "BONE_TURRET", "FX_WEAPON04", None, "HellfireDrone->AFT09"),
 "Lazr_AmericaVehicleSpyDrone":      ("TYPE89IFV_SKN", "TURRET", "FX_WEAPONA", None, "SpyDrone->Type89IFV"),
}
AIR = {
 "Lazr_AmericaJetRaptor":         ("J20_SKN", None, None, None, "Raptor->J20"),
 "Lazr_AmericaJetB52":            ("H20_SKN", None, "FX_WEAPON_01", None, "B52->H20"),
 "Lazr_AmericaJetAurora":         ("J35_SKN", None, None, None, "Aurora->J35"),
 "Lazr_AmericaJetStealthFighter": ("J16_SKN", None, None, None, "Stealth->J16"),
 "Lazr_AmericaJetA10Thunderbolt": ("JH7_SKN", None, None, None, "A10->JH7"),
 "Lazr_AmericaVehicleComanche":   ("WZ10_SKN", None, "FX_ROCKET01", "WZ10_IDLA", "Comanche->WZ10"),
 "Lazr_AmericaVehicleChinook":    ("Z20H_SKN", None, None, "Z20H_IDLA", "Chinook->Z20"),
 "Lazr_AmericaJetCargoPlane":     ("H6K_SKN", None, None, None, "CargoPlane->H6K"),
 "Lazr_AmericaJetSpectreGunship": ("H8_SKN", None, None, None, "Spectre->H8"),
}
# object -> (model, animPrefix, fire)
INF = {
 "Lazr_AmericaInfantryColonelBurton":   ("SHANHAIJINGASSAULTINFANTRY_SKN", "ShanHaiJingAssaultInfantry", "FX01"),
 "Lazr_AmericaInfantryRanger":          ("SHANHAIJINGASSAULTINFANTRY_SKN", "ShanHaiJingAssaultInfantry", "FX01"),
 "Lazr_AmericaInfantryMissileDefender": ("SHANHAIJINGATINFANTRY_SKN", "ShanHaiJingATInfantry", "ROCKET"),
 "Lazr_AmericaInfantryPilot":           ("SHANHAIJINGATINFANTRY_SKN", "ShanHaiJingATInfantry", None),
 "Lazr_AmericaInfantryPathfinder":      ("SHANHAIJINGASSAULTINFANTRY_SKN", "ShanHaiJingAssaultInfantry", "FX01"),
}
# object -> (model, fire)
BLD = {
 "Lazr_AmericaCommandCenter":        ("ABCONYARD_SKN", None),
 "Lazr_AmericaPowerPlant":           ("ABPOWERPLANT_SKN", None),
 "Lazr_AmericaBarracks":             ("ABBARRACKS_SKN", None),
 "Lazr_AmericaWarFactory":           ("ABWARFACTORY_SKN", None),
 "Lazr_AmericaAirfield":             ("ABAIRFIELD_SKN", None),
 "Lazr_AmericaSupplyCenter":         ("ABREFINERY_SKN", None),
 "Lazr_AmericaSupplyDropZone":       ("ABAIRCRAFTBUNKER_SKN", None),
 "Lazr_AmericaStrategyCenter":       ("ABLOWTECHSTRUCTURE_SKN", None),
 "Lazr_AmericaFireBase":             ("ABBASEDEFENCETECH3_SKN", "fx_weapon"),
 "Lazr_AmericaPatriotBattery":       ("ALLIEDAEGISLARGEDEFENSEBASE_SKN", None),
 "Lazr_AmericaParticleCannonUplink": ("ABSUPERWEAPON_SKN", None),
 "Lazr_AmericaLaserCannonUplink":    ("ABSUPERWEAPONADVANCED_SKN", "FX_WEAPON_04"),
 "Lazr_AmericaLaserCannon":          ("ALLIEDCHRONOLAB_SKN", None),
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
              "      Animation = %s_ATKZ" % pre, "      AnimationMode = ONCE",
              "      WeaponLaunchBone = PRIMARY %s" % fire, "      UseWeaponTiming = Yes", "    End",
              "    ConditionState = BETWEEN_FIRING_SHOTS_A", "      Model = %s" % mdl,
              "      Animation = %s_ATKZ" % pre, "      AnimationMode = MANUAL", "    End"]
    for cond, tag, mode in (("DYING", "_DTBA", "ONCE"),
                            ("DYING EXPLODED_FLAILING", "_DTFA", "LOOP"),
                            ("DYING EXPLODED_BOUNCING", "_DTPA", "ONCE"),
                            ("FREEFALL", "_FLYA", "LOOP")):
        L += ["    ConditionState = %s" % cond, "      Model = %s" % mdl,
              "      Animation = %s%s" % (pre, tag), "      AnimationMode = %s" % mode, "    End"]
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
