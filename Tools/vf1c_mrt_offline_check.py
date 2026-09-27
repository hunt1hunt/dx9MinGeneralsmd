#!/usr/bin/env python3
# VF-1c offline HLSL validation: replicates BuildTerrainMRTSrc() string surgery
# on the four terrain variant sources extracted READ-ONLY from
# W3DShaderManager.cpp, assembles TERRAIN_POINTLIGHT_HLSL + surgered src, and
# compiles each with fxc (ps_3_0). Validates anchors + HLSL syntax without
# running the game. Writes only .hlsl/.fxo files into %TEMP%.
import re, subprocess, sys, os, tempfile

CPP = r"E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\W3DShaderManager.cpp"
FXC = r"C:\Program Files (x86)\Microsoft DirectX SDK (February 2010)\Utilities\bin\x86\fxc.exe"

text = open(CPP, "r", encoding="gbk", errors="replace").read()

def unescape(s):
    return s.replace("\\n", "\n").replace("\\t", "\t").replace('\\"', '"').replace("\\\\", "\\")

TERM = re.compile(r'\n\t[^\n]*";\n')  # literal-block terminator line: tab + "..." + ;

def extract(start_idx, end_idx):
    return unescape("".join(re.findall(r'"((?:[^"\\]|\\.)*)"', text[start_idx:end_idx])))

di = text.find("static const char* TERRAIN_POINTLIGHT_HLSL =")
assert di != -1
mj = TERM.search(text, di)
pointlight = extract(di, mj.end() - 1)

PROLOG = (
    "float4x4 camVP : register(c32);\n"
    "float2 octEncode(float3 n) {\n"
    "    n /= abs(n.x) + abs(n.y) + abs(n.z);\n"
    "    float2 p = n.xy;\n"
    "    p = (n.z >= 0) ? p : (1 - abs(p.yx)) * (2 * step(0, p) - 1);\n"
    "    return p * 0.5 + 0.5;\n"
    "}\n"
    "struct MRT_OUT { float4 c0 : COLOR0; float4 c1 : COLOR1; float4 c2 : COLOR2; };\n"
    "MRT_OUT main("
)

def surgery(src, color):
    p1 = src.find("float4 main(")
    assert p1 != -1 and src.find("float4 main(", p1 + 1) == -1, "anchor1 not unique"
    src = src[:p1] + PROLOG + src[p1 + 12:]
    p2 = src.find(") : COLOR\n")
    assert p2 != -1 and src.find(") : COLOR\n", p2 + 1) == -1, "anchor2 not unique"
    src = src[:p2] + ")\n" + src[p2 + 10:]
    tailfind = "    return float4(" + color + ", 1.0);\n"
    p3 = src.find(tailfind)
    assert p3 != -1 and src.find(tailfind, p3 + 1) == -1, "anchor3 not unique"
    tail = (
        "    MRT_OUT o;\n"
        "    float4 clipP = mul(float4(worldPos, 1.0), camVP);\n"
        "    float ndcZ = clipP.z / clipP.w;\n"
        "    o.c0 = float4(" + color + ", 1.0);\n"
        "    o.c1 = float4(octEncode(geoN), c2.y, 0.0);\n"
        "    o.c2 = float4(ndcZ, ndcZ * ndcZ, 0.0, 0.0);\n"
        "    return o;\n"
    )
    return src[:p3] + tail + src[p3 + len(tailfind):]

variants = [
    ("terrain_pbr_nm_mrt",       'm_dwPBRPixelShader30, "terrain_pbr_nm_30"',       "result"),
    ("terrain_pbr_nm_noise1_mrt", 'm_dwPBRNoise1PixelShader30, "terrain_pbr_nm_noise1_30"', "lit"),
    ("terrain_pbr_nm_noise2_mrt", 'm_dwPBRNoise2PixelShader30, "terrain_pbr_nm_noise2_30"', "lit"),
    ("terrain_pbr_nm_noise12_mrt",'m_dwPBRNoise12PixelShader30, "terrain_pbr_nm_noise12_30"', "lit"),
]

outdir = os.path.join(tempfile.gettempdir(), "vf1c_check")
os.makedirs(outdir, exist_ok=True)

# FAN-2.0 native repro: the PLAIN ps_2_a base twin (no surgery) - reproduces
# the runtime "X5589 invalid const register num: 32" from the game log.
mi0 = text.find('m_dwPBRPixelShader30, "terrain_pbr_nm_30"')
assert mi0 != -1
si0 = text.rfind("const char* src =", 0, mi0)
assert si0 != -1
mj0 = TERM.search(text, si0)
plain = pointlight + extract(si0, mj0.end() - 1)
plainPath = os.path.join(outdir, "plain_twin.hlx")
open(plainPath, "w").write(plain)
r0 = subprocess.run([FXC, "/nologo", "/T", "ps_2_a", "/E", "main", "/Fo", plainPath + ".fxo", plainPath],
                    capture_output=True, text=True)
print("%-28s %s" % ("plain_twin ps_2_a", "OK" if r0.returncode == 0 else "FAIL"))
if r0.returncode != 0:
    print(r0.stdout, r0.stderr)
    fail += 1

fail = 0
for name, marker, color in variants:
    mi = text.find(marker)
    assert mi != -1, "marker not found: " + marker
    si = text.rfind("const char* src =", 0, mi)
    assert si != -1
    mj2 = TERM.search(text, si, mi)
    src = extract(si, mj2.end() - 1)
    full = pointlight + surgery(src, color)
    hlsl = os.path.join(outdir, name + ".hlsl")
    open(hlsl, "w").write(full)
    r = subprocess.run([FXC, "/nologo", "/T", "ps_3_0", "/E", "main", "/Fo", hlsl + ".fxo", hlsl],
                       capture_output=True, text=True)
    ok = r.returncode == 0
    print("%-28s %s" % (name, "OK" if ok else "FAIL"))
    if not ok:
        fail += 1
        print(r.stdout, r.stderr)
print("RESULT:", "ALL_OK" if fail == 0 else "%d FAILURES" % fail)
sys.exit(1 if fail else 0)
