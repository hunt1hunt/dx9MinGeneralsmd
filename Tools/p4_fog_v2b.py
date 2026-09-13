import re

# --- GlobalData.cpp: FogHeight parse + default ---
p2 = r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngine\Source\Common\GlobalData.cpp'
d2 = open(p2, 'rb').read().decode('utf-8')
m = re.search(r'^\t\{ "FogEnd".*$', d2, re.M)
assert m, 'fogend parse line'
line = m.group(0)
d2 = d2.replace(line, line + '\n\t{ "FogHeight",\t\t\t\tINI::parseReal,\t\t\t\tNULL,\t\t\toffsetof( GlobalData, m_fogHeight ) },', 1)
m3 = re.search(r'^\tm_fogEnd = 2400\.0f;.*$', d2, re.M)
assert m3, 'fogend default'
l3 = m3.group(0)
d2 = d2.replace(l3, l3 + '\n\tm_fogHeight = 350.0f;\t\t// P4 height ceiling (<=1 disables)', 1)
open(p2, 'wb').write(d2.encode('utf-8'))

# --- W3XEffectManager.cpp: case 23 gains z=1/FogHeight ---
p3 = r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\W3XEffectManager.cpp'
d3 = open(p3, 'rb').read().decode('utf-8')
i = d3.find('case 23:')
assert i >= 0
j = d3.find('case 24:', i)
assert j > i
seg = d3[i:j]
assert 'fih' not in seg
seg = seg.replace('float fs = 1e6f, fir = 0.0f;', 'float fs = 1e6f, fir = 0.0f, fih = 0.0f;')
seg = seg.replace('if (fe > fs) fir = 1.0f / (fe - fs);',
                  'if (fe > fs) fir = 1.0f / (fe - fs);\n\t\t\t\t\tfloat fh = TheGlobalData->m_fogHeight;\n\t\t\t\t\tif (fh > 1.0f) fih = 1.0f / fh;')
seg = seg.replace('D3DXVECTOR4 v(fs, fir, 0.0f, 0.0f);', 'D3DXVECTOR4 v(fs, fir, fih, 0.0f);')
seg = seg.replace('(start, 1/(end-start), 0, 0); disabled = 1e6',
                  '(start, 1/(end-start), 1/FogHeight, 0); disabled = 1e6')
d3 = d3[:i] + seg + d3[j:]
open(p3, 'wb').write(d3.encode('utf-8'))
print('v2b done')
