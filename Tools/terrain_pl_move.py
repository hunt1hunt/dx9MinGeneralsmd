p = r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\W3DShaderManager.cpp'
lines = open(p, 'rb').read().decode('utf-8').split('\n')

# locate the misplaced block: from the '// 2026-09-12 ②B: forward point lights' comment
# through its closing '\t\t\t}' line (1-based line numbers from grep: ~6918-6947)
start = end = -1
for idx, ln in enumerate(lines):
    if '2026-09-12 ②B: forward point lights - the same W3X registry' in ln:
        start = idx
        break
assert start >= 0, 'block start not found'
# walk to the block's closing brace: a line that is exactly '\t\t\t}'
end = start
while lines[end].strip() != '}' or '\t\t\t}' != lines[end]:
    end += 1
    if end > start + 60: raise SystemExit('block end not found')
block = lines[start:end+1]
del lines[start:end+1]

# find the terrain set() c8 upload line (unique 'sdbgT')
target = -1
for idx, ln in enumerate(lines):
    if 'SetPixelShaderConstantF(8, sdbgT, 1);' in ln:
        target = idx
        break
assert target >= 0, 'target not found'
lines[target+1:target+1] = block

open(p, 'wb').write('\n'.join(lines).encode('utf-8'))
print('moved block (%d lines) to after line %d' % (len(block), target+1))
