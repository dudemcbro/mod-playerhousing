#!/usr/bin/env python3
"""End-to-end test for the Collection keeping count (no furnishing items in the bags), the
housing window as the only way in (no menus), and ghosts a client moves itself.

Expects the same server and accounts as housing_smoke.py (FreeMode on, the cleared island):
houseowner and houseguest (players) and admin (GM). The test characters' housing state is
reset at the start of every run.
"""

import argparse
import math
import sys
import time

from housing_smoke import (BANK_CHEST, CHAIR, ELWYNN_ACHIEVEMENT, FARMHOUSE, STORMWIND, GHOST_NPC, HOUSE_KEY, KEY_SPELL, LAMP_POST, LANTERN, LAYOUTS, MANNEQUIN,
                           MOVERS, MUSIC_BOX, QUEST_HOME, RAZORFEN_LEANTO, STEWARD, SWORD, SLOT_MAIN_HAND, TABLE, TYPEID_GAMEOBJECT,
                           TYPEID_UNIT, addon_list, addon_state, check, connect, db, finish, gear_of, get_or_create_char, has, joined,
                           live, log, move, placement_of, placements, results, shape_of, spell_of, stand_next_to, storage, unlocked, wait_for,
                           wait_for_map)

L = LAYOUTS["cleared"]
RUINED_TOWER = 902214
WESTFALL_SHED = 902210
HOUSING_MAP = 1


def addon_since(wc, mark, prefix):
    """Addon messages (from the server's whispers) since mark that start with prefix."""
    return [m for m in wc.addon_messages[mark:] if m.startswith(prefix)]


def ghost_pieces(wc, mark):
    """The last full list of ghost pieces the server sent since mark: [(guid, dx, dy, dz, dO)]."""
    pieces = {}
    count = 0
    for m in addon_since(wc, mark, "HOUSING\tgpiece\t"):
        fields = m.split("\t")
        index, count = int(fields[2]), int(fields[3])
        guid = (int(fields[4], 16) << 32) | int(fields[5], 16)
        pieces[index] = (guid, float(fields[6]), float(fields[7]), float(fields[8]), float(fields[9]))
    return [pieces[i] for i in range(1, count + 1) if i in pieces]


# Beside Krook by the innkeeper in Stormwind, where characters get their House Key.
KROOK_SW = (-8862.90, 672.10, 98.00)


def key_from_krook(wc, option):
    """Talk to the Krook in sight and take the House Key option; returns (menu, messages, after)."""
    wait_for(lambda: wc.nearest(STEWARD, TYPEID_UNIT) is not None, 10, wc)
    krook = wc.nearest(STEWARD, TYPEID_UNIT)
    if not krook:
        return None, [], []
    menu, msgs = wc.gossip_hello(krook.guid)
    if not menu or not any(i["text"].startswith(option) for i in menu["items"]):
        return menu, msgs, []
    _, after = wc.gossip_select(option)
    return menu, msgs, after


def last_pose(wc, mark):
    """The last ghost pose since mark: dict(x, y, z, o, lift, turn, wall_turn, zfix, on_wall)."""
    poses = addon_since(wc, mark, "HOUSING\tgpose\t")
    if not poses:
        return None
    f = poses[-1].split("\t")[2:]
    return dict(x=float(f[0]), y=float(f[1]), z=float(f[2]), o=float(f[3]), lift=float(f[4]), turn=float(f[5]),
                wall_turn=float(f[6]), zfix=float(f[7]), on_wall=f[8] == "1")


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
    # One island per account: the owner's account keeps it under Krookowner; a second character
    # of the account (made below) shares it.
    alt_rows = db("SELECT guid FROM characters WHERE name='Krookalt'")
    alt_ids = ",".join(r[0] for r in alt_rows) or "0"
    for table, column in (("mod_playerhousing_house", "owner_guid"), ("mod_playerhousing_storage", "owner_guid"),
                          ("mod_playerhousing_placement", "owner_guid"), ("mod_playerhousing_character", "guid")):
        db("DELETE FROM %s WHERE %s IN (%s)" % (table, column, alt_ids))
    guest_account = int(db("SELECT account FROM characters WHERE guid=%d" % guest_guid)[0][0])
    db("REPLACE INTO mod_playerhousing_account (account_id, home_guid) VALUES (%d, %d), (%d, %d)"
       % (owner_account, owner_guid, guest_account, guest_guid))

    wait_for(lambda: db("SELECT COUNT(*) FROM characters WHERE online=1 AND guid IN (%s)" % ids) == [["0"]], 60)
    for table, column in (("mod_playerhousing_placement", "owner_guid"), ("mod_playerhousing_acl", "owner_guid"),
                          ("mod_playerhousing_acl", "guest_guid"), ("mod_playerhousing_house", "owner_guid"),
                          ("mod_playerhousing_storage", "owner_guid"), ("mod_playerhousing_character", "guid"),
                          ("mod_playerhousing_collection", "guid"), ("mod_playerhousing_saved_layout", "owner_guid"),
                          ("mod_playerhousing_saved_piece", "owner_guid"), ("mod_playerhousing_set", "owner_guid"),
                          ("mod_playerhousing_set_piece", "owner_guid"), ("mod_playerhousing_placement_gear", "owner_guid")):
        db("DELETE FROM %s WHERE %s IN (%s)" % (table, column, ids))
    db("DELETE FROM mod_playerhousing_collection WHERE account_id IN (SELECT account FROM characters WHERE guid IN (%s))" % ids)
    db("DELETE FROM character_queststatus WHERE guid IN (%s) AND quest BETWEEN 900400 AND 900404" % ids)
    db("DELETE FROM character_queststatus_rewarded WHERE guid IN (%s) AND quest BETWEEN 900400 AND 900404" % ids)
    test_items = "itemEntry BETWEEN 901100 AND 902999 OR itemEntry = %d" % SWORD
    db("DELETE FROM character_inventory WHERE guid IN (%s) AND item IN (SELECT guid FROM item_instance WHERE %s)" % (ids, test_items))
    db("DELETE FROM item_instance WHERE owner_guid IN (%s) AND (%s)" % (ids, test_items))
    db("UPDATE characters SET level=15, money=1000000 WHERE guid=%d" % owner_guid)
    # A veteran: explored Elwynn and Exalted with Stormwind, so past progress unlocks pieces.
    db("DELETE FROM character_achievement WHERE guid=%d AND achievement=%d" % (owner_guid, ELWYNN_ACHIEVEMENT))
    db("INSERT INTO character_achievement (guid, achievement, date) VALUES (%d, %d, UNIX_TIMESTAMP())" % (owner_guid, ELWYNN_ACHIEVEMENT))
    db("REPLACE INTO character_reputation (guid, faction, standing, flags) VALUES (%d, %d, 42999, 1)" % (owner_guid, STORMWIND))
    db("UPDATE characters SET map=0, position_x=-8949.95, position_y=-132.49, position_z=83.53, orientation=0 "
       "WHERE guid IN (%s, %d)" % (ids, admin_char["guid"] & 0xFFFFFFFF))
    db("UPDATE characters SET position_x=%.2f, position_y=%.2f, position_z=%.2f WHERE guid=%d" % (KROOK_SW + (owner_guid,)))

    # ------------------------------------------------------------- first login
    log("== first login: pieces are counts, not items")
    owner.login(owner_char["guid"])
    owner.pump(2.0)
    check("first login: no House Key yet", owner.count_item(HOUSE_KEY) == 0, str(owner.backpack()))
    msgs = owner.command(".house home")
    check("no key, no way home: Krook has one", has(msgs, "Krook") and owner.map_id == 0, joined(msgs))
    msgs = owner.command(".house key")
    check(".house key sends players to Krook", has(msgs, "Krook") and owner.count_item(HOUSE_KEY) == 0, joined(msgs))
    menu, hello, msgs = key_from_krook(owner, "I'd like a house")
    check("Krook by the Stormwind innkeeper offers a house", menu is not None and msgs is not None and any(
        i["text"].startswith("I'd like a house") for i in menu["items"]), str(menu))
    wait_for(lambda: owner.count_item(HOUSE_KEY) == 1, 10, owner)
    owner.pump(1.0)
    msgs = owner.messages_since(0)
    check("Krook gives the key: told about the house and the window", has(msgs, "You have a house") and has(msgs, "housing window"), joined(msgs))
    check("first login: the House Key is the one housing item in the bags",
          owner.count_item(HOUSE_KEY) == 1 and not any(owner.count_item(i) for i in (CHAIR, TABLE, LANTERN)), str(owner.backpack()))
    have = storage(owner_guid)
    check("first login: chair, table and lantern wait in the Collection", all(have.get(i) == 1 for i in (CHAIR, TABLE, LANTERN)), str(have))
    check("past progress unlocks give one of each to place",
          all(have.get(i) == 1 for i in (LAMP_POST, RAZORFEN_LEANTO, FARMHOUSE, MUSIC_BOX)), str(have))

    # Without the addon there's no window to open: the command says so.
    msgs = owner.command(".house")
    check("without the addon, .house says the window needs it", has(msgs, "addon"), joined(msgs))

    # The addon says it's there (with a DLL that moves ghosts), as the real one does.
    owner.addon_command("house addon 2 mouse local", owner_char["name"], wait=1.0)
    mark = len(owner.addon_messages)
    owner.command(".house")
    check(".house opens the housing window", addon_since(owner, mark, "HOUSING\topen"), str(owner.addon_messages[mark:]))
    mark = len(owner.addon_messages)
    msgs = owner.use_item(HOUSE_KEY, KEY_SPELL)
    check("the House Key opens the window too, no menu", addon_since(owner, mark, "HOUSING\topen") and owner.last_gossip is None,
          joined(msgs) + str(owner.addon_messages[mark:]))
    check("the House Key stays in the bags", owner.count_item(HOUSE_KEY) == 1)

    # A lost key: Krook has another (and no second set of first pieces).
    krook_sw = owner.nearest(STEWARD, TYPEID_UNIT)
    menu, _ = owner.gossip_hello(krook_sw.guid) if krook_sw else (None, None)
    check("with a key, Krook offers no other", krook_sw is not None and not (menu and any("House Key" in i["text"] for i in menu["items"])), str(menu))
    owned = storage(owner_guid)
    owner.destroy_item(HOUSE_KEY)
    wait_for(lambda: owner.count_item(HOUSE_KEY) == 0, 5, owner)
    menu, _, msgs = key_from_krook(owner, "I've lost my House Key")
    wait_for(lambda: owner.count_item(HOUSE_KEY) == 1, 5, owner)
    check("a lost key: Krook gives another", owner.count_item(HOUSE_KEY) == 1 and storage(owner_guid) == owned, "%s %s" % (menu, joined(msgs)))

    # ------------------------------------------------------------- old items join the counts
    log("== furnishing items from before")
    admin.login(admin_char["guid"])
    admin.pump(1.0)
    admin.command(".additem %s %d 2" % (args.owner_char, CHAIR), wait=1.5)
    admin.command(".additem %s %d 1" % (args.owner_char, MOVERS[0]), wait=1.5)
    wait_for(lambda: owner.count_item(CHAIR) == 2 and owner.count_item(MOVERS[0]) == 1, 4, owner)
    check("(setup) two old chairs and a Move a Piece in the bags", owner.count_item(CHAIR) == 2 and owner.count_item(MOVERS[0]) == 1,
          str(owner.backpack()))
    mark = owner.message_mark()
    owner.addon_command("house data collection", owner_char["name"], wait=1.5)
    wait_for(lambda: owner.count_item(CHAIR) == 0, 3, owner)
    msgs = owner.messages_since(mark)
    check("opening the Collection turns them into counts", owner.count_item(CHAIR) == 0 and storage(owner_guid).get(CHAIR) == 3,
          "%s %s" % (owner.backpack(), storage(owner_guid)))
    check("and the Move a Piece just goes", owner.count_item(MOVERS[0]) == 0, str(owner.backpack()))
    check("the player is told where they went", has(msgs, "Collection"), joined(msgs))
    rows = addon_list(owner, "collection") or []
    stored = next((r for r in rows if r[0] == "storage"), None)
    check("the Collection list carries the counts", stored is not None and ("%d:3" % CHAIR) in stored[1], str(rows))

    admin.command(".additem %s %d 1" % (args.owner_char, LANTERN), wait=1.5)
    wait_for(lambda: owner.count_item(LANTERN) == 1, 3, owner)
    mark = len(owner.addon_messages)
    # The targeting circle's spot is near the player, as the client would have it.
    msgs = owner.use_item(LANTERN, spell_of(LANTERN), (owner.pos[0] + 2.0, owner.pos[1], owner.pos[2]))
    check("using an old furnishing item puts it in the Collection instead",
          owner.count_item(LANTERN) == 0 and storage(owner_guid).get(LANTERN) == 2 and addon_since(owner, mark, "HOUSING\topen\tCollection"),
          joined(msgs) + " " + str(storage(owner_guid)))

    # ------------------------------------------------------------- home, Krook
    log("== home")
    owner.command(".house home")
    home = wait_for_map(owner, HOUSING_MAP)
    check("going home from the window's command", home and math.dist(owner.pos[:2], L["landing"][:2]) < 5, str(owner.pos))
    if not home:
        return finish()
    wait_for(lambda: owner.nearest(STEWARD, TYPEID_UNIT) is not None, 6, owner)
    krook = owner.nearest(STEWARD, TYPEID_UNIT)
    if krook:
        stand_next_to(owner, krook, 2.0)
    mark = len(owner.addon_messages)
    menu, _ = owner.gossip_hello(krook.guid) if krook else (None, None)
    check("Krook offers his tour's quests and nothing else, and opens the window",
          menu is not None and any(q["id"] == QUEST_HOME for q in menu["quests"]) and not menu["items"]
          and addon_since(owner, mark, "HOUSING\topen"), str(menu))

    # ------------------------------------------------------------- placing from the Collection
    log("== placing")
    move(owner, *L["landing"])
    spot = (L["chair"][0], L["chair"][1], L["ground"])
    owner.command(".house ghost %d" % CHAIR)
    state = addon_state(owner)
    check("a chair from the Collection follows the owner", state is not None and int(state[21]) == CHAIR, str(state))
    msgs = owner.command(".house ghost place at %.2f %.2f %.2f" % spot)
    chair = placement_of(owner_guid, CHAIR)
    check("set down where the mouse pointed", chair is not None and math.dist((chair["x"], chair["y"]), spot[:2]) < 0.05, joined(msgs))
    check("one chair fewer in the Collection, none in the bags", storage(owner_guid).get(CHAIR) == 2 and owner.count_item(CHAIR) == 0,
          str(storage(owner_guid)))
    msgs = owner.command(".house undo")
    check("undo puts it back in the Collection", placement_of(owner_guid, CHAIR) is None and storage(owner_guid).get(CHAIR) == 3, joined(msgs))
    owner.command(".house redo")
    check("redo takes it out again", placement_of(owner_guid, CHAIR) is not None and storage(owner_guid).get(CHAIR) == 2,
          str(storage(owner_guid)))

    # A building the owner has one of (unlocked by past progress), then a second one: bought (free here).
    lean_spot = (L["farmhouse"][0], L["farmhouse"][1], L["farmhouse"][2])
    move(owner, L["farmhouse_stand"][0], L["farmhouse_stand"][1], L["ground"])
    for _ in range(2):
        owner.command(".house ghost %d" % RAZORFEN_LEANTO)
        owner.command(".house ghost place at %.2f %.2f %.2f" % lean_spot)
        lean_spot = (lean_spot[0] + 12.0, lean_spot[1], lean_spot[2])
    leantos = [p for p in placements(owner_guid) if p["item"] == RAZORFEN_LEANTO]
    check("the first lean-to is the owned one, the second a new copy", len(leantos) == 2 and storage(owner_guid).get(RAZORFEN_LEANTO, 0) == 0,
          "%s %s" % (leantos, storage(owner_guid)))
    msgs = owner.command(".house pickup %d" % leantos[-1]["id"]) if leantos else []
    check("picking up sends it to the Collection, quietly", storage(owner_guid).get(RAZORFEN_LEANTO) == 1 and not has(msgs, "bags"),
          joined(msgs))
    owner.command(".house pickup %d" % leantos[0]["id"]) if leantos else None

    # An M2 building uses its exact translucent model, not a rectangular block. Once placed,
    # its goober edit copy receives the same right-click interaction as ordinary furnishings.
    admin.command(".house unlock %d %s" % (WESTFALL_SHED, args.owner_char), wait=1.5)
    mark = len(owner.addon_messages)
    owner.addon_command("house ghost %d" % WESTFALL_SHED, owner_char["name"], wait=1.0)
    shed_ghost = ghost_pieces(owner, mark)
    ghost_model = db("SELECT ModelName FROM creaturemodeldata_dbc WHERE ID=%d" % (60000 + WESTFALL_SHED - 900000), "acore_world")
    check("the Westfall Shed ghost uses its exact translucent model, not a box",
          len(shed_ghost) == 1 and shed_ghost[0][0] and ghost_model and "WestfallShed" in ghost_model[0][0],
          "%s %s" % (shed_ghost, ghost_model))
    shed_spot = (L["farmhouse"][0], L["farmhouse"][1], L["farmhouse"][2])
    owner.command(".house ghost place at %.2f %.2f %.2f" % shed_spot)
    shed = placement_of(owner_guid, WESTFALL_SHED)
    owner.command(".house decorate on")
    shed_edit_entry = 920000 + (WESTFALL_SHED - 900000)
    wait_for(lambda: owner.nearest(shed_edit_entry, TYPEID_GAMEOBJECT) is not None, 5, owner)
    shed_go = owner.nearest(shed_edit_entry, TYPEID_GAMEOBJECT)
    if shed_go:
        stand_next_to(owner, shed_go, 2.0)
    mark = len(owner.addon_messages)
    menu, msgs = owner.use_gameobject(shed_go.guid) if shed_go else (None, [])
    owner.pump(0.5)
    state = addon_state(owner)
    check("a placed building right-clicks onto the mouse like a furnishing",
          shed and shed_go and menu is None and state and state[21] == str(WESTFALL_SHED) and state[22] == "move"
          and addon_since(owner, mark, "HOUSING\topen"), "%s %s" % (state, joined(msgs)))
    owner.command(".house ghost place at %.2f %.2f %.2f" % (shed_spot[0] + 2.0, shed_spot[1], shed_spot[2]))
    moved_shed = placement_of(owner_guid, WESTFALL_SHED)
    check("the building sets down at its new spot", moved_shed and abs(moved_shed["x"] - (shed_spot[0] + 2.0)) < 0.05, str(moved_shed))
    owner.command(".house pickup %d" % moved_shed["id"]) if moved_shed else None
    owner.command(".house decorate off")
    move(owner, *L["landing"])

    msgs = owner.command(".house ghost %d" % BANK_CHEST)
    check("a locked piece can't be placed", has(msgs, "still locked"), joined(msgs))
    msgs = owner.command(".house take all")
    check("taking out of House Storage is gone: pieces stay in the Collection", has(msgs, "stay in your Collection"), joined(msgs))
    owner.command(".house get %d 2" % TABLE)
    check("buying copies adds them to the Collection", storage(owner_guid).get(TABLE) == 3 and owner.count_item(TABLE) == 0,
          str(storage(owner_guid)))
    mark = owner.message_mark()
    admin.command(".house unlock %d %s" % (BANK_CHEST, args.owner_char), wait=1.5)
    msgs = owner.messages_since(mark)
    check("an unlock gives one to place, and says so", storage(owner_guid).get(BANK_CHEST) == 1 and has(msgs, "waiting in your Collection"),
          joined(msgs))
    admin.command(".house unlock %d %s" % (BANK_CHEST, args.owner_char), wait=1.5)
    check("unlocking it again gives no extra one", storage(owner_guid).get(BANK_CHEST) == 1, str(storage(owner_guid)))

    # ------------------------------------------------------------- ghosts the client moves itself
    log("== local ghosts")
    name = owner_char["name"]
    mark = len(owner.addon_messages)
    owner.addon_command("house ghost %d" % TABLE, name, wait=1.0)
    pieces = ghost_pieces(owner, mark)
    ghost = pieces[0][0] if pieces else None
    check("the client hears which creature to move, and where it sits from the lead",
          len(pieces) == 1 and ghost and owner.find_objects(GHOST_NPC, TYPEID_UNIT) and abs(pieces[0][1]) < 0.01, str(pieces))
    moves_before = owner.monster_moves.get(ghost, 0)
    x, y, z = L["table"][0], L["table"][1], L["ground"]
    for step in range(8):
        owner.addon_command("house ghost at %.2f %.2f %.2f 0 0 1" % (x + step * 0.3, y, z), name, echo=step + 2)
        owner.pump(0.12)
    owner.pump(0.5)
    pose = last_pose(owner, mark)
    check("the server follows along without moving it itself", owner.monster_moves.get(ghost, 0) == moves_before,
          "%d moves" % (owner.monster_moves.get(ghost, 0) - moves_before))
    check("and says where its rules put it (flat ground: nothing to fix)", pose is not None and abs(pose["x"] - (x + 2.1)) < 0.05
          and abs(pose["zfix"]) < 0.3 and not pose["on_wall"], str(pose))
    owner.addon_command("house grid 1", name, wait=0.5)
    owner.addon_command("house ghost at %.2f %.2f %.2f 0 0 1" % (x + 0.4, y + 0.3, z), name, echo=20, wait=0.6)
    pose = last_pose(owner, mark)
    check("with the grid on the pose is on the grid", pose is not None and abs(pose["x"] - round(pose["x"])) < 0.01
          and abs(pose["y"] - round(pose["y"])) < 0.01, str(pose))
    owner.addon_command("house grid off", name, wait=0.3)
    owner.addon_command("house ghost adjust 0 0 0.5 0", name, echo=21, wait=0.6)
    pose = last_pose(owner, mark)
    check("raising it tells the client the lift", pose is not None and abs(pose["lift"] - 0.5) < 0.01, str(pose))
    before = storage(owner_guid).get(TABLE)
    owner.addon_command("house ghost place at %.2f %.2f %.2f 0 0 1" % (x, y, z), name, echo=22, wait=1.0)
    table = placement_of(owner_guid, TABLE)
    check("a click sets it down where the client showed it", table is not None and math.dist((table["x"], table["y"]), (x, y)) < 0.05
          and storage(owner_guid).get(TABLE) == before - 1, "%s %s" % (table, storage(owner_guid)))

    # Placed list: a click on one puts a green ring under it; closing the list takes it away.
    RING = 903991
    owner.addon_command("house highlight %d" % (table["id"] if table else 0), name, wait=1.0)
    ring = owner.nearest(RING, TYPEID_GAMEOBJECT)
    check("a chosen piece gets a green ring under it", ring is not None and table is not None
          and math.dist((ring.x, ring.y), (table["x"], table["y"])) < 0.1, str(ring and (ring.x, ring.y)))
    owner.addon_command("house highlight 0", name, wait=1.0)
    check("and loses it when the list closes", owner.nearest(RING, TYPEID_GAMEOBJECT) is None)

    # A lantern on a wall faces out from it; under a ceiling it hangs, its top at the point.
    lantern_height = float(db("SELECT height * 1 FROM mod_playerhousing_piece WHERE item_entry = %d" % LANTERN, "acore_world")[0][0])
    mark = len(owner.addon_messages)
    owner.addon_command("house ghost %d" % LANTERN, name, wait=1.0)
    owner.addon_command("house ghost at %.2f %.2f %.2f 1 0 0" % (x + 1, y + 1, z + 2), name, echo=23, wait=0.6)
    pose = last_pose(owner, mark)
    check("a lantern on a wall faces out from it", pose is not None and pose["on_wall"] and abs(pose["z"] - (z + 2)) < 0.05
          and abs(pose["o"]) < 0.05, str(pose))
    owner.addon_command("house ghost at %.2f %.2f %.2f 0 0 -1" % (x + 1, y + 1, z + 3), name, echo=24, wait=0.6)
    pose = last_pose(owner, mark)
    check("under a ceiling it hangs from it", pose is not None and not pose["on_wall"]
          and abs(pose["z"] - (z + 3 - lantern_height)) < 0.05, "%s height %.2f" % (pose, lantern_height))
    owner.addon_command("house ghost cancel", name, wait=0.5)

    # A client without the local flag gets the server's moves, as before.
    owner.addon_command("house addon 2 mouse", name, wait=0.5)
    mark = len(owner.addon_messages)
    owner.addon_command("house ghost %d" % LANTERN, name, wait=1.0)
    lantern_ghost = owner.nearest(GHOST_NPC, TYPEID_UNIT)
    moves_before = owner.monster_moves.get(lantern_ghost.guid, 0) if lantern_ghost else 0
    owner.addon_command("house ghost at %.2f %.2f %.2f" % (x + 2, y + 2, z), name, echo=30, wait=0.6)
    check("without it the server moves the ghost", lantern_ghost and owner.monster_moves.get(lantern_ghost.guid, 0) > moves_before
          and not addon_since(owner, mark, "HOUSING\tgpose"), str(owner.monster_moves.get(lantern_ghost.guid if lantern_ghost else 0)))
    owner.addon_command("house ghost cancel", name, wait=0.5)
    owner.addon_command("house addon 2 mouse local", name, wait=0.5)

    # ------------------------------------------------------------- a click picks a piece up
    log("== clicks")
    owner.command(".house decorate on")
    chair_go = owner.nearest(live(CHAIR), TYPEID_GAMEOBJECT) or owner.nearest(920000 + (CHAIR - 900000), TYPEID_GAMEOBJECT)
    mark = len(owner.addon_messages)
    menu, msgs = owner.use_gameobject(chair_go.guid) if chair_go else (None, [])
    owner.pump(0.5)
    state = addon_state(owner)
    chair = placement_of(owner_guid, CHAIR)
    check("decorating, a click picks the piece up onto the mouse and opens the window, no menu",
          chair and state and int(state[4]) == chair["id"] and state[21] == str(CHAIR) and state[22] == "move" and menu is None
          and addon_since(owner, mark, "HOUSING\topen"), "%s %s" % (state, menu))
    check("no green ring under the picked piece (retired)", owner.nearest(903991, TYPEID_GAMEOBJECT) is None)
    # Held: size and tilt go on it, and are set with it.
    owner.command(".house size bigger")
    owner.command(".house tilt forward 90")
    owner.pump(0.3)
    state = addon_state(owner)
    check("the held piece's size and tilt are in the state", state and state[26] == "110" and state[27] == "90", str(state))
    moved_before = placement_of(owner_guid, CHAIR)
    shape_before = shape_of(owner_guid, moved_before["id"]) if moved_before else None
    owner.command(".house ghost place at %.2f %.2f %.2f" % (moved_before["x"] + 1.0, moved_before["y"], L["ground"]) if moved_before else ".house ghost place")
    after = placement_of(owner_guid, CHAIR)
    shape = shape_of(owner_guid, after["id"]) if after else None
    check("set down, it keeps them", shape and abs(shape["scale"] / shape_before["scale"] - 1.1) < 0.02 and abs(shape["pitch"] - math.pi / 2) < 0.01,
          "%s -> %s" % (shape_before, shape))
    # A click on another piece while one is held does nothing (the addon's right-click puts it back).
    owner.command(".house ghost move %d" % after["id"]) if after else None
    if chair_go:
        owner.use_gameobject(chair_go.guid)
    owner.pump(0.3)
    state = addon_state(owner)
    check("a click while holding one leaves it held", state and state[22] == "move", str(state))
    owner.command(".house ghost cancel")
    owner.command(".house undo")
    owner.command(".house decorate off")

    # ------------------------------------------------------------- tilting all the way round
    log("== tilt")
    table = placement_of(owner_guid, TABLE)
    tid = table["id"] if table else 0

    def pitch_roll():
        row = db("SELECT pitch, roll FROM mod_playerhousing_placement WHERE owner_guid=%d AND placement_id=%d" % (owner_guid, tid))
        return (float(row[0][0]), float(row[0][1])) if row else (None, None)

    owner.command(".house tilt forward 90 %d" % tid)
    msgs = owner.command(".house tilt forward 90 %d" % tid)
    pitch, _ = pitch_roll()
    check("two 90 degree tilts turn a piece upside down", pitch is not None and abs(abs(pitch) - math.pi) < 0.01, "%s %s" % (pitch, joined(msgs)))

    def upside_down():
        # Its top points down: the rotation takes straight up to (nearly) straight down.
        go = owner.nearest(live(TABLE), TYPEID_GAMEOBJECT)
        if not go or not go.rotation:
            return False
        x, y, _, _ = go.rotation
        return 1 - 2 * (x * x + y * y) < -0.99

    wait_for(upside_down, 6, owner)
    check("and the client sees it upside down (not stood back up)", upside_down(),
          str(owner.nearest(live(TABLE), TYPEID_GAMEOBJECT).rotation if owner.nearest(live(TABLE), TYPEID_GAMEOBJECT) else None))
    owner.command(".house tilt forward 90 %d" % tid)
    pitch, _ = pitch_roll()
    check("and on round past 180 (no limit)", pitch is not None and abs(pitch + math.pi / 2) < 0.01, str(pitch))
    owner.command(".house tilt right 270 %d" % tid)
    _, roll = pitch_roll()
    check("rolling to its side goes round too", roll is not None and abs(roll + math.pi / 2) < 0.01, str(roll))
    owner.command(".house tilt straight %d" % tid)
    check("stood straight again", pitch_roll() == (0.0, 0.0), str(pitch_roll()))

    # ------------------------------------------------------------- mannequins from the window
    log("== mannequin")
    stand_spot = L["stand"]
    move(owner, L["stand_stand"][0], L["stand_stand"][1], L["ground"])
    owner.command(".house ghost %d" % MANNEQUIN)
    owner.command(".house ghost place at %.2f %.2f %.2f" % stand_spot)
    stand = placement_of(owner_guid, MANNEQUIN)
    check("a mannequin places from the Collection (a free copy)", stand is not None, str(placements(owner_guid)))
    state = addon_state(owner)
    check("selected, the addon hears it's a mannequin", state is not None and len(state) > 25 and state[25] == "1", str(state))
    admin.command(".additem %s %d 1" % (args.owner_char, SWORD), wait=1.5)
    wait_for(lambda: owner.count_item(SWORD) == 1, 3, owner)
    owner.addon_command("house data stand %d" % (stand["id"] if stand else 0), name, wait=1.0)
    rows = addon_list(owner, "stand") or []
    check("the sheet has the figure and its look", any(r[0] == "figure" for r in rows) and any(r[0] == "look" for r in rows), str(rows))
    msgs = owner.command(".house stand dress %d %d" % (SWORD, stand["id"] if stand else 0))
    wait_for(lambda: owner.count_item(SWORD) == 0, 3, owner)
    check("dressing it takes the sword from the bags", stand and gear_of(owner_guid, stand["id"]).get(SLOT_MAIN_HAND, (0, 0))[1] == SWORD
          and owner.count_item(SWORD) == 0, joined(msgs))
    msgs = owner.command(".house stand undress all %d" % (stand["id"] if stand else 0))
    wait_for(lambda: owner.count_item(SWORD) == 1, 3, owner)
    check("undressing gives it back", owner.count_item(SWORD) == 1 and stand and not gear_of(owner_guid, stand["id"]), joined(msgs))
    owner.command(".house stand dress %d %d" % (SWORD, stand["id"] if stand else 0))

    # Its race, man or woman (a random look a character could have).
    stand_id = stand["id"] if stand else 0
    look_of = lambda: int((db("SELECT look FROM mod_playerhousing_placement WHERE owner_guid = %d AND placement_id = %d" % (owner_guid, stand_id)) or [["0"]])[0][0])
    msgs = owner.command(".house stand look 11 female %d" % stand_id)
    check("a Draenei woman", look_of() & 0x10F == 0x10B, "%x %s" % (look_of(), joined(msgs)))
    looks = set()
    for _ in range(4):
        owner.command(".house stand look 11 female %d" % stand_id, wait=0.3)
        looks.add(look_of() >> 9)
    check("each new look is drawn again", len(looks) > 1 and look_of() & 0x10F == 0x10B, str(looks))
    owner.addon_command("house data stand %d" % stand_id, name, wait=1.0)
    rows = addon_list(owner, "stand") or []
    check("the sheet hears its look", any(r[0] == "look" and r[1] == str(look_of()) for r in rows), str(rows))
    msgs = owner.command(".house stand pose next %d" % stand_id)
    check("no poses for now: it stands", has(msgs, "Usage") and (look_of() >> 4) & 0xF == 0, joined(msgs))

    # Trading gear: the mannequin's onto the character, the character's onto it; again, back.
    worn = lambda: {int(r[0]): int(r[1]) for r in db("SELECT ci.slot, ii.itemEntry FROM character_inventory ci JOIN item_instance ii "
                                                    "ON ii.guid = ci.item WHERE ci.guid = %d AND ci.bag = 0 AND ci.slot < 19" % owner_guid)}
    char_before, stand_before = worn(), {s: g[1] for s, g in gear_of(owner_guid, stand_id).items()}
    msgs = owner.command(".house stand trade %d" % stand_id)
    stand_after = {s: g[1] for s, g in gear_of(owner_guid, stand_id).items()}
    char_after = worn()
    shown = {k: v for k, v in char_before.items() if k in (0, 2, 3, 4, 5, 6, 7, 8, 9, 14, 15, 16, 17, 18)}
    check("trading puts the character's gear on the mannequin", stand_after == shown, "%s -> %s %s" % (char_before, stand_after, joined(msgs)))
    check("and the mannequin's on the character (or in the bags)", all(char_after.get(s) == e or owner.count_item(e) for s, e in stand_before.items())
          and not any(char_after.get(s) == e for s, e in shown.items() if s not in stand_before), "%s %s" % (char_after, joined(msgs)))
    owner.command(".house stand trade %d" % stand_id)
    check("trading back puts it all back", worn() == char_before and {s: g[1] for s, g in gear_of(owner_guid, stand_id).items()} == stand_before
          or owner.count_item(SWORD) == 1, "%s %s" % (worn(), gear_of(owner_guid, stand_id)))
    if owner.count_item(SWORD) == 1:
        owner.command(".house stand dress %d %d" % (SWORD, stand_id))

    # ------------------------------------------------------------- sets follow like a piece
    log("== sets")
    chair = placement_of(owner_guid, CHAIR)
    table = placement_of(owner_guid, TABLE)
    owner.command(".house decorate on")
    if chair and table:
        owner.command(".house select %d" % chair["id"])
        owner.command(".house group add %d" % table["id"])
    msgs = owner.command(".house set save Duo")
    owner.addon_command("house data sets", name, wait=1.0)
    sets = [r for r in (addon_list(owner, "sets") or []) if r[0] == "set"]
    check("save the chair and table as a set", sets and sets[0][2] == "Duo", joined(msgs) + str(sets))
    before = len(placements(owner_guid))
    have_before = storage(owner_guid)
    mark = len(owner.addon_messages)
    owner.addon_command("house set place Duo", name, wait=1.0)
    pieces = ghost_pieces(owner, mark)
    check("a set follows as one ghost, a creature a piece", len(pieces) == 2 and all(p[0] for p in pieces), str(pieces))
    move(owner, *L["landing"])
    msgs = owner.command(".house ghost place at %.2f %.2f %.2f" % (L["landing"][0] + 3, L["landing"][1] - 3, L["ground"]))
    check("set down, every piece of it", len(placements(owner_guid)) == before + 2, joined(msgs))
    have = storage(owner_guid)
    check("its pieces came out of the Collection", have.get(CHAIR, 0) == have_before.get(CHAIR, 0) - 1, "%s -> %s" % (have_before, have))

    # ------------------------------------------------------------- layouts from the window
    log("== layouts")
    msgs = owner.command(".house layout save Home")
    owner.addon_command("house data layouts", name, wait=1.0)
    rows = addon_list(owner, "layouts") or []
    layout = next((r for r in rows if r[0] == "layout"), None)
    check("the layouts list says what each would still need (nothing)", layout is not None and layout[6:9] == ["0", "0", "0"], str(rows))
    owner.command(".house layout rename %s Cottage" % (layout[1] if layout else "1"))
    owner.command(".house layout copyable on")
    owner.addon_command("house data layouts", name, wait=1.0)
    rows = addon_list(owner, "layouts") or []
    check("renamed, and visitors may copy it", any(r[0] == "layout" and r[2] == "Cottage" for r in rows)
          and any(r[0] == "limit" and r[2] == "1" for r in rows), str(rows))
    placed = len(placements(owner_guid))
    msgs = owner.command(".house packup")
    check("pack up puts every piece in the Collection", not placements(owner_guid) and has(msgs, "Collection"), joined(msgs))
    owner.pump(3.5)  # heavy steps wait a few seconds between them
    msgs = owner.command(".house layout load Cottage")
    check("and the layout sets them all out again", len(placements(owner_guid)) == placed, joined(msgs))
    # Pack up gave the sword back; the mannequin wears it again for the guest below.
    stand = placement_of(owner_guid, MANNEQUIN)
    owner.command(".house stand dress %d %d" % (SWORD, stand["id"] if stand else 0))
    owner.command(".house decorate off")

    # ------------------------------------------------------------- the music box opens the Island tab
    music_spot = L["music"]
    move(owner, L["music_stand"][0], L["music_stand"][1], L["ground"])
    owner.command(".house ghost %d" % MUSIC_BOX)
    owner.command(".house ghost place at %.2f %.2f %.2f" % music_spot)
    music = owner.nearest(live(MUSIC_BOX), TYPEID_GAMEOBJECT)
    mark = len(owner.addon_messages)
    owner.use_gameobject(music.guid) if music else None
    check("the owner's music box opens the window's Island tab", addon_since(owner, mark, "HOUSING\topen\tIsland"),
          str(owner.addon_messages[mark:]))

    # ------------------------------------------------------------- a guest at the mannequin
    log("== guest")
    owner.command(".house privacy public")
    guest.login(guest_char["guid"])
    guest.pump(1.0)
    guest.command(".house visit %s" % args.owner_char)
    wait_for_map(guest, HOUSING_MAP)
    figure = guest.nearest(900201, TYPEID_UNIT)
    if figure:
        stand_next_to(guest, figure, 2.0)
    menu, msgs = guest.gossip_hello(figure.guid) if figure else (None, [])
    check("a guest clicking the mannequin hears what it wears", has(msgs, "wears") and menu is None, joined(msgs))
    msgs = guest.command(".house layout copy")
    check("and may copy the island's layout", has(msgs, "Saved") or has(msgs, "copy"), joined(msgs))

    # ------------------------------------------------------------- logging out on an island
    log("== logging out on an island")
    check("a ruined tower (a world model) is a destructible building, which the client draws",
          db("SELECT type FROM gameobject_template WHERE entry=%d" % live(RUINED_TOWER), "acore_world") == [["33"]])
    spot = owner.pos
    owner.logout_to_characters()
    with owner.objects_lock:
        owner.objects.clear()
    owner.login(owner_char["guid"])
    wait_for_map(owner, HOUSING_MAP)
    back = owner.map_id == HOUSING_MAP and math.dist(owner.pos[:3], spot[:3]) < 3.0
    check("the owner logs back in on their island, where they stood", back, "%s %s -> %s" % (owner.map_id, spot, owner.pos))
    wait_for(lambda: owner.nearest(live(MUSIC_BOX), TYPEID_GAMEOBJECT) is not None, 10, owner)
    check("with the island set out around them", owner.nearest(live(MUSIC_BOX), TYPEID_GAMEOBJECT) is not None)
    guest.logout_to_characters()
    guest.login(guest_char["guid"])
    wait_for(lambda: guest.map_id == 0, 12, guest)
    check("a guest logging back in goes back where they came from", guest.map_id == 0, str((guest.map_id, guest.pos)))

    # ------------------------------------------------------------- every character shares the island
    log("== one island per account")
    owned_before = storage(owner_guid)
    owner.logout_to_characters()
    alt_char = get_or_create_char(owner, "Krookalt")
    alt_guid = alt_char["guid"] & 0xFFFFFFFF
    db("UPDATE characters SET map=0, position_x=%.2f, position_y=%.2f, position_z=%.2f WHERE guid=%d" % (KROOK_SW + (alt_guid,)))
    with owner.objects_lock:
        owner.objects.clear()
    owner.login(alt_char["guid"])
    owner.pump(1.0)
    check("a second character gets no island of its own", db("SELECT COUNT(*) FROM mod_playerhousing_house WHERE owner_guid=%d" % alt_guid) == [["0"]])
    menu, _, msgs = key_from_krook(owner, "I'd like a house")
    wait_for(lambda: owner.count_item(HOUSE_KEY) == 1, 5, owner)
    check("its House Key from Krook: the account's island", owner.count_item(HOUSE_KEY) == 1 and has(owner.messages_since(0), "shared"),
          "%s %d %s" % (menu, owner.count_item(HOUSE_KEY), joined(owner.messages_since(0))))
    check("and no second set of first pieces", storage(owner_guid) == owned_before and not storage(alt_guid), "%s %s" % (owned_before, storage(alt_guid)))
    owner.command(".house home")
    wait_for_map(owner, HOUSING_MAP)
    owner.pump(1.0)
    state = addon_state(owner)
    check("Go home takes it to the account's island, as its own", state and state[2] == "1" and state[11] == args.owner_char, str(state))
    wait_for(lambda: owner.nearest(live(MUSIC_BOX), TYPEID_GAMEOBJECT) is not None, 10, owner)
    check("with everything the first character placed", owner.nearest(live(MUSIC_BOX), TYPEID_GAMEOBJECT) is not None)
    placed_before = len(placements(owner_guid))
    move(owner, L["chair"][0] + 2.0, L["chair"][1], L["ground"])
    owner.command(".house ghost %d" % LANTERN)
    owner.command(".house ghost place at %.2f %.2f %.2f" % (L["chair"][0] + 3.0, L["chair"][1], L["ground"]))
    check("what it places is the account's", len(placements(owner_guid)) == placed_before + 1 and not placements(alt_guid),
          "%d -> %d" % (placed_before, len(placements(owner_guid))))
    guest.command(".house visit Krookalt")
    wait_for_map(guest, HOUSING_MAP)
    guest.pump(1.0)
    gstate = addon_state(guest)
    check("visiting any of the account's characters goes to its island", gstate and gstate[11] == args.owner_char, str(gstate))

    # ------------------------------------------------------------- turning turns what you hold
    log("== turning")
    owner.addon_command("house addon 2 mouse local", "Krookalt", wait=0.5)
    owner.command(".house ghost %d" % LANTERN)
    x, y, z, o = owner.pos
    mark = len(owner.addon_messages)
    owner.addon_command("house ghost at %.2f %.2f %.2f" % (x + 3.0, y, L["ground"]), "Krookalt", wait=0.5)
    owner.pump(0.5)
    before = last_pose(owner, mark)
    mark = len(owner.addon_messages)
    move(owner, x, y, z, o + 0.3)
    owner.pump(0.6)
    after = last_pose(owner, mark)
    turned = before and after and abs(math.remainder(after["turn"] - before["turn"] - 0.3, 2 * math.pi)) < 0.02
    check("turning on the spot turns the held piece as much", turned, "%s -> %s" % (before, after))
    owner.command(".house ghost cancel")

    # ------------------------------------------------------------- Krook
    log("== Krook")
    owner.command(".house krook")
    owner.pump(1.0)
    krook = owner.nearest(STEWARD, TYPEID_UNIT)
    near_owner = krook and math.dist((krook.x, krook.y), owner.pos[:2]) < 5.0
    check("Call Krook brings him over", near_owner, str(krook and (krook.x, krook.y)) + " " + str(owner.pos))
    # The tour done (by any character of the account): next visit he stays away.
    db("REPLACE INTO character_queststatus_rewarded (guid, quest, active) VALUES (%d, 900404, 1)" % owner_guid)
    guest.command(".house leave")
    owner.command(".house leave")
    wait_for(lambda: owner.map_id != HOUSING_MAP or owner.pos[0] < 15000, 12, owner)
    owner.pump(2.0)
    with owner.objects_lock:
        owner.objects.clear()
    owner.command(".house home")
    wait_for_map(owner, HOUSING_MAP)
    owner.pump(3.0)
    krook = owner.nearest(STEWARD, TYPEID_UNIT)
    check("after the tour, Krook isn't on the island until called", krook is None or math.dist((krook.x, krook.y), owner.pos[:2]) > 60,
          str(krook and (krook.x, krook.y)))
    owner.command(".house krook")
    owner.pump(1.0)
    check("and comes when called", owner.nearest(STEWARD, TYPEID_UNIT) is not None)
    db("DELETE FROM character_queststatus_rewarded WHERE guid=%d AND quest=900404" % owner_guid)

    owner.close()
    guest.close()
    admin.close()
    return finish()


if __name__ == "__main__":
    sys.exit(main())
