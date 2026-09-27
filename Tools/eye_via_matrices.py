import re
p = r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\W3XRenderObj.cpp'
d = open(p, 'rb').read()

BS = chr(92)
anchor = ('\tD3DXHANDLE hWVP = effect->GetParameterByName(NULL, "WorldViewProj");\n'
          '\tif (hWVP) effect->SetMatrix(hWVP, (const D3DXMATRIX*)&wvp);\n').encode()
assert d.count(anchor) == 1, d.count(anchor)

inject = anchor + (
'\t// 2026-09-12 EYEPOSITION VIA THE PROVEN BINDER: BindEngineConstants logs prove\n'
'\t// its SetVector(EyePosition) is CALLED with valid camera values, yet the PS\n'
'\t// reads zeros (Time too) - the D3DX apply layer drops this path\'s values for\n'
'\t// the point-light effect. Matrices and per-mesh constants set HERE verifiably\n'
'\t// land, so bind the camera position through this same call site: inverse-view\n'
'\t// row 3 = camera world position.\n'
'\tD3DXHANDLE hEyeP = effect->GetParameterByName(NULL, "EyePosition");\n'
'\tif (hEyeP) {\n'
'\t\tMatrix4x4 curViewE;\n'
'\t\tDX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curViewE);\n'
'\t\tD3DXMATRIX invViewE;\n'
'\t\tfloat detE;\n'
'\t\tD3DXMatrixInverse(&invViewE, &detE, (const D3DXMATRIX*)&curViewE);\n'
'\t\tfloat eyePos[4] = { invViewE._41, invViewE._42, invViewE._43, 1.0f };\n'
'\t\teffect->SetVector(hEyeP, (const D3DXVECTOR4*)eyePos);\n'
'\t}\n').encode()

d = d.replace(anchor, inject)
open(p, 'wb').write(d)
print('EyePosition via matrices-binder installed')

# FX: restore param-based ro (remove the literal override) so the new binding is what's tested
for t in [r'D:\!!!!!!!QWCSB\!!!!!!!QWCSB\Shaders\RA3\w3x_pointlight.fx',
          r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\Shaders\RA3\w3x_pointlight.fx']:
    dd = open(t,'rb').read()
    old_lit = b'    ro = float3(1200.0, 400.0, 320.0) ;   // DEBUG LITERAL: bypass param plumbing entirely\n'
    assert dd.count(old_lit) == 1, t
    open(t,'wb').write(dd.replace(old_lit, b''))
    print('literal removed:', t)
