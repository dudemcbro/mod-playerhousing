#!/usr/bin/env python3
"""Adds the housing items to a 3.3.5a client's Item.dbc and ItemDisplayInfo.dbc, so bags show
their icons instead of question marks. The rows come from client_items.tsv (written by
tools/content/build_content.py); rows the files already have with the same ids are replaced.

    dbc_add_items.py client_items.tsv Item.dbc ItemDisplayInfo.dbc OUT_DIR
"""

import argparse
import os
import struct
import sys

HEADER = struct.Struct("<4s4I")


def read(path, fields, size):
    data = open(path, "rb").read()
    magic, records, got_fields, got_size, _ = HEADER.unpack_from(data)
    if magic != b"WDBC" or got_fields != fields or got_size != size:
        sys.exit("%s is not a 3.3.5a file (%d fields of %d bytes expected)" % (path, fields, size))
    rows = [bytearray(data[HEADER.size + i * size:HEADER.size + (i + 1) * size]) for i in range(records)]
    return rows, bytearray(data[HEADER.size + records * size:])


def write(path, fields, size, rows, strings):
    rows.sort(key=lambda row: struct.unpack_from("<I", row)[0])
    with open(path, "wb") as f:
        f.write(HEADER.pack(b"WDBC", len(rows), fields, size, len(strings)))
        for row in rows:
            f.write(row)
        f.write(strings)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tsv")
    ap.add_argument("item_dbc")
    ap.add_argument("display_dbc")
    ap.add_argument("out_dir")
    args = ap.parse_args()

    items, displays = {}, {}
    for line in open(args.tsv, encoding="utf-8"):
        if not line.strip() or line.startswith("#"):
            continue
        # Other kinds of rows (the ghosts) are dbc_add_ghosts.py's.
        parts = line.rstrip("\n").split("\t")
        if parts[0] == "item":
            items[int(parts[1])] = int(parts[2])
        elif parts[0] == "display":
            displays[int(parts[1])] = parts[2]

    # Item.dbc: id, class, subclass, sound override subclass, material, display, inventory type, sheath
    rows, strings = read(args.item_dbc, 8, 32)
    rows = [row for row in rows if struct.unpack_from("<I", row)[0] not in items]
    rows += [bytearray(struct.pack("<8i", entry, 15, 0, -1, 0, display, 0, 0)) for entry, display in items.items()]
    write(os.path.join(args.out_dir, "Item.dbc"), 8, 32, rows, strings)

    # ItemDisplayInfo.dbc: 25 fields; the sixth is the inventory icon. Everything else stays
    # empty (offset 0 is the empty string), since these items are never worn.
    rows, strings = read(args.display_dbc, 25, 100)
    known = {struct.unpack_from("<I", row)[0] for row in rows}
    missing = sorted({display for display in items.values() if display not in known and display not in displays})
    if missing:
        sys.exit("the client has no item display %s; rebuild with this client's data" % ", ".join(map(str, missing[:5])))
    rows = [row for row in rows if struct.unpack_from("<I", row)[0] not in displays]
    for display, icon in displays.items():
        row = bytearray(100)
        struct.pack_into("<I", row, 0, display)
        struct.pack_into("<I", row, 20, len(strings))
        strings += icon.encode("utf-8") + b"\0"
        rows.append(row)
    write(os.path.join(args.out_dir, "ItemDisplayInfo.dbc"), 25, 100, rows, strings)

    print("added %d items and %d icons" % (len(items), len(displays)))


if __name__ == "__main__":
    main()
