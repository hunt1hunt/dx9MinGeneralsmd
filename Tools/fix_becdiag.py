import re
p = r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\W3XRenderObj.cpp'
d = open(p, 'rb').read()

# remove the broken inserted block (whatever form it took)
pat = re.compile(rb'\{ static int s_becN = 0;.*?\};? \}', re.S)
m = pat.search(d)
if m:
    d = d[:m.start()] + d[m.end():]
    print('broken block removed')
else:
    print('no broken block found (maybe clean)')

BS = chr(92)
good = ('{ static int s_becN = 0; if ((s_becN++ % 3000) == 0) PLightDiag("BindEngineConstants ran for %s' + BS + 'n", sm.fxName.str()); }').encode()

anchor = b'W3XEffectManager::Instance()->BindEngineConstants(drawEffect, rinfo);'
assert d.count(anchor) == 1, d.count(anchor)
d = d.replace(anchor, anchor + b'\r\n\t\t\t\t' + good)

inc = b'#include "always.h"'
assert d.count(inc) >= 1
d = d.replace(inc, inc + b'\r\nextern void PLightDiag(const char *fmt, ...);', 1)

open(p, 'wb').write(d)
print('fixed OK')
