#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
lazr_warfactory_paging.py — 给激光将军兵工厂加"可翻页的两页操作栏"(2026-10-02)

机制与步兵将军完全一致(照抄 GLA Worker 的假/真建筑命令集切换):
  两个免费的 OBJECT 升级 + 两个 CommandSetUpgrade 模块,互相 RemovesUpgrades,
  于是永远只持有其中一个 -> 翻页可以无限来回。
"""
import os, re, shutil, sys

D = r"D:\zerohour\Data\INI"
UPG = os.path.join(D, "Upgrade.ini")
CBT = os.path.join(D, "CommandButton.ini")
CST = os.path.join(D, "CommandSet.ini")
LAS = os.path.join(D, "Object", "LaserGeneral.ini")

P1, P2 = "Lazr_WarFactoryPage1", "Lazr_WarFactoryPage2"


def append(path, text, marker):
    d = open(path, "rb").read()
    if marker.encode() in d:
        print("  (skip, already present: %s)" % os.path.basename(path)); return False
    s = d.decode("utf-8")
    if not s.endswith("\n"):
        s += "\n"
    s += text
    open(path, "wb").write(s.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
    b = open(path, "rb").read()
    print("  [OK] %-20s +%d bytes" % (os.path.basename(path), len(b) - len(d)))
    return True


# ---------- 1) 翻页升级 ----------
append(UPG, """
;------------------------------------------------------------------------------
; 激光将军 兵工厂 命令栏翻页 (2026-10-02)
; 与步兵将军同一套机制:两个免费瞬发 OBJECT 升级互相 RemovesUpgrades,翻页可无限来回。
;------------------------------------------------------------------------------
Upgrade Lazr_WarFactoryPage1
  Type               = OBJECT
  BuildTime          = 0.0
  BuildCost          = 0
  ButtonImage        = SUFakeToggle
End

Upgrade Lazr_WarFactoryPage2
  Type               = OBJECT
  BuildTime          = 0.0
  BuildCost          = 0
  ButtonImage        = SUFakeToggle
End
""", "Lazr_WarFactoryPage2")

# ---------- 2) 按钮(缺失的 5 个载具 + 2 个翻页箭头) ----------
append(CBT, """
;------------------------------------------------------------------------------
; 激光将军兵工厂:补齐缺失的载具建造按钮 + 翻页箭头 (2026-10-02)
; 翻页按钮故意不写 TextLabel/DescriptLabel(引擎对两者都先判 isNotEmpty) -> 纯图标。
;------------------------------------------------------------------------------
CommandButton Lazr_Command_ConstructAmericaVehicleTomahawk
  Command           = UNIT_BUILD
  Object            = Lazr_AmericaVehicleTomahawk
  TextLabel         = CONTROLBAR:ConstructAmericaVehicleTomahawk
  ButtonImage       = SACTomahawk
  ButtonBorderType  = BUILD
  DescriptLabel     = CONTROLBAR:ToolTipUSABuildTomahawk
End

CommandButton Lazr_Command_ConstructAmericaVehicleBattleDrone
  Command           = UNIT_BUILD
  Object            = Lazr_AmericaVehicleBattleDrone
  TextLabel         = CONTROLBAR:AirF_ConstructAmericaVehicleBattleDrone
  ButtonImage       = SASoloDrone
  ButtonBorderType  = BUILD
  DescriptLabel     = CONTROLBAR:ToolTipUSABuildBattleDrone
End

CommandButton Lazr_Command_ConstructAmericaVehicleScoutDrone
  Command           = UNIT_BUILD
  Object            = Lazr_AmericaVehicleScoutDrone
  TextLabel         = CONTROLBAR:AirF_ConstructAmericaVehicleBattleDrone
  ButtonImage       = SASoloDrone
  ButtonBorderType  = BUILD
  DescriptLabel     = CONTROLBAR:ToolTipUSABuildBattleDrone
End

CommandButton Lazr_Command_ConstructAmericaVehicleHellfireDrone
  Command           = UNIT_BUILD
  Object            = Lazr_AmericaVehicleHellfireDrone
  TextLabel         = CONTROLBAR:AirF_ConstructAmericaVehicleBattleDrone
  ButtonImage       = SASoloDrone
  ButtonBorderType  = BUILD
  DescriptLabel     = CONTROLBAR:ToolTipUSABuildBattleDrone
End

CommandButton Lazr_Command_ConstructAmericaVehicleSpyDrone
  Command           = UNIT_BUILD
  Object            = Lazr_AmericaVehicleSpyDrone
  TextLabel         = CONTROLBAR:AirF_ConstructAmericaVehicleBattleDrone
  ButtonImage       = SASoloDrone
  ButtonBorderType  = BUILD
  DescriptLabel     = CONTROLBAR:ToolTipUSABuildBattleDrone
End

CommandButton Lazr_Command_WarFactoryPage2
  Command           = OBJECT_UPGRADE
  Upgrade           = Lazr_WarFactoryPage2
  ButtonImage       = SUFakeToggle
  ButtonBorderType  = UPGRADE
End

CommandButton Lazr_Command_WarFactoryPage1
  Command           = OBJECT_UPGRADE
  Upgrade           = Lazr_WarFactoryPage1
  ButtonImage       = SUFakeToggle
  ButtonBorderType  = UPGRADE
End
""", "Lazr_Command_WarFactoryPage2")

# ---------- 3) 两页命令集 ----------
PAGE1 = """CommandSet Lazr_AmericaWarFactoryCommandSet
  1  = Lazr_Command_WarFactoryPage2
  2  = Lazr_Command_ConstructAmericaTankCrusader
  3  = Lazr_Command_ConstructAmericaVehicleHumvee
  4  = Lazr_Command_ConstructAmericaVehicleMedic
  5  = Lazr_Command_ConstructAmericaDozer
  6  = Lazr_Command_ConstructAmericaVehiclePaladin
  7  = Lazr_Command_ConstructAmericaVehicleSentryDrone
  8  = Lazr_Command_ConstructAmericaVehicleAvenger
  9  = Lazr_Command_ConstructAmericaVehicleMicrowave
  10 = Lazr_Command_ConstructAmericaVehicleTomahawk
  11 = Command_UpgradeAmericaSentryDroneGun
  12 = Command_UpgradeAmericaTOWMissile
  13 = Command_SetRallyPoint
  14 = Command_Sell
End"""

PAGE2 = """CommandSet Lazr_AmericaWarFactoryCommandSetPage2
  1  = Lazr_Command_WarFactoryPage1
  2  = Lazr_Command_ConstructAmericaVehicleBattleDrone
  3  = Lazr_Command_ConstructAmericaVehicleScoutDrone
  4  = Lazr_Command_ConstructAmericaVehicleHellfireDrone
  5  = Lazr_Command_ConstructAmericaVehicleSpyDrone
  13 = Command_SetRallyPoint
  14 = Command_Sell
End"""

src = open(CST, encoding="utf-8", newline="").read()
m = re.search(r"(?mi)^CommandSet Lazr_AmericaWarFactoryCommandSet *\r?\n.*?\r?\nEnd\r?\n", src, re.S)
assert m, "Lazr_AmericaWarFactoryCommandSet not found"
src = src[:m.start()] + PAGE1.replace("\n", "\r\n") + "\r\n" + src[m.end():]
if "Lazr_AmericaWarFactoryCommandSetPage2" not in src:
    if not src.endswith("\n"):
        src += "\r\n"
    src += "\r\n" + PAGE2.replace("\n", "\r\n") + "\r\n"
open(CST, "wb").write(src.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
print("  [OK] CommandSet.ini: page1 改写 + page2 新增")

# ---------- 4) 兵工厂挂两个 CommandSetUpgrade ----------
lsrc = open(LAS, encoding="utf-8", newline="").read()
assert "Lazr_WarFactoryPage2" not in lsrc, "modules already present"
objs = list(re.finditer(r"(?m)^Object[ \t]+(\S+)", lsrc))
MODS = """  ; ---- 命令栏翻页 (2026-10-02) ------------------------------------------------
  ; TriggeredBy = 翻页箭头按钮给的免费瞬发升级; RemovesUpgrades = 另一个页升级,
  ; 于是永远只持有其中一个,翻页可无限来回。
  Behavior = CommandSetUpgrade ModuleTag_Page1
    TriggeredBy               = Lazr_WarFactoryPage2
    RemovesUpgrades           = Lazr_WarFactoryPage1
    CommandSet                = Lazr_AmericaWarFactoryCommandSetPage2
  End
  Behavior = CommandSetUpgrade ModuleTag_Page2
    TriggeredBy               = Lazr_WarFactoryPage1
    RemovesUpgrades           = Lazr_WarFactoryPage2
    CommandSet                = Lazr_AmericaWarFactoryCommandSet
  End
"""
done = False
for i, m in enumerate(objs):
    if m.group(1) != "Lazr_AmericaWarFactory":
        continue
    e = objs[i+1].start() if i+1 < len(objs) else len(lsrc)
    body = lsrc[m.start():e]
    # 插到该对象的 CommandSet 行之后(保证在对象体内)
    cs = re.search(r"(?m)^[ \t]*CommandSet[ \t]*=[ \t]*\S+.*$", body)
    assert cs, "no CommandSet line"
    ins = cs.end() + 1
    indented = "\r\n".join(MODS.rstrip("\n").split("\n"))
    body = body[:ins] + indented + "\r\n" + body[ins:]
    lsrc = lsrc[:m.start()] + body + lsrc[e:]
    done = True
    break
assert done, "Lazr_AmericaWarFactory not found"
open(LAS, "wb").write(lsrc.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
print("  [OK] LaserGeneral.ini: WarFactory +2 CommandSetUpgrade")
for f in (UPG, CBT, CST, LAS):
    b = open(f, "rb").read()
    print("     %-24s %8d bytes CRLF=%d bareLF=%d" % (os.path.basename(f), len(b),
          b.count(b"\r\n"), b.count(b"\n") - b.count(b"\r\n")))
