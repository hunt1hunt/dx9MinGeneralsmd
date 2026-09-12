import os, re, shutil

RA3 = r'D:\红警3有关的素材集合\新建文件夹\art\terrain'
GAME = r'D:\!!!!!!!QWCSB\!!!!!!!QWCSB'
MAPLOG = r'E:\Source\repos\MinGeneralsfreebuild2ok\Tools\ra3_terrain_mapping.txt'

ra3_files = [f for f in os.listdir(RA3) if f.lower().endswith('.tga') and '_nrm' not in f.lower()]
# group by category (strip trailing digits)
cats = {}
for f in ra3_files:
	m = re.match(r'^(.*?)(\d*)\.tga$', f)
	cat = m.group(1)
	cats.setdefault(cat, []).append(f)
for c in cats:
	cats[c].sort()

# ZH texture list (from extracted Terrain.ini)
zh = [l.strip() for l in open(r'E:\Source\repos\MinGeneralsfreebuild2ok\Tools\zh_terrain_textures.txt') if l.strip()]

# Category mapping: ZH name pattern keyword -> RA3 category list (order = preference)
def zh_cat(name):
	n = name.lower()
	if 'cliff' in n: return 'cliff'
	if 'rock' in n: return 'rock'
	if 'dirt' in n: return 'dirt'
	if 'grass' in n or n.startswith('tg'): return 'grass'
	if 'sand' in n: return 'sand'
	if 'cobble' in n: return 'cobble'
	if 'conc' in n or 'asph' in n or 'pavement' in n or 'road' in n: return 'concrete'
	if 'snow' in n: return 'snow'
	if 'crop' in n or 'farm' in n: return 'dirt'
	if 'gras' in n: return 'grass'
	return None

RA3_MAP = {
	'cliff': cats.get('tcliff_egypt', []) + cats.get('tcliff_gen', []),
	'rock': cats.get('trock_redzone', []),
	'dirt': cats.get('tdirt_egypt', []) + cats.get('tdirt_redzone', []),
	'grass': cats.get('tgrass', []) + cats.get('tr_grass_', []),
	'sand': cats.get('ttsandyellow', []),
	'cobble': cats.get('tt_concrete_', []),
	'concrete': cats.get('tt_concrete_', []),
	'snow': [],  # no RA3 snow in this pack - keep ZH
}

counters = {}
log = ['# RA3 -> ZH terrain texture mapping (loose override)']
copied = 0
for z in zh:
	c = zh_cat(z)
	if not c or not RA3_MAP.get(c):
		log.append('%-24s KEEP (no RA3 %s)' % (z, c or '?'))
		continue
	pool = RA3_MAP[c]
	i = counters.get(c, 0)
	src = pool[i % len(pool)]
	counters[c] = i + 1
	dst = os.path.join(GAME, z)
	shutil.copyfile(os.path.join(RA3, src), dst)
	log.append('%-24s <- %-24s [%s]' % (z, src, c))
	copied += 1

open(MAPLOG, 'w').write('\n'.join(log))
print('copied %d / %d (mapping log: %s)' % (copied, len(zh), MAPLOG))
