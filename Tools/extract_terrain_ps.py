import os

SRC = "GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DShaderManager.cpp"
raw = open(SRC, 'rb').read()
lines = raw.split(b'\n')

BS = 0x5C

def cstrings_bytes(seg):
    out = []
    i, n = 0, len(seg)
    in_str = False
    cur = None
    while i < n:
        c = seg[i]
        if in_str:
            if c == BS and i + 1 < n:
                nxt = seg[i+1]
                if nxt == ord('n'):
                    cur.append(b'\n')
                elif nxt == ord('"'):
                    cur.append(b'"')
                elif nxt == BS:
                    cur.append(bytes([BS]))
                else:
                    cur.append(bytes([c, nxt]))
                i += 2
                continue
            if c == ord('"'):
                in_str = False
                out.append(b''.join(cur))
                cur = None
                i += 1
                continue
            cur.append(bytes([c]))
        else:
            if c == ord('"'):
                in_str = True
                cur = []
            elif c == ord('/') and i + 1 < n and seg[i+1] == ord('/'):
                j = seg.find(b'\n', i)
                i = n if j < 0 else j
                continue
        i += 1
    return b''.join(out)

text = raw
start = text.index(b'static const char* TERRAIN_POINTLIGHT_HLSL')
start = text.index(b'=', start) + 1
lex = text[start:start+20000]
i, n = 0, len(lex)
in_str = False
end_at = n
while i < n:
    c = lex[i]
    if in_str:
        if c == BS:
            i += 2; continue
        if c == ord('"'):
            in_str = False
    else:
        if c == ord('"'):
            in_str = True
        elif c == ord(';'):
            end_at = i
            break
    i += 1
pl = cstrings_bytes(lex[:end_at])

spans = {"base": (2366, 2498), "noise1": (2592, 2703), "noise2": (2723, 2830), "noise12": (2850, 2964)}
outdir = "GeneralsMD/Build/ps_src"
os.makedirs(outdir, exist_ok=True)
for name, (a, b) in spans.items():
    src = cstrings_bytes(b'\n'.join(lines[a-1:b-1]))
    open(os.path.join(outdir, name + "_orig.hlsl"), 'wb').write(pl + src)
    sw = src.replace(b'float2 tex1 : TEXCOORD1,', b'float2 tex1 : TEXCOORD1, float2 tex4 : TEXCOORD4,')
    sw = sw.replace(b'float2 p = tex2D(s5, tex0).rg * 2.0 - 1.0;', b'float2 p = tex2D(s5, tex4).rg * 2.0 - 1.0;')
    sw = sw.replace(b'sampler s4 : register(s4);', b'sampler s4 : register(s4);\nsampler s6 : register(s6);')
    sw = sw.replace(b'float detail = tex2D(s4, tex0 * 8.0).r;', b'float detail = tex2D(s6, tex0 * 8.0).r;')
    assert b'tex2D(s5, tex4)' in sw, name + ": s5 swap failed"
    assert b'tex2D(s6, tex0 * 8.0)' in sw, name + ": detail swap failed"
    assert b'sampler s6' in sw, name + ": s6 decl failed"
    assert b'TEXCOORD4' in sw, name + ": input decl failed"
    open(os.path.join(outdir, name + "_swap.hlsl"), 'wb').write(sw and pl + sw)
    print(name, "ok", len(src), "bytes")
