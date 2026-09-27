#!/usr/bin/env python3
"""Hides a building in a WotLK ADT map tile by moving its placement deep underground.

Removing a MODF entry would shift the chunk indices every MCNK's MCRF refers to, so the
placement is kept and only its height (and bounding box) is lowered. The client still
lists the building but never draws it anywhere near the player. Its doodad set moves
with it.

    adt_sink_wmo.py Kalimdor_1_1.adt Kalimdor_1_1.cleared.adt            # guild house
    adt_sink_wmo.py IN.adt OUT.adt --wmo guildhousea.wmo --depth 2000
"""

import argparse
import struct
import sys

MODF_ENTRY = struct.Struct("<II3f3f3f3fHHHH")  # 64 bytes; vectors are x, height, z


def chunks(data):
    p = 0
    while p + 8 <= len(data):
        magic = data[p:p + 4][::-1].decode("ascii", "replace")  # stored reversed ("REVM" -> MVER)
        size = struct.unpack_from("<I", data, p + 4)[0]
        yield magic, p + 8, size
        p += 8 + size
    if p != len(data):
        raise ValueError("trailing bytes after last chunk (%d of %d)" % (p, len(data)))


def sink(data, wmo_name, depth):
    data = bytearray(data)
    found = {}
    for magic, start, size in chunks(bytes(data)):
        if magic in ("MWMO", "MWID", "MODF") and magic not in found:
            found[magic] = (start, size)
    for needed in ("MWMO", "MWID", "MODF"):
        if needed not in found:
            raise ValueError("tile has no %s chunk" % needed)

    mwmo_start, mwmo_size = found["MWMO"]
    names = bytes(data[mwmo_start:mwmo_start + mwmo_size])
    mwid_start, mwid_size = found["MWID"]
    offsets = struct.unpack_from("<%dI" % (mwid_size // 4), data, mwid_start)

    def wmo_path(name_id):
        off = offsets[name_id]
        return names[off:names.index(b"\x00", off)].decode("ascii", "replace")

    modf_start, modf_size = found["MODF"]
    changed = []
    for i in range(modf_size // MODF_ENTRY.size):
        at = modf_start + i * MODF_ENTRY.size
        entry = list(MODF_ENTRY.unpack_from(data, at))
        path = wmo_path(entry[0])
        if not path.lower().replace("\\", "/").endswith("/" + wmo_name.lower()) and path.lower() != wmo_name.lower():
            continue
        entry[3] -= depth    # position height
        entry[9] -= depth    # bounding box lower height
        entry[12] -= depth   # bounding box upper height
        MODF_ENTRY.pack_into(data, at, *entry)
        changed.append((i, entry[1], path))
    return bytes(data), changed


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--wmo", default="guildhousea.wmo", help="WMO file name to hide (case-insensitive)")
    ap.add_argument("--depth", type=float, default=2000.0, help="how far to lower it, in yards")
    args = ap.parse_args()

    original = open(args.input, "rb").read()
    patched, changed = sink(original, args.wmo, args.depth)
    if not changed:
        sys.exit("no placement of %s in %s" % (args.wmo, args.input))
    assert len(patched) == len(original)
    open(args.output, "wb").write(patched)
    for index, unique_id, path in changed:
        print("sank MODF #%d (unique id %d, %s) by %.0f yd" % (index, unique_id, path, args.depth))


if __name__ == "__main__":
    main()
