p = r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngine\GameEngine.dsp'
d = open(p, 'rb').read().decode('latin-1')

def insert_after_entry(text, anchor_source, new_source):
    i = text.find(anchor_source)
    assert i >= 0, anchor_source
    # skip to end of the anchor SOURCE line, then past its '# End Source File' line
    j = text.find('\n', i)
    k = text.find('# End Source File', j)
    assert k >= 0
    kend = text.find('\n', k)
    entry = '# Begin Source File\r\n\r\nSOURCE=' + new_source + '\r\n# End Source File\r\n'
    assert new_source not in text
    return text[:kend+1] + entry + text[kend+1:]

d = insert_after_entry(d,
    'SOURCE=.\\Source\\GameLogic\\Object\\Update\\LaserUpdate.cpp',
    '.\\Source\\GameLogic\\Object\\Update\\WeaponFireLaserUpdate.cpp')

d = insert_after_entry(d,
    'SOURCE=.\\Include\\GameLogic\\Module\\AssistedTargetingUpdate.h',
    '.\\Include\\GameLogic\\Module\\WeaponFireLaserUpdate.h')

open(p, 'wb').write(d.encode('latin-1'))
# sanity: balanced Begin/End Source File counts
s = d.count('# Begin Source File')
e = d.count('# End Source File')
print('dsp updated, Begin=%d End=%d %s' % (s, e, 'OK' if s == e else 'MISMATCH!'))
