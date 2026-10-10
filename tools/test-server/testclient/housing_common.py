#!/usr/bin/env python3
"""What the end-to-end tests share: the housing items and objects they use, the database and
client helpers, and the pass/fail tally (check, finish). The tests themselves are in
collection_smoke.py."""

import argparse
import math
import os
import struct
import subprocess
import sys
import time

from wowclient import (CMSG_AUTOBANK_ITEM, CMSG_USE_ITEM, TARGET_FLAG_DEST_LOCATION, TYPEID_GAMEOBJECT, TYPEID_UNIT, WorldClient,
                       auth_login, srp6_verifier)

HOUSING_MAP = 1
STEWARD = 900200
HOUSE_KEY, KEY_SPELL = 902000, 18282
MARKER_GO = 903990
RING_GO = 903991                                          # under each selected piece while decorating

CHAIR, TABLE, LANTERN = 901105, 901106, 901104          # first-login gifts
CART, SHREDDED_TENT = 902200, 902201                    # wreckage on the island
RAZORFEN_LEANTO, CANVAS_TENT = 902204, 901100           # shelters: level 10, level 20
FARMHOUSE = 902220                                      # Exalted with Stormwind
LAMP_POST = 902300                                      # Explore Elwynn Forest
MAILBOX = 902900                                        # level 80: a working mailbox
MANNEQUIN, MANNEQUIN_NPC = 901107, 900201               # a stand, and the figure it shows
BANK_CHEST, CHEST_BANKER = 901108, 900202               # level 20: opens the bank
BANKSLOT_NOTBANKER, BANKSLOT_OK = 2, 3
MUSIC_BOX, GRIZZLY_HILLS = 901109, 12816               # level 10; a zone music track
WEATHER_RAIN = 4
HOGGER_FIGURINE, HOGGER_FIGURE_NPC = 902952, 932952   # unlocked by defeating Hogger
QUEST_HOME, QUEST_PLACE, QUEST_CHANGE, QUEST_UNDO, QUEST_OPEN = range(900400, 900405)   # Krook's welcome tour
PICNIC_BASKET = 901110                                  # the tour's reward
SWORD, PANTS, PANTS_DISPLAY = 25, 39, 9892              # gear for the mannequin
UNIT_VIRTUAL_ITEM_SLOT_ID = 0x06 + 0x32                 # main hand, off hand, ranged
SLOT_LEGS, SLOT_MAIN_HAND = 6, 15
MOVERS = range(901190, 901200)                          # "Move a Piece" items
GHOST_NPC = 900203                                      # a see-through piece following its player
UNIT_FIELD_DISPLAYID = 0x06 + 0x3D
ELWYNN_ACHIEVEMENT, STORMWIND = 776, 72
WORN_DAGGER = 2092                                      # fills bags (does not stack)


def live(item):
    return 910000 + (item - 900000)


def edit(item):
    return 920000 + (item - 900000)


LAYOUTS = {
    # GM Island without the guild hall: the plateau where it stood is open ground.
    "cleared": dict(
        landing=(16240.0, 16296.0, 12.92),
        chair=(16244.0, 16292.0), table=(16245.2, 16292.0), ground=12.92,
        far_stand=(16300.0, 16240.0, 25.2), far_target=(16310.0, 16240.0, 25.27),
        farmhouse_stand=(16250.0, 16326.0), farmhouse=(16258.0, 16338.0, 12.96),
        inside=(16259.0, 16340.0, 12.99),
        mailbox_stand=(16237.0, 16294.0), mailbox=(16234.0, 16290.0, 12.92),
        stand_stand=(16233.0, 16300.0), stand=(16230.0, 16300.0, 12.92),
        chest_stand=(16248.0, 16306.0), chest=(16250.0, 16310.0, 12.92),
        music_stand=(16238.0, 16304.0), music=(16240.0, 16306.0, 12.92),
        figure_stand=(16244.0, 16300.0), figure=(16246.0, 16300.0, 12.92),
        sea_stand=(16250.0, 16120.0, 0.0), sea_target=(16250.0, 16098.0, 0.0),
        past_edge=(16250.0, 16108.0, 0.0)),
}
L = LAYOUTS["cleared"]
results = []


def log(msg):
    print(msg, flush=True)


def check(name, ok, detail=""):
    results.append((name, bool(ok), detail))
    log("%s %s%s" % ("PASS" if ok else "FAIL", name, (" :: " + detail) if detail and not ok else ""))
    return ok


def db(sql, database="acore_characters"):
    out = subprocess.run(["mysql", "-uacore", "-pacore", "-N", "-B", database, "-e", sql], capture_output=True, text=True)
    if out.returncode != 0:
        raise RuntimeError(out.stderr)
    return [line.split("\t") for line in out.stdout.splitlines() if line]


def has(messages, needle):
    return any(needle.lower() in m.lower() for m in messages)


def joined(messages):
    return " | ".join(messages)


def options(menu):
    return [i["text"] for i in (menu or {}).get("items", [])]


def ensure_account(name, password):
    salt = os.urandom(32)
    verifier = srp6_verifier(name.upper(), password, salt)
    db("INSERT INTO account (username, salt, verifier, expansion) VALUES ('%s', 0x%s, 0x%s, 2) "
       "ON DUPLICATE KEY UPDATE salt=VALUES(salt), verifier=VALUES(verifier)" % (name.upper(), salt.hex(), verifier.hex()), "acore_auth")


def connect(args, account, password):
    key, realms = auth_login(args.host, args.auth_port, account, password)
    realm = realms[0]
    host, port = realm["address"].split(":")
    wc = WorldClient(args.host, int(port), account, key, realm_id=realm["id"],
                     log=(lambda m: log("[%s]%s" % (account, m))) if args.verbose else None)
    wc.connect()
    return wc


def get_or_create_char(wc, name):
    chars = {c["name"]: c for c in wc.enum_chars()}
    if name not in chars:
        code = wc.create_char(name, race=1, cls=8)  # Human Mage
        if code != 0x2F:
            raise RuntimeError("character create for %s failed: 0x%x" % (name, code))
        chars = {c["name"]: c for c in wc.enum_chars()}
    return chars[name]


def placements(owner_guid):
    rows = db("SELECT placement_id, source_item_entry, pos_x, pos_y, pos_z, orientation FROM mod_playerhousing_placement "
              "WHERE owner_guid=%d ORDER BY placement_id" % owner_guid)
    return [dict(id=int(r[0]), item=int(r[1]), x=float(r[2]), y=float(r[3]), z=float(r[4]), o=float(r[5])) for r in rows]


def shape_of(owner_guid, placement_id):
    """A placement's size and tilt (radians)."""
    row = db("SELECT scale, pitch, roll FROM mod_playerhousing_placement WHERE owner_guid=%d AND placement_id=%d"
             % (owner_guid, placement_id))[0]
    return dict(scale=float(row[0]), pitch=float(row[1]), roll=float(row[2]))


def template_size(entry):
    return float(db("SELECT size FROM gameobject_template WHERE entry=%d" % entry, "acore_world")[0][0])


def placement_of(owner_guid, item):
    return next((p for p in placements(owner_guid) if p["item"] == item), None)


def go_entries(wc):
    return {o.entry for o in wc.find_objects(type_id=TYPEID_GAMEOBJECT)}


def nearest_go(wc, entry):
    return wc.nearest(entry, TYPEID_GAMEOBJECT)


def wait_for(predicate, timeout=6.0, wc=None):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if predicate():
            return True
        if wc:
            wc.pump(0.25)
        else:
            time.sleep(0.25)
    return predicate()


def wait_for_map(wc, map_id, timeout=12.0):
    ok = wait_for(lambda: wc.map_id == map_id, timeout, wc)
    wc.pump(2.0)
    return ok


def move(wc, x, y, z, o=None):
    wc.move_to(x, y, z, o)
    wc.pump(0.4)


def stand_next_to(wc, obj, dist=2.0):
    angle = math.atan2(wc.pos[1] - obj.y, wc.pos[0] - obj.x)
    move(wc, obj.x + math.cos(angle) * dist, obj.y + math.sin(angle) * dist, obj.z, angle + math.pi)


SIT_ON_CHAIR = (4, 5, 6)  # UNIT_STAND_STATE_SIT_LOW_CHAIR .. SIT_HIGH_CHAIR


def addon_state(wc):
    """The last island state the server whispered for the client addon, as its fields."""
    states = [m for m in wc.addon_messages if m.startswith("HOUSING\tstate\t")]
    return states[-1].split("\t") if states else None


# Targeting circle radius (yards) of each placement spell (tools/content/build_content.py).
CIRCLE_RADIUS = {61736: 1, 52923: 2, 53261: 3, 68316: 5, 45959: 8, 54686: 10, 48431: 15, 55295: 18, 32150: 20}


def addon_list(wc, kind):
    """The rows of the last list of this kind sent to the addon's window, or None."""
    msgs = wc.addon_messages
    starts = [i for i, m in enumerate(msgs) if m == "HOUSING\tbegin\t" + kind or m.startswith("HOUSING\tbegin\t" + kind + "\t")]
    if not starts:
        return None
    rows = []
    for m in msgs[starts[-1] + 1:]:
        if m == "HOUSING\tend\t" + kind or m.startswith("HOUSING\tend\t" + kind + "\t"):
            return rows
        if m.startswith("HOUSING\trow\t" + kind + "\t"):
            rows.append(m.split("\t")[3:])
    return None


def id_ranges(text):
    """901100-901102,902001 -> {901100, 901101, 901102, 902001}"""
    ids = set()
    for part in text.split(","):
        first, _, last = part.partition("-")
        if first:
            ids.update(range(int(first), int(last or first) + 1))
    return ids


def spell_of(item):
    """The item's spell, whose targeting circle the client shows (sized to the piece)."""
    return int(db("SELECT spellid_1 FROM item_template WHERE entry=%d" % item, "acore_world")[0][0])


def gear_of(owner_guid, placement_id):
    """What a stand wears: slot -> (item guid, item entry)."""
    rows = db("SELECT slot, item_guid, item_entry FROM mod_playerhousing_placement_gear WHERE owner_guid=%d AND placement_id=%d"
              % (owner_guid, placement_id))
    return {int(r[0]): (int(r[1]), int(r[2])) for r in rows}


def item_guid_in_bags(wc, entry):
    slot, guid = wc.find_item(entry)
    return (guid & 0xFFFFFFFF) if guid else None


def storage(owner_guid):
    return {int(r[0]): int(r[1]) for r in db("SELECT item_entry, count FROM mod_playerhousing_storage WHERE owner_guid=%d" % owner_guid)}


def unlocked(account_id, guid):
    return {int(r[0]) for r in db("SELECT item_entry FROM mod_playerhousing_collection WHERE (account_id=%d AND guid=0) OR guid=%d"
                                  % (account_id, guid))}


def krook_turn_in(wc, quest_id, next_quest=None):
    """Hands a tour quest in to Krook on the island and, when he offers the next, takes it."""
    krook = wc.nearest(STEWARD, TYPEID_UNIT)
    if not krook:
        return False
    stand_next_to(wc, krook, 2.0)
    wc.quest_offered = None
    done = wc.turn_in_quest(krook.guid, quest_id)
    if done and next_quest:
        wait_for(lambda: wc.quest_offered == next_quest, 3, wc)
        wc.accept_quest(krook.guid, next_quest)
    return done


def destroy_all(wc, entry):
    """Every stack of this item out of the backpack."""
    for _ in range(10):
        if not wc.count_item(entry):
            return
        wc.destroy_item(entry)


def rings_in_sight(wc):
    return [o for o in wc.find_objects(type_id=TYPEID_GAMEOBJECT) if o.entry == RING_GO]


def ghosts_in_sight(wc):
    return [o for o in wc.find_objects(type_id=TYPEID_UNIT) if o.entry == GHOST_NPC]


def ghost_display(item):
    """A piece's see-through model (Housing::GHOST_DISPLAY_BASE plus the item's offset)."""
    return 60000 + item - 900000


def angle_diff(a, b):
    d = (a - b) % (2 * math.pi)
    return min(d, 2 * math.pi - d)


def finish():
    passed = sum(1 for _, ok, _ in results if ok)
    log("")
    log("%d/%d checks passed" % (passed, len(results)))
    for name, ok, detail in results:
        if not ok:
            log("  FAILED: %s %s" % (name, detail))
    return 0 if results and passed == len(results) else 1
