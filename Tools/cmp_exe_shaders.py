import re, sys

def strings(path, minlen=8):
    data = open(path, 'rb').read()
    out = []
    for m in re.finditer(rb'[\x20-\x7e]{%d,}' % minlen, data):
        out.append((m.start(), m.group().decode('ascii')))
    return out

def shader_fragments(path):
    frags = []
    for off, s in strings(path, 6):
        if ('float ' in s or 'sampler ' in s or 'tex2D' in s or 'return' in s
                or 'register(' in s or s.startswith('    ') or 'main(' in s
                or '{\\n' in s or '}\\n' in s or 'lerp' in s or 'step(' in s):
            frags.append((off, s))
    return frags

a = shader_fragments(r"D:/!!!!!!!QWCSB/!!!!!!!QWCSB/RTS.exe.bak_20260921_224411")
b = shader_fragments(r"E:/Source/repos/MinGeneralsfreebuild2ok/GeneralsMD/Run/RTS.exe")
print("working:", len(a), " new:", len(b))

def dump(frags, name):
    with open(name, 'w') as f:
        for off, s in frags:
            f.write("%08x %s\n" % (off, s))

dump(a, r"E:/Source/repos/MinGeneralsfreebuild2ok/Build/shader_frag_working.txt")
dump(b, r"E:/Source/repos/MinGeneralsfreebuild2ok/Build/shader_frag_new.txt")
print("dumped")
