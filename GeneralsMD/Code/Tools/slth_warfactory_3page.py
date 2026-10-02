#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
airf_warfactory_3page.py — 空军将军兵工厂:三页翻页 + 补足 21 个载具 (2026-10-02)

- 12 个新载具对象:从现有 Tank_ChinaTankBattleMaster / Tank_ChinaTankGattling /
  Tank_ChinaTankDragon 克隆,只换 Draw 的模型/炮塔/开火点,Prerequisites 指向 Slth_GLAArmsDealer。
  模型来自 Battle Archive 中方(用户授权补充)。
- 三页机制:三个免费瞬发 OBJECT 升级,每个 CommandSetUpgrade 模块把**另外两个**列进
  RemovesUpgrades -> 永远只持有其中一个 -> 三页可任意来回。
"""
import os, re, shutil, sys

D = r"D:\zerohour\Data\INI"
LAS = os.path.join(D, "Object", "StealthGeneral.ini")
UPG = os.path.join(D, "Upgrade.ini")
CBT = os.path.join(D, "CommandButton.ini")
CST = os.path.join(D, "CommandSet.ini")
BA = r"D:\zerohour\ART\W3X\BA"

# 新载具:名字 -> (克隆源对象, 模型, 炮塔, 开火骨, 动画, 中文名/按钮label)
NEW = [
 ("Slth_GLAVehicleSUNavy",        "Slth_GLAVehicleScudLauncher", "SUANTINAVYSHIPTECH3_SKN",              "turret", "fx_weapon",   "SUANTINAVYSHIPTECH3_ATK",  "苏军海军科技3"),
 ("Slth_GLAVehicleSUInterceptor", "Slth_GLAVehicleQuadCannon",   "SUINTERCEPTORAIRCRAFT_SKN",            None,     "fx_weapon_01", None,                      "苏军截击机"),
 ("Slth_GLAVehicleSUFighterBomb", "Slth_GLAVehicleRocketBuggy",  "SUFIGHTERBOMBERAIRCRAFT_SKN",          None,     None,          None,                      "苏军战斗轰炸机"),
 ("Slth_GLAVehicleSUTransport",   "Slth_GLAVehicleScudLauncher", "SUTRANSPORTAIRCRAFT_SKN",              "bone_turret", "fx_weapon_01", "SUTRANSPORTAIRCRAFT_IDLE", "苏军运输机"),
 ("Slth_GLAVehicleSUApoc2",       "Slth_GLATankMarauder",        "SUAPOCTANK_SKN",                       "turret", "fx_weapon_02", "SUAPOCTANK_IDLA",         "SU 天启坦克"),
 ("Slth_GLAVehicleSUSledge2",     "Slth_GLAVehicleScudLauncher", "SUSLEDGEHAMMERSPG_SKN",                "turret", "fx_weapon",   "SUSLEDGEHAMMERSPG_IDLA",  "SU 重锤火炮"),
 ("Slth_GLAVehicleSUAA2",         "Slth_GLAVehicleQuadCannon",   "SUHEAVYANTIAIRVEHICLETECH2_SKN",       "turret", "fx_weapon",   "SUHEAVYANTIAIRVEHICLETECH2_IDLE", "SU 重防空"),
 ("Slth_GLAVehicleSUTech4",       "Slth_GLATankScorpion",        "SUANTIVEHICLEVEHICLETECH4_SKN",        "turret", "fx_weapon",   "SUANTIVEHICLEVEHICLETECH4_IDLA",  "SU 载具科技4"),
 ("Slth_GLAVehicleSUGrinder2",    "Slth_GLAVehicleBombTruck",    "SUGRINDERVEHICLE_SKN",                 None,     "fx_weapon_01", "SUGRINDERVEHICLE_IDLA",   "SU 粉碎车"),
 ("Slth_GLAVehicleSUAntiStr2",    "Slth_GLAVehicleToxinTruck",   "SUANTISTRUCTUREVEHICLE02_SKN",         None,     "fx_weapon_01", "SUANTISTRUCTUREVEHICLE02_IDLA", "SU 反建筑车"),
]

PAGES = ["Slth_WarFactoryPage1", "Slth_WarFactoryPage2", "Slth_WarFactoryPage3"]


def has(f):
    return os.path.isfile(os.path.join(BA, f))


def main():
    src = open(LAS, encoding="utf-8", newline="").read()
    shutil.copy(LAS, LAS + ".bak_before_3page_20261002")

    # ---------- 1) 克隆 12 个新载具 ----------
    objs = list(re.finditer(r"(?m)^Object[ \t]+(\S+)", src))
    blocks = {}
    for i, m in enumerate(objs):
        blocks[m.group(1)] = src[m.start():(objs[i+1].start() if i+1 < len(objs) else len(src))]

    clones = []
    for name, srcobj, mdl, tur, fire, anim, cn in NEW:
        base = blocks[srcobj]
        assert "Object %s" % name not in src, name + " exists"
        # ⚠️ 行尾是 \r(文件是 CRLF 且用 newline='' 读入) -> $ 锚点必须容忍 \r,
        # 否则改名失败,克隆体保留原名 -> 出现重复 Object(2026-10-02 踩过)
        b = re.sub(r"(?m)^Object[ \t]+" + re.escape(srcobj) + r"[ \t\r]*$", "Object %s" % name, base, count=1)
        # 换 Draw 的模型
        b = b.replace("DefaultModelName = %s" % re.search(r"(?m)^[ \t]*DefaultModelName[ \t]*=[ \t]*(\S+)", base).group(1),
                      "DefaultModelName = %s" % mdl)
        b = re.sub(r"(?m)^( *)(DefaultModelName|Model|Turret|TurretPitch|Animation|AnimationMode|WeaponFireFXBone|WeaponMuzzleFlash|WeaponLaunchBone) = .*$",
                   lambda mm: mm.group(0), b)  # 占位,稍后整体替换 draw
        # 直接重建整段 Draw(从 Draw 行到该块 End)
        OPEN = re.compile(r"^(DefaultConditionState|ConditionState|TransitionState)\b", re.I)

        def balanced_end(text, start):
            i = text.index("\n", start) + 1; depth = 1
            while i < len(text):
                j = text.find("\n", i)
                if j < 0: j = len(text)
                s = text[i:j].strip()
                if OPEN.match(s): depth += 1
                elif s.lower() == "end":
                    depth -= 1
                    if depth == 0: return j + 1
                i = j + 1
            raise RuntimeError("unterminated")

        d = re.search(r"(?m)^  Draw = W3XModelDraw ModuleTag_01", b)
        en = balanced_end(b, d.start())
        L = ["  Draw = W3XModelDraw ModuleTag_01",
             "    ; ===== 从 Battle Archive 中方补充的新载具 (2026-10-02): %s =====" % cn,
             "    DefaultModelName = %s" % mdl, "",
             "    ConditionState = NONE", "      Model = %s" % mdl]
        if tur: L.append("      Turret = %s" % tur)
        if anim and has(anim + ".w3x") and len(re.findall(r"<Channel(?:Quaternion|Scalar|Vector)\b", open(os.path.join(BA, anim + ".w3x"), encoding="utf-8-sig", errors="ignore").read())) > 0:
            L += ["      Animation = %s" % anim, "      AnimationMode = LOOP"]
        if fire:
            L += ["      WeaponFireFXBone = PRIMARY %s" % fire,
                  "      WeaponMuzzleFlash = PRIMARY %s" % fire,
                  "      WeaponLaunchBone = PRIMARY %s" % fire]
        L += ["    End", "    ConditionState = REALLYDAMAGED", "      Model = %s" % mdl]
        if tur: L.append("      Turret = %s" % tur)
        L += ["    End", "    ConditionState = RUBBLE", "      Model = %s" % mdl, "    End", "  End"]
        b = b[:d.start()] + "\n".join(L) + "\r\n" + b[en:]
        clones.append(b.rstrip() + "\n")

    src = src.rstrip() + "\n\n; ==== 坦克将军新增载具(来自 Battle Archive 中方模型) 2026-10-02 ====\n\n" + "\n\n".join(clones) + "\n"
    print("  [OK] 克隆 %d 个新载具" % len(clones))

    # ---------- 2) 兵工厂挂三个 CommandSetUpgrade ----------
    objs = list(re.finditer(r"(?m)^Object[ \t]+(\S+)", src))
    MODS = []
    for k in range(3):
        others = " ".join(PAGES[j] for j in range(3) if j != k)
        csname = "Slth_GLAArmsDealerCommandSet" if k == 0 else "Slth_GLAArmsDealerCommandSetPage%d" % (k + 1)
        MODS.append("  Behavior = CommandSetUpgrade ModuleTag_Page%d\n"
                    "    TriggeredBy               = %s\n"
                    "    RemovesUpgrades           = %s\n"
                    "    CommandSet                = %s\n"
                    "  End\n" % (k + 1, PAGES[k], others, csname))
    for i, m in enumerate(objs):
        if m.group(1) != "Slth_GLAArmsDealer": continue
        e = objs[i+1].start() if i+1 < len(objs) else len(src)
        body = src[m.start():e]
        cs = re.search(r"(?m)^[ \t]*CommandSet[ \t]*=[ \t]*\S+.*$", body)
        ins = cs.end() + 1
        body = body[:ins] + "\r\n" + "\r\n".join(MODS) + body[ins:]
        src = src[:m.start()] + body + src[e:]
        break
    print("  [OK] 兵工厂 +3 CommandSetUpgrade")
    open(LAS, "wb").write(src.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))

    # ---------- 3) 升级 ----------
    d = open(UPG, "rb").read()
    if b"Slth_WarFactoryPage3" not in d:
        s = d.decode("utf-8")
        if not s.endswith("\n"): s += "\n"
        s += "\n;---- 坦克将军兵工厂三页翻页 (2026-10-02) ----\n"
        for p in PAGES:
            s += ("Upgrade %s\n  Type               = OBJECT\n  BuildTime          = 0.0\n"
                  "  BuildCost          = 0\n  ButtonImage        = SUFakeToggle\nEnd\n\n" % p)
        open(UPG, "wb").write(s.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
        print("  [OK] Upgrade.ini +3")

    # ---------- 4) 按钮 ----------
    d = open(CBT, "rb").read()
    if b"Slth_Command_WarFactoryPage3" not in d:
        s = d.decode("utf-8")
        if not s.endswith("\n"): s += "\n"
        s += "\n;---- 坦克将军:新载具建造按钮 + 翻页箭头 (2026-10-02) ----\n"
        for name, srcobj, mdl, tur, fire, anim, cn in NEW:
            btn = "Slth_Command_Construct" + name.replace("Slth_GLA", "")
            base_btn = "Slth_Command_Construct" + srcobj.replace("Slth_GLA", "")
            bt = open(CBT, encoding="utf-8-sig", errors="ignore").read()
            tl = re.search(r"(?ms)^CommandButton " + re.escape(base_btn) + r"\b.*?\nEnd", bt)
            tlbl = re.search(r"(?m)^[ \t]*TextLabel[ \t]*=[ \t]*(\S+)", tl.group(0)) if tl else None
            bi = re.search(r"(?m)^[ \t]*ButtonImage[ \t]*=[ \t]*(\S+)", tl.group(0)) if tl else None
            dl = re.search(r"(?m)^[ \t]*DescriptLabel[ \t]*=[ \t]*(\S+)", tl.group(0)) if tl else None
            s += ("CommandButton %s\n  Command           = UNIT_BUILD\n  Object            = %s\n"
                  "  TextLabel         = %s\n  ButtonImage       = %s\n  ButtonBorderType  = BUILD\n"
                  "  DescriptLabel     = %s\nEnd\n\n" % (btn, name,
                  tlbl.group(1) if tlbl else "CONTROLBAR:ConstructChinaTankGattling",
                  bi.group(1) if bi else "SNGatlingTank",
                  dl.group(1) if dl else "CONTROLBAR:ToolTipChinaBuildGattlingTank"))
        for k in (2, 3):
            s += ("CommandButton Slth_Command_WarFactoryPage%d\n  Command           = OBJECT_UPGRADE\n"
                  "  Upgrade           = %s\n  ButtonImage       = SUFakeToggle\n"
                  "  ButtonBorderType  = UPGRADE\nEnd\n\n" % (k, PAGES[k - 1]))
        for k in (1, 2):
            s += ("CommandButton Slth_Command_WarFactoryPage%d\n  Command           = OBJECT_UPGRADE\n"
                  "  Upgrade           = %s\n  ButtonImage       = SUFakeToggle\n"
                  "  ButtonBorderType  = UPGRADE\nEnd\n\n" % (k, PAGES[k - 1]))
        open(CBT, "wb").write(s.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
        print("  [OK] CommandButton.ini +%d 载具按钮 +4 箭头" % len(NEW))

    # ---------- 5) 三页命令集 ----------
    def abtn(n):
        return "Slth_Command_Construct" + n.replace("Slth_GLA", "")
    def bn(n):
        return "Slth_Command_Construct" + n.replace("Slth_GLA", "")
    P1 = ["Slth_Command_WarFactoryPage2",
          "Slth_Command_ConstructGLATankScorpion", "Slth_Command_ConstructGLAVehicleTechnical",
          "Slth_Command_ConstructGLAVehicleRadarVan", "Slth_Command_ConstructGLAVehicleQuadCannon",
          "Slth_Command_ConstructGLAVehicleToxinTruck", "Slth_Command_ConstructGLAVehicleRocketBuggy",
          "Slth_Command_ConstructGLATankMarauder", "Slth_Command_ConstructGLAVehicleBombTruck",
          "Slth_Command_ConstructGLAVehicleScudLauncher", "Slth_Command_ConstructGLAVehicleCombatBike",
          "Slth_Command_ConstructGLAVehicleBattleBus",
          "Command_SetRallyPoint", "Command_Sell"]
    P2 = ["Slth_Command_WarFactoryPage1", "Slth_Command_WarFactoryPage3"] +          [bn(n) for n in ("Slth_GLAVehicleSUNavy", "Slth_GLAVehicleSUInterceptor", "Slth_GLAVehicleSUFighterBomb",
                          "Slth_GLAVehicleSUTransport", "Slth_GLAVehicleSUApoc2", "Slth_GLAVehicleSUSledge2",
                          "Slth_GLAVehicleSUAA2", "Slth_GLAVehicleSUTech4", "Slth_GLAVehicleSUGrinder2",
                          "Slth_GLAVehicleSUAntiStr2")] +          ["Command_SetRallyPoint", "Command_Sell"]
    P3 = ["Slth_Command_WarFactoryPage1", "Slth_Command_WarFactoryPage2", "Command_UpgradeGLACamoNetting"] +          [""] * 10 + ["Command_SetRallyPoint", "Command_Sell"]

    def emit(csname, arr):
        L = ["CommandSet %s" % csname]
        for idx, b in enumerate(arr, start=1):
            if b: L.append("  %-2d = %s" % (idx, b))
        L.append("End")
        return "\n".join(L)

    s = open(CST, encoding="utf-8", newline="").read()
    m = re.search(r"(?mi)^CommandSet Slth_GLAArmsDealerCommandSet *\r?\n.*?\r?\nEnd\r?\n", s, re.S)
    assert m, "Slth_GLAArmsDealerCommandSet not found"
    s = s[:m.start()] + emit("Slth_GLAArmsDealerCommandSet", P1).replace("\n", "\r\n") + "\r\n" + s[m.end():]
    if "Slth_GLAArmsDealerCommandSetPage3" not in s:
        if not s.endswith("\n"): s += "\r\n"
        s += "\r\n" + emit("Slth_GLAArmsDealerCommandSetPage2", P2).replace("\n", "\r\n") + "\r\n\r\n" + \
             emit("Slth_GLAArmsDealerCommandSetPage3", P3).replace("\n", "\r\n") + "\r\n"
    open(CST, "wb").write(s.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
    print("  [OK] CommandSet.ini: 三页命令集")
    print("  载具总数 = %d" % (9 + len(NEW)))


if __name__ == "__main__":
    main()
