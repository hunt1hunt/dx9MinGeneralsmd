#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
tank_warfactory_3page.py — 坦克将军兵工厂:三页翻页 + 补足 21 个载具 (2026-10-02)

- 12 个新载具对象:从现有 Tank_ChinaTankBattleMaster / Tank_ChinaTankGattling /
  Tank_ChinaTankDragon 克隆,只换 Draw 的模型/炮塔/开火点,Prerequisites 指向 Tank_ChinaWarFactory。
  模型来自 Battle Archive 中方(用户授权补充)。
- 三页机制:三个免费瞬发 OBJECT 升级,每个 CommandSetUpgrade 模块把**另外两个**列进
  RemovesUpgrades -> 永远只持有其中一个 -> 三页可任意来回。
"""
import os, re, shutil, sys

D = r"D:\zerohour\Data\INI"
LAS = os.path.join(D, "Object", "TankGeneral.ini")
UPG = os.path.join(D, "Upgrade.ini")
CBT = os.path.join(D, "CommandButton.ini")
CST = os.path.join(D, "CommandSet.ini")
BA = r"D:\zerohour\ART\W3X\BA"

# 新载具:名字 -> (克隆源对象, 模型, 炮塔, 开火骨, 动画, 中文名/按钮label)
NEW = [
 ("Tank_ChinaTankZTZ99A",      "Tank_ChinaTankBattleMaster", "ZTZ99A_SKN",    "TURRET",     "FX_WEAPONA",   None,             "ZTZ-99A 主战坦克"),
 ("Tank_ChinaTankZTZ96A",      "Tank_ChinaTankBattleMaster", "ZTZ96A_SKN",    "A27E141C",   "FX_WEAPON",    None,             "ZTZ-96A 主战坦克"),
 ("Tank_ChinaTankVT4",         "Tank_ChinaTankBattleMaster", "VT4AMBT_SKN",   "TURRET",     "FX_WEAPONA",   None,             "VT-4 主战坦克"),
 ("Tank_ChinaTankWZ122",       "Tank_ChinaTankBattleMaster", "WZ122_SKN",     None,         "FX_WEAPON",    None,             "WZ-122 试验坦克"),
 ("Tank_ChinaTankZTQ15",       "Tank_ChinaTankBattleMaster", "ZTQ15B_SKN",    "TURRETA",    "FX_WEAPONA",   None,             "ZTQ-15 轻坦"),
 ("Tank_ChinaTankZLT11",       "Tank_ChinaTankBattleMaster", "ZLT11_SKN",     "TURRET",     "FX_WEAPONA",   None,             "ZLT-11 突击炮"),
 ("Tank_ChinaVehiclePGZ09",    "Tank_ChinaTankGattling",     "PGZ09_SKN",     "04C4B72D",   "38361B8C",     "PGZ09_IDLA",     "PGZ-09 自行高炮"),
 ("Tank_ChinaVehicleType625AA","Tank_ChinaTankGattling",     "TYPE625AA_SKN", None,         "FX_CANNON",    None,             "Type-625 弹炮合一"),
 ("Tank_ChinaVehicleLD2000",   "Tank_ChinaTankGattling",     "LD2000TURRET_SKN", "TURRET",  "GUN",          None,             "LD-2000 近防炮"),
 ("Tank_ChinaVehiclePLZ05A",   "Tank_ChinaTankDragon",       "PLZ05A_SKN",    "BONE_TURRET","FX_WEAPON",    None,             "PLZ-05 自行榴弹炮"),
 ("Tank_ChinaVehicleSR5",      "Tank_ChinaTankDragon",       "SR5GMLRS_SKN",  "04C4B72D",   "FX_WEAPON01",  "SR5GMLRS_IDLE",  "SR-5 模块化火箭炮"),
 ("Tank_ChinaVehicleAFT09",    "Tank_ChinaTankDragon",       "AFT09MISSLELUNCHER_SKN", "BONE_TURRET", "FX_WEAPON04", None,   "AFT-09 反坦克导弹车"),
]

PAGES = ["Tank_WarFactoryPage1", "Tank_WarFactoryPage2", "Tank_WarFactoryPage3"]


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
        csname = "Tank_ChinaWarFactoryCommandSet" if k == 0 else "Tank_ChinaWarFactoryCommandSetPage%d" % (k + 1)
        MODS.append("  Behavior = CommandSetUpgrade ModuleTag_Page%d\n"
                    "    TriggeredBy               = %s\n"
                    "    RemovesUpgrades           = %s\n"
                    "    CommandSet                = %s\n"
                    "  End\n" % (k + 1, PAGES[k], others, csname))
    for i, m in enumerate(objs):
        if m.group(1) != "Tank_ChinaWarFactory": continue
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
    if b"Tank_WarFactoryPage3" not in d:
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
    if b"Tank_Command_WarFactoryPage3" not in d:
        s = d.decode("utf-8")
        if not s.endswith("\n"): s += "\n"
        s += "\n;---- 坦克将军:新载具建造按钮 + 翻页箭头 (2026-10-02) ----\n"
        for name, srcobj, mdl, tur, fire, anim, cn in NEW:
            btn = "Tank_Command_Construct" + name.replace("Tank_China", "")
            base_btn = "Tank_Command_Construct" + srcobj.replace("Tank_China", "")
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
            s += ("CommandButton Tank_Command_WarFactoryPage%d\n  Command           = OBJECT_UPGRADE\n"
                  "  Upgrade           = %s\n  ButtonImage       = SUFakeToggle\n"
                  "  ButtonBorderType  = UPGRADE\nEnd\n\n" % (k, PAGES[k - 1]))
        for k in (1, 2):
            s += ("CommandButton Tank_Command_WarFactoryPage%d\n  Command           = OBJECT_UPGRADE\n"
                  "  Upgrade           = %s\n  ButtonImage       = SUFakeToggle\n"
                  "  ButtonBorderType  = UPGRADE\nEnd\n\n" % (k, PAGES[k - 1]))
        open(CBT, "wb").write(s.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
        print("  [OK] CommandButton.ini +%d 载具按钮 +4 箭头" % len(NEW))

    # ---------- 5) 三页命令集 ----------
    def abtn(n):
        return "Tank_Command_Construct" + n.replace("Tank_China", "")
    P1 = ["Tank_Command_WarFactoryPage2", "Tank_Command_ConstructChinaTankBattleMaster",
          "Tank_Command_ConstructChinaTankEmperor", "Tank_Command_ConstructChinaTankDragon",
          "Tank_Command_ConstructChinaTankGattling", "Tank_Command_ConstructChinaTankECM",
          "Tank_Command_ConstructChinaVehicleTroopCrawler", "Tank_Command_ConstructChinaVehicleListeningOutpost",
          "Tank_Command_ConstructChinaDozer", "Tank_Command_ConstructChinaVehicleSupplyTruck",
          "Command_UpgradeChinaChainGuns", "Command_UpgradeChinaBlackNapalm", "Command_SetRallyPoint", "Command_Sell"]
    P2 = ["Tank_Command_WarFactoryPage1", "Tank_Command_WarFactoryPage3"] + \
         [abtn(n) for n in ("Tank_ChinaTankZTZ99A", "Tank_ChinaTankZTZ96A", "Tank_ChinaTankVT4",
                            "Tank_ChinaTankWZ122", "Tank_ChinaTankZTQ15", "Tank_ChinaTankZLT11",
                            "Tank_ChinaVehiclePGZ09", "Tank_ChinaVehicleType625AA", "Tank_ChinaVehicleLD2000")] + \
         ["Command_UpgradeChinaMines", "Command_SetRallyPoint", "Command_Sell"]
    P3 = ["Tank_Command_WarFactoryPage2"] + \
         [abtn(n) for n in ("Tank_ChinaVehiclePLZ05A", "Tank_ChinaVehicleSR5", "Tank_ChinaVehicleAFT09")] + \
         [""] * 9 + ["Command_UpgradeEMPMines", "Command_SetRallyPoint", "Command_Sell"]

    def emit(csname, arr):
        L = ["CommandSet %s" % csname]
        for idx, b in enumerate(arr, start=1):
            if b: L.append("  %-2d = %s" % (idx, b))
        L.append("End")
        return "\n".join(L)

    s = open(CST, encoding="utf-8", newline="").read()
    m = re.search(r"(?mi)^CommandSet Tank_ChinaWarFactoryCommandSet *\r?\n.*?\r?\nEnd\r?\n", s, re.S)
    assert m, "Tank_ChinaWarFactoryCommandSet not found"
    s = s[:m.start()] + emit("Tank_ChinaWarFactoryCommandSet", P1).replace("\n", "\r\n") + "\r\n" + s[m.end():]
    if "Tank_ChinaWarFactoryCommandSetPage3" not in s:
        if not s.endswith("\n"): s += "\r\n"
        s += "\r\n" + emit("Tank_ChinaWarFactoryCommandSetPage2", P2).replace("\n", "\r\n") + "\r\n\r\n" + \
             emit("Tank_ChinaWarFactoryCommandSetPage3", P3).replace("\n", "\r\n") + "\r\n"
    open(CST, "wb").write(s.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
    print("  [OK] CommandSet.ini: 三页命令集")
    print("  载具总数 = %d" % (9 + len(NEW)))


if __name__ == "__main__":
    main()
