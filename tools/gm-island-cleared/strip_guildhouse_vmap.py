#!/usr/bin/env python3
"""Removes the GM Island guild house from the server's collision data (vmaps).

The house (Guildhousea.wmo) and the props built into it are listed as model spawns in
Kalimdor's vmap tile 001_01_01.vmtile. Dropping those entries makes the server treat the
spot as open ground; the core skips tree nodes whose spawn was never loaded.

Keep this in step with the client patch (make_client_patch.sh): a server without the house
and a client with it (or the other way round) disagree about walls.

    strip_guildhouse_vmap.py DATA_DIR            # writes the stripped tile, keeps a .orig copy
    strip_guildhouse_vmap.py DATA_DIR --restore  # puts the original tile back
"""

import argparse
import os
import shutil
import struct
import sys

TILE = os.path.join("vmaps", "001_01_01.vmtile")
HOUSE = "guildhousea.wmo"
MOD_M2 = 1
MOD_HAS_BOUND = 4
MID = 0.5 * 64 * 533.33333


def read_tile(data):
    magic = data[:8]
    count = struct.unpack_from("<I", data, 8)[0]
    p = 12
    spawns = []
    for _ in range(count):
        start = p
        flags, _adt, uid = struct.unpack_from("<IHI", data, p)
        p += 10
        pos = struct.unpack_from("<3f", data, p)
        p += 12 + 12 + 4
        bound = None
        if flags & MOD_HAS_BOUND:
            bound = struct.unpack_from("<6f", data, p)
            p += 24
        nlen = struct.unpack_from("<I", data, p)[0]
        p += 4
        name = data[p:p + nlen].rstrip(b"\x00").decode(errors="replace")
        p += nlen + 4  # name + BIH node index
        spawns.append({"flags": flags, "id": uid, "pos": pos, "bound": bound, "name": name, "raw": data[start:p]})
    if p != len(data):
        raise ValueError("unexpected trailing data in tile (%d of %d bytes parsed)" % (p, len(data)))
    return magic, spawns


def house_entries(spawns):
    house = next((s for s in spawns if s["name"].lower() == HOUSE), None)
    if house is None or house["bound"] is None:
        return []
    lo, hi = house["bound"][:3], house["bound"][3:]
    # The extractor numbers a WMO's doodads right after the WMO itself, up to the next WMO.
    next_wmo = min((s["id"] for s in spawns if not s["flags"] & MOD_M2 and s["id"] > house["id"]), default=1 << 32)
    doodads = [s for s in spawns
               if s["flags"] & MOD_M2 and house["id"] < s["id"] < next_wmo
               and all(lo[i] - 2.0 <= s["pos"][i] <= hi[i] + 2.0 for i in range(3))]
    return [house] + doodads


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("data_dir", help="server DataDir (contains vmaps/)")
    ap.add_argument("--restore", action="store_true", help="put the original tile back")
    args = ap.parse_args()

    tile = os.path.join(args.data_dir, TILE)
    backup = tile + ".orig"

    if args.restore:
        if not os.path.exists(backup):
            sys.exit("no backup at %s; the tile was never stripped" % backup)
        shutil.copyfile(backup, tile)
        print("restored %s" % tile)
        return

    if not os.path.exists(backup):
        shutil.copyfile(tile, backup)

    magic, spawns = read_tile(open(backup, "rb").read())
    remove = house_entries(spawns)
    if not remove:
        sys.exit("%s not found in %s" % (HOUSE, backup))

    removed_ids = {s["id"] for s in remove}
    kept = [s for s in spawns if s["id"] not in removed_ids]
    with open(tile, "wb") as out:
        out.write(magic)
        out.write(struct.pack("<I", len(kept)))
        for s in kept:
            out.write(s["raw"])

    house = remove[0]
    print("removed %s and %d built-in props from %s (%d spawns left)" % (house["name"], len(remove) - 1, tile, len(kept)))
    print("house footprint: X %.1f..%.1f  Y %.1f..%.1f" % (
        MID - house["bound"][3], MID - house["bound"][0], MID - house["bound"][4], MID - house["bound"][1]))


if __name__ == "__main__":
    main()
