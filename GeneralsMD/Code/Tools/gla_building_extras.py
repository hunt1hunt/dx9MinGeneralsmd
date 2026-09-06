# -*- coding: utf-8 -*-
"""gla_building_extras.py — 2026-09-06
GLA counterpart of us_building_extras.py (fixed newline handling):
injects IDLE animations, turret lines, door draws, and ScudStorm
weapon states into the GLA W3XModelDraw blocks in FactionBuilding.ini.
"""
import re

INI = r'E:/!!!!!!!QWCSB/Data/INI/Object/FactionBuilding.ini'

# object -> (model, idle_anim, smokes, turret, extra_draws, weapon_states)
BUILDINGS = {
    'GLACommandCenter': ('GLACONSTRUCTIONYARD_SKN', 'GLACONSTRUCTIONYARD_IDLE', [], None, '', None),
    'GLABarracks': ('GLABARRACKS_SKN', 'GLABARRACKS_IDLE', [], None, 'BARRACKS_DOOR', None),
    'GLAArmsDealer': ('GLAWARFACTORY_SKN', 'GLAWARFACTORY_IDLE', [], None, 'ARMSDEALER_DOOR', None),
    'GLAArmsDealerNoHole': ('GLAWARFACTORY_SKN', 'GLAWARFACTORY_IDLE', [], None, '', None),
    'GLABlackMarket': ('GLABASESMUG_SKN', None, [], None, '', None),
    'GLAScudStorm': ('GLASUPERWEAPONADVANCED_SKN', 'GLASUPERWEAPONADVANCED_IDLE', [], None, '', 'SUPERWEAPON'),
    'GLAStingerSite': ('GLABASEDEFENSE_SKN', 'GLABASEDEFENSE_IDLA', [],
                       ['Turret = BONE_TURRET', 'TurretPitch = BONE_TURRETEL'], '', None),
    'GLAStingerSiteNoHole': ('GLABASEDEFENSE_SKN', 'GLABASEDEFENSE_IDLA', [],
                             ['Turret = BONE_TURRET', 'TurretPitch = BONE_TURRETEL'], '', None),
    'GLAPowerPlantWindmillBlade': ('GLAPOWERPLANT_SKN', 'GLAPOWERPLANT_IDLE', [], None, '', None),
    'GLAPalace': ('GLATECHSTRUCTURE_SKN', 'GLATECHSTRUCTURE_IDLE', [], None, '', None),
    'GLASupplyStash': ('GLAREFINERY_SKN', 'GLAREFINERY_IDLE', [], None, '', None),
}


def door_draw(tag, model):
    return ('  ; ----------------- RA3 door (animated) -------------------\n'
            '  Draw = W3XModelDraw ModuleTag_%02d\n' % tag +
            '    DefaultModelName = %s\n' % model +
            '    ConditionState = NONE\n      Model = %s\n    End\n' % model +
            '    ConditionState = DOOR_1_OPENING\n      Model = %s\n      Animation = %s\n      AnimationMode = ONCE\n    End\n' % (model, model) +
            '    ConditionState = DOOR_1_WAITING_OPEN\n      Model = %s\n      Animation = %s\n      AnimationMode = MANUAL\n    End\n' % (model, model) +
            '    ConditionState = DOOR_1_CLOSING\n      Model = %s\n      Animation = %s\n      AnimationMode = ONCE_BACKWARDS\n    End\n' % (model, model) +
            '  End')


def superweapon_states(model):
    st = ''
    for cond, mode in (('PREATTACK_A', 'ONCE'), ('FIRING_A', 'MANUAL'), ('BETWEEN_FIRING_SHOTS_A', 'MANUAL')):
        st += ('    ConditionState = %s\n' % cond +
               '      Model = %s\n' % model +
               '      Animation = GLASUPERWEAPONADVANCED_OPN\n'
               '      AnimationMode = %s\n    End\n' % mode)
    return st


def main():
    d = open(INI, 'r', encoding='utf-8', errors='replace').read()
    for obj, (model, idle, smokes, turret, extra, weapon) in BUILDINGS.items():
        mo = re.search(r'^Object %s\s*$' % re.escape(obj), d, re.M)
        if not mo:
            print('[SKIP] %s: object not found' % obj)
            continue
        start = mo.end()
        nxt = re.search(r'^Object ', d[start:], re.M)
        end = start + (nxt.start() if nxt else len(d) - start)
        block = d[start:end]

        if idle and idle in block:
            print('[SKIP] %s: already injected' % obj)
            continue
        if not idle and 'Turret = BONE_TURRET\n      TurretPitch' in block:
            print('[SKIP] %s: already injected' % obj)
            continue

        mo2 = re.search(r'(Draw = W3XModelDraw[^\n]*\n'
                        r'(\s*)DefaultModelName = %s\n'
                        r'(\s*)ConditionState = NONE\n'
                        r'\s*Model = %s\n)' % (re.escape(model), re.escape(model)), block)
        if not mo2:
            print('[FAIL] %s: main block pattern not found' % obj)
            continue
        inject = ''
        if idle:
            inject += '%s      Animation = %s\n%s      AnimationMode = LOOP\n' % (
                mo2.group(3), idle, mo2.group(3))
        for s in smokes:
            inject += '%s      ParticleSysBone = %s\n' % (mo2.group(3), s)
        if turret:
            for t in turret:
                inject += '%s      %s\n' % (mo2.group(3), t)
        new_block = block[:mo2.end(1)] + inject + block[mo2.end(1):]

        if weapon == 'SUPERWEAPON':
            rd = new_block.find('    ConditionState = REALLYDAMAGED')
            if rd >= 0:
                new_block = new_block[:rd] + superweapon_states(model) + new_block[rd:]

        tag = 9
        if extra == 'BARRACKS_DOOR':
            new_block = new_block.rstrip('\n') + '\n' + door_draw(tag, 'GLABARRACKS_DOOR') + '\n'
        elif extra == 'ARMSDEALER_DOOR':
            new_block = new_block.rstrip('\n') + '\n' + door_draw(tag, 'GLAWARFACTORY_DOOR') + '\n'

        d = d[:start] + new_block + d[end:]
        print('[OK] %s (idle=%s turret=%s door=%s weapon=%s)' % (
            obj, bool(idle), bool(turret), bool(extra), bool(weapon)))

    open(INI, 'w', encoding='utf-8', errors='replace').write(d)
    print('saved', INI)


if __name__ == '__main__':
    main()
