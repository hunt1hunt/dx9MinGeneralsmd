import sys, bisect, re

MAP = r"E:/Source/repos/MinGeneralsfreebuild2ok/GeneralsMD/Run/RTS.map"

symbols = []
hexre = re.compile(r'^[0-9A-Fa-f]{8}$')
for line in open(MAP, errors='replace'):
    parts = line.split()
    if not parts or not parts[0].startswith('0001:'):
        continue
    # find the VA token (8 hex digits) after the symbol name
    for p in parts[2:]:
        if hexre.match(p):
            va = int(p, 16)
            name = ' '.join(parts[1:parts.index(p)])
            symbols.append((va, name, parts[-1]))
            break
symbols.sort()
addrs = [s[0] for s in symbols]
print("symbols:", len(symbols))

for t in [int(x, 16) for x in sys.argv[1:]]:
    i = bisect.bisect_right(addrs, t) - 1
    if 0 <= i < len(symbols):
        va, name, obj = symbols[i]
        print(f"0x{t:08X} -> {name} +0x{t-va:X}  [{obj}]")
    else:
        print(f"0x{t:08X} -> ?")
