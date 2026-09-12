import re

p = r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\W3DShaderManager.cpp'
d = open(p, 'rb').read().decode('utf-8')
n0 = len(d)

T = '\t\t\t'
# 1) base variant: insert call after result *= terrainShadow
old = T + '"    result *= terrainShadow(shadowUVZ);\\n"\n'
new = old + T + '"    result += terrainPointLights(worldPos, N, terrainColor);\\n"\n'
assert d.count(old) == 1, 'base anchor count=%d' % d.count(old)
d = d.replace(old, new)

# 2) noise1/2/12: insert call after lit *= terrainShadow (3x)
old2 = T + '"    lit *= terrainShadow(shadowUVZ);\\n"\n'
new2 = old2 + T + '"    lit += terrainPointLights(worldPos, N, terrainColor);\\n"\n'
assert d.count(old2) == 3, 'lit anchor count=%d' % d.count(old2)
d = d.replace(old2, new2)

# 3) prepend the helper to the 4 terrain compile call sites
for handle in ['m_dwPBRPixelShader, "terrain_pbr_nm"',
               'm_dwPBRNoise1PixelShader, "terrain_pbr_nm_noise1"',
               'm_dwPBRNoise2PixelShader, "terrain_pbr_nm_noise2"',
               'm_dwPBRNoise12PixelShader, "terrain_pbr_nm_noise12"']:
    old3 = 'compilePBRShader(src, &' + handle
    new3 = 'compilePBRShader((std::string(TERRAIN_POINTLIGHT_HLSL) + src).c_str(), &' + handle
    assert d.count(old3) == 1, handle
    d = d.replace(old3, new3)

# 4) feed c13-c29 in set(), right after the unconditional c8 debug upload
anchor = '\t\t\tfloat sdbgT[4] = { TheGlobalData ? (float)TheGlobalData->m_pbrDebugMode : 0.0f, 0.0f, 0.0f, 0.0f };\n'
i = d.find(anchor)
assert i >= 0
j = d.find('\n', i + len(anchor))  # end of the SetPixelShaderConstantF(8...) line after anchor
# anchor is followed by the SetPixelShaderConstantF(8, sdbgT, 1); line
k = d.find('SetPixelShaderConstantF(8, sdbgT, 1);', j)
kend = d.find('\n', k)
feed = '''
			// 2026-09-12 ②B: forward point lights - the same W3X registry the
			// object PBR shaders feed from. c13-c20 = pos.xyz+outerRange,
			// c21-c28 = color.rgb+innerRange, c29.x = count. Terrain renders
			// BEFORE the W3X objects in the forward pass, so this reads the
			// previous frame's registrations (buildings are static - exact).
			{
				float plPosR[8][4], plColI[8][4];
				for (int pi = 0; pi < 8; pi++) {
					for (int pk = 0; pk < 4; pk++) { plPosR[pi][pk] = 0.0f; plColI[pi][pk] = 0.0f; }
				}
				int pln = W3XGetForwardPointLightCount();
				if (pln > 8) pln = 8;
				const W3XForwardPointLight *pl = W3XGetForwardPointLights();
				for (int pi = 0; pi < pln; pi++) {
					plPosR[pi][0] = pl[pi].x; plPosR[pi][1] = pl[pi].y; plPosR[pi][2] = pl[pi].z; plPosR[pi][3] = pl[pi].outerRadius;
					plColI[pi][0] = pl[pi].r;  plColI[pi][1] = pl[pi].g;  plColI[pi][2] = pl[pi].b;  plColI[pi][3] = pl[pi].innerRadius;
				}
				float plCnt[4] = { (float)pln, 0.0f, 0.0f, 0.0f };
				DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(13, (const float*)plPosR, 8);
				DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(21, (const float*)plColI, 8);
				DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(29, plCnt, 1);
			}
'''
d = d[:kend+1] + feed + d[kend+1:]

# 5) include the registry header
oldinc = '#include "W3DDevice/GameClient/W3DShaderManager.h"'
if oldinc in d:
    d = d.replace(oldinc, oldinc + '\n#include "W3DDevice/GameClient/W3XEffectManager.h"', 1)
else:
    # fallback: anchor on another known include
    alt = '#include "W3DDevice/GameClient/W3DDeferredRenderer.h"'
    assert alt in d
    d = d.replace(alt, alt + '\n#include "W3DDevice/GameClient/W3XEffectManager.h"', 1)

open(p, 'wb').write(d.encode('utf-8'))
print('patched OK, +%d bytes' % (len(d) - n0))
