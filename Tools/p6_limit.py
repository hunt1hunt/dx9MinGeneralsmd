import re

p = r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\W3DDeferredRenderer.cpp'
d = open(p, 'rb').read().decode('utf-8')

# toneMap PS head: anchor on the first hdr sample line (regex across the C-string)
pat = re.compile(r'("float4 tmParams : register\(c0\);\\n"\n\t"float4 main\(PS_IN input\) : COLOR \{\\n"\n\t"  float3 hdrColor = tex2D\(hdrSampler, input\.tex0\)\.rgb \* tmParams\.x;\\n")')
m = pat.search(d)
assert m, 'tonemap head regex'
repl = ('"float4 tmParams : register(c0);\\n"\n'
        '\t"// c0.w = P6 soft HDR limiter: values above it are compressed 75% pre-curve\\n"\n'
        '\t"float4 main(PS_IN input) : COLOR {\\n"\n'
        '\t"  float3 hdrColor = tex2D(hdrSampler, input.tex0).rgb * tmParams.x;\\n"\n'
        '\t"  float3 over6 = max(hdrColor - tmParams.w, 0);\\n"\n'
        '\t"  hdrColor -= over6 * 0.75;\\n"')
d = d[:m.start(1)] + repl + d[m.end(1):]

# c0 feed: w = limiter
a2 = 'float tm[4] = { 1.0f, 4.0f, 0.0f, 1.0f };'
assert a2 in d
d = d.replace(a2, 'float tm[4] = { 1.0f, 4.0f, 0.0f, 6.0f };', 1)
a3 = 'tm[2] = (float)TheGlobalData->m_toneMapMode;'
assert a3 in d
d = d.replace(a3, a3 + '\n\t\t\ttm[3] = TheGlobalData->m_hdrLimiter;\t// P6: soft pre-curve clamp', 1)
open(p, 'wb').write(d.encode('utf-8'))

# GlobalData.h
p2 = r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngine\Include\Common\GlobalData.h'
d2 = open(p2, 'rb').read().decode('utf-8')
m2 = re.search(r'^\tInt m_toneMapMode;.*$', d2, re.M)
assert m2
d2 = d2.replace(m2.group(0), m2.group(0) + '\n\tReal m_hdrLimiter;\t\t///< P6: soft pre-tonemap clamp (default 6)', 1)
open(p2, 'wb').write(d2.encode('utf-8'))

# GlobalData.cpp
p3 = r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngine\Source\Common\GlobalData.cpp'
d3 = open(p3, 'rb').read().decode('utf-8')
m3 = re.search(r'^\t\{ "ToneMapMode".*$', d3, re.M)
assert m3
d3 = d3.replace(m3.group(0), m3.group(0) + '\n\t{ "HDRLimiter",\t\t\t\tINI::parseReal,\t\t\t\tNULL,\t\t\toffsetof( GlobalData, m_hdrLimiter ) },', 1)
m4 = re.search(r'^\tm_toneMapMode = 0;.*$', d3, re.M)
assert m4
d3 = d3.replace(m4.group(0), '\tm_toneMapMode = 1;\t\t\t// P2/P6: ACES shoulder default (user-verified)\n\tm_hdrLimiter = 6.0f;\t\t// P6: soft pre-curve clamp', 1)
d3 = d3.replace('\tm_fogStart = 500.0f;\t\t\t// P4', '\tm_fogStart = 200.0f;\t\t\t// P4 (user-tuned)')
d3 = d3.replace('\tm_fogEnd = 2400.0f;\t\t\t\t// P4', '\tm_fogEnd = 900.0f;\t\t\t\t// P4 (user-tuned)')
open(p3, 'wb').write(d3.encode('utf-8'))
print('P6 done')
