#!/usr/bin/env python3
"""Load test for mod-playerhousing: many players on their islands at once.

Each player logs in, goes home, opens the island to everyone, places pieces (the first-login
chair, then more with "another"), turns, nudges, undoes and redoes, and visits another
player's island. The GM account watches the server's update times meanwhile. At the end:
how long each kind of action took to answer, the server's update times, and whether every
island shows only its own pieces.

Needs a running server with the module (FreeMode on) and the GM account the smoke test uses.
Player accounts LOADTEST01.. are created on first run (password "loadtest").

  python3 load_test.py --players 40 --pieces 8 --edits 20
"""

import argparse
import math
import os
import statistics
import subprocess
import sys
import threading
import time

from wowclient import TYPEID_GAMEOBJECT, WorldClient, auth_login, srp6_verifier

HOUSING_MAP = 1
LANDING = (16240.0, 16296.0, 12.92)
CHAIR = 901105
PIECE_OBJECTS = range(911100, 923000)   # live and clickable copies of curated pieces


def db(sql, database="acore_characters"):
    out = subprocess.run(["mysql", "-uacore", "-pacore", "-N", "-B", database, "-e", sql], capture_output=True, text=True)
    if out.returncode != 0:
        raise RuntimeError(out.stderr)
    return [line.split("\t") for line in out.stdout.splitlines() if line]


def ensure_account(name, password):
    salt = os.urandom(32)
    verifier = srp6_verifier(name.upper(), password, salt)
    db("INSERT INTO account (username, salt, verifier, expansion) VALUES ('%s', 0x%s, 0x%s, 2) "
       "ON DUPLICATE KEY UPDATE salt=VALUES(salt), verifier=VALUES(verifier)" % (name.upper(), salt.hex(), verifier.hex()), "acore_auth")


def connect(host, auth_port, account, password):
    key, realms = auth_login(host, auth_port, account, password)
    realm = realms[0]
    rhost, port = realm["address"].split(":")
    wc = WorldClient(host, int(port), account, key, realm_id=realm["id"])
    wc.connect()
    return wc


def char_name(index):
    letters = "abcdefghijklmnopqrstuvwxyz"
    return "Loadtest" + letters[index // 26] + letters[index % 26]


def get_or_create_char(wc, name):
    chars = {c["name"]: c for c in wc.enum_chars()}
    if name not in chars:
        code = wc.create_char(name, race=1, cls=8)
        if code != 0x2F:
            raise RuntimeError("character create for %s failed: 0x%x" % (name, code))
        chars = {c["name"]: c for c in wc.enum_chars()}
    return chars[name]


class Stats:
    def __init__(self):
        self.lock = threading.Lock()
        self.latency = {}      # action -> [seconds]
        self.errors = []
        self.placed = 0

    def add(self, action, seconds):
        with self.lock:
            self.latency.setdefault(action, []).append(seconds)

    def error(self, text):
        with self.lock:
            self.errors.append(text)


def timed(stats, wc, action, send, expect, timeout=10.0):
    """Sends something and waits for a system message containing `expect`: the time it took."""
    mark = wc.message_mark()
    start = time.time()
    send()
    deadline = start + timeout
    while time.time() < deadline:
        if any(expect in m for m in wc.messages_since(mark)):
            stats.add(action, time.time() - start)
            return True
        wc.pump(0.02)
    stats.error("%s: %s got no \"%s\" (%s)" % (wc.account, action, expect, " / ".join(wc.messages_since(mark)[-3:])))
    return False


def worker(index, args, names, stats, ready, go):
    name = names[index]
    account = "LOADTEST%02d" % (index + 1)
    try:
        wc = connect(args.host, args.auth_port, account, "loadtest")
        char = get_or_create_char(wc, name)
        wc.login(char["guid"])
        wc.pump(2.0)
    except Exception as exc:  # noqa: BLE001 - reported, not fatal for the others
        stats.error("%s: login failed: %r" % (account, exc))
        ready.release()
        return
    ready.release()
    go.wait()

    try:
        # Home, open to everyone.
        start = time.time()
        wc.command(".house home", wait=0.1)
        while time.time() - start < 15 and not (wc.map_id == HOUSING_MAP and math.dist(wc.pos[:2], LANDING[:2]) < 6):
            wc.pump(0.05)
        stats.add("go home (teleport)", time.time() - start)
        wc.pump(1.0)
        timed(stats, wc, "privacy", lambda: wc.command(".house privacy public", wait=0), "Your island is now")

        # Each player spreads out around the landing so the pieces don't pile up.
        angle = index * 2.39996
        radius = 6.0 + (index % 6) * 3.0
        center = (LANDING[0] + math.cos(angle) * radius, LANDING[1] + math.sin(angle) * radius)
        wc.move_to(center[0], center[1], LANDING[2])
        wc.pump(0.3)

        spell = int(db("SELECT spellid_1 FROM item_template WHERE entry=%d" % CHAIR, "acore_world")[0][0])
        for piece in range(args.pieces):
            spot = (center[0] + math.cos(piece) * 2.0, center[1] + math.sin(piece) * 2.0, LANDING[2])
            if wc.count_item(CHAIR) == 0:
                if not timed(stats, wc, "another like this", lambda: wc.command(".house another", wait=0), "Right-click"):
                    break
                deadline = time.time() + 5
                while wc.count_item(CHAIR) == 0 and time.time() < deadline:
                    wc.pump(0.05)
            before = wc.count_item(CHAIR)
            if timed(stats, wc, "place", lambda: wc.use_item(CHAIR, spell, spot, wait=0), "Placed"):
                with stats.lock:
                    stats.placed += 1
                # The chair is taken on the player's next update: wait for it, or the next
                # placement would try to use the same one.
                deadline = time.time() + 5
                while wc.count_item(CHAIR) >= before and time.time() < deadline:
                    wc.pump(0.05)
            wc.pump(0.2)

        for edit in range(args.edits):
            command, expect = [(".house rotate 15", "Turned"), (".house nudge forward", "Nudged"),
                               (".house undo", "Undid"), (".house redo", "Redid")][edit % 4]
            timed(stats, wc, command.split()[1], lambda: wc.command(command, wait=0), expect)
            wc.pump(0.25)

        # What this player sees at home: only their own pieces.
        wc.pump(1.0)
        mine = int(db("SELECT COUNT(*) FROM mod_playerhousing_placement WHERE owner_guid=%d" % (char["guid"] & 0xFFFFFFFF))[0][0])
        seen = len([o for o in wc.find_objects(type_id=TYPEID_GAMEOBJECT) if o.entry in PIECE_OBJECTS])
        if seen > mine:
            stats.error("%s sees %d pieces at home, but has %d: another island shows through" % (name, seen, mine))

        # A visit next door, and back.
        other = names[(index + 1) % len(names)]
        start = time.time()
        wc.command(".house visit %s" % other, wait=0.1)
        while time.time() - start < 15 and not (wc.map_id == HOUSING_MAP and math.dist(wc.pos[:2], LANDING[:2]) < 6
                                                and time.time() - start > 0.5):
            wc.pump(0.05)
        stats.add("visit (teleport)", time.time() - start)
        wc.pump(2.0)
        wc.command(".house leave", wait=1.0)
    except Exception as exc:  # noqa: BLE001
        stats.error("%s: %r" % (account, exc))
    finally:
        # A clean logout, so the next run doesn't find the character still in the world.
        try:
            wc.logout()
        except Exception:  # noqa: BLE001
            wc.close()


def watch_server(args, stop, samples):
    """The GM's .server info every few seconds: the world update times."""
    user, password = args.admin.split(":")
    try:
        admin = connect(args.host, args.auth_port, user, password)
        chars = admin.enum_chars()
        admin.login(chars[0]["guid"])
        admin.pump(2.0)
    except Exception as exc:  # noqa: BLE001
        print("server watch failed: %r" % exc)
        return
    while not stop.is_set():
        msgs = admin.command(".server info", wait=1.0)
        sample = {}
        for m in msgs:
            if m.startswith("Connected players:"):
                sample["players"] = int(m.split()[2].rstrip("."))
            elif m.startswith("|- Mean:"):
                sample["mean"] = int(m.split()[2].rstrip("ms"))
            elif m.startswith("|- Percentiles"):
                values = [int(v.strip().rstrip("ms,")) for v in m.split(":")[1].split(",")]
                sample["p95"], sample["p99"], sample["max"] = values
        if sample:
            samples.append(sample)
        stop.wait(args.interval)
    try:
        admin.logout()
    except Exception:  # noqa: BLE001
        admin.close()


def percentile(values, p):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(round(p / 100.0 * (len(ordered) - 1))))]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--auth-port", type=int, default=3724)
    ap.add_argument("--admin", default="admin:admin")
    ap.add_argument("--players", type=int, default=40)
    ap.add_argument("--pieces", type=int, default=8)
    ap.add_argument("--edits", type=int, default=20)
    ap.add_argument("--interval", type=float, default=3.0, help="seconds between server samples")
    args = ap.parse_args()

    names = [char_name(i) for i in range(args.players)]
    for i in range(args.players):
        ensure_account("LOADTEST%02d" % (i + 1), "loadtest")

    # A fresh start for the load characters' housing. Only once they are out of the world: a
    # character still in it from an earlier run keeps its old bags and island in memory, and
    # logging in again picks those up instead of the reset.
    guids = [r[0] for r in db("SELECT guid FROM characters WHERE name IN (%s)" % ",".join("'%s'" % n for n in names))]
    if guids:
        ids = ",".join(guids)
        deadline = time.time() + 120
        while int(db("SELECT COUNT(*) FROM characters WHERE online=1 AND guid IN (%s)" % ids)[0][0]) and time.time() < deadline:
            time.sleep(2)
        still = int(db("SELECT COUNT(*) FROM characters WHERE online=1 AND guid IN (%s)" % ids)[0][0])
        if still:
            print("%d load characters are still in the world; try again in a minute" % still)
            return 2
        for table, column in (("mod_playerhousing_placement", "owner_guid"), ("mod_playerhousing_house", "owner_guid"),
                              ("mod_playerhousing_storage", "owner_guid"), ("mod_playerhousing_character", "guid"),
                              ("mod_playerhousing_visit_log", "owner_guid"), ("mod_playerhousing_placement_gear", "owner_guid")):
            db("DELETE FROM %s WHERE %s IN (%s)" % (table, column, ids))
        items = "itemEntry BETWEEN 901100 AND 902999 OR itemEntry BETWEEN 940000 AND 944999"
        db("DELETE FROM character_inventory WHERE guid IN (%s) AND item IN (SELECT guid FROM item_instance WHERE %s)" % (ids, items))
        db("DELETE FROM item_instance WHERE owner_guid IN (%s) AND (%s)" % (ids, items))
        db("UPDATE characters SET map=0, position_x=-8949.95, position_y=-132.49, position_z=83.53 WHERE guid IN (%s)" % ids)

    stats = Stats()
    samples = []
    stop = threading.Event()
    watcher = threading.Thread(target=watch_server, args=(args, stop, samples), daemon=True)
    watcher.start()

    ready = threading.Semaphore(0)
    go = threading.Event()
    threads = [threading.Thread(target=worker, args=(i, args, names, stats, ready, go), daemon=True) for i in range(args.players)]
    print("logging in %d players..." % args.players)
    for thread in threads:
        thread.start()
        time.sleep(0.15)  # the auth server takes logins one at a time
    for _ in threads:
        ready.acquire()
    baseline = list(samples)
    print("all in; go")
    started = time.time()
    go.set()
    for thread in threads:
        thread.join(timeout=600)
    elapsed = time.time() - started
    stop.set()
    watcher.join(timeout=10)

    print("\n%d players, %d pieces placed in %.0f s" % (args.players, stats.placed, elapsed))
    print("\n%-22s %6s %8s %8s %8s" % ("action", "count", "p50 ms", "p95 ms", "max ms"))
    for action, values in sorted(stats.latency.items()):
        print("%-22s %6d %8.0f %8.0f %8.0f" % (action, len(values), statistics.median(values) * 1000, percentile(values, 95) * 1000,
                                              max(values) * 1000))
    during = samples[len(baseline):]
    if during:
        print("\nserver update time while loaded (from %d samples of .server info):" % len(during))
        print("  mean %d ms, worst p95 %d ms, worst p99 %d ms, worst max %d ms, players up to %d" % (
            round(statistics.mean(s["mean"] for s in during if "mean" in s)), max(s.get("p95", 0) for s in during),
            max(s.get("p99", 0) for s in during), max(s.get("max", 0) for s in during), max(s.get("players", 0) for s in during)))
    rss = subprocess.run(["ps", "-o", "rss=", "-C", "worldserver"], capture_output=True, text=True).stdout.split()
    if rss:
        print("worldserver memory: %d MB" % (int(rss[0]) // 1024))
    if stats.errors:
        print("\n%d problems:" % len(stats.errors))
        for error in stats.errors[:30]:
            print("  " + error)
    else:
        print("\nno problems")
    return 1 if stats.errors else 0


if __name__ == "__main__":
    sys.exit(main())
