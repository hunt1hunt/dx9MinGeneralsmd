BS = chr(92)
p = r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\W3XEffectManager.cpp'
d = open(p, 'rb').read()

anchor = ('DEBUG_LOG(("[W3X_P3] GetEffect(\'%s\') loaded OK, cache slot %d' + BS + 'n", fxPath, idx));').encode()
assert d.count(anchor) == 1, d.count(anchor)

NL = BS + 'n'   # literal backslash-n for C strings
lines = [
 '',
 '\t// TEMP INTROSPECTION (remove): one-shot dump of EVERY parameter of the',
 '\t// point-light effect - names, types, element counts.',
 '\tif (strstr(fxPath, "pointlight") != NULL) {',
 '\t\tstatic bool s_plDumped = false;',
 '\t\tif (!s_plDumped) {',
 '\t\t\ts_plDumped = true;',
 '\t\t\tPLightDiag("=== POINTLIGHT EFFECT PARAM DUMP ===' + NL + '");',
 '\t\t\tfor (UINT pi = 0; ; pi++) {',
 '\t\t\t\tD3DXHANDLE hp = effect->GetParameter(NULL, pi);',
 '\t\t\t\tif (!hp) break;',
 '\t\t\t\tD3DXPARAMETER_DESC pd;',
 '\t\t\t\tZeroMemory(&pd, sizeof(pd));',
 '\t\t\t\tif (FAILED(effect->GetParameterDesc(hp, &pd))) continue;',
 '\t\t\t\tPLightDiag("  param[%u] name=%s type=%d class=%d elems=%u' + NL + '",',
 '\t\t\t\t\tpi, pd.Name ? pd.Name : "(null)", (int)pd.Type, (int)pd.Class, pd.Elements);',
 '\t\t\t}',
 '\t\t\tD3DXHANDLE hV = effect->GetParameterByName(NULL, "View");',
 '\t\t\tD3DXHANDLE hE = effect->GetParameterByName(NULL, "EyePosition");',
 '\t\t\tD3DXHANDLE hT = effect->GetParameterByName(NULL, "Time");',
 '\t\t\tPLightDiag("  byName: View=%p EyePosition=%p Time=%p' + NL + '", (void*)hV, (void*)hE, (void*)hT);',
 '\t\t}',
 '\t}',
]
inject = '\r\n'.join(l.encode() for l in lines)

d = d.replace(anchor, anchor + b'\r\n' + inject)
open(p, 'wb').write(d)
print('introspection dump installed')
