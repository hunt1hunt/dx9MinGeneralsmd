import re

P = r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\W3DShaderManager.cpp'
d = open(P, 'rb').read().decode('utf-8')

# --- 1) terrain fog: add height factor at all 4 sites (after the saturate line) ---
anchor = '* fogParams.y);\\n"'
n = d.count(anchor)
assert n == 4, 'fog saturate sites = %d' % n
d = d.replace(anchor, '* fogParams.y);\\n"\n\t\t\t"    fogA *= 1.0 - saturate(worldPos.z * fogParams.z);\\n"')

# --- 2) terrain set(): fih in c30.z ---
a2 = 'float fs = 1e6f, fir = 0.0f;'
assert d.count(a2) == 1
d = d.replace(a2, 'float fs = 1e6f, fir = 0.0f, fih = 0.0f;')
a3 = '\t\t\t\t\t\tif (fe > fs) fir = 1.0f / (fe - fs);'
assert d.count(a3) == 1
d = d.replace(a3, a3 + '\n\t\t\t\t\t\tfloat fh = TheGlobalData->m_fogHeight;\n\t\t\t\t\t\tif (fh > 1.0f) fih = 1.0f / fh;\t// else 0: no height falloff')
a4 = 'float c30[4] = { fs, fir, 0.0f, 0.0f };'
assert d.count(a4) == 1
d = d.replace(a4, 'float c30[4] = { fs, fir, fih, 0.0f };')

# --- 3) pbr_unit + NT: fog before final return + const decls ---
a5 = '"    return float4(result, albedo.a);\\n"'
n5 = d.count(a5)
print('pbr return sites:', n5)
assert n5 >= 2
fogret = ('"    float fogAP = saturate((distance(worldPos, c2.xyz) - fogC0.x) * fogC0.y);\\n"\n'
          '\t\t\t"    fogAP *= 1.0 - saturate(worldPos.z * fogC0.z);\\n"\n'
          '\t\t\t"    result = lerp(result, fogC1.rgb, fogAP);\\n"\n'
          '\t\t\t"    return float4(result, albedo.a);\\n"')
d = d.replace(a5, fogret)

a6 = '"float3 c10 : register(c10);\\n"'
n6 = d.count(a6)
print('c10 decl sites:', n6)
assert n6 >= 2
d = d.replace(a6, '"float3 c10 : register(c10);\\n"\n\t\t\t"float4 fogC0 : register(c30);\\n"\n\t\t\t"float4 fogC1 : register(c31);\\n"')

# --- 4) W3DPBRShader::set(): upload c30/c31 after the c11 dbg block ---
a7 = '\tfloat dbg[4] = { (float)TheGlobalData->m_pbrDebugMode, 0.0f, 0.0f, 0.0f };\n\t\tDX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(11, dbg, 1);\n\t}'
alt = '\tfloat dbg[4] = { (float)TheGlobalData->m_pbrDebugMode, 0.0f, 0.0f, 0.0f };\n\t\tDX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(11, dbg, 1);\n\t}\n'
if a7 not in d and alt.rstrip('\n') not in d:
    # locate loosely
    m = re.search(r'\n\t*\{\n\t*//[^\n]*debug mode off[^\n]*\n\t*float dbg\[4\][^;]*;[^\n]*\n\t*DX8Wrapper[^\n]*SetPixelShaderConstantF\(11, dbg, 1\);', d)
    print('dbg block regex:', bool(m))
    raise SystemExit('dbg anchor not found')
anchor_dbg = alt if alt.rstrip('\n') in d else a7
fogfeed = anchor_dbg + '''
	// P4 distance+height fog (c30/c31) - same values the terrain shaders get.
	{
		float fs = 1e6f, fir = 0.0f, fih = 0.0f;
		float fr = 0.65f, fg = 0.72f, fb = 0.80f;
		if (TheGlobalData && TheGlobalData->m_useDistanceFog) {
			fs = TheGlobalData->m_fogStart;
			float fe = TheGlobalData->m_fogEnd;
			if (fe > fs) fir = 1.0f / (fe - fs);
			float fh = TheGlobalData->m_fogHeight;
			if (fh > 1.0f) fih = 1.0f / fh;
			fr = TheGlobalData->m_fogColorR;
			fg = TheGlobalData->m_fogColorG;
			fb = TheGlobalData->m_fogColorB;
		}
		float c30[4] = { fs, fir, fih, 0.0f };
		float c31[4] = { fr, fg, fb, 1.0f };
		DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(30, c30, 1);
		DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(31, c31, 1);
	}'''
d = d.replace(anchor_dbg, fogfeed, 1)

open(P, 'wb').write(d.encode('utf-8'))
print('W3DShaderManager P4.1 done')
