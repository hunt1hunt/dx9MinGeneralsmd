import io
p = r"E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\W3DShaderManager.cpp"
lines = io.open(p, encoding='gbk', errors='surrogateescape', newline='').readlines()
# remove ALL previously inserted broken viz lines (they contain a real newline pattern: line ends with .r; + newline + quote-line)
out = []
skip_next_quote = 0
i = 0
while i < len(lines):
    l = lines[i]
    if 'if (m22 > 0.5) return tex2D' in l or 'if (m23 > 0.5) return saturate' in l:
        # broken line: ends with ".r;" then REAL newline then a lone quote line? inspect: our broken insert = one line containing real \n before closing quote?
        # The insert was: '\t...\t"    if (m22...) return tex2D(s4, suv).r;' + BSN + '\n' where BSN had a REAL newline.
        # When split by readlines, it became: LINE_A = '...\t"    if (m22...) return tex2D(s4, suv).r;\n'  and LINE_B = '"\n'
        # So current file has the code line (fine, but unterminated string) followed by a lone quote line.
        # We drop LINE_A and if next line is a lone quote, drop it too.
        if i + 1 < len(lines) and lines[i+1].strip() == '"':
            i += 2
        else:
            i += 1
        continue
    out.append(l)
    i += 1
lines = out
# now insert CORRECT viz lines after each m23 line, using explicit chr() to avoid any escaping ambiguity
BS = chr(92)          # backslash
Q = '"'
NL = chr(10)
lit_n = BS + 'n'      # literal backslash-n (2 chars)
viz1 = '\t\t\t\t' + Q + '    if (m22 > 0.5) return tex2D(s4, suv).r;' + lit_n + Q + NL
viz2 = '\t\t\t\t' + Q + '    if (m23 > 0.5) return saturate((sd - tex2D(s4, suv).r) * 8.0 + 0.5);' + lit_n + Q + NL
m23idx = [i for i, l in enumerate(lines) if 'float m23 = step(22.5' in l]
assert len(m23idx) == 4, m23idx
for i in reversed(m23idx):
    lines.insert(i+1, viz1)
    lines.insert(i+2, viz2)
io.open(p, 'w', encoding='gbk', errors='surrogateescape', newline='').writelines(lines)
# verify: show one inserted line raw
for i, l in enumerate(lines):
    if 'if (m22 > 0.5)' in l:
        print('sample line repr:', repr(l))
        break
print('viz lines fixed, count =', sum(1 for l in lines if 'if (m22 > 0.5)' in l))
