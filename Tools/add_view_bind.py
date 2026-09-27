p = r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\W3XRenderObj.cpp'
d = open(p, 'rb').read()

anchor = b'\tD3DXHANDLE hWVP = effect->GetParameterByName(NULL, "WorldViewProj");\n\tif (hWVP) effect->SetMatrix(hWVP, (const D3DXMATRIX*)&wvp);\n'
assert d.count(anchor) == 1, d.count(anchor)

inject = anchor + (
'\t// 2026-09-12 VIEW VIA THE PROVEN BINDER: BindEngineConstants\' View/Time/EyePosition\n'
'\t// sets verifiably never reach the w3x_pointlight effect (quadrant-probe test:\n'
'\t// View = identity in its VS while World/VP bound HERE work). Bind View in this\n'
'\t// same proven call site so view-space lighting shaders get a real matrix.\n'
'\tD3DXHANDLE hViewP = effect->GetParameterByName(NULL, "View");\n'
'\tif (hViewP) {\n'
'\t\tMatrix4x4 curViewB;\n'
'\t\tDX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curViewB);\n'
'\t\teffect->SetMatrix(hViewP, (const D3DXMATRIX*)&curViewB);\n'
'\t}\n').encode()

d = d.replace(anchor, inject)
open(p, 'wb').write(d)
print('View via matrices-binder installed')
