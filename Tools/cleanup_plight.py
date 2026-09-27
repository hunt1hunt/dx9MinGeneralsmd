import re

# ============ 1) W3XModelDraw.cpp: remove PLightDiag function + 4 calls ============
p = r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\Drawable\Draw\W3XModelDraw.cpp'
d = open(p, 'rb').read()

# remove helper function block
i = d.find(b'// TEMP PLIGHT DIAG (remove): light-volume load-chain logger')
j = d.find(b'}', d.find(b'va_end(a);', i))
assert i >= 0 and j > i
d = d[:i] + d[j+1:]
print('helper removed')

# remove 4 call sites
for pat in [
    re.compile(rb'\n\tPLightDiag\("load \'%s\'[^;]*;', re.S),
    re.compile(rb'\n\t\tPLightDiag\("CONTAINER FAIL \'%s\'[^;]*;', re.S),
    re.compile(rb'\n\t\t\t\tPLightDiag\("routed \'%s\'[^;]*;', re.S),
    re.compile(rb'PLightDiag\("container OK %s subs=%d[^;]*;\n\t', re.S),
]:
    d, n = pat.subn(lambda m: b'' if b'container OK' not in m.group(0) else b'', d)
open(p, 'wb').write(d)
dd = open(p, 'rb').read()
print('W3XModelDraw PLightDiag refs left:', dd.count(b'PLightDiag'))

# ============ 2) W3XEffectManager.cpp ============
p2 = r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\W3XEffectManager.cpp'
d2 = open(p2, 'rb').read()
d2 = d2.replace(b'\r\nextern void PLightDiag(const char *fmt, ...);', b'').replace(b'\nextern void PLightDiag(const char *fmt, ...);', b'')
d2 = d2.replace(b'PLightDiag("effect FAIL \'%s\' hr=0x%08X\\n", fxPath, (int)hr);\r\n\t\t', b'').replace(b'PLightDiag("effect FAIL \'%s\' hr=0x%08X\\n", fxPath, (int)hr);\n\t\t', b'')
# EYEBIND block
i2 = d2.find(b'{ static int s_eyeN = 0;')
if i2 >= 0:
    j2 = d2.find(b'} }', i2)
    assert j2 > i2
    d2 = d2[:i2] + d2[j2+3:]
    print('EYEBIND removed')
# introspection block
i3 = d2.find(b'// TEMP INTROSPECTION (remove)')
if i3 >= 0:
    # block spans from comment to the closing '\t}' before the next section - find '\t}\r\n}' pattern... safer: find start at the comment line's preceding \t, end at the standalone '\t}' that closes "if (strstr"
    j3 = d2.find(b'PLightDiag("  byName: View=%p EyePosition=%p Time=%p\\n", (void*)hV, (void*)hE, (void*)hT);\r\n\t\t}\r\n\t}', i3)
    if j3 < 0:
        j3 = d2.find(b'PLightDiag("  byName: View=%p EyePosition=%p Time=%p\\n", (void*)hV, (void*)hE, (void*)hT);\n\t\t}\n\t}', i3)
    assert j3 >= 0, 'introspection end not found'
    endtok = d2.find(b'\t}', j3 + 10)
    # find the end of the if(strstr) block: count from comment start the matching brace is hard; simpler: cut to after '\t}\r\n}' pattern
    # The block structure ends with: \t\t}\r\n\t}\r\n right after byName line
    pat_end = d2.find(b'}', d2.find(b'hT);', i3))
    # two more closing braces: \t\t} then \t}
    pat_end = d2.find(b'}', pat_end+1)
    pat_end = d2.find(b'}', pat_end+1)
    assert pat_end > i3
    d2 = d2[:i3 - 2] + d2[pat_end+1:] if d2[i3-2:i3] == b'\t\t' or True else d2
    print('introspection removed')
open(p2, 'wb').write(d2)
dd2 = open(p2, 'rb').read()
print('W3XEffectManager PLightDiag refs left:', dd2.count(b'PLightDiag'))

# ============ 3) W3XRenderObj.cpp ============
p3 = r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\W3XRenderObj.cpp'
d3 = open(p3, 'rb').read()
d3 = d3.replace(b'\r\nextern void PLightDiag(const char *fmt, ...);', b'').replace(b'\nextern void PLightDiag(const char *fmt, ...);', b'')
# bind-invocation log
i4 = d3.find(b'{ static int s_becN = 0;')
if i4 >= 0:
    j4 = d3.find(b'}', i4)
    # the block is one line ending with }
    j4 = d3.find(b'\n', i4)
    d3 = d3[:i4] + d3[j4+1:]
    print('bind-invocation log removed')
# EyePosition-via-matrices block
i5 = d3.find(b'// 2026-09-12 EYEPOSITION VIA THE PROVEN BINDER')
if i5 >= 0:
    j5 = d3.find(b'\t}\n', d3.find(b'effect->SetVector(hEyeP, (const D3DXVECTOR4*)eyePos);', i5))
    if j5 < 0:
        j5 = d3.find(b'\t}\r\n', d3.find(b'effect->SetVector(hEyeP, (const D3DXVECTOR4*)eyePos);', i5))
    assert j5 > i5
    d3 = d3[:i5-1] + d3[j5+3:]
    print('EyePosition bind removed')
# View-via-matrices block (NOT the ViewProjInverse one!)
i6 = d3.find(b'// 2026-09-12 VIEW VIA THE PROVEN BINDER')
if i6 >= 0:
    j6 = d3.find(b'\t}\n', d3.find(b'effect->SetMatrix(hViewP, (const D3DXMATRIX*)&curViewB);', i6))
    if j6 < 0:
        j6 = d3.find(b'\t}\r\n', d3.find(b'effect->SetMatrix(hViewP, (const D3DXMATRIX*)&curViewB);', i6))
    assert j6 > i6
    d3 = d3[:i6-1] + d3[j6+3:]
    print('View bind removed')
open(p3, 'wb').write(d3)
dd3 = open(p3, 'rb').read()
print('W3XRenderObj PLightDiag refs left:', dd3.count(b'PLightDiag'))
print('ViewProjInverse bind kept:', b'ViewProjInverse' in dd3)

# ============ 4) FX: remove blue debug branch ============
for t in [r'D:\!!!!!!!QWCSB\!!!!!!!QWCSB\Shaders\RA3\w3x_pointlight.fx',
          r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\Shaders\RA3\w3x_pointlight.fx']:
    df = open(t,'rb').read().decode('utf-8').replace('\r\n','\n')
    old = '    if (dot(camPos, camPos) < 1.0) return float4(0.0, 0.0, 0.12, 1) ;   // DEBUG: matrix dead -> blue\n'
    if old in df:
        df = df.replace(old, '')
        open(t,'wb').write(df.replace('\n','\r\n').encode('utf-8'))
        print('blue branch removed:', t)
print('CLEANUP DONE')
