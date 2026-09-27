#!/usr/bin/env python3
"""End-to-end smoke test for mod-playerhousing using the headless client.

Expects a running authserver + worldserver and two plain player accounts
(see README). Character state is reset at the start of each run.
"""

import argparse
import math
import subprocess
import sys
import time

from wowclient import (INVENTORY_SLOT_BAG_0, TYPEID_GAMEOBJECT, TYPEID_UNIT, WorldClient, auth_login)

STEWARD_ENTRY = 900200
# Houses are phased copies on GM Island (Kalimdor), in one of two layouts.
HOUSING_MAP = 1
HUMAN_STAGE1_OBJECT = 180334                       # Stormwind Rug
GNOME_STAGE1_OBJECT = 193586                       # Gnome Maintenance Light
ISLAND_DB_GAMEOBJECTS = set(range(101766, 101780)) | {178934}  # GM Island's own chairs (phase 1)
ISLAND_DB_CREATURES = {6491}                                    # its Spirit Healer
LAYOUTS = {
    # Inside the guild house: open ground-floor hall spots with line of sight from the
    # stand point (measured from the house's collision mesh).
    "guildhouse": dict(
        floor_z=13.18,
        starter=frozenset({193684, 181302, 179977}),   # lantern, bedroll, crate
        stand=(16235.0, 16297.0),
        targets=[(16239.0, 16300.0), (16230.0, 16299.0), (16240.0, 16294.0)],
        move=(16247.0, 16298.0),
        through_walls=None),
    # House removed (tools/gm-island-cleared): a campsite on the plateau where it stood.
    "cleared": dict(
        floor_z=13.0,
        starter=frozenset({184592, 1798, 193684, 181302, 179977}),  # campsite
        stand=(16246.0, 16288.0),
        targets=[(16252.0, 16292.0), (16232.0, 16290.0), (16250.0, 16282.0)],
        move=(16236.0, 16284.0),
        # From the old hall to where the entry room's back wall stood.
        through_walls=((16245.0, 16298.0), (16222.0, 16290.0))),
}
LAYOUT = LAYOUTS["guildhouse"]
FLOOR_Z = LAYOUT["floor_z"]
STARTER_STYLE_OBJECTS = LAYOUT["starter"]
HALL_STAND = LAYOUT["stand"]
PLACE_TARGETS = LAYOUT["targets"]
MOVE_SPOT = LAYOUT["move"]
FLARE = 1543
ITEM_CAMPFIRE = 901101
ITEM_CHAIR = 901105
GO_CAMPFIRE = 1798
GO_CHAIR = 180047

results = []


def log(msg):
    print(msg, flush=True)


def check(name, ok, detail=""):
    results.append((name, bool(ok), detail))
    log("%s %s%s" % ("PASS" if ok else "FAIL", name, (" :: " + detail) if detail else ""))
    return ok


def db(sql, database="acore_characters"):
    out = subprocess.run(["mysql", "-uacore", "-pacore", "-N", "-B", database, "-e", sql],
                         capture_output=True, text=True)
    if out.returncode != 0:
        raise RuntimeError(out.stderr)
    return [line.split("\t") for line in out.stdout.splitlines() if line]


def any_contains(messages, needle):
    return any(needle.lower() in m.lower() for m in messages)


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


def steward_near(wc, timeout=4.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        s = wc.nearest(STEWARD_ENTRY, TYPEID_UNIT)
        if s:
            return s
        wc.pump(0.25)
    return None


def house_steward_or_summon(wc):
    """Returns the nearest steward, summoning one with .krook add if the house has none."""
    s = steward_near(wc, timeout=2.0)
    if s:
        return s
    wc.command(".krook add")
    return steward_near(wc)


def arrived_in_house(wc):
    """True once the client is on the housing map and the server is sending it that map's objects."""
    return wait_for_map(wc, HOUSING_MAP) and steward_near(wc, timeout=5.0) is not None


def relog(args, wc, account, password, char, clean=False):
    """Logs the character back in. clean=True logs out properly first; otherwise the
    connection just drops, and the core re-attaches the still-in-world character."""
    if clean:
        wc.logout()
    else:
        wc.close()
    guid = char["guid"] & 0xFFFFFFFF
    deadline = time.time() + 60
    while db("SELECT online FROM characters WHERE guid=%d" % guid) != [["0"]] and time.time() < deadline:
        time.sleep(1)
    fresh = connect(args, account, password)
    fresh.enum_chars()
    fresh.login(char["guid"])
    return fresh


def walk_next_to(wc, obj, dist=2.0):
    angle = math.atan2(wc.pos[1] - obj.y, wc.pos[0] - obj.x)
    wc.move_to(obj.x + math.cos(angle) * dist, obj.y + math.sin(angle) * dist, obj.z, angle + math.pi)


def open_steward(wc, steward):
    walk_next_to(wc, steward)
    menu, _ = wc.gossip_hello(steward.guid)
    return menu


def wait_for_map(wc, map_id, timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if wc.map_id == map_id:
            wc.pump(2.0)
            return True
        wc.pump(0.25)
    return wc.map_id == map_id


def point_in_front(wc, dist):
    x, y, z, o = wc.pos
    return x + math.cos(o) * dist, y + math.sin(o) * dist, z


def placements(owner_guid):
    return db("SELECT placement_id, source_item_entry, catalog_id, spawn_entry FROM mod_playerhousing_placement "
              "WHERE owner_guid=%d ORDER BY placement_id" % owner_guid)


def go_entries(wc):
    return {o.entry for o in wc.find_objects(type_id=TYPEID_GAMEOBJECT)}


def unit_entries(wc):
    return {o.entry for o in wc.find_objects(type_id=TYPEID_UNIT)}


def sees(wc, other):
    return other.player_guid in {o.guid for o in wc.find_objects()}


def walk_to(wc, spot, z=FLOOR_Z):
    wc.move_to(spot[0], spot[1], z)
    wc.pump(0.5)


def enter_own_house(wc):
    wc.command(".krook add")
    s = steward_near(wc)
    if not s:
        return False
    open_steward(wc, s)
    wc.gossip_select("Enter my house", wait=1.0)
    return arrived_in_house(wc)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--auth-port", type=int, default=3724)
    ap.add_argument("--owner", default="houseowner:houseowner")
    ap.add_argument("--guest", default="houseguest:houseguest")
    ap.add_argument("--owner-char", default="Krookowner")
    ap.add_argument("--guest-char", default="Krookguest")
    ap.add_argument("--layout", choices=sorted(LAYOUTS), default="guildhouse",
                    help="which SQL layout the server runs (sql/layouts)")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    global LAYOUT, FLOOR_Z, STARTER_STYLE_OBJECTS, HALL_STAND, PLACE_TARGETS, MOVE_SPOT
    LAYOUT = LAYOUTS[args.layout]
    FLOOR_Z = LAYOUT["floor_z"]
    STARTER_STYLE_OBJECTS = LAYOUT["starter"]
    HALL_STAND = LAYOUT["stand"]
    PLACE_TARGETS = LAYOUT["targets"]
    MOVE_SPOT = LAYOUT["move"]

    owner_acc, owner_pw = args.owner.split(":")
    guest_acc, guest_pw = args.guest.split(":")

    # ---------------------------------------------------------------- setup
    log("== creating/loading characters")
    owner = connect(args, owner_acc, owner_pw)
    owner_char = get_or_create_char(owner, args.owner_char)
    guest = connect(args, guest_acc, guest_pw)
    guest_char = get_or_create_char(guest, args.guest_char)
    owner_guid = owner_char["guid"] & 0xFFFFFFFF
    guest_guid = guest_char["guid"] & 0xFFFFFFFF

    # A previous aborted run can leave a character in-world for a while; wait so the
    # reset below is not undone and the login hooks run normally.
    deadline = time.time() + 60
    while db("SELECT COUNT(*) FROM characters WHERE online=1 AND guid IN (%d, %d)" % (owner_guid, guest_guid)) != [["0"]]:
        if time.time() > deadline:
            raise RuntimeError("test characters are still online")
        time.sleep(1)

    # Reset module state for a repeatable run, and fund the owner (both are offline here).
    for table in ("mod_playerhousing_placement", "mod_playerhousing_unlock", "mod_playerhousing_acl", "mod_playerhousing_house"):
        db("DELETE FROM %s WHERE owner_guid IN (%d, %d)" % (table, owner_guid, guest_guid))
    db("UPDATE characters SET money=20000000 WHERE guid=%d" % owner_guid)
    db("UPDATE characters SET money=1000000 WHERE guid=%d" % guest_guid)
    db("DELETE FROM character_inventory WHERE guid IN (%d, %d) AND item IN "
       "(SELECT guid FROM item_instance WHERE itemEntry BETWEEN 901100 AND 901106)" % (owner_guid, guest_guid))

    # ------------------------------------------------------------- login
    log("== owner login")
    owner.login(owner_char["guid"])
    start_map = owner.map_id
    owner.pump(1.5)
    house = db("SELECT style_id, stage, is_private FROM mod_playerhousing_house WHERE owner_guid=%d" % owner_guid)
    check("starter house auto-provisioned on first login", house == [["1", "0", "1"]], str(house))
    unlocks = sorted(int(r[0]) for r in db("SELECT catalog_id FROM mod_playerhousing_unlock WHERE owner_guid=%d" % owner_guid))
    check("starter catalog unlocks granted on first login", unlocks == [1001, 1002, 1003, 1004, 1101], str(unlocks))

    msgs = owner.command(".krook")
    check(".krook prints usage", any_contains(msgs, "Krook commands"), " | ".join(msgs))
    msgs = owner.command(".krook status")
    check(".krook status outside house", any_contains(msgs, "Style: Human Cottage") and any_contains(msgs, "Privacy: private"), " | ".join(msgs))
    msgs = owner.command(".krook leave")
    check(".krook leave refuses outside a house", any_contains(msgs, "not currently in a house"), " | ".join(msgs))
    msgs = owner.command(".krook add 1001")
    check("placement selection refused outside house", any_contains(msgs, "inside your house"), " | ".join(msgs))

    msgs = owner.command(".krook add")
    city_steward = steward_near(owner)
    check(".krook add spawns a steward", any_contains(msgs, "spawned a steward") and city_steward is not None,
          " | ".join(msgs) + " steward=%r" % city_steward)
    if not city_steward:
        return finish()

    menu = open_steward(owner, city_steward)
    options = [i["text"] for i in (menu or {}).get("items", [])]
    check("steward main gossip menu", menu is not None and "Enter my house" in options and "Furniture tools" in options, str(options))
    owner.gossip_select("How housing works")
    owner.gossip_select("Show house status")

    # -------------------------------------------------------- enter house
    log("== owner enters house")
    owner.gossip_select("Enter my house", wait=1.0)
    entered = arrived_in_house(owner)
    check("owner arrives in the GM Island guild house", entered, "map=%s pos=%s" % (owner.map_id, owner.pos))
    if not entered:
        return finish()
    owner.pump(1.0)

    house_steward = steward_near(owner)
    gos = go_entries(owner)
    check("stage 0 moving-in props spawned", STARTER_STYLE_OBJECTS <= gos, "missing=%s" % sorted(STARTER_STYLE_OBJECTS - gos))
    check("GM Island's own spawns are hidden inside the house",
          not (gos & ISLAND_DB_GAMEOBJECTS) and not (unit_entries(owner) & ISLAND_DB_CREATURES),
          "gameobjects=%s creatures=%s" % (sorted(gos & ISLAND_DB_GAMEOBJECTS), sorted(unit_entries(owner) & ISLAND_DB_CREATURES)))

    msgs = owner.command(".krook status")
    check(".krook status inside house", any_contains(msgs, "Stage: 0"), " | ".join(msgs))

    # --------------------------------------------------------- furniture
    menu = open_steward(owner, house_steward)
    check("steward gossip works inside house", menu is not None)
    owner.gossip_select("Furniture tools")
    _, msgs = owner.gossip_select("Show available catalog")
    check("catalog lists unlocked starter items", any_contains(msgs, "[Unlocked] Starter Chair"), " | ".join(msgs[:6]))

    vendor = owner.list_vendor(house_steward.guid)
    vendor_items = {i["entry"]: i for i in (vendor or {}).get("items", [])}
    check("Krook's Cranny vendor lists furniture kits", ITEM_CAMPFIRE in vendor_items and ITEM_CHAIR in vendor_items,
          str(sorted(vendor_items)))
    pushed, msgs = owner.buy(house_steward.guid, ITEM_CAMPFIRE, vendor_items.get(ITEM_CAMPFIRE, {}).get("slot", 0))
    check("bought Campfire Kit", any(p["entry"] == ITEM_CAMPFIRE for p in pushed), str(pushed) + " " + " | ".join(msgs))
    pushed_chair, msgs = owner.buy(house_steward.guid, ITEM_CHAIR, vendor_items.get(ITEM_CHAIR, {}).get("slot", 0))
    check("bought Cozy Chair Kit", any(p["entry"] == ITEM_CHAIR for p in pushed_chair), str(pushed_chair))
    owner.pump(1.0)

    walk_to(owner, HALL_STAND)
    before = placements(owner_guid)
    camp = next((p for p in pushed if p["entry"] == ITEM_CAMPFIRE), None)
    camp_guid = owner.item_guid_for_entry(ITEM_CAMPFIRE)
    if camp and camp_guid:
        tx, ty = PLACE_TARGETS[0]
        msgs = owner.use_item_at(INVENTORY_SLOT_BAG_0, camp["slot"], camp_guid, FLARE, tx, ty, FLOOR_Z)
        owner.pump(1.0)
        after = placements(owner_guid)
        check("furniture item placed via Flare targeting", len(after) == len(before) + 1, " | ".join(msgs) + " placements=%s" % after)
        check("placed furniture item consumed", owner.item_guid_for_entry(ITEM_CAMPFIRE) is None or
              not db("SELECT 1 FROM item_instance WHERE guid=%d" % (camp_guid & 0xFFFFFFFF)))
        check("placed furniture visible to the owner", GO_CAMPFIRE in go_entries(owner))
    else:
        check("furniture item placed via Flare targeting", False, "item slot/guid unknown: %s %s" % (camp, camp_guid))

    chair = next((p for p in pushed_chair if p["entry"] == ITEM_CHAIR), None)
    chair_guid = owner.item_guid_for_entry(ITEM_CHAIR)
    if chair and chair_guid:
        tx, ty = PLACE_TARGETS[2]
        msgs = owner.use_item_at(INVENTORY_SLOT_BAG_0, chair["slot"], chair_guid, FLARE, tx, ty, FLOOR_Z)
        check("stage 1 furniture refused at stage 0", len(placements(owner_guid)) == len(before) + 1 and any_contains(msgs, "Housing:"),
              " | ".join(msgs))

    # ----------------------------------------------------------- upgrade
    open_steward(owner, house_steward)
    _, msgs = owner.gossip_select("Upgrade my house", wait=3.0)
    stage = db("SELECT stage FROM mod_playerhousing_house WHERE owner_guid=%d" % owner_guid)
    check("upgrade to stage 1 charges gold and saves", any_contains(msgs, "upgraded to stage 1") and stage == [["1"]],
          " | ".join(msgs) + " db=%s" % stage)
    owner.pump(2.0)
    gos = go_entries(owner)
    expected = STARTER_STYLE_OBJECTS | {HUMAN_STAGE1_OBJECT, GO_CAMPFIRE}
    check("house contents stay spawned after upgrading from inside",
          steward_near(owner, timeout=2.0) is not None and expected <= gos, "missing=%s" % sorted(expected - gos))
    house_steward = house_steward_or_summon(owner)

    walk_to(owner, HALL_STAND)
    before = placements(owner_guid)
    msgs = owner.command(".krook add 1101")
    check(".krook add <catalogId> selects furniture", any_contains(msgs, "Selected Stormwind Rug"), " | ".join(msgs))
    tx, ty = PLACE_TARGETS[1]
    msgs = owner.cast_at(FLARE, tx, ty, FLOOR_Z)
    owner.pump(1.0)
    after = placements(owner_guid)
    check("catalog furniture placed via Flare cast", len(after) == len(before) + 1, " | ".join(msgs) + " placements=%s" % after)

    if chair and owner.item_guid_for_entry(ITEM_CHAIR):
        before = placements(owner_guid)
        tx, ty = PLACE_TARGETS[2]
        msgs = owner.use_item_at(INVENTORY_SLOT_BAG_0, chair["slot"], owner.item_guid_for_entry(ITEM_CHAIR), FLARE, tx, ty, FLOOR_Z)
        owner.pump(1.0)
        check("stage 1 furniture accepted after upgrade", len(placements(owner_guid)) == len(before) + 1, " | ".join(msgs))

    if LAYOUT["through_walls"]:
        stand, target = LAYOUT["through_walls"]
        walk_to(owner, stand)
        before = placements(owner_guid)
        owner.command(".krook add 1002")
        msgs = owner.cast_at(FLARE, target[0], target[1], FLOOR_Z)
        owner.pump(1.0)
        check("server has no house left: placement across its old walls works", len(placements(owner_guid)) == len(before) + 1,
              " | ".join(msgs))

    rows = placements(owner_guid)
    open_steward(owner, house_steward)
    owner.gossip_select("Furniture tools")
    _, msgs = owner.gossip_select("List placed furniture")
    check("list placed furniture", len(msgs) >= len(rows) and len(rows) > 0, " | ".join(msgs))

    if rows:
        first_id = rows[0][0]
        walk_to(owner, MOVE_SPOT)
        open_steward(owner, house_steward)
        owner.gossip_select("Furniture tools")
        _, msgs = owner.gossip_select("Move furniture placement by ID", code=first_id)
        check("move furniture by placement ID", any_contains(msgs, "Moved placement #%s" % first_id), " | ".join(msgs))
        owner.gossip_select("Remove furniture placement by ID", code=rows[-1][0])
        gone = not db("SELECT 1 FROM mod_playerhousing_placement WHERE owner_guid=%d AND placement_id=%s" % (owner_guid, rows[-1][0]))
        check("remove furniture by placement ID", gone)
    owner_furniture = {int(r[3]) for r in placements(owner_guid)}

    # ------------------------------------------------------------- guest
    log("== guest login")
    guest.login(guest_char["guid"])
    guest.pump(1.5)
    guest.command(".krook add")
    g_steward = steward_near(guest)
    check("guest can spawn a city steward", g_steward is not None)
    if g_steward:
        open_steward(guest, g_steward)
        _, msgs = guest.gossip_select("Visit a player's house", code=args.owner_char, wait=2.0)
        check("private house refuses uninvited guest", guest.map_id != HOUSING_MAP and any_contains(msgs, "private"),
              " | ".join(msgs))

        log("== guest enters their own house while the owner is home")
        in_own = enter_own_house(guest)
        check("second character enters their own house at the same spot", in_own, "map=%s" % guest.map_id)
        if in_own:
            guest.pump(1.0)
            check("houses are private: the guest does not see the owner", not sees(guest, owner))
            check("houses are private: the owner does not see the guest", not sees(owner, guest))
            leaked = (go_entries(guest) & owner_furniture) - STARTER_STYLE_OBJECTS
            check("houses are private: the owner's furniture is not in the guest's house", not leaked, "leaked=%s" % sorted(leaked))
            guest.command(".krook leave", wait=1.0)
            check("guest leaves their own house", wait_for_map(guest, start_map), "map=%s" % guest.map_id)

        open_steward(owner, house_steward)
        owner.gossip_select("Guest access list")
        _, msgs = owner.gossip_select("Invite guest by name", code=args.guest_char)
        acl = db("SELECT guest_guid FROM mod_playerhousing_acl WHERE owner_guid=%d" % owner_guid)
        check("owner invites guest", acl == [[str(guest_guid)]], " | ".join(msgs) + " acl=%s" % acl)

        log("== guest visits while the owner is home")
        guest.command(".krook add")
        g_steward = steward_near(guest)
        open_steward(guest, g_steward)
        guest.gossip_select("Visit a player's house", code=args.owner_char, wait=1.0)
        visited = arrived_in_house(guest)
        check("invited guest can visit while the owner is home", visited, "map=%s" % guest.map_id)
        if visited:
            guest.pump(1.0)
            check("guest and owner see each other in the same house", sees(guest, owner) and sees(owner, guest))
            missing = owner_furniture - go_entries(guest)
            check("guest sees the owner's furniture", not missing, "missing=%s" % sorted(missing))
            before = placements(owner_guid)
            msgs = guest.command(".krook add 1001")
            guest.cast_at(FLARE, PLACE_TARGETS[0][0], PLACE_TARGETS[0][1], FLOOR_Z)
            check("guest cannot place furniture in owner's house", placements(owner_guid) == before and any_contains(msgs, "Housing:"),
                  " | ".join(msgs))
            h_steward = steward_near(guest)
            if h_steward:
                open_steward(guest, h_steward)
                guest.gossip_select("Furniture tools")
                _, msgs = guest.gossip_select("Remove furniture placement by ID", code=before[0][0] if before else "1")
                check("guest cannot remove owner's furniture", placements(owner_guid) == before, " | ".join(msgs))

            log("== owner steps out and comes back while the guest is inside")
            owner.command(".krook leave", wait=1.0)
            wait_for_map(owner, start_map)
            back = enter_own_house(owner)
            check("owner can come home while a guest is inside", back and sees(owner, guest), "map=%s" % owner.map_id)
            house_steward = house_steward_or_summon(owner)

            guest.command(".krook leave", wait=1.0)
            check("guest .krook leave returns to origin", wait_for_map(guest, start_map), "map=%s" % guest.map_id)

    # ------------------------------------------------------- style / relog
    if owner.map_id != HOUSING_MAP:
        enter_own_house(owner)
        house_steward = house_steward_or_summon(owner)
    open_steward(owner, house_steward)
    owner.gossip_select("Change house style")
    _, msgs = owner.gossip_select("Gnome", wait=3.0)
    style = db("SELECT style_id FROM mod_playerhousing_house WHERE owner_guid=%d" % owner_guid)
    check("change style to gnome", style == [["2"]] and any_contains(msgs, "style set to gnome"), " | ".join(msgs))
    owner.pump(2.0)
    gos = go_entries(owner)
    expected = STARTER_STYLE_OBJECTS | {GNOME_STAGE1_OBJECT}
    check("house contents stay spawned after changing style from inside",
          steward_near(owner, timeout=2.0) is not None and expected <= gos, "missing=%s" % sorted(expected - gos))
    house_steward = house_steward_or_summon(owner)

    open_steward(owner, house_steward)
    owner.gossip_select("Privacy options")
    _, msgs = owner.gossip_select("Set house to public")
    priv = db("SELECT is_private FROM mod_playerhousing_house WHERE owner_guid=%d" % owner_guid)
    check("set house public", priv == [["0"]], " | ".join(msgs))

    log("== owner reconnects inside house")
    owner = relog(args, owner, owner_acc, owner_pw, owner_char)
    owner.pump(2.0)
    check("a dropped connection re-attaches inside the house", owner.map_id == HOUSING_MAP and steward_near(owner, 3.0) is not None,
          "map=%s" % owner.map_id)

    log("== owner logs out inside house and logs back in")
    owner = relog(args, owner, owner_acc, owner_pw, owner_char, clean=True)
    wait_for_map(owner, start_map, timeout=8.0)
    check("relog inside house returns the player to where they entered from", owner.map_id == start_map, "map=%s" % owner.map_id)
    owner.pump(2.0)
    check("normal phase restored: owner and guest see each other outside", sees(owner, guest) and sees(guest, owner))

    back = enter_own_house(owner)
    remaining = {int(r[3]) for r in placements(owner_guid) if r[3] != "0"}
    gos = go_entries(owner)
    check("furniture persists across relog", back and remaining <= gos, "expected=%s seen=%s" % (sorted(remaining), sorted(gos)))
    owner.command(".krook leave", wait=1.0)
    check("owner .krook leave", wait_for_map(owner, start_map), "map=%s" % owner.map_id)

    for c in (owner, guest):
        if c.errors:
            log("client parse errors: %s" % c.errors[:5])
        try:
            c.logout()
        except Exception:
            c.close()
    return finish()


def finish():
    passed = sum(1 for _, ok, _ in results if ok)
    log("\n%d/%d checks passed" % (passed, len(results)))
    for name, ok, detail in results:
        if not ok:
            log("  FAILED: %s %s" % (name, detail))
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    try:
        code = main()
    except Exception:
        import traceback
        traceback.print_exc()
        code = finish() or 1
    sys.exit(code)
