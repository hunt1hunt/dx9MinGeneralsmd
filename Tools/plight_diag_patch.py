import re

P1 = r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\Drawable\Draw\W3XModelDraw.cpp'
P2 = r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\W3XEffectManager.cpp'

def patch(path, pairs, tag):
    d = open(path, 'rb').read()
    for old, new, name in pairs:
        c = d.count(old)
        assert c == 1, (tag, name, c)
        d = d.replace(old, new)
    open(path, 'wb').write(d)
    print(tag, 'patched')

# ---- W3XModelDraw.cpp ----
HELPER = b'''
// TEMP PLIGHT DIAG (remove): light-volume load-chain logger -> E:\\GeneralsMD_DeferredRT.log
static void PLightDiag(const char *fmt, ...)
{
\tstatic FILE *s_pl = NULL;
\tif (!s_pl) { s_pl = fopen("E:\\\\GeneralsMD_DeferredRT.log", "a"); if (s_pl) setvbuf(s_pl, NULL, _IOLBF, 1024); }
\tif (!s_pl) return;
\tfprintf(s_pl, "[%09u] PLIGHT: ", GetTickCount());
\tva_list a; va_start(a, fmt); vfprintf(s_pl, fmt, a); va_end(a);
}
'''
BS = chr(92)  # backslash
a1_old = ('\tDEBUG_LOG(("[W3X_P5] loadW3XModel(\'%s\')' + BS + 'n", containerName));').encode()
a1_new = a1_old + ('\n\tPLightDiag("load \'%s\'' + BS + 'n", containerName);').encode()

a2_old = ('\t\tDEBUG_LOG(("[W3X_P5]   Failed to parse container \'%s\'' + BS + 'n", containerPath));').encode()
a2_new = a2_old + ('\n\t\tPLightDiag("CONTAINER FAIL \'%s\'' + BS + 'n", containerPath);').encode()

a3_old = ('robj->SetSubMeshShader((int)i, "Shaders' + BS + BS + 'RA3' + BS + BS + 'w3x_pointlight.fx", 0, sm.constants);').encode()
a3_new = a3_old + ('\n\t\t\t\tPLightDiag("routed \'%s\'' + BS + 'n", sm.name.str());').encode()

a4_old = b'DEBUG_LOG(("[W3X_P5]   Container parsed, hierarchy=%s, %d sub-objects'
a4_new = b'PLightDiag("container OK %s subs=%d\\n", hierarchyName.str(), (int)subObjects.size());\n\t' + a4_old

inc = b'#include "GameClient/Drawable.h"'
inc_new = inc + HELPER

patch(P1, [
    (inc, inc_new, 'helper'),
    (a1_old, a1_new, 'a1'),
    (a2_old, a2_new, 'a2'),
    (a3_old, a3_new, 'a3'),
    (a4_old, a4_new, 'a4'),
    (b'#include "always.h"', b'#include "always.h"\n#include <stdarg.h>', 'stdarg'),
], 'W3XModelDraw')

# ---- W3XEffectManager.cpp ----
a5_old = b'DEBUG_LOG(("[W3X_P3] GetEffect(\'%s\') D3DXCreateEffect FAILED hr=0x%08X: %s\\n",'
a5_new = b'PLightDiag("effect FAIL \'%s\' hr=0x%08X\\n", fxPath, (int)hr);\n\t\t' + a5_old
patch(P2, [
    (b'#include "always.h"', b'#include "always.h"\nextern void PLightDiag(const char *fmt, ...);', 'extern'),
    (a5_old, a5_new, 'a5'),
], 'W3XEffectManager')
print('ALL DONE')
