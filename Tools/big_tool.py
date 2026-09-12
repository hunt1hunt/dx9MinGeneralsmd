import struct, sys, os

def read_big(path):
	f = open(path, 'rb')
	magic = f.read(4)
	assert magic == b'BIGF', 'not a BIGF: %r' % magic
	# BIGF: uint32 archive size (BE), uint32 entry count (BE), uint32 header size (BE)
	_, nentries, _ = struct.unpack('>III', f.read(12))
	entries = []
	for i in range(nentries):
		off, size = struct.unpack('>II', f.read(8))
		# variable-length null-terminated name
		chars = []
		while True:
			c = f.read(1)
			if not c or c == b'\0':
				break
			chars.append(c)
		name = b''.join(chars).decode('latin-1')
		entries.append((name, off, size))
	return f, entries

def list_entries(path, filt=None):
	f, entries = read_big(path)
	for name, off, size in entries:
		if filt is None or filt.lower() in name.lower():
			print('%10d  %s' % (size, name))
	f.close()

def extract(path, want, outdir):
	f, entries = read_big(path)
	if not os.path.isdir(outdir):
		os.makedirs(outdir)
	got = 0
	for name, off, size in entries:
		if want.lower() in name.lower():
			f.seek(off)
			data = f.read(size)
			out = os.path.join(outdir, os.path.basename(name))
			open(out, 'wb').write(data)
			print('extracted %s (%d bytes)' % (out, size))
			got += 1
	f.close()
	print('%d file(s)' % got)

if __name__ == '__main__':
	cmd = sys.argv[1]
	if cmd == 'list':
		list_entries(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
	elif cmd == 'x':
		extract(sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else '.')
