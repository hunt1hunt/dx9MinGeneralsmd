import io
p = r"E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\W3DShaderManager.cpp"
lines = io.open(p, encoding='gbk', errors='surrogateescape', newline='').readlines()
BS = chr(92); Q = '"'; NL = chr(10); lit_n = BS + 'n'
viz1_old = '\t\t\t\t' + Q + '    if (m22 > 0.5) return tex2D(s4, suv).r;' + lit_n + Q + NL
viz2_old = '\t\t\t\t' + Q + '    if (m23 > 0.5) return saturate((sd - tex2D(s4, suv).r) * 8.0 + 0.5);' + lit_n + Q + NL
viz1_new = '\t\t\t\t' + Q + '    float vizS = tex2D(s4, suv).r;' + lit_n + Q + NL
viz2_new = '\t\t\t\t' + Q + '    float vizC = saturate((sd - vizS) * 8.0 + 0.5);' + lit_n + Q + NL
viz3_new = '\t\t\t\t' + Q + '    return lerp(lerp(base, vizS, m22), vizC, m23);' + lit_n + Q + NL
n1 = sum(1 for l in lines if l == viz1_old)
n2 = sum(1 for l in lines if l == viz2_old)
print('found branchy viz lines:', n1, n2)
assert n1 == 4 and n2 == 4
out = []
for l in lines:
    if l == viz1_old:
        out.append(viz1_new)
    elif l == viz2_old:
        out.append(viz2_new)
        out.append(viz3_new)
    else:
        out.append(l)
io.open(p, 'w', encoding='gbk', errors='surrogateescape', newline='').writelines(out)
print('branchless viz installed (sample always executes, lerp selects)')
