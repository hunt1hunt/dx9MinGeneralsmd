#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
big_extract.py -- list / extract files from a SAGE .big archive (BIGF format).

Needed because the Zero Hour install ships most of its INI data inside
INI.big, so an INI file that was never overridden loose (e.g.
Data/INI/Object/ChinaInfantry.ini) has to be pulled out before it can be
shadowed by a loose replacement - the ThingFactory loads the whole
Data\\INI\\Object directory, so a loose file only wins if it carries the same
name AND the complete original content.

Archive layout (all BE except the archive-size field):
    char[4]  "BIGF"
    uint32   total archive size            (little endian)
    uint32   file count                    (big endian)
    uint32   header size, i.e. bytes from offset 16 to the first file's data
    per entry:
        uint32 offset (BE), uint32 size (BE), name NUL-terminated
    NOTE: many references describe an 8-byte alignment pad after the name, but
    the Generals / Zero Hour INI.big examined here has NO pad (entry length is
    exactly 8 + strlen(name) + 1). The reader matches both layouts and picks
    whichever reproduces the archive's header size.

Two front ends are supported (kept merged so older callers keep working):

  # flag style
  python big_extract.py <archive.big> --list [PATTERN]
  python big_extract.py <archive.big> --extract <out_dir> [PATTERN]
    PATTERN  case-insensitive substring match on the archived path.
             Omit to operate on every entry.

  # subcommand style
  python big_extract.py list   <archive.big> [PATTERN]
  python big_extract.py cat    <archive.big> <inner path>          # -> stdout
  python big_extract.py get    <archive.big> <inner path> <outfile>
  python big_extract.py getall <archive.big> <out_dir> [prefix]
    inner paths are case-insensitive, / or \\ both accepted
    (e.g. "Data\\INI\\Upgrade.ini").
"""
import os
import struct
import sys


def read_entries(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"BIGF":
        raise ValueError("not a BIGF archive: %s" % path)
    total, = struct.unpack_from("<I", data, 4)
    count, = struct.unpack_from(">I", data, 8)
    header, = struct.unpack_from(">I", data, 12)

    def parse(padded):
        entries = []
        pos = 16
        for _ in range(count):
            if pos + 8 > len(data):
                return None
            off, size = struct.unpack_from(">II", data, pos)
            pos += 8
            end = data.find(b"\0", pos)
            if end < 0:
                return None
            name = data[pos:end].decode("latin-1")
            pos = end + 1
            if padded:
                entry_len = 8 + len(name) + 1
                pos += (8 - (entry_len % 8)) % 8
            entries.append((name, off, size))
        return entries, pos

    # pick the layout that reproduces the archive's declared header size and
    # keeps every offset inside the file
    best = None
    for padded in (False, True):
        r = parse(padded)
        if not r:
            continue
        entries, endpos = r
        # the declared header size can carry a small tail (the last entry is
        # often padded to an 8-byte boundary); the offset/range sanity check is
        # the real discriminator between the two layouts
        ok = (abs((endpos - 16) - header) <= 16
              and all(0 <= o <= len(data) and 0 < s <= len(data)
                      for _, o, s in entries)
              and all(all(32 <= ord(c) < 127 for c in n) and n for n, _, _ in entries))
        if ok:
            best = (entries, padded)
            break
    if best is None:
        r = parse(False)
        if not r:
            raise ValueError("cannot parse entries in %s" % path)
        best = (r[0], False)
        print("WARN: could not validate against header size %d" % header)
    entries, padded = best
    if padded:
        print("note: archive uses 8-byte padded entry names")
    return data, entries


# --- helpers used by the subcommand front end -------------------------------

def _norm(s):
    # archived names use backslashes; accept either separator
    return s.replace("/", "\\").lower()


def _find(entries, want):
    w = _norm(want)
    for name, off, size in entries:
        if _norm(name) == w:
            return name, off, size
    base = w.rsplit("\\", 1)[-1]
    hits = [(n, o, s) for n, o, s in entries
            if _norm(n).rsplit("\\", 1)[-1] == base]
    return hits[0] if len(hits) == 1 else None


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)

    a0 = sys.argv[1]
    # subcommand style: first arg is a verb, archive is argv[2]
    if a0 in ("list", "cat", "get", "getall"):
        cmd, path, rest = a0, sys.argv[2], sys.argv[3:]
        if not os.path.isfile(path):
            print("ERROR: archive not found: %s" % path)
            sys.exit(1)
        data, entries = read_entries(path)

        if cmd == "list":
            pat = _norm(rest[0]) if rest else ""
            n = 0
            for name, off, size in entries:
                if pat and pat not in _norm(name):
                    continue
                print("  %9d  %s" % (size, name))
                n += 1
            print("  %d match(es)" % n)
            return 0

        if cmd in ("cat", "get"):
            if not rest:
                print("need <inner path>")
                return 2
            hit = _find(entries, rest[0])
            if not hit:
                print("[MISS] %s" % rest[0])
                return 1
            name, off, size = hit
            blob = data[off:off + size]
            if cmd == "cat":
                sys.stdout.write(blob.decode("latin1"))
            else:
                out = rest[1] if len(rest) > 1 else os.path.basename(name)
                d = os.path.dirname(out)
                if d and not os.path.isdir(d):
                    os.makedirs(d)
                with open(out, "wb") as fh:
                    fh.write(blob)
                print("[OK] %s -> %s (%d bytes)" % (name, out, len(blob)))
            return 0

        # getall
        outdir = rest[0] if rest else "."
        prefix = _norm(rest[1]) if len(rest) > 1 else ""
        n = 0
        for name, off, size in entries:
            key = _norm(name)
            if prefix and not key.startswith(prefix):
                continue
            dst = os.path.join(outdir, name.replace("/", os.sep).replace("\\", os.sep))
            d = os.path.dirname(dst)
            if d and not os.path.isdir(d):
                os.makedirs(d)
            with open(dst, "wb") as fh:
                fh.write(data[off:off + size])
            n += 1
        print("[OK] extracted %d files -> %s" % (n, outdir))
        return 0

    # flag style: big_extract.py <archive> --list|--extract ...
    path, mode, rest = a0, sys.argv[2], sys.argv[3:]
    if not os.path.isfile(path):
        print("ERROR: archive not found: %s" % path)
        sys.exit(1)
    data, entries = read_entries(path)
    print("archive %s: %d entry/entries, %d bytes, header %d"
          % (os.path.basename(path), len(entries), len(data), 16))

    if mode == "--list":
        pat = _norm(rest[0]) if rest else ""
        n = 0
        for name, off, size in entries:
            if pat and pat not in _norm(name):
                continue
            print("  %9d  %s" % (size, name))
            n += 1
        print("  %d match(es)" % n)
        return 0

    if mode != "--extract":
        print(__doc__)
        sys.exit(2)

    out_dir = rest[0] if rest else "."
    pat = _norm(rest[1]) if len(rest) > 1 else ""
    n = skipped = 0
    for name, off, size in entries:
        if pat and pat not in _norm(name):
            continue
        rel = name.replace("\\", "/").lstrip("/")
        dst = os.path.join(out_dir, rel)
        if os.path.exists(dst):
            skipped += 1
            continue
        d = os.path.dirname(dst)
        if d and not os.path.isdir(d):
            os.makedirs(d)
        with open(dst, "wb") as f:
            f.write(data[off:off + size])
        n += 1
    print("extracted %d file(s) to %s (%d already present)"
          % (n, os.path.abspath(out_dir), skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main())
