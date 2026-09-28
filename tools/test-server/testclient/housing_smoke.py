#!/usr/bin/env python3
"""End-to-end test for mod-playerhousing using the headless client.

Expects a running authserver and worldserver with the module (FreeMode on, the cleared
island layout by default) and three accounts: houseowner and houseguest (players) and
admin (GM). The test characters' housing state is reset at the start of every run.
"""

import argparse
import math
import subprocess
import sys
import time

from wowclient import TYPEID_GAMEOBJECT, TYPEID_UNIT, WorldClient, auth_login

HOUSING_MAP = 1
STEWARD = 900200
HOUSE_KEY, KEY_SPELL, FLARE = 902000, 18282, 1543
MARKER_GO = 903990

CHAIR, TABLE, LANTERN = 901105, 901106, 901104          # first-login gifts
CART, SHREDDED_TENT = 902200, 902201                    # wreckage on the island
RAZORFEN_LEANTO, CANVAS_TENT = 902204, 901100           # shelters: level 10, level 20
FARMHOUSE = 902220                                      # Exalted with Stormwind
LAMP_POST = 902300                                      # Explore Elwynn Forest
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


def storage(owner_guid):
    return {int(r[0]): int(r[1]) for r in db("SELECT item_entry, count FROM mod_playerhousing_storage WHERE owner_guid=%d" % owner_guid)}


def unlocked(account_id, guid):
    return {int(r[0]) for r in db("SELECT item_entry FROM mod_playerhousing_collection WHERE (account_id=%d AND guid=0) OR guid=%d"
                                  % (account_id, guid))}


def angle_diff(a, b):
    d = (a - b) % (2 * math.pi)
    return min(d, 2 * math.pi - d)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--auth-port", type=int, default=3724)
    ap.add_argument("--owner", default="houseowner:houseowner")
    ap.add_argument("--guest", default="houseguest:houseguest")
    ap.add_argument("--admin", default="admin:admin")
    ap.add_argument("--owner-char", default="Krookowner")
    ap.add_argument("--guest-char", default="Krookguest")
    ap.add_argument("--admin-char", default="Krookadmin")
    ap.add_argument("--layout", choices=sorted(LAYOUTS), default="cleared")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    owner_acc, owner_pw = args.owner.split(":")
    guest_acc, guest_pw = args.guest.split(":")
    admin_acc, admin_pw = args.admin.split(":")

    # ---------------------------------------------------------------- setup
    log("== characters")
    owner = connect(args, owner_acc, owner_pw)
    owner_char = get_or_create_char(owner, args.owner_char)
    guest = connect(args, guest_acc, guest_pw)
    guest_char = get_or_create_char(guest, args.guest_char)
    admin = connect(args, admin_acc, admin_pw)
    admin_char = get_or_create_char(admin, args.admin_char)
    owner_guid = owner_char["guid"] & 0xFFFFFFFF
    guest_guid = guest_char["guid"] & 0xFFFFFFFF
    ids = "%d, %d" % (owner_guid, guest_guid)
    owner_account = int(db("SELECT account FROM characters WHERE guid=%d" % owner_guid)[0][0])

    # A previous aborted run can leave characters in the world for a while.
    wait_for(lambda: db("SELECT COUNT(*) FROM characters WHERE online=1 AND guid IN (%s)" % ids) == [["0"]], 60)

    # Fresh start: no island, no unlocks, no housing items. The owner is a level 15 veteran who
    # explored Elwynn Forest and is Exalted with Stormwind, to check that past progress counts.
    for table, column in (("mod_playerhousing_placement", "owner_guid"), ("mod_playerhousing_acl", "owner_guid"),
                          ("mod_playerhousing_acl", "guest_guid"), ("mod_playerhousing_house", "owner_guid"),
                          ("mod_playerhousing_storage", "owner_guid"), ("mod_playerhousing_character", "guid"),
                          ("mod_playerhousing_collection", "guid")):
        db("DELETE FROM %s WHERE %s IN (%s)" % (table, column, ids))
    db("DELETE FROM mod_playerhousing_collection WHERE account_id IN (SELECT account FROM characters WHERE guid IN (%s))" % ids)
    db("DELETE FROM character_inventory WHERE guid IN (%s) AND item IN (SELECT guid FROM item_instance WHERE itemEntry BETWEEN 901100 AND 902999 OR itemEntry=%d)"
       % (ids, WORN_DAGGER))
    db("DELETE FROM item_instance WHERE owner_guid IN (%s) AND (itemEntry BETWEEN 901100 AND 902999 OR itemEntry=%d)" % (ids, WORN_DAGGER))
    db("UPDATE characters SET level=15, money=1000000 WHERE guid=%d" % owner_guid)
    # Everyone starts in Northshire, so no login teleport gets in the way.
    db("UPDATE characters SET map=0, position_x=-8949.95, position_y=-132.49, position_z=83.53, orientation=0 "
       "WHERE guid IN (%s, %d)" % (ids, admin_char["guid"] & 0xFFFFFFFF))
    db("DELETE FROM character_achievement WHERE guid=%d AND achievement=%d" % (owner_guid, ELWYNN_ACHIEVEMENT))
    db("INSERT INTO character_achievement (guid, achievement, date) VALUES (%d, %d, UNIX_TIMESTAMP())" % (owner_guid, ELWYNN_ACHIEVEMENT))
    db("REPLACE INTO character_reputation (guid, faction, standing, flags) VALUES (%d, %d, 42999, 1)" % (owner_guid, STORMWIND))

    # ------------------------------------------------------------- first login
    log("== first login")
    owner.login(owner_char["guid"])
    wait_for(lambda: owner.count_item(HOUSE_KEY) == 1, 10, owner)
    owner.pump(1.0)
    msgs = owner.messages_since(0)
    check("first login: told about the house", has(msgs, "You have a house"), joined(msgs))
    check("first login: House Key in the bags", owner.count_item(HOUSE_KEY) == 1, str(owner.backpack()))
    check("first login: chair, table and lantern in the bags",
          all(owner.count_item(i) == 1 for i in (CHAIR, TABLE, LANTERN)), str(owner.backpack()))
    check("first login: past progress credited", has(msgs, "Your past adventures unlocked"), joined(msgs))
    have = unlocked(owner_account, owner_guid)
    check("veteran: exploring Elwynn unlocked the lamp post", LAMP_POST in have, str(sorted(have)))
    check("veteran: level 15 unlocked the level 10 shelters", RAZORFEN_LEANTO in have and CANVAS_TENT not in have, str(sorted(have)))
    check("veteran: Exalted with Stormwind unlocked the farmhouse for this character",
          [[str(owner_guid)]] == db("SELECT guid FROM mod_playerhousing_collection WHERE item_entry=%d" % FARMHOUSE))
    house = db("SELECT is_private FROM mod_playerhousing_house WHERE owner_guid=%d" % owner_guid)
    check("island created, private by default", house == [["1"]], str(house))

    msgs = owner.command(".house")
    menu = owner.last_gossip
    check(".house opens the Home menu", "Go home" in options(menu) and any(o.startswith("Collection") for o in options(menu)),
          str(options(menu)))
    msgs = owner.command(".house undo")
    check("editing outside the island points home", has(msgs, "Go home first"), joined(msgs))

    # ------------------------------------------------------------- going home
    log("== House Key: go home")
    msgs = owner.use_item(HOUSE_KEY, KEY_SPELL)
    menu = owner.last_gossip
    check("House Key opens the Home menu", "Go home" in options(menu), joined(msgs) + " " + str(options(menu)))
    check("House Key stays in the bags", owner.count_item(HOUSE_KEY) == 1)
    owner.gossip_select("Go home", wait=1.0)
    home = wait_for_map(owner, HOUSING_MAP)
    check("Go home lands on the island", home and math.dist(owner.pos[:2], L["landing"][:2]) < 5, "map=%s pos=%s" % (owner.map_id, owner.pos))
    if not home:
        return finish()
    wait_for(lambda: owner.nearest(STEWARD, TYPEID_UNIT) is not None, 6, owner)
    msgs = owner.messages_since(0)
    check("Krook greets on the first visit", has(msgs, "Welcome to your island"), joined(msgs[-8:]))
    check("Krook stands by the landing spot", owner.nearest(STEWARD, TYPEID_UNIT) is not None)
    items = {p["item"] for p in placements(owner_guid)}
    check("the fallen cart and shredded tent are waiting", {CART, SHREDDED_TENT} <= items, str(items))
    check("the wreckage is visible", {live(CART), live(SHREDDED_TENT)} <= go_entries(owner), str(sorted(go_entries(owner))))

    # ------------------------------------------------------------- place, undo, redo
    log("== placing")
    move(owner, *L["landing"])
    chair_spot = (L["chair"][0], L["chair"][1], L["ground"])
    msgs = owner.use_item(CHAIR, FLARE, chair_spot)
    chair = placement_of(owner_guid, CHAIR)
    check("chair placed where the circle was clicked",
          chair is not None and math.dist((chair["x"], chair["y"]), chair_spot[:2]) < 0.01 and abs(chair["z"] - chair_spot[2]) < 0.01,
          joined(msgs) + " " + str(chair))
    check("placement message has the counts", has(msgs, "Placed Westfall Chair") and has(msgs, "furnishings"), joined(msgs))
    check("placed piece faces the player", chair is not None and angle_diff(chair["o"], math.atan2(L["landing"][1] - chair_spot[1], L["landing"][0] - chair_spot[0])) < 0.05,
          str(chair))
    wait_for(lambda: owner.count_item(CHAIR) == 0, 3, owner)
    check("the chair left the bags", owner.count_item(CHAIR) == 0, str(owner.backpack()))
    check("the chair is visible", live(CHAIR) in go_entries(owner))

    msgs = owner.command(".house undo")
    check("undo takes the chair back", placement_of(owner_guid, CHAIR) is None and has(msgs, "Undid: placed Westfall Chair"), joined(msgs))
    wait_for(lambda: owner.count_item(CHAIR) == 1, 3, owner)
    check("undo puts the chair back in the bags", owner.count_item(CHAIR) == 1, str(owner.backpack()))
    msgs = owner.command(".house redo")
    wait_for(lambda: owner.count_item(CHAIR) == 0, 3, owner)
    check("redo places it again and uses the item", placement_of(owner_guid, CHAIR) is not None and owner.count_item(CHAIR) == 0, joined(msgs))

    table_spot = (L["table"][0], L["table"][1], L["ground"])
    msgs = owner.use_item(TABLE, FLARE, table_spot)
    check("no spacing rule: a table fits right next to the chair", placement_of(owner_guid, TABLE) is not None, joined(msgs))

    move(owner, *L["far_stand"])
    msgs = owner.use_item(LANTERN, FLARE, L["far_target"])
    lantern = placement_of(owner_guid, LANTERN)
    check("placing works far from the old house area", lantern is not None, joined(msgs))
    if lantern:
        owner.command(".house undo")
        wait_for(lambda: owner.count_item(LANTERN) == 1, 3, owner)

    move(owner, *L["sea_stand"])
    before = len(placements(owner_guid))
    msgs = owner.use_item(LANTERN, FLARE, L["sea_target"])
    check("a spot past the edge of the island is refused", len(placements(owner_guid)) == before and has(msgs, "off your island"), joined(msgs))
    move(owner, *L["past_edge"])
    wait_for(lambda: math.dist(owner.pos[:2], L["landing"][:2]) < 5, 5, owner)
    msgs = owner.messages_since(0)
    check("swimming past the edge brings you back to the beach", math.dist(owner.pos[:2], L["landing"][:2]) < 5 and has(msgs, "edge of your island"),
          str(owner.pos))
    move(owner, *L["landing"])

    # ------------------------------------------------------------- decorate: click to edit
    log("== decorate mode")
    owner.command(".house")
    _, msgs = owner.gossip_select("Start decorating")
    check("decorate mode starts from the Home menu", has(msgs, "Decorating"), joined(msgs))
    owner.pump(1.0)
    gos = go_entries(owner)
    check("the chair turns into its clickable copy while decorating", edit(CHAIR) in gos and live(CHAIR) not in gos, str(sorted(gos)))
    check("a snap rune appears on the table", MARKER_GO in gos, str(sorted(gos)))

    chair_go = nearest_go(owner, edit(CHAIR))
    if chair_go:
        stand_next_to(owner, chair_go)
        menu, msgs = owner.use_gameobject(chair_go.guid)
        check("clicking a piece opens its menu", "Pick up (back to your bags)" in options(menu), joined(msgs) + str(options(menu)))
        before = placement_of(owner_guid, CHAIR)
        _, msgs = owner.gossip_select("Turn left 45")
        after = placement_of(owner_guid, CHAIR)
        check("turn left 45° from the piece menu", before and after and abs(angle_diff(after["o"], before["o"]) - math.pi / 4) < 0.02,
              joined(msgs) + " %s -> %s" % (before, after))
        check("the piece menu follows the respawned piece", "Nudge..." in options(owner.last_gossip), str(options(owner.last_gossip)))
        owner.gossip_select("Nudge...")
        _, msgs = owner.gossip_select("Up")
        raised = placement_of(owner_guid, CHAIR)
        check("nudge up raises it by 0.1 yd", raised and abs(raised["z"] - after["z"] - 0.1) < 0.01, joined(msgs))
        owner.gossip_select("Back to the piece")
        _, msgs = owner.gossip_select("Undo:")
        check("undo from the piece menu", abs(placement_of(owner_guid, CHAIR)["z"] - after["z"]) < 0.01, joined(msgs))
    else:
        check("clicking a piece opens its menu", False, "no clickable chair in sight")

    marker = nearest_go(owner, MARKER_GO)
    if marker:
        stand_next_to(owner, marker)
        menu, msgs = owner.use_gameobject(marker.guid)
        check("clicking the rune offers small pieces", any(o.startswith("Put Lantern here") for o in options(menu)), str(options(menu)))
        _, msgs = owner.gossip_select("Put Lantern here")
        table = placement_of(owner_guid, TABLE)
        lantern = placement_of(owner_guid, LANTERN)
        check("the lantern snaps onto the table top", table and lantern and math.dist((lantern["x"], lantern["y"]), (table["x"], table["y"])) < 0.01
              and lantern["z"] > table["z"] + 0.3, joined(msgs) + " table=%s lantern=%s" % (table, lantern))

    # ------------------------------------------------------------- pick up, storage
    log("== pick up and storage")
    lantern = placement_of(owner_guid, LANTERN)
    msgs = owner.command(".house pickup %d" % lantern["id"]) if lantern else []
    wait_for(lambda: owner.count_item(LANTERN) == 1, 3, owner)
    check("pick up returns the piece to the bags", owner.count_item(LANTERN) == 1 and placement_of(owner_guid, LANTERN) is None, joined(msgs))

    # Fill the owner's bags: the admin appears next to them (selecting needs the same map).
    admin.enum_chars()
    admin.login(admin_char["guid"])
    admin.pump(2.0)
    admin.command(".gm on")
    admin.command(".appear %s" % args.owner_char, wait=1.0)
    wait_for_map(admin, HOUSING_MAP)
    admin.select(owner_char["guid"])
    admin.command(".additem %s %d 20" % (args.owner_char, WORN_DAGGER), wait=2.0)
    owner.pump(1.5)
    table = placement_of(owner_guid, TABLE)
    msgs = owner.command(".house pickup %d" % table["id"]) if table else []
    check("with full bags a picked-up piece goes to House Storage", storage(owner_guid).get(TABLE) == 1 and has(msgs, "House Storage"),
          joined(msgs) + " storage=%s" % storage(owner_guid))
    msgs = owner.command(".house undo")
    check("undo takes it back out of storage", placement_of(owner_guid, TABLE) is not None and TABLE not in storage(owner_guid), joined(msgs))
    owner.command(".house pickup %d" % placement_of(owner_guid, TABLE)["id"])
    admin.select(owner_char["guid"])
    removed = admin.command(".additem %s %d -%d" % (args.owner_char, WORN_DAGGER, owner.count_item(WORN_DAGGER)), wait=2.0)
    wait_for(lambda: owner.count_item(WORN_DAGGER) == 0, 4, owner)
    check("(setup) bags emptied again", owner.count_item(WORN_DAGGER) == 0, joined(removed))
    owner.command(".house storage")
    _, msgs = owner.gossip_select("Take everything")
    wait_for(lambda: owner.count_item(TABLE) == 1, 3, owner)
    check("Take everything empties House Storage into the bags", owner.count_item(TABLE) == 1 and not storage(owner_guid), joined(msgs))

    # ------------------------------------------------------------- collection and unlocks
    log("== collection")
    owner.command(".house collection")
    menu = owner.last_gossip
    check("the Collection lists categories with counts", any(o.startswith("Buildings (") for o in options(menu)), str(options(menu)))
    _, _ = owner.gossip_select("Buildings (")
    menu = owner.last_gossip
    locked = [o for o in options(menu) if o.startswith("Canvas Tent:")]
    check("locked pieces say how to earn them, with progress", locked and "level 20" in locked[0] and "you're level 15" in locked[0], str(options(menu)))
    _, msgs = owner.gossip_select("Razorfen Lean-to")
    wait_for(lambda: owner.count_item(RAZORFEN_LEANTO) == 1, 3, owner)
    check("an unlocked piece gives a free copy (FreeMode)", owner.count_item(RAZORFEN_LEANTO) == 1, joined(msgs))

    mark = owner.message_mark()
    admin.command(".character level %s 20" % args.owner_char, wait=2.0)
    owner.pump(1.5)
    msgs = owner.messages_since(mark)
    check("reaching level 20 unlocks the Canvas Tent on the spot", CANVAS_TENT in unlocked(owner_account, owner_guid) and has(msgs, "Canvas Tent"),
          joined(msgs))

    # ------------------------------------------------------------- buildings
    log("== buildings")
    owner.command(".house collection")
    owner.gossip_select("Buildings (")
    _, msgs = owner.gossip_select("Westfall Farmhouse")
    wait_for(lambda: owner.count_item(FARMHOUSE) == 1, 3, owner)
    move(owner, L["farmhouse_stand"][0], L["farmhouse_stand"][1], L["ground"])
    msgs = owner.use_item(FARMHOUSE, FLARE, L["farmhouse"])
    farmhouse = placement_of(owner_guid, FARMHOUSE)
    check("a faction building places like furniture", farmhouse is not None and has(msgs, "buildings"), joined(msgs))
    wait_for(lambda: owner.count_item(CHAIR) == 0, 1, owner)
    owner.command(".house pickup %d" % placement_of(owner_guid, CHAIR)["id"])
    wait_for(lambda: owner.count_item(CHAIR) == 1, 3, owner)
    move(owner, L["inside"][0] - 2, L["inside"][1], L["inside"][2])
    owner.use_item(CHAIR, FLARE, L["inside"])
    owner.command(".house")
    owner.gossip_select("Change a piece near me")
    _, _ = owner.gossip_select("Westfall Farmhouse")
    menu = owner.last_gossip
    check("a building's menu asks before picking up", "Pick up..." in options(menu), str(options(menu)))
    _, _ = owner.gossip_select("Pick up...")
    menu = owner.last_gossip
    check("choose the building only, or the building and what's inside",
          "Pick up the building only" in options(menu) and any("pieces inside it" in o for o in options(menu)), str(options(menu)))
    _, msgs = owner.gossip_select("Pick up the building and the")
    wait_for(lambda: owner.count_item(FARMHOUSE) == 1, 3, owner)
    check("the building and the chair inside come back", placement_of(owner_guid, FARMHOUSE) is None and placement_of(owner_guid, CHAIR) is None
          and owner.count_item(FARMHOUSE) == 1 and owner.count_item(CHAIR) == 1, joined(msgs))
    msgs = owner.command(".house undo")
    check("undo puts the building and the chair back", placement_of(owner_guid, FARMHOUSE) is not None and placement_of(owner_guid, CHAIR) is not None,
          joined(msgs))

    # ------------------------------------------------------------- people
    log("== visitors")
    owner.command(".house")
    owner.gossip_select("Island settings")
    _, msgs = owner.gossip_select("Greeting for visitors", code="Mind the coffins.")
    check("greeting saved", db("SELECT greeting FROM mod_playerhousing_house WHERE owner_guid=%d" % owner_guid) == [["Mind the coffins."]], joined(msgs))

    guest.enum_chars()
    guest.login(guest_char["guid"])
    guest.pump(2.0)
    msgs = guest.command(".house visit %s" % args.owner_char, wait=2.0)
    check("a private island refuses strangers", guest.map_id != HOUSING_MAP and has(msgs, "private"), joined(msgs))

    owner.command(".house")
    owner.gossip_select("Island settings")
    owner.gossip_select("Guests")
    mark = guest.message_mark()
    _, msgs = owner.gossip_select("Invite by name...", code=args.guest_char)
    guest.pump(1.0)
    check("invite by name", db("SELECT 1 FROM mod_playerhousing_acl WHERE owner_guid=%d AND guest_guid=%d" % (owner_guid, guest_guid)) == [["1"]],
          joined(msgs))
    check("the guest is told about the invitation", has(guest.messages_since(mark), "invited you"), joined(guest.messages_since(mark)))

    guest.command(".house visit")
    menu = guest.last_gossip
    check("the visit menu counts invitations", any(o.startswith("Islands you're invited to (1)") for o in options(menu)), str(options(menu)))
    guest.gossip_select("Islands you're invited to")
    owner_mark = owner.message_mark()
    _, msgs = guest.gossip_select("Visit %s's island" % args.owner_char, wait=1.0)
    arrived = wait_for_map(guest, HOUSING_MAP)
    check("the guest visits with one click", arrived, "map=%s" % guest.map_id)
    guest.pump(1.5)
    check("the guest sees the greeting", has(guest.messages_since(0), "Mind the coffins."), joined(guest.messages_since(0)[-5:]))
    check("the owner hears the guest arrive", has(owner.messages_since(owner_mark), "arrived on your island"), joined(owner.messages_since(owner_mark)))
    check("the guest sees the owner's farmhouse", live(FARMHOUSE) in go_entries(guest), str(sorted(go_entries(guest))))
    msgs = guest.command(".house undo")
    check("guests can't change anything", has(msgs, "Only the owner"), joined(msgs))

    guest.command(".house leave", wait=1.0)
    wait_for(lambda: guest.map_id != HOUSING_MAP or not guest.find_objects(entry=live(FARMHOUSE)), 8, guest)
    guest.command(".house home", wait=1.0)
    wait_for_map(guest, HOUSING_MAP)
    guest.pump(1.5)
    check("islands are private copies: the guest's own island has no farmhouse", live(FARMHOUSE) not in go_entries(guest), str(sorted(go_entries(guest))))
    guest.command(".house leave", wait=1.0)

    owner.command(".house")
    owner.gossip_select("Island settings")
    _, msgs = owner.gossip_select("Privacy:")
    check("privacy cycles to Friends & guild", db("SELECT is_private FROM mod_playerhousing_house WHERE owner_guid=%d" % owner_guid) == [["2"]], joined(msgs))
    _, msgs = owner.gossip_select("Privacy:")
    check("then to Public", db("SELECT is_private FROM mod_playerhousing_house WHERE owner_guid=%d" % owner_guid) == [["0"]], joined(msgs))
    owner.gossip_select("Privacy:")

    # ------------------------------------------------------------- pack up, unstuck, relog
    log("== pack up, unstuck, relog")
    count = len(placements(owner_guid))
    owner.command(".house")
    _, msgs = owner.gossip_select("Pack up everything")
    check("pack up everything empties the island", not placements(owner_guid) and has(msgs, "Packed up %d pieces" % count), joined(msgs))
    msgs = owner.command(".house undo")
    check("undo puts everything back", len(placements(owner_guid)) == count, joined(msgs))
    owner.command(".house decorate off")

    move(owner, L["landing"][0] + 30, L["landing"][1] + 30, L["ground"] + 3)
    msgs = owner.command(".house unstuck")
    wait_for(lambda: math.dist(owner.pos[:2], L["landing"][:2]) < 3, 4, owner)
    check("unstuck returns to the landing spot", math.dist(owner.pos[:2], L["landing"][:2]) < 3, joined(msgs) + str(owner.pos))

    owner.logout()
    deadline = time.time() + 30
    while db("SELECT online FROM characters WHERE guid=%d" % owner_guid) != [["0"]] and time.time() < deadline:
        time.sleep(1)
    owner = connect(args, owner_acc, owner_pw)
    owner.enum_chars()
    owner.login(owner_char["guid"])
    owner.pump(3.0)
    check("logging back in after logging out on the island returns you to where you came from",
          owner.map_id != HOUSING_MAP or math.dist(owner.pos[:2], L["landing"][:2]) > 300, "map=%s pos=%s" % (owner.map_id, owner.pos))
    check("placements survive the relog", len(placements(owner_guid)) == count)

    return finish()


def finish():
    passed = sum(1 for _, ok, _ in results if ok)
    log("")
    log("%d/%d checks passed" % (passed, len(results)))
    for name, ok, detail in results:
        if not ok:
            log("  FAILED: %s %s" % (name, detail))
    return 0 if results and passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
