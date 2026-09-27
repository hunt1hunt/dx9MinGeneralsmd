#!/usr/bin/env python3
# FAN-2.0 native-compat repro: extract the PLAIN ps_2_a terrain twin source
# (TERRAIN_POINTLIGHT_HLSL + base variant src) exactly as the game assembles
# it, then compile with fxc ps_2_a to reproduce / inspect the runtime
# "error X5589: Invalid const register num: 32. Max allowed is 31."
import re, subprocess, sys, os

CPP = r"E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\W3DShaderManager.cpp"
FXC = r"C:\Users\Administrator\AppData\Local\Temp\fxc9.exe"

text = open(CPP, "r", encoding="gbk", errors="replace").read()
QRE = re.compile(r'"((?:[^"\\]|\\.)*)"')
TERM = re.compile(r'\n\t[^\n]*";\n')

def unesc(s):
    return s.replace("\\n", "\n").replace("\\t", "\t").replace('\\"', '"').replace("\\\\", "\\")

di = text.find("static const char* TERRAIN_POINTLIGHT_HLSL =")
assert di != -1
mj = TERM.search(text, di)
pointlight = unesc("".join(QRE.findall(text[di:mj.end() - 1])))

mi = text.find('m_dwPBRPixelShader30, "terrain_pbr_nm_30"')
assert mi != -1
si = text.rfind("const char* src =", 0, mi)
assert si != -1
mj2 = TERM.search(text, si)
src = unesc("".join(QRE.findall(text[si:si + mj2.end() - 1])))

full = pointlight + src
out = os.path.join(os.environ.get("TEMP", "/tmp"), "plain_twin.hlsl")
open(out, "w").write(full)
lines = full.split("\n")
print("assembled lines:", len(lines))
for n in range(63, 69):
    if n <= len(lines):
        print("L%d: %s" % (n, lines[n - 1][:110]))
regs = sorted(set(int(m) for m in re.findall(r"register\(c(\d+)\)", full)))
print("declared c-registers:", regs)

r = subprocess.run([FXC, "/nologo", "/T", "ps_2_a", "/E", "main", "/Fo", out + ".fxo", out],
                   capture_output=True, text=True)
print("fxc ps_2_a exit:", r.returncode)
print((r.stdout or "") + (r.stderr or "")[:1500])
