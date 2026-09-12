import re

p = r'D:\!!!!!!!QWCSB\!!!!!!!QWCSB\Data\INI\Object\AmericaInfantry.ini'
d = open(p, 'rb').read().decode('utf-8', errors='replace')

BEAM = '''
  ; 2026-09-12 ④ STARRY LASER: muzzle->victim beam while the weapon fires.
  ; The texture name contains "starry" -> SegLineRenderer engages the
  ; starry pixel shader (screen-space FXstarrysky256quad starfield).
  Draw = W3DLaserDraw ModuleTag_StarryLaser
    Texture = FXstarrysky256quad.dds
    NumBeams = 1
    InnerBeamWidth = 2.0
    InnerColor = R:255 G:235 B:190 A:200
    ScrollRate = 0.4
    Tile = No
    Segments = 2
    ArcHeight = 0.0
  End
  ClientUpdate = LaserUpdate ModuleTag_StarryLU
    ;nothing - driven by WeaponFireLaserUpdate below
  End
  ClientUpdate = WeaponFireLaserUpdate ModuleTag_StarryWFL
    TriggerWeaponSlot = PRIMARY
    MuzzleBoneName = fx01
    HoldFrames = 5
    DecayFrames = 12
    MuzzleHeight = 12.0
  End
'''

def attach(objname):
    global d
    i = d.find('Object ' + objname)
    assert i >= 0, objname
    # insert right after the object's opening line's following blank line
    j = d.find('\n', i)
    # skip to end of that line, insert block after it
    d = d[:j+1] + BEAM + d[j+1:]
    print('attached:', objname)

attach('AmericaInfantryRanger')
attach('AmericaInfantryMissileDefender')

open(p, 'wb').write(d.encode('utf-8'))
print('INI done')
