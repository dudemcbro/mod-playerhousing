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
HOUSE_KEY, KEY_SPELL = 902000, 18282
MARKER_GO = 903990

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
WEATHER_FINE, WEATHER_RAIN = 0, 4
SWORD, PANTS, PANTS_DISPLAY = 25, 39, 9892              # gear for the mannequin
UNIT_VIRTUAL_ITEM_SLOT_ID = 0x06 + 0x32                 # main hand, off hand, ranged
SLOT_LEGS, SLOT_MAIN_HAND = 6, 15
MOVERS = range(901190, 901200)                          # "Move a Piece" items
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
CIRCLE_RADIUS = {61736: 1, 47004: 2, 42340: 3, 69680: 4, 43440: 5, 61985: 6, 34435: 8, 1543: 10, 26540: 15, 29882: 20}


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
                          ("mod_playerhousing_collection", "guid"), ("mod_playerhousing_saved_layout", "owner_guid"),
                          ("mod_playerhousing_saved_piece", "owner_guid"), ("mod_playerhousing_like", "owner_guid"),
                          ("mod_playerhousing_like", "liker_guid"), ("mod_playerhousing_visit_log", "owner_guid"),
                          ("mod_playerhousing_visit_log", "visitor_guid")):
        db("DELETE FROM %s WHERE %s IN (%s)" % (table, column, ids))
    db("DELETE FROM mod_playerhousing_collection WHERE account_id IN (SELECT account FROM characters WHERE guid IN (%s))" % ids)
    db("DELETE FROM character_social WHERE guid IN (%s) AND friend IN (%s)" % (ids, ids))
    # Gear on stands and gear Krook mailed back.
    db("DELETE FROM mod_playerhousing_placement_gear WHERE owner_guid IN (%s)" % ids)
    db("DELETE FROM mail_items WHERE receiver IN (%s) AND mail_id IN (SELECT id FROM mail WHERE sender=%d AND messageType=3)" % (ids, STEWARD))
    db("DELETE FROM mail WHERE receiver IN (%s) AND sender=%d AND messageType=3" % (ids, STEWARD))
    test_items = "itemEntry BETWEEN 901100 AND 902999 OR itemEntry IN (%d, %d, %d)" % (WORN_DAGGER, SWORD, PANTS)
    db("DELETE FROM character_inventory WHERE guid IN (%s) AND item IN (SELECT guid FROM item_instance WHERE %s)" % (ids, test_items))
    db("DELETE FROM item_instance WHERE owner_guid IN (%s) AND (%s)" % (ids, test_items))
    db("UPDATE characters SET level=15, money=1000000, bankSlots=0 WHERE guid=%d" % owner_guid)
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
    state = addon_state(owner)
    check("first login: the client addon hears the server has housing", state is not None and state[2] == "0", str(state))
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
    msgs = owner.use_item(CHAIR, spell_of(CHAIR), chair_spot)
    check("placing furniture doesn't open a menu (by default only buildings do)", owner.last_gossip is None,
          str(options(owner.last_gossip)))
    chair = placement_of(owner_guid, CHAIR)
    check("chair placed where the circle was clicked",
          chair is not None and math.dist((chair["x"], chair["y"]), chair_spot[:2]) < 0.01 and abs(chair["z"] - chair_spot[2]) < 0.01,
          joined(msgs) + " " + str(chair))
    check("the targeting circle fits the piece: small for a chair, large for a farmhouse",
          CIRCLE_RADIUS.get(spell_of(CHAIR), 99) <= 2 and CIRCLE_RADIUS.get(spell_of(FARMHOUSE), 0) >= 8,
          "chair %d, farmhouse %d" % (spell_of(CHAIR), spell_of(FARMHOUSE)))
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
    msgs = owner.use_item(TABLE, spell_of(TABLE), table_spot)
    check("no spacing rule: a table fits right next to the chair", placement_of(owner_guid, TABLE) is not None, joined(msgs))

    move(owner, *L["far_stand"])
    msgs = owner.use_item(LANTERN, spell_of(LANTERN), L["far_target"])
    lantern = placement_of(owner_guid, LANTERN)
    check("placing works far from the old house area", lantern is not None, joined(msgs))
    if lantern:
        owner.command(".house undo")
        wait_for(lambda: owner.count_item(LANTERN) == 1, 3, owner)

    move(owner, *L["sea_stand"])
    before = len(placements(owner_guid))
    msgs = owner.use_item(LANTERN, spell_of(LANTERN), L["sea_target"])
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
    owner.addon_messages.clear()
    msgs = owner.command(".house state")
    state = addon_state(owner)
    check("the addon's state request is quiet and answered", not msgs and state is not None
          and state[2:4] == ["1", "1"] and state[11] == args.owner_char, joined(msgs) + " " + str(state))
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
        state = addon_state(owner)
        check("the addon sees the clicked piece selected", state is not None and before is not None
              and state[4] == str(before["id"]) and state[5] == "Westfall Chair" and state[13] == "0", str(state))
        _, msgs = owner.gossip_select("Turn left 45")
        after = placement_of(owner_guid, CHAIR)
        state = addon_state(owner)
        check("the addon's Undo button names the last change", state is not None and state[10].startswith("turned Westfall Chair 45"), str(state))
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

    # What stands on a table goes where the table goes, in one undoable step.
    table = placement_of(owner_guid, TABLE)
    lantern = placement_of(owner_guid, LANTERN)
    if table and lantern:
        msgs = owner.command(".house nudge forward 0.5 %d" % table["id"])
        moved_table = placement_of(owner_guid, TABLE)
        moved_lantern = placement_of(owner_guid, LANTERN)
        shift = (moved_table["x"] - table["x"], moved_table["y"] - table["y"])
        check("the lantern moves with its table", math.hypot(*shift) > 0.4
              and abs(moved_lantern["x"] - lantern["x"] - shift[0]) < 0.01 and abs(moved_lantern["y"] - lantern["y"] - shift[1]) < 0.01
              and has(msgs, "with the Lantern"), joined(msgs))
        msgs = owner.command(".house rotate 90 %d" % table["id"])
        turned = placement_of(owner_guid, LANTERN)
        check("and turns with it", angle_diff(turned["o"], moved_lantern["o"] + math.pi / 2) < 0.02, joined(msgs))
        owner.command(".house undo")
        msgs = owner.command(".house undo")
        back = placement_of(owner_guid, LANTERN)
        check("one undo per move puts both back", math.dist((back["x"], back["y"]), (lantern["x"], lantern["y"])) < 0.01
              and math.dist((placement_of(owner_guid, TABLE)["x"], placement_of(owner_guid, TABLE)["y"]), (table["x"], table["y"])) < 0.01,
              joined(msgs))
    else:
        check("the lantern moves with its table", False, "no table and lantern to move")

    # ------------------------------------------------------------- size, tilt, grid, copies
    log("== size, tilt, grid and copies")
    table = placement_of(owner_guid, TABLE)
    lantern = placement_of(owner_guid, LANTERN)
    if table and lantern:
        normal = shape_of(owner_guid, table["id"])["scale"]
        msgs = owner.command(".house size bigger %d" % table["id"])
        raised = placement_of(owner_guid, LANTERN)
        check("bigger: the table grows by a tenth", abs(shape_of(owner_guid, table["id"])["scale"] / normal - 1.1) < 0.001
              and has(msgs, "made Tiny Table bigger (110%) with the Lantern"), joined(msgs))
        check("the lantern stays on the bigger table top", math.dist((raised["x"], raised["y"]), (lantern["x"], lantern["y"])) < 0.01
              and abs((raised["z"] - table["z"]) - (lantern["z"] - table["z"]) * 1.1) < 0.01, "%s -> %s" % (lantern, raised))
        # The table has no clickable copy: it's changed through its rune.
        wait_for(lambda: nearest_go(owner, live(TABLE)) is not None and abs(nearest_go(owner, live(TABLE)).scale() - template_size(live(TABLE)) * 1.1) < 0.01,
                 3, owner)
        go = nearest_go(owner, live(TABLE))
        check("the client sees it bigger", go is not None and abs(go.scale() - template_size(live(TABLE)) * 1.1) < 0.01,
              "scale %s" % (go.scale() if go else None))
        owner.command(".house size 500 %d" % table["id"])
        check("sizes stop at the server's limit (200%)", abs(shape_of(owner_guid, table["id"])["scale"] / normal - 2.0) < 0.001)
        msgs = owner.command(".house size bigger %d" % table["id"])
        check("and say so", has(msgs, "as big as it gets (200%)"), joined(msgs))
        msgs = owner.command(".house size normal %d" % table["id"])
        back = placement_of(owner_guid, LANTERN)
        check("normal size, and the lantern comes back down with the top", abs(shape_of(owner_guid, table["id"])["scale"] - normal) < 0.001
              and abs(back["z"] - lantern["z"]) < 0.01 and has(msgs, "brought Tiny Table back to normal size"), joined(msgs))

    chair = placement_of(owner_guid, CHAIR)
    msgs = owner.command(".house tilt forward 10 %d" % chair["id"])
    shape = shape_of(owner_guid, chair["id"])
    check("tilt forward 10°", abs(math.degrees(shape["pitch"]) - 10) < 0.05 and shape["roll"] == 0
          and has(msgs, "tilted Westfall Chair 10° forward"), joined(msgs) + " " + str(shape))

    def tilted_go():
        go = nearest_go(owner, edit(CHAIR))
        return go if go and go.rotation and abs(math.degrees(go.yaw_pitch_roll()[1]) - 10) < 0.5 else None
    wait_for(lambda: tilted_go() is not None, 3, owner)
    go = nearest_go(owner, edit(CHAIR))
    angles = go.yaw_pitch_roll() if go and go.rotation else None
    check("the client gets the tilted rotation, still facing the same way", angles is not None and abs(math.degrees(angles[1]) - 10) < 0.5
          and angle_diff(angles[0], chair["o"]) < 0.02, "rotation %s angles %s o %.3f" % (go.rotation if go else None, angles, chair["o"]))
    owner.command(".house tilt right 90 %d" % chair["id"])
    check("tilting stops at the server's limit (45°)", abs(math.degrees(shape_of(owner_guid, chair["id"])["roll"]) - 45) < 0.05)
    msgs = owner.command(".house tilt straight %d" % chair["id"])
    shape = shape_of(owner_guid, chair["id"])
    check("stand it straight", shape["pitch"] == 0 and shape["roll"] == 0 and has(msgs, "stood Westfall Chair straight"), joined(msgs))

    chair_go = nearest_go(owner, edit(CHAIR))
    if chair_go:
        stand_next_to(owner, chair_go)
        owner.use_gameobject(chair_go.guid)
        owner.gossip_select("More turns, tilt and size...")
        menu = owner.last_gossip
        check("more turns, tilt and size: 90° and 5° turns, tilts, sizes",
              all(o in options(menu) for o in ("Turn left 90°", "Turn right 5°", "Tilt forward 5° (its front down)", "Bigger (up to 200%)"))
              and "Normal size" not in options(menu), str(options(menu)))
        before = placement_of(owner_guid, CHAIR)
        owner.gossip_select("Turn right 90°")
        after = placement_of(owner_guid, CHAIR)
        check("turn right 90°, and the menu stays for the next step", abs(angle_diff(after["o"], before["o"]) - math.pi / 2) < 0.02
              and "Turn left 90°" in options(owner.last_gossip), str(options(owner.last_gossip)))
        owner.gossip_select("Bigger")
        owner.gossip_select("Tilt back 5°")
        check("once it's bigger and tilted, the menu offers normal size and straight",
              "Normal size" in options(owner.last_gossip) and "Stand it straight" in options(owner.last_gossip)
              and options(owner.last_gossip)[0].startswith("Westfall Chair: Size 110%, tilted 5° back"), str(options(owner.last_gossip)))
        owner.gossip_select("Back to the piece")
        check("the piece menu offers another like this", "Place another like this" in options(owner.last_gossip), str(options(owner.last_gossip)))
    else:
        check("more turns, tilt and size: 90° and 5° turns, tilts, sizes", False, "no clickable chair in sight")

    # Another like this: FreeMode hands over a new chair, which lands turned, sized and
    # tilted like the first.
    source = placement_of(owner_guid, CHAIR)
    source_shape = shape_of(owner_guid, source["id"])
    owner.addon_messages.clear()
    _, msgs = owner.gossip_select("Place another like this") if owner.last_gossip else (None, [])
    wait_for(lambda: owner.count_item(CHAIR) == 1, 3, owner)
    state = addon_state(owner)
    check("another like this: a chair in the bags, and the addon knows which", owner.count_item(CHAIR) == 1 and state is not None
          and len(state) > 15 and state[15] == str(CHAIR) and has(msgs, "this one's turn, size and tilt"), joined(msgs) + " " + str(state))
    move(owner, *L["landing"])
    copy_spot = (L["chair"][0] - 2.0, L["chair"][1] - 2.0, L["ground"])
    msgs = owner.use_item(CHAIR, spell_of(CHAIR), copy_spot)
    copy = next((p for p in placements(owner_guid) if p["item"] == CHAIR and p["id"] != source["id"]), None)
    copy_shape = shape_of(owner_guid, copy["id"]) if copy else None
    check("the copy lands where clicked, turned, sized and tilted like the first", copy is not None
          and math.dist((copy["x"], copy["y"]), copy_spot[:2]) < 0.01 and angle_diff(copy["o"], source["o"]) < 0.01
          and abs(copy_shape["scale"] - source_shape["scale"]) < 0.001 and abs(copy_shape["pitch"] - source_shape["pitch"]) < 0.001,
          joined(msgs) + " %s %s / %s %s" % (source, source_shape, copy, copy_shape))
    state = addon_state(owner)
    check("the next chair places normally again", state is not None and state[15] == "0", str(state))
    if copy:
        owner.command(".house pickup %d" % copy["id"])
        wait_for(lambda: owner.count_item(CHAIR) == 1, 3, owner)
        owner.destroy_item(CHAIR)
        wait_for(lambda: owner.count_item(CHAIR) == 0, 3, owner)
    owner.command(".house size normal %d" % source["id"])
    owner.command(".house tilt straight %d" % source["id"])

    # The grid: pieces land on it, new ones face straight or diagonal, nudges go one square.
    msgs = owner.command(".house grid 1")
    check("grid on", has(msgs, "Grid on") and db("SELECT grid FROM mod_playerhousing_character WHERE guid=%d" % owner_guid) == [["4"]],
          joined(msgs))
    owner.command(".house pickup %d" % source["id"])
    wait_for(lambda: owner.count_item(CHAIR) == 1, 3, owner)
    move(owner, L["landing"][0] + 0.3, L["landing"][1] + 0.7, L["landing"][2])
    odd_spot = (L["chair"][0] + 0.37, L["chair"][1] - 0.41, L["ground"])
    msgs = owner.use_item(CHAIR, spell_of(CHAIR), odd_spot)
    chair = placement_of(owner_guid, CHAIR)
    on_grid = chair is not None and abs(chair["x"] - round(chair["x"])) < 0.001 and abs(chair["y"] - round(chair["y"])) < 0.001
    squared = chair is not None and min(angle_diff(chair["o"], k * math.pi / 4) for k in range(8)) < 0.001
    check("with the grid on, a piece lands on it, facing straight or diagonal", on_grid and squared
          and math.dist((chair["x"], chair["y"]), odd_spot[:2]) < 0.75, joined(msgs) + " " + str(chair))
    if chair:
        msgs = owner.command(".house nudge forward 0.25 %d" % chair["id"])
        nudged = placement_of(owner_guid, CHAIR)
        step = (round(nudged["x"] - chair["x"], 3), round(nudged["y"] - chair["y"], 3))
        check("nudges move one square along the grid", step in ((1.0, 0.0), (-1.0, 0.0), (0.0, 1.0), (0.0, -1.0)), joined(msgs) + " " + str(step))
    msgs = owner.command(".house grid off")
    check("grid off", has(msgs, "Grid off") and db("SELECT grid FROM mod_playerhousing_character WHERE guid=%d" % owner_guid) == [["0"]],
          joined(msgs))

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
    check("the Collection counts what's new", options(menu)[0].startswith("Collection:") and " new" in options(menu)[0], str(options(menu)))
    _, _ = owner.gossip_select("Buildings (")
    menu = owner.last_gossip
    locked = [o for o in options(menu) if o.startswith("Canvas Tent:")]
    check("locked pieces say how to earn them, with progress", locked and "level 20" in locked[0] and "you're level 15" in locked[0], str(options(menu)))
    check("pieces unlocked by past progress are marked new", "Razorfen Lean-to (new)" in options(menu), str(options(menu)))
    _, _ = owner.gossip_select("Razorfen Lean-to")
    menu = owner.last_gossip
    check("an unlocked piece has a page: what you have, get one or five", options(menu)[:3] == ["Razorfen Lean-to: you have none yet", "Get one", "Get 5"],
          str(options(menu)))
    _, msgs = owner.gossip_select("Get one")
    wait_for(lambda: owner.count_item(RAZORFEN_LEANTO) == 1, 3, owner)
    check("get one gives a free copy (FreeMode)", owner.count_item(RAZORFEN_LEANTO) == 1
          and options(owner.last_gossip)[0] == "Razorfen Lean-to: 1 in your bags", joined(msgs) + " " + str(options(owner.last_gossip)))
    owner.gossip_select("Back to Buildings")
    check("once seen, it isn't new any more", "Razorfen Lean-to" in options(owner.last_gossip), str(options(owner.last_gossip)))

    owner.command(".house collection")
    owner.gossip_select("Showing all pieces")
    menu = owner.last_gossip
    check("the Collection can show only unlocked pieces", "Showing unlocked pieces only (click to show all)" in options(menu), str(options(menu)))
    owner.gossip_select("Buildings (")
    pieces = [o for o in options(owner.last_gossip)[1:] if o not in ("Back to the Collection", "Next page", "Previous page")]
    check("and then lists no locked ones", pieces and not any(": " in o for o in pieces), str(options(owner.last_gossip)))
    owner.command(".house collection")
    owner.gossip_select("Showing unlocked pieces only")

    owner.command(".house collection lamp")
    menu = owner.last_gossip
    check("searching the Collection by name, locked pieces included", menu is not None and options(menu)[0].startswith('"lamp":')
          and "Stormwind Lamp Post (new)" in options(menu) and any(o.startswith("Tauren Lamp Post: Explore") for o in options(menu)),
          str(options(menu)))
    owner.gossip_select("Stormwind Lamp Post")
    _, msgs = owner.gossip_select("Get 5")
    wait_for(lambda: owner.count_item(LAMP_POST) == 5, 3, owner)
    check("get 5 at once", owner.count_item(LAMP_POST) == 5 and has(msgs, "Here are 5 of the Stormwind Lamp Post"), joined(msgs))
    check("the page goes back to the search", "Back to the search" in options(owner.last_gossip), str(options(owner.last_gossip)))
    while owner.count_item(LAMP_POST):
        if not owner.destroy_item(LAMP_POST):
            break

    mark = owner.message_mark()
    admin.command(".character level %s 20" % args.owner_char, wait=2.0)
    owner.pump(1.5)
    msgs = owner.messages_since(mark)
    check("reaching level 20 unlocks the Canvas Tent on the spot", CANVAS_TENT in unlocked(owner_account, owner_guid) and has(msgs, "Canvas Tent"),
          joined(msgs))

    mark = owner.message_mark()
    admin.command(".house unlock Mailbox %s" % args.owner_char, wait=1.5)
    msgs = owner.messages_since(mark)
    check("a GM unlocks a piece for a player, who is told", MAILBOX in unlocked(owner_account, owner_guid) and has(msgs, "Mailbox"), joined(msgs))
    owner.command(".house")
    check("the Home menu says there's something new", any(o.startswith("Collection (") and " new)" in o for o in options(owner.last_gossip)),
          str(options(owner.last_gossip)))
    owner.command(".house collection")
    owner.gossip_select("Capstones (")
    check("the Mailbox is marked new", "Mailbox (new)" in options(owner.last_gossip), str(options(owner.last_gossip)))
    owner.gossip_select("Mailbox")
    owner.gossip_select("Get one")
    wait_for(lambda: owner.count_item(MAILBOX) == 1, 3, owner)
    move(owner, L["mailbox_stand"][0], L["mailbox_stand"][1], L["ground"])
    msgs = owner.command(".house adjust all")
    check("the adjust menu can open after placing anything", has(msgs, "After placing anything"), joined(msgs))
    msgs = owner.use_item(MAILBOX, spell_of(MAILBOX), L["mailbox"])
    check("a working mailbox places like any piece", placement_of(owner_guid, MAILBOX) is not None, joined(msgs))
    menu = owner.last_gossip
    check("right after placing, the menu offers to keep it or take it back",
          "Keep it here" in options(menu) and "Take it back (back to your bags)" in options(menu), str(options(menu)))
    _, msgs = owner.gossip_select("Take it back")
    wait_for(lambda: owner.count_item(MAILBOX) == 1, 3, owner)
    check("take it back returns it to the bags", placement_of(owner_guid, MAILBOX) is None and owner.count_item(MAILBOX) == 1, joined(msgs))
    owner.use_item(MAILBOX, spell_of(MAILBOX), L["mailbox"])
    owner.gossip_select("Keep it here")
    check("keep it here leaves it standing", placement_of(owner_guid, MAILBOX) is not None)
    owner.command(".house adjust buildings")

    # The Bank Chest opens its owner's bank through a banker that only works by the chest.
    check("level 20 unlocked the Bank Chest too", BANK_CHEST in unlocked(owner_account, owner_guid))
    owner.command(".house collection bank chest")
    owner.gossip_select("Bank Chest")
    owner.gossip_select("Get one")
    wait_for(lambda: owner.count_item(BANK_CHEST) == 1, 3, owner)
    move(owner, L["chest_stand"][0], L["chest_stand"][1], L["ground"])
    msgs = owner.use_item(BANK_CHEST, spell_of(BANK_CHEST), L["chest"])
    check("a Bank Chest places like any piece", placement_of(owner_guid, BANK_CHEST) is not None, joined(msgs))
    owner.command(".house decorate off")  # decorating, a click opens the piece's own menu
    wait_for(lambda: nearest_go(owner, live(BANK_CHEST)) is not None, 3, owner)
    chest_go = nearest_go(owner, live(BANK_CHEST))
    menu, msgs = owner.use_gameobject(chest_go.guid) if chest_go else (None, [])
    check("clicking it offers the bank and House Storage", menu is not None and "Open my bank" in options(menu)
          and any(o.startswith("House Storage (") for o in options(menu)), joined(msgs) + " " + str(options(menu)))
    owner.bank_banker = None
    if menu:
        owner.gossip_select("Open my bank")
    wait_for(lambda: owner.bank_banker is not None, 3, owner)
    banker = owner.nearest(CHEST_BANKER, TYPEID_UNIT)
    check("the bank opens, with an unseen banker at the chest", owner.bank_banker is not None and banker is not None
          and owner.bank_banker == banker.guid and math.dist((banker.x, banker.y), L["chest"][:2]) < 1.0, str(owner.bank_banker))
    result = owner.buy_bank_slot(owner.bank_banker) if owner.bank_banker else None
    check("the bank works there (buying a bank slot)", result == BANKSLOT_OK, str(result))
    move(owner, *L["landing"])
    result = owner.buy_bank_slot(owner.bank_banker) if owner.bank_banker else None
    check("but not from across the island", result == BANKSLOT_NOTBANKER, str(result))
    owner.command(".house decorate on")

    # ------------------------------------------------------------- buildings
    log("== buildings")
    owner.command(".house collection")
    owner.gossip_select("Buildings (")
    owner.gossip_select("Westfall Farmhouse")
    _, msgs = owner.gossip_select("Get one")
    wait_for(lambda: owner.count_item(FARMHOUSE) == 1, 3, owner)
    move(owner, L["farmhouse_stand"][0], L["farmhouse_stand"][1], L["ground"])
    msgs = owner.use_item(FARMHOUSE, spell_of(FARMHOUSE), L["farmhouse"])
    farmhouse = placement_of(owner_guid, FARMHOUSE)
    check("a faction building places like furniture", farmhouse is not None and has(msgs, "buildings"), joined(msgs))
    check("placing a building opens its menu right away", "Keep it here" in options(owner.last_gossip), str(options(owner.last_gossip)))
    owner.gossip_select("Keep it here")
    wait_for(lambda: owner.count_item(CHAIR) == 0, 1, owner)
    owner.command(".house pickup %d" % placement_of(owner_guid, CHAIR)["id"])
    wait_for(lambda: owner.count_item(CHAIR) == 1, 3, owner)
    move(owner, L["inside"][0] - 2, L["inside"][1], L["inside"][2])
    owner.use_item(CHAIR, spell_of(CHAIR), L["inside"])
    # "Inside" is the building's outline, turned the way it faces: a lantern in a corner
    # (inside the outline, but farther from the middle than the old circle reached) counts;
    # a table just past the wall doesn't.
    house = placement_of(owner_guid, FARMHOUSE)

    def around_house(forward, left):
        o = house["o"]
        return (house["x"] + math.cos(o) * forward - math.sin(o) * left,
                house["y"] + math.sin(o) * forward + math.cos(o) * left, L["inside"][2])
    owner.use_item(LANTERN, spell_of(LANTERN), around_house(8.5, 8.5))
    owner.use_item(TABLE, spell_of(TABLE), around_house(13.0, 0.0))
    owner.command(".house")
    owner.gossip_select("Change a piece near me")
    _, _ = owner.gossip_select("Westfall Farmhouse")
    menu = owner.last_gossip
    check("a building's menu asks before picking up", "Pick up..." in options(menu), str(options(menu)))
    _, _ = owner.gossip_select("Pick up...")
    menu = owner.last_gossip
    check("choose the building only, or the building and what's inside",
          "Pick up the building only" in options(menu) and any("inside it" in o for o in options(menu)), str(options(menu)))
    check("inside follows the building's outline: the corner lantern counts, the table past the wall doesn't",
          any(o.startswith("Pick up the building and the 2 pieces inside it") for o in options(menu)), str(options(menu)))
    _, msgs = owner.gossip_select("Pick up the building and the")
    wait_for(lambda: owner.count_item(FARMHOUSE) == 1, 3, owner)
    check("the building and the chair inside come back", placement_of(owner_guid, FARMHOUSE) is None and placement_of(owner_guid, CHAIR) is None
          and owner.count_item(FARMHOUSE) == 1 and owner.count_item(CHAIR) == 1, joined(msgs))
    msgs = owner.command(".house undo")
    check("undo puts the building and the chair back", placement_of(owner_guid, FARMHOUSE) is not None and placement_of(owner_guid, CHAIR) is not None
          and placement_of(owner_guid, LANTERN) is not None and placement_of(owner_guid, TABLE) is not None, joined(msgs))

    # ------------------------------------------------------------- stands
    log("== mannequin")
    owner.command(".house collection")
    owner.gossip_select("Starter (")
    owner.gossip_select("Mannequin")
    owner.gossip_select("Get one")
    wait_for(lambda: owner.count_item(MANNEQUIN) == 1, 3, owner)
    move(owner, L["stand_stand"][0], L["stand_stand"][1], L["ground"])
    msgs = owner.use_item(MANNEQUIN, spell_of(MANNEQUIN), L["stand"])
    stand = placement_of(owner_guid, MANNEQUIN)
    stand_id = stand["id"] if stand else 0
    check("a mannequin places like any piece", stand is not None, joined(msgs))
    race, gender = [int(v) for v in db("SELECT race, gender FROM characters WHERE guid=%d" % owner_guid)[0]]
    look = db("SELECT look FROM mod_playerhousing_placement WHERE owner_guid=%d AND placement_id=%d" % (owner_guid, stand_id))
    check("a new mannequin takes after its owner", look == [[str(race | (gender << 8))]], str(look))

    def figure():
        wait_for(lambda: owner.nearest(MANNEQUIN_NPC, TYPEID_UNIT) is not None, 3, owner)
        return owner.nearest(MANNEQUIN_NPC, TYPEID_UNIT)

    fig = figure()
    looks = owner.mirror_image(fig.guid) if fig else None
    check("the figure is dressed by mirror image, as its owner's race", looks is not None and looks["race"] == race
          and looks["gender"] == gender and not any(looks["items"]), str(looks))

    admin.select(owner_char["guid"])
    admin.command(".additem %s %d 1" % (args.owner_char, SWORD), wait=1.5)
    admin.command(".additem %s %d 1" % (args.owner_char, PANTS), wait=1.5)
    wait_for(lambda: owner.count_item(SWORD) == 1 and owner.count_item(PANTS) == 1, 3, owner)
    sword_guid = item_guid_in_bags(owner, SWORD)
    pants_guid = item_guid_in_bags(owner, PANTS)

    fig = figure()
    menu, _ = owner.gossip_hello(fig.guid) if fig else (None, [])
    check("clicking the mannequin opens its menu", "Put gear on..." in options(menu)
          and any(o.startswith("Figure: Human") for o in options(menu)), str(options(menu)))
    owner.gossip_select("Put gear on...")
    menu = owner.last_gossip
    check("the dress menu lists the gear in the bags", any(o.startswith("Worn Shortsword (main hand") for o in options(menu))
          and any(o.startswith("Recruit's Pants (legs") for o in options(menu)), str(options(menu)))
    _, msgs = owner.gossip_select("Worn Shortsword")
    wait_for(lambda: owner.count_item(SWORD) == 0, 3, owner)
    gear = gear_of(owner_guid, stand_id)
    check("the sword leaves the bags for the mannequin, still the same item",
          owner.count_item(SWORD) == 0 and gear.get(SLOT_MAIN_HAND) == (sword_guid, SWORD)
          and db("SELECT COUNT(*) FROM item_instance WHERE guid=%d" % (sword_guid or 0)) == [["1"]], joined(msgs) + " " + str(gear))
    owner.pump(1.0)
    fig = figure()
    check("the figure holds the sword", fig is not None and fig.fields.get(UNIT_VIRTUAL_ITEM_SLOT_ID) == SWORD,
          str(fig.fields.get(UNIT_VIRTUAL_ITEM_SLOT_ID) if fig else None))
    check("the menu follows the figure", any(o.startswith("Take off Worn Shortsword") for o in options(owner.last_gossip)),
          str(options(owner.last_gossip)))

    owner.gossip_select("Put gear on...")
    _, msgs = owner.gossip_select("Recruit's Pants")
    wait_for(lambda: owner.count_item(PANTS) == 0, 3, owner)
    owner.pump(1.0)
    fig = figure()
    looks = owner.mirror_image(fig.guid) if fig else None
    check("the figure wears the pants", looks is not None and looks["items"][5] == PANTS_DISPLAY, str(looks))

    msgs = owner.command(".house undo")
    wait_for(lambda: owner.count_item(PANTS) == 1, 3, owner)
    check("undo gives the pants back, the very same item", item_guid_in_bags(owner, PANTS) == pants_guid
          and SLOT_LEGS not in gear_of(owner_guid, stand_id), joined(msgs))
    msgs = owner.command(".house redo")
    wait_for(lambda: owner.count_item(PANTS) == 0, 3, owner)
    check("redo puts them back on", gear_of(owner_guid, stand_id).get(SLOT_LEGS) == (pants_guid, PANTS), joined(msgs))

    fig = figure()
    owner.gossip_hello(fig.guid)
    _, msgs = owner.gossip_select("Take off Worn Shortsword")
    wait_for(lambda: owner.count_item(SWORD) == 1, 3, owner)
    check("taking the sword off puts it back in the bags", owner.count_item(SWORD) == 1
          and SLOT_MAIN_HAND not in gear_of(owner_guid, stand_id), joined(msgs))

    msgs = owner.command(".house pickup %d" % stand_id)
    wait_for(lambda: owner.count_item(PANTS) == 1 and owner.count_item(MANNEQUIN) == 1, 3, owner)
    check("picking the mannequin up returns it and its gear", placement_of(owner_guid, MANNEQUIN) is None
          and owner.count_item(MANNEQUIN) == 1 and item_guid_in_bags(owner, PANTS) == pants_guid, joined(msgs))
    msgs = owner.command(".house undo")
    wait_for(lambda: owner.count_item(PANTS) == 0, 3, owner)
    check("undo puts the mannequin back, dressed", placement_of(owner_guid, MANNEQUIN) is not None
          and gear_of(owner_guid, stand_id).get(SLOT_LEGS) == (pants_guid, PANTS), joined(msgs))

    # Moving with the circle: a Move a Piece item, then the new spot. Same facing, same gear.
    stand = placement_of(owner_guid, MANNEQUIN)
    msgs = owner.command(".house move %d" % stand_id)
    wait_for(lambda: any(owner.count_item(e) for e in MOVERS), 3, owner)
    mover = next((e for e in MOVERS if owner.count_item(e)), None)
    check("choosing Move hands out a Move a Piece item", mover is not None and has(msgs, "Right-click Move a Piece"), joined(msgs))
    new_spot = (L["stand"][0] - 2.0, L["stand"][1] + 2.0, L["ground"])
    msgs = owner.use_item(mover, spell_of(mover), new_spot) if mover else []
    moved = placement_of(owner_guid, MANNEQUIN)
    check("it moves to the clicked spot, facing the same way, still dressed", moved is not None
          and math.dist((moved["x"], moved["y"]), new_spot[:2]) < 0.01 and angle_diff(moved["o"], stand["o"]) < 0.01
          and gear_of(owner_guid, stand_id).get(SLOT_LEGS) == (pants_guid, PANTS), joined(msgs) + " " + str(moved))
    wait_for(lambda: not any(owner.count_item(e) for e in MOVERS), 3, owner)
    check("the Move a Piece item is used up", not any(owner.count_item(e) for e in MOVERS))
    msgs = owner.command(".house undo")
    back = placement_of(owner_guid, MANNEQUIN)
    check("undo moves it back", back is not None and math.dist((back["x"], back["y"]), (stand["x"], stand["y"])) < 0.01, joined(msgs))

    # Bags full: what comes off is mailed, never lost.
    admin.select(owner_char["guid"])
    admin.command(".additem %s %d 20" % (args.owner_char, WORN_DAGGER), wait=2.0)
    owner.pump(1.5)
    fig = figure()
    owner.gossip_hello(fig.guid)
    _, msgs = owner.gossip_select("Take off Recruit's Pants")
    owner.pump(1.0)
    check("with full bags, gear taken off is mailed to the owner", has(msgs, "mailed")
          and db("SELECT COUNT(*) FROM mail_items WHERE item_guid=%d AND receiver=%d" % (pants_guid or 0, owner_guid)) == [["1"]], joined(msgs))
    admin.select(owner_char["guid"])
    admin.command(".additem %s %d -%d" % (args.owner_char, WORN_DAGGER, owner.count_item(WORN_DAGGER)), wait=2.0)
    wait_for(lambda: owner.count_item(WORN_DAGGER) == 0, 4, owner)

    # The sword goes back on for the visitors.
    fig = figure()
    owner.gossip_hello(fig.guid)
    owner.gossip_select("Put gear on...")
    owner.gossip_select("Worn Shortsword")
    wait_for(lambda: owner.count_item(SWORD) == 0, 3, owner)

    # ------------------------------------------------------------- saved layouts
    log("== saved layouts")
    count = len(placements(owner_guid))
    msgs = owner.command(".house layout save Test Corner")
    saved = db("SELECT COUNT(*) FROM mod_playerhousing_saved_piece WHERE owner_guid=%d" % owner_guid)
    check("save the island as a layout", has(msgs, "Saved your island as Test Corner (%d pieces)" % count) and saved == [[str(count)]],
          joined(msgs) + " " + str(saved))
    chair = placement_of(owner_guid, CHAIR)
    owner.command(".house pickup %d" % chair["id"])
    wait_for(lambda: owner.count_item(CHAIR) == 1, 3, owner)
    owner.command(".house")
    owner.gossip_select("Saved layouts")
    check("the saved layouts menu lists it", any(o.startswith("Test Corner (%d pieces" % count) for o in options(owner.last_gossip)),
          str(options(owner.last_gossip)))
    owner.gossip_select("Test Corner")
    menu = owner.last_gossip
    check("a layout's page: set it out, save over it, rename, send, delete",
          all(o in options(menu) for o in ("Set it out on my island", "Save my island over it", "Rename it...", "Send a copy to a player...", "Delete it")),
          str(options(menu)))
    _, msgs = owner.gossip_select("Set it out on my island")
    wait_for(lambda: owner.count_item(CHAIR) == 0, 3, owner)
    back = placement_of(owner_guid, CHAIR)
    check("setting it out puts everything where it was", len(placements(owner_guid)) == count and back is not None
          and math.dist((back["x"], back["y"]), (chair["x"], chair["y"])) < 0.01 and has(msgs, "Set out Test Corner: %d pieces" % count),
          joined(msgs))
    wait_for(lambda: owner.count_item(SWORD) == 1, 3, owner)
    check("gear on the mannequin comes back to the bags", owner.count_item(SWORD) == 1, str(owner.backpack()))
    msgs = owner.command(".house undo")
    wait_for(lambda: owner.count_item(SWORD) == 0, 3, owner)
    stand = placement_of(owner_guid, MANNEQUIN)
    check("one undo puts the island back, the sword on the mannequin again", placement_of(owner_guid, CHAIR) is None and stand is not None
          and SLOT_MAIN_HAND in gear_of(owner_guid, stand["id"]) and has(msgs, "Undid: set out Test Corner"), joined(msgs))
    move(owner, chair["x"] - 2.0, chair["y"], chair["z"])
    owner.use_item(CHAIR, spell_of(CHAIR), (chair["x"], chair["y"], chair["z"]))

    owner.command(".house layout")
    owner.gossip_select("Test Corner")
    _, msgs = owner.gossip_select("Rename it...", code="Krook's Corner")
    check("rename (names can have quotes)", db("SELECT name FROM mod_playerhousing_saved_layout WHERE owner_guid=%d" % owner_guid) == [["Krook's Corner"]],
          joined(msgs))
    msgs = owner.command(".house layout send krook %s" % args.guest_char)
    check("layouts don't go to strangers", has(msgs, "none of those") and not db(
          "SELECT 1 FROM mod_playerhousing_saved_layout WHERE owner_guid=%d" % guest_guid), joined(msgs))
    db("INSERT INTO character_social (guid, friend, flags, note) VALUES (%d, %d, 1, '')" % (guest_guid, owner_guid))
    msgs = owner.command(".house layout send krook %s" % args.guest_char)
    check("but do go to a friend who has you on their list", db("SELECT name, source FROM mod_playerhousing_saved_layout WHERE owner_guid=%d" % guest_guid)
          == [["Krook's Corner (from %s)" % args.owner_char, args.owner_char]]
          and db("SELECT COUNT(*) FROM mod_playerhousing_saved_piece WHERE owner_guid=%d" % guest_guid) == [[str(count)]], joined(msgs))
    db("DELETE FROM character_social WHERE guid=%d AND friend=%d" % (guest_guid, owner_guid))

    owner.command(".house")
    owner.gossip_select("Island settings")
    _, msgs = owner.gossip_select("Visitors may copy my layout")
    check("visitors may copy the layout", has(msgs, "Visitors can now save a copy"), joined(msgs))

    # ------------------------------------------------------------- ambience
    log("== ambience")
    owner.command(".house decorate off")  # decorating, a click opens the piece's own menu
    owner.command(".house")
    owner.gossip_select("Island settings")
    owner.gossip_select("Island ambience")
    menu = owner.last_gossip
    check("island ambience: weather, time of day, and music once there's a Music Box",
          "Weather: clear (click to change)" in options(menu) and "Time of day: the server's time (click to change)" in options(menu)
          and "Music: place a Music Box to choose some" in options(menu), str(options(menu)))
    for _ in range(3):
        owner.gossip_select("Weather:")
    wait_for(lambda: owner.weather is not None and owner.weather[0] == WEATHER_RAIN, 3, owner)
    check("rain on the island", owner.weather is not None and owner.weather[0] == WEATHER_RAIN
          and db("SELECT weather FROM mod_playerhousing_house WHERE owner_guid=%d" % owner_guid) == [["3"]], str(owner.weather))
    for _ in range(4):
        owner.gossip_select("Time of day:")
    wait_for(lambda: owner.clock == (0, 0), 3, owner)
    check("always night on the island", owner.clock == (0, 0) and "Time of day: night (click to change)" in options(owner.last_gossip),
          str(owner.clock) + " " + str(options(owner.last_gossip)))

    owner.command(".house collection music box")
    owner.gossip_select("Music Box")
    owner.gossip_select("Get one")
    wait_for(lambda: owner.count_item(MUSIC_BOX) == 1, 3, owner)
    move(owner, L["music_stand"][0], L["music_stand"][1], L["ground"])
    owner.use_item(MUSIC_BOX, spell_of(MUSIC_BOX), L["music"])
    wait_for(lambda: nearest_go(owner, live(MUSIC_BOX)) is not None, 3, owner)
    music_go = nearest_go(owner, live(MUSIC_BOX))
    menu, msgs = owner.use_gameobject(music_go.guid) if music_go else (None, [])
    check("the Music Box lists tunes", menu is not None and "Grizzly Hills" in options(menu) and options(menu)[0] == "Music Box: silent",
          joined(msgs) + " " + str(options(menu)))
    owner.music.clear()
    if menu:
        owner.gossip_select("Grizzly Hills")
    wait_for(lambda: GRIZZLY_HILLS in owner.music, 3, owner)
    check("and plays one for the island", GRIZZLY_HILLS in owner.music
          and db("SELECT music FROM mod_playerhousing_house WHERE owner_guid=%d" % owner_guid) == [[str(GRIZZLY_HILLS)]], str(owner.music))

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

    # Out of decorate mode, so guests find working furniture.
    owner.command(".house decorate off")
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
    check("and gets the island's rain, night and music", guest.weather is not None and guest.weather[0] == WEATHER_RAIN
          and guest.clock == (0, 0) and GRIZZLY_HILLS in guest.music, "%s %s %s" % (guest.weather, guest.clock, guest.music))
    music_go = nearest_go(guest, live(MUSIC_BOX))
    if music_go:
        stand_next_to(guest, music_go, 2.0)
        menu, msgs = guest.use_gameobject(music_go.guid)
        check("a guest hears what the music box plays", menu is None and has(msgs, "playing Grizzly Hills"), joined(msgs))
    msgs = guest.command(".house undo")
    check("guests can't change anything", has(msgs, "Only the owner"), joined(msgs))
    chest_go = nearest_go(guest, live(BANK_CHEST))
    if chest_go:
        stand_next_to(guest, chest_go, 2.0)
        menu, msgs = guest.use_gameobject(chest_go.guid)
        check("a guest finds the Bank Chest locked", menu is None and has(msgs, "The chest is locked"), joined(msgs) + " " + str(options(menu)))
    else:
        check("a guest finds the Bank Chest locked", False, "no chest in sight")
    guest.command(".house")
    _, msgs = guest.gossip_select("Save a copy of this island's layout")
    copied = db("SELECT l.name, COUNT(p.placement_id) FROM mod_playerhousing_saved_layout l JOIN mod_playerhousing_saved_piece p "
                "ON p.owner_guid = l.owner_guid AND p.layout_id = l.layout_id WHERE l.owner_guid=%d AND l.source='%s' AND l.name LIKE '%%island' "
                "GROUP BY l.name" % (guest_guid, args.owner_char))
    check("a visitor saves a copy of the island's layout", copied == [["%s's island" % args.owner_char, str(len(placements(owner_guid)))]],
          joined(msgs) + " " + str(copied))
    guest.command(".house layout")
    guest.gossip_select("%s's island" % args.owner_char)
    menu = guest.last_gossip
    check("its page offers the missing pieces the visitor has unlocked, and says what's locked",
          any(o.startswith("Get the ") and "missing" in o for o in options(menu)) and any("still locked" in o for o in options(menu))
          and "Go home to set it out" in options(menu), str(options(menu)))
    _, msgs = guest.gossip_select("Get the ")
    wait_for(lambda: guest.count_item(MANNEQUIN) >= 1, 3, guest)
    check("and gets them", guest.count_item(MANNEQUIN) >= 1 and has(msgs, "Got "), joined(msgs))

    # Roommates: a guest the owner lets decorate. Their pieces stay theirs.
    mark = guest.message_mark()
    msgs = owner.command(".house roommate %s" % args.guest_char)
    guest.pump(1.0)
    check("the owner makes the guest a roommate, who is told", has(msgs, "is now a roommate") and has(guest.messages_since(mark), "made you a roommate")
          and db("SELECT roommate FROM mod_playerhousing_acl WHERE owner_guid=%d AND guest_guid=%d" % (owner_guid, guest_guid)) == [["1"]],
          joined(msgs) + " / " + joined(guest.messages_since(mark)))
    guest.command(".house")
    check("a roommate's Home menu offers decorating", "Start decorating" in options(guest.last_gossip)
          and options(guest.last_gossip)[0] == "You're a roommate on %s's island" % args.owner_char, str(options(guest.last_gossip)))
    msgs = guest.command(".house decorate on")
    check("a roommate can decorate", has(msgs, "Decorating"), joined(msgs))
    before = {p["id"] for p in placements(owner_guid)}
    guest_chairs = guest.count_item(CHAIR)
    spot = (guest.pos[0] + 2.0, guest.pos[1], guest.pos[2])
    msgs = guest.use_item(CHAIR, spell_of(CHAIR), spot)
    placed = [p for p in placements(owner_guid) if p["id"] not in before]
    placed_by = db("SELECT placed_by FROM mod_playerhousing_placement WHERE owner_guid=%d AND placement_id=%d" % (owner_guid, placed[0]["id"])) if placed else []
    check("a roommate places their own chair on the owner's island", len(placed) == 1 and placed[0]["item"] == CHAIR
          and placed_by == [[str(guest_guid)]], joined(msgs) + " " + str(placed_by))
    table = placement_of(owner_guid, TABLE)
    msgs = guest.command(".house nudge forward 0.25 %d" % table["id"]) if table else []
    moved = placement_of(owner_guid, TABLE)
    check("and moves the owner's pieces", moved is not None and math.dist((moved["x"], moved["y"]), (table["x"], table["y"])) > 0.2
          and has(msgs, "Nudged Tiny Table"), joined(msgs))
    msgs = guest.command(".house undo")
    back = placement_of(owner_guid, TABLE)
    check("with their own undo", back is not None and math.dist((back["x"], back["y"]), (table["x"], table["y"])) < 0.01, joined(msgs))
    msgs = guest.command(".house packup")
    check("but can't pack up the island", has(msgs, "Only the island's owner") and len(placements(owner_guid)) == len(before) + 1, joined(msgs))
    if placed:
        msgs = owner.command(".house pickup %d" % placed[0]["id"])
        check("the owner picking up a roommate's piece sends it to the roommate's House Storage", storage(guest_guid).get(CHAIR) == 1
              and has(msgs, "House Storage of whoever placed it"), joined(msgs) + " " + str(storage(guest_guid)))
        msgs = owner.command(".house undo")
        check("and undo takes it back out", placement_of(owner_guid, CHAIR) is not None and not storage(guest_guid).get(CHAIR)
              and len(placements(owner_guid)) == len(before) + 1, joined(msgs))
        mine = [p for p in placements(owner_guid) if p["id"] not in before]
        msgs = guest.command(".house pickup %d" % mine[0]["id"]) if mine else []
        wait_for(lambda: guest.count_item(CHAIR) == guest_chairs, 3, guest)
        check("a roommate picking up their own piece gets it back in their bags", guest.count_item(CHAIR) == guest_chairs, joined(msgs))
    guest.command(".house decorate off")

    # Likes and the visitor log.
    guest.command(".house")
    check("a visitor can like the island", "Like this island (0 likes)" in options(guest.last_gossip), str(options(guest.last_gossip)))
    mark = owner.message_mark()
    _, msgs = guest.gossip_select("Like this island")
    owner.pump(0.5)
    check("and does, and the owner hears of it", "You like this island (1 like): take it back" in options(guest.last_gossip)
          and has(owner.messages_since(mark), "likes your island")
          and db("SELECT liker_guid FROM mod_playerhousing_like WHERE owner_guid=%d" % owner_guid) == [[str(guest_guid)]],
          joined(msgs) + " " + str(options(guest.last_gossip)))
    msgs = owner.command(".house like")
    check("owners can't like their own island", has(msgs, "can't like your own"), joined(msgs))
    owner.command(".house")
    owner.gossip_select("Island settings")
    owner.gossip_select("Visitor log (1 this week, 1 likes)")
    check("the visitor log lists the guest", any(o.startswith(args.guest_char + ", ") for o in options(owner.last_gossip)),
          str(options(owner.last_gossip)))

    msgs = owner.command(".house unroommate %s" % args.guest_char)
    msgs = guest.command(".house nudge forward 0.25 %d" % table["id"]) if table else []
    check("once a guest again, no more changes", has(msgs, "Only the owner"), joined(msgs))
    chair_go = nearest_go(guest, live(CHAIR))
    if chair_go:
        stand_next_to(guest, chair_go, 1.5)
        menu, msgs = guest.use_gameobject(chair_go.guid)
        stand_state = guest.stand_state
        check("a guest clicking a chair sits in it, no menu", menu is None and stand_state in SIT_ON_CHAIR
              and math.dist(guest.pos[:2], (chair_go.x, chair_go.y)) < 1.0,
              "stand state %d, pos %s, chair (%.1f, %.1f) %s" % (stand_state, guest.pos, chair_go.x, chair_go.y, joined(msgs)))
    else:
        check("a guest clicking a chair sits in it, no menu", False, "no chair in sight: %s" % sorted(go_entries(guest)))
    figure_npc = guest.nearest(MANNEQUIN_NPC, TYPEID_UNIT)
    if figure_npc:
        stand_next_to(guest, figure_npc, 2.0)
        menu, _ = guest.gossip_hello(figure_npc.guid)
        check("a guest sees what the mannequin wears, and can't change it", "main hand: Worn Shortsword" in options(menu)
              and "Put gear on..." not in options(menu), str(options(menu)))
    else:
        check("a guest sees what the mannequin wears, and can't change it", False, "no mannequin in sight")
    mailbox_go = nearest_go(guest, live(MAILBOX))
    if mailbox_go:
        stand_next_to(guest, mailbox_go, 2.0)
        opened = guest.mailbox_opened
        guest.open_mailbox(mailbox_go.guid)
        check("a guest can use the owner's mailbox", guest.mailbox_opened > opened, "mailbox at (%.1f, %.1f)" % (mailbox_go.x, mailbox_go.y))
    else:
        check("a guest can use the owner's mailbox", False, "no mailbox in sight: %s" % sorted(go_entries(guest)))

    guest.command(".house leave", wait=1.0)
    wait_for(lambda: guest.map_id != HOUSING_MAP or not guest.find_objects(entry=live(FARMHOUSE)), 8, guest)
    wait_for(lambda: guest.clock != (0, 0), 3, guest)
    now = time.localtime()
    check("leaving brings back the real clock and weather", guest.clock is not None and guest.clock[0] in (now.tm_hour, (now.tm_hour - 1) % 24)
          and guest.weather is not None and guest.weather[0] == WEATHER_FINE, "%s %s" % (guest.clock, guest.weather))
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

    guest.command(".house visit")
    check("the visit menu has the most liked islands", "Most liked islands (1)" in options(guest.last_gossip), str(options(guest.last_gossip)))
    guest.gossip_select("Most liked islands")
    check("with their likes", "Visit %s's island (1 like)" % args.owner_char in options(guest.last_gossip), str(options(guest.last_gossip)))

    # ------------------------------------------------------------- pack up, unstuck, relog
    log("== pack up, unstuck, relog")
    count = len(placements(owner_guid))
    owner.command(".house decorate on")
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

    owner.command(".house move")  # left unfinished: its item goes at the next login
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
    check("an unfinished move's item is gone after logging back in", not any(owner.count_item(e) for e in MOVERS),
          str(owner.backpack()))

    visits_mark = owner.message_mark()
    owner.command(".house home", wait=1.0)
    wait_for_map(owner, HOUSING_MAP)
    wait_for(lambda: owner.nearest(MANNEQUIN_NPC, TYPEID_UNIT) is not None, 6, owner)
    wait_for(lambda: has(owner.messages_since(visits_mark), "since you were last home"), 3, owner)
    fig = owner.nearest(MANNEQUIN_NPC, TYPEID_UNIT)
    check("coming home, the owner hears who visited", has(owner.messages_since(visits_mark), "since you were last home"),
          joined(owner.messages_since(visits_mark)[-10:]))
    check("back home, the mannequin still holds the sword", fig is not None and fig.fields.get(UNIT_VIRTUAL_ITEM_SLOT_ID) == SWORD,
          str(fig.fields.get(UNIT_VIRTUAL_ITEM_SLOT_ID) if fig else None))

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
