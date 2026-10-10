#!/usr/bin/env python3
"""Adds rows to a 3.3.5a client's GameObjectDisplayInfo.dbc (id, model path, no sounds, its
bounding box). Rows the file already has with the same ids are replaced.

    dbc_add_gameobject_displays.py client_gameobject_displays.tsv GameObjectDisplayInfo.dbc OUT_DIR
"""

import argparse
import os
import struct

FIELDS = 19  # ID, ModelName, Sound[10], GeoBoxMin[3], GeoBoxMax[3], ObjectEffectPackageID


def read_rows(path):
    rows = []
    with open(path) as f:
        for line in f:
            if not line.strip() or line.startswith("#"):
                continue
            parts = line.rstrip("\n").split("\t")
            rows.append((int(parts[0]), parts[1], [float(v) for v in parts[2:8]]))
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tsv")
    ap.add_argument("dbc")
    ap.add_argument("out_dir")
    args = ap.parse_args()

    data = open(args.dbc, "rb").read()
    magic, count, fields, size, string_size = struct.unpack_from("<4s4I", data)
    if magic != b"WDBC" or fields != FIELDS or size != FIELDS * 4:
        raise SystemExit("%s is not a 3.3.5a GameObjectDisplayInfo.dbc" % args.dbc)
    rows = [data[20 + i * size:20 + (i + 1) * size] for i in range(count)]
    strings = bytearray(data[20 + count * size:20 + count * size + string_size])

    added = read_rows(args.tsv)
    ids = {display for display, _, _ in added}
    rows = [row for row in rows if struct.unpack_from("<I", row)[0] not in ids]
    for display, model, box in added:
        offset = len(strings)
        strings += model.encode("ascii") + b"\x00"
        rows.append(struct.pack("<II10I6fI", display, offset, *([0] * 10), *box, 0))
    rows.sort(key=lambda row: struct.unpack_from("<I", row)[0])

    out = struct.pack("<4s4I", b"WDBC", len(rows), FIELDS, size, len(strings)) + b"".join(rows) + bytes(strings)
    os.makedirs(args.out_dir, exist_ok=True)
    with open(os.path.join(args.out_dir, "GameObjectDisplayInfo.dbc"), "wb") as f:
        f.write(out)
    print("GameObjectDisplayInfo.dbc: %d rows (%d added or replaced)" % (len(rows), len(added)))


if __name__ == "__main__":
    main()
