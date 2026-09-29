#!/usr/bin/env python3
"""Adds the housing ghosts to a 3.3.5a client's CreatureModelData.dbc and CreatureDisplayInfo.dbc:
the see-through copies of pieces that follow a player placing or moving them. Each piece's
model gets a creature model and a see-through display; figurines and the mannequin, already
creatures, get a see-through copy of their display. The rows come from client_items.tsv
(written by tools/content/build_content.py); rows the files already have with the same ids
are replaced.

    dbc_add_ghosts.py client_items.tsv CreatureModelData.dbc CreatureDisplayInfo.dbc OUT_DIR
"""

import argparse
import os
import struct
import sys

from dbc_add_items import read, write

MODEL_FIELDS, MODEL_SIZE = 28, 112
DISPLAY_FIELDS, DISPLAY_SIZE = 16, 64


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tsv")
    ap.add_argument("model_dbc")
    ap.add_argument("display_dbc")
    ap.add_argument("out_dir")
    args = ap.parse_args()

    alpha = 150
    models, copies = {}, {}
    for line in open(args.tsv, encoding="utf-8"):
        if not line.strip() or line.startswith("#"):
            continue
        parts = line.rstrip("\n").split("\t")
        if parts[0] == "alpha":
            alpha = max(0, min(255, int(parts[1])))
        elif parts[0] == "ghostmodel":
            models[int(parts[1])] = (parts[2], [float(v) for v in parts[3].split()])
        elif parts[0] == "ghostcopy":
            copies[int(parts[1])] = int(parts[2])

    # CreatureModelData.dbc: the model file and its bounds; nothing else a ghost needs (no
    # blood, no footsteps, no collision).
    rows, strings = read(args.model_dbc, MODEL_FIELDS, MODEL_SIZE)
    rows = [row for row in rows if struct.unpack_from("<I", row)[0] not in models]
    for ghost, (path, box) in models.items():
        row = bytearray(MODEL_SIZE)
        struct.pack_into("<I", row, 0, ghost)
        struct.pack_into("<I", row, 8, len(strings))
        strings += path.encode("utf-8") + b"\0"
        struct.pack_into("<f", row, 16, 1.0)       # model scale
        struct.pack_into("<i", row, 20, -1)        # no blood
        struct.pack_into("<6f", row, 68, *box)     # its bounds
        struct.pack_into("<2f", row, 92, 1.0, 1.0)  # effect scales
        rows.append(row)
    write(os.path.join(args.out_dir, "CreatureModelData.dbc"), MODEL_FIELDS, MODEL_SIZE, rows, strings)

    # CreatureDisplayInfo.dbc: a see-through display of each (the sixth field is how solid).
    rows, strings = read(args.display_dbc, DISPLAY_FIELDS, DISPLAY_SIZE)
    known = {struct.unpack_from("<I", row)[0]: row for row in rows}
    rows = [row for row in rows if struct.unpack_from("<I", row)[0] not in models and struct.unpack_from("<I", row)[0] not in copies]
    for ghost in models:
        row = bytearray(DISPLAY_SIZE)
        struct.pack_into("<2I", row, 0, ghost, ghost)
        struct.pack_into("<f", row, 16, 1.0)
        struct.pack_into("<I", row, 20, alpha)
        rows.append(row)
    missing = []
    for ghost, source in copies.items():
        if source not in known:
            missing.append(source)
            continue
        row = bytearray(known[source])
        struct.pack_into("<I", row, 0, ghost)
        struct.pack_into("<I", row, 20, alpha)
        rows.append(row)
    write(os.path.join(args.out_dir, "CreatureDisplayInfo.dbc"), DISPLAY_FIELDS, DISPLAY_SIZE, rows, strings)

    if missing:
        print("warning: the client has no creature display %s: those figurines have no ghost" % ", ".join(map(str, missing[:5])),
              file=sys.stderr)
    print("added %d ghost models and %d ghost copies" % (len(models), len(copies) - len(missing)))


if __name__ == "__main__":
    main()
