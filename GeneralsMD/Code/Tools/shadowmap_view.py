# -*- coding: utf-8 -*-
"""shadowmap_view.py — convert E:\shadowmap_real.ppm (grayscale depth bytes) to
a viewable grayscale PNG with contrast boost, with decode-back self-check."""
import zlib, struct

SRC = r"E:\shadowmap_real.ppm"
DST = r"E:\shadow_real_view.png"

f = open(SRC, "rb")
assert f.readline().strip() == b"P6"
w, h = map(int, f.readline().split())
f.readline()
d = f.read()
f.close()

S = 4
W, H = w // S, h // S
px = bytearray(W * H)
for y in range(H):
    base_y = y * S * w
    for x in range(W):
        acc = 0
        for dy in range(S):
            row = base_y + dy * w
            for dx in range(S):
                acc += d[(row + x * S + dx) * 3]
        v = acc // (S * S)
        v = min(255, max(0, int((v - 140) * 4 + 140)))
        px[y * W + x] = v

print("gray stats: min %d max %d" % (min(px), max(px)))

def chunk(t, p):
    c = t + p
    return struct.pack(">I", len(p)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

raw = b"".join(b"\x00" + bytes(px[y * W:(y + 1) * W]) for y in range(H))
png = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 0, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(raw, 9))
       + chunk(b"IEND", b""))
open(DST, "wb").write(png)

# decode-back self-check
off = 8
idat = b""
while off < len(png):
    ln = struct.unpack(">I", png[off:off + 4])[0]
    typ = png[off + 4:off + 8]
    if typ == b"IDAT":
        idat += png[off + 8:off + 8 + ln]
    off += 12 + ln
dec = zlib.decompress(idat)
sel = dec[1::W]  # first byte of each scanline row
print("decode-back row-start samples: min %d max %d" % (min(sel), max(sel)))
print("wrote", DST, W, "x", H)
