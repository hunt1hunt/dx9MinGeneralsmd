# -*- coding: utf-8 -*-
"""us_building_extras.py — 2026-09-06
Inject per-building extras into the W3XModelDraw blocks that
patch_building_ini.ps1 generated for the US->EU building replacement:
  * IDLE loop animations (ConditionState = NONE)
  * smoke ParticleSysBone lines (FX_SMOKE bones)
  * turret lines (BONE_TURRET / BONE_TURRETEL)
  * Airfield: 4 hangar-door draws with DOOR_1_* conditions
  * Barracks: door draw with DOOR_1_* conditions
  * ParticleCannonUplink: PREATTACK/FIRING/BETWEEN states (OPN = the collider opening)
Run AFTER patch_building_ini.ps1 + the model-name fix pass. Idempotent-ish:
skips a building when its marker line is already present.
"""
import re

INI = r'E:/!!!!!!!QWCSB/Data/INI/Object/FactionBuilding.ini'

# building -> (model, idle_anim, smokes, turret, extra_draws, weapon_states)
BUILDINGS = {
    'AmericaCommandCenter': ('EUCONSTRUCTIONYARD_SKN', 'EUCONSTRUCTIONYARD_IDLE',
                             ['FX_SMOKE01 SteamVent', 'FX_SMOKE02 SteamVent'], None, '', None),
    'AmericaBarracks': ('EUBARRACKS_SKN', 'EUBARRACKS_IDLE', [], None,
                        # door: model+animation in one container (EUBARRACKS_DOOR anim id = EUBARRACKS_DOOR)
                        '''
  ; ----------------- RA3 barracks door (animated) -------------------
  Draw = W3XModelDraw ModuleTag_09
    DefaultModelName = EUBARRACKS_DOOR
    ConditionState = NONE
      Model = EUBARRACKS_DOOR
    End
    ConditionState = DOOR_1_OPENING
      Model = EUBARRACKS_DOOR
      Animation = EUBARRACKS_DOOR
      AnimationMode = ONCE
    End
    ConditionState = DOOR_1_WAITING_OPEN
      Model = EUBARRACKS_DOOR
      Animation = EUBARRACKS_DOOR
      AnimationMode = MANUAL
    End
    ConditionState = DOOR_1_CLOSING
      Model = EUBARRACKS_DOOR
      Animation = EUBARRACKS_DOOR
      AnimationMode = ONCE_BACKWARDS
    End
  End''', None),
    'AmericaPowerPlant': ('EUPOWERPLANT_SKN', 'EUPOWERPLANT_IDLE',
                          ['FX_SMOKE01 SteamVent'], None, '', None),
    'AmericaAirfield': ('EUAIRFIELD_SKN', 'EUAIRFIELD_IDLE', [], None, 'AIRFIELD_DOORS', None),
    'AmericaSupplyCenter': ('EUREFINERY_SKN', 'EUREFINERY_IDLE', [], None, '', None),
    'AmericaStrategyCenter': ('EUSPECIALCENTER_SKN', 'EUSPECIALCENTER_IDLE', [], None, '', None),
    'AmericaParticleCannonUplink': ('EUSUPERWEAPONADVANCED_SKN', 'EUSUPERWEAPONADVANCED_IDLE',
                                    [], None, '', 'SUPERWEAPON'),
    'AmericaPatriotBattery': ('EUBASEDEFENSE_SKN', 'EUBASEDEFENSE_IDLE', [],
                              ['Turret = BONE_TURRET', 'TurretPitch = BONE_TURRETEL'], '', None),
    'AmericaFireBase': ('EUOUTPOST_SKN', None,
                        [], ['Turret = BONE_TURRET', 'TurretPitch = BONE_TURRETEL'], '', None),
    'AmericaCheckpoint': ('EUBUNKER_SKN', 'EUBUNKER_IDLE', [], None, '', None),
}


def airfield_doors():
    blocks = []
    for i in (1, 2, 3, 4):
        m = 'EUAIRFIELD_DOOR_0%d_SKN' % i
        blocks.append(
            '  ; ----------------- RA3 airfield hangar door %d -------------------\n' % i +
            '  Draw = W3XModelDraw ModuleTag_%02d\n' % (8 + i) +
            '    DefaultModelName = %s\n' % m +
            '    ConditionState = NONE\n      Model = %s\n    End\n' % m +
            '    ConditionState = DOOR_1_OPENING\n      Model = %s\n      Animation = %s\n      AnimationMode = ONCE\n    End\n' % (m, m) +
            '    ConditionState = DOOR_1_WAITING_OPEN\n      Model = %s\n      Animation = %s\n      AnimationMode = MANUAL\n    End\n' % (m, m) +
            '    ConditionState = DOOR_1_CLOSING\n      Model = %s\n      Animation = %s\n      AnimationMode = ONCE_BACKWARDS\n    End\n' % (m, m) +
            '  End')
    return '\n' + '\n'.join(blocks) + '\n'


def superweapon_states():
    return ('    ConditionState = PREATTACK_A\n'
            '      Model = EUSUPERWEAPONADVANCED_SKN\n'
            '      Animation = EUSUPERWEAPONADVANCED_OPN\n'
            '      AnimationMode = ONCE\n'
            '    End\n'
            '    ConditionState = FIRING_A\n'
            '      Model = EUSUPERWEAPONADVANCED_SKN\n'
            '      Animation = EUSUPERWEAPONADVANCED_OPN\n'
            '      AnimationMode = MANUAL\n'
            '    End\n'
            '    ConditionState = BETWEEN_FIRING_SHOTS_A\n'
            '      Model = EUSUPERWEAPONADVANCED_SKN\n'
            '      Animation = EUSUPERWEAPONADVANCED_OPN\n'
            '      AnimationMode = MANUAL\n'
            '    End\n')


def main():
    d = open(INI, 'r', encoding='utf-8', errors='replace').read()
    for obj, (model, idle, smokes, turret, extra, weapon) in BUILDINGS.items():
        # locate the object block
        mo = re.search(r'^Object %s\s*$' % re.escape(obj), d, re.M)
        if not mo:
            print('[SKIP] %s: object not found' % obj)
            continue
        start = mo.end()
        nxt = re.search(r'^Object ', d[start:], re.M)
        end = start + (nxt.start() if nxt else len(d) - start)
        block = d[start:end]

        marker = 'Animation = %s' % (idle or '')
        if idle and idle in block:
            print('[SKIP] %s: idle already injected' % obj)
            continue
        if not idle and ('Turret = BONE_TURRET' in block or extra.strip() in block):
            print('[SKIP] %s: extras already injected' % obj)
            continue

        # 1) inject into the NONE ConditionState of the main draw
        none_pat = (r'(Draw = W3XModelDraw[^\n]*\n'
                    r'(\s*)DefaultModelName = %s\n'
                    r'(\s*)ConditionState = NONE\n'
                    r'\s*Model = %s\n)' % (re.escape(model), re.escape(model)))
        mo2 = re.search(none_pat, block)
        if not mo2:
            print('[FAIL] %s: main W3X block pattern not found' % obj)
            continue
        inject = ''
        if idle:
            inject += '\n%s      Animation = %s\n%s      AnimationMode = LOOP' % (
                mo2.group(3), idle, mo2.group(3))
        for s in smokes:
            inject += '\n%s      ParticleSysBone = %s' % (mo2.group(3), s)
        if turret:
            for t in turret:
                inject += '\n%s      %s' % (mo2.group(3), t)
        new_block = block[:mo2.end(1)] + inject + block[mo2.end(1):]

        # 2) weapon states inserted before REALLYDAMAGED
        if weapon == 'SUPERWEAPON':
            rd = new_block.find('    ConditionState = REALLYDAMAGED')
            if rd >= 0:
                new_block = new_block[:rd] + superweapon_states() + new_block[rd:]

        # 3) extra draws appended at the very end of the object block
        if extra == 'AIRFIELD_DOORS':
            new_block = new_block.rstrip('\n') + '\n' + airfield_doors()
        elif extra.strip():
            new_block = new_block.rstrip('\n') + '\n' + extra + '\n'

        d = d[:start] + new_block + d[end:]
        print('[OK] %s patched (idle=%s smokes=%d turret=%s extra=%s)' % (
            obj, bool(idle), len(smokes), bool(turret), bool(extra)))

    open(INI, 'w', encoding='utf-8', errors='replace').write(d)
    print('saved', INI)


if __name__ == '__main__':
    main()
