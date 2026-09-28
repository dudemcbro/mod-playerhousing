#!/usr/bin/env python3
"""Builds the housing content from tools/content/pieces.py:

  sql/db_world/base/mod_playerhousing_world_content.sql   items, objects, pieces, rules
  docs/UNLOCKS.md                                          the same list for people

Needs the world database (to copy models and behavior from existing gameobjects) and the
client data's dbc folder (for model sizes, achievement, faction and creature names):

  python3 tools/content/build_content.py [--dbc DIR] [--mysql "mysql -uacore -pacore acore_world"]
"""

import argparse
import os
import shlex
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MODULE = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import pieces as content  # noqa: E402

CATEGORIES = ["Starter", "Buildings", "Exploration", "Dungeons", "Raids", "Reputation", "Professions", "Holidays", "Capstones"]
FLAG_BITS = {"surface": 0x01, "small": 0x02, "per_char": 0x04, "gift": 0x08, "wreckage": 0x10, "stand": 0x20}
RANKS = ["Hated", "Hostile", "Unfriendly", "Neutral", "Friendly", "Honored", "Revered", "Exalted"]
SKILLS = {164: "Blacksmithing", 186: "Mining", 171: "Alchemy", 185: "Cooking", 202: "Engineering", 773: "Inscription",
          129: "First Aid", 197: "Tailoring", 165: "Leatherworking", 333: "Enchanting", 755: "Jewelcrafting",
          182: "Herbalism", 393: "Skinning", 356: "Fishing"}

GO_TYPE_CHAIR, GO_TYPE_GENERIC, GO_TYPE_GOOBER = 7, 5, 10
GO_SCRIPT = "go_playerhousing_piece"
ITEM_SCRIPT = "item_playerhousing_piece"
# The targeting circle of each piece comes from its item's spell. These ground-target spells
# have circles from 1 to 20 yards, so the circle shows how much room a piece takes. None of
# them ever casts: spell_playerhousing_place stops the cast as soon as the circle is clicked.
CIRCLE_SPELLS = [(1.0, 61736), (2.0, 47004), (3.0, 42340), (4.0, 69680), (5.0, 43440),
                 (6.0, 61985), (8.0, 34435), (10.0, 1543), (15.0, 26540), (20.0, 29882)]
PLACE_SCRIPT = "spell_playerhousing_place"


def circle_spell(footprint):
    """The spell whose circle best matches a piece of this radius (yards)."""
    for radius, spell in CIRCLE_SPELLS:
        if footprint <= radius * 1.1:
            return spell
    return CIRCLE_SPELLS[-1][1]

# Copy cost in copper on live servers (FreeMode makes everything free).
COST = {"Starter": 0, "Exploration": 5000, "Dungeons": 10000, "Raids": 50000, "Reputation": 10000,
        "Professions": 20000, "Holidays": 5000, "Capstones": 100000}
SHELTER_COST, FACTION_BUILDING_COST = 2000, 50000
QUALITY = {"Starter": 1, "Buildings": 2, "Exploration": 2, "Dungeons": 3, "Raids": 4, "Reputation": 3,
           "Professions": 2, "Holidays": 2, "Capstones": 4}
ICON_FURNISHING, ICON_BUILDING = 1102, 7744


def live_entry(item):
    return 910000 + (item - 900000)


def edit_entry(item):
    return 920000 + (item - 900000)


def read_dbc(path):
    with open(path, "rb") as f:
        data = f.read()
    magic, records, fields, size, strings = struct.unpack_from("<4s4I", data, 0)
    string_block = data[20 + records * size:]

    def text(offset):
        end = string_block.index(b"\0", offset)
        return string_block[offset:end].decode("utf-8", "replace")

    rows = []
    for i in range(records):
        base = 20 + i * size
        rows.append((struct.unpack_from("<%dI" % fields, data, base), struct.unpack_from("<%df" % fields, data, base)))
    return rows, text


def sql_text(value):
    return "'" + str(value).replace("\\", "\\\\").replace("'", "''") + "'"


class World:
    def __init__(self, mysql):
        self.mysql = shlex.split(mysql)

    def query(self, sql):
        out = subprocess.run(self.mysql + ["-N", "-B", "-e", sql], check=True, capture_output=True, text=True).stdout
        return [line.split("\t") for line in out.splitlines() if line]

    def gameobject(self, entry):
        rows = self.query("SELECT type, displayId, size, " + ", ".join("Data%d" % i for i in range(24)) +
                          " FROM gameobject_template WHERE entry=%d" % entry)
        if not rows:
            raise SystemExit("gameobject %d not found in the world database" % entry)
        row = rows[0]
        return {"type": int(row[0]), "display": int(row[1]), "size": float(row[2]), "data": [int(v) for v in row[3:27]]}

    def creature_name(self, entry):
        rows = self.query("SELECT name FROM creature_template WHERE entry=%d" % entry)
        return rows[0][0] if rows else "creature %d" % entry

    def quest_title(self, entry):
        rows = self.query("SELECT LogTitle FROM quest_template WHERE ID=%d" % entry)
        return rows[0][0] if rows else "quest %d" % entry


def load_names(dbc):
    boxes = {}
    rows, text = read_dbc(os.path.join(dbc, "GameObjectDisplayInfo.dbc"))
    for ints, floats in rows:
        boxes[ints[0]] = floats[12:18]

    achievements = {}
    rows, text = read_dbc(os.path.join(dbc, "Achievement.dbc"))
    for ints, _ in rows:
        achievements[ints[0]] = text(ints[4])

    factions = {}
    rows, text = read_dbc(os.path.join(dbc, "Faction.dbc"))
    for ints, _ in rows:
        factions[ints[0]] = text(ints[23])

    areas = {}
    rows, text = read_dbc(os.path.join(dbc, "AreaTable.dbc"))
    for ints, _ in rows:
        areas[ints[0]] = text(ints[11])
    return boxes, achievements, factions, areas


def describe_rule(rule, category, world, names):
    kind, p1, p2 = rule
    _, achievements, factions, areas = names
    if kind == 1:
        return "reach level %d" % p1
    if kind == 2:
        name = achievements.get(p1, "achievement %d" % p1)
        if name.startswith("Explore "):
            return name[0].lower() + name[1:]
        if category in ("Dungeons", "Raids"):
            return "complete " + name
        return "earn the achievement " + name
    if kind == 3:
        return "reach %s with %s" % (RANKS[min(p2, 7)], factions.get(p1, "faction %d" % p1))
    if kind == 4:
        return "complete the quest " + world.quest_title(p1)
    if kind == 5:
        return "defeat " + world.creature_name(p1)
    if kind == 6:
        return "discover " + areas.get(p1, "area %d" % p1)
    if kind == 7:
        return "reach %d in %s" % (p2, SKILLS.get(p1, "skill %d" % p1))
    return "not available"


def describe(piece, world, names):
    if piece.get("hint"):
        return piece["hint"]
    groups = piece.get("rules") or []
    if not groups:
        return ""
    parts = [" and ".join(describe_rule(rule, piece["cat"], world, names) for rule in group) for group in groups]
    text = " or ".join(parts)
    return text[0].upper() + text[1:]


def build(args):
    world = World(args.mysql)
    names = load_names(args.dbc)
    boxes = names[0]

    gameobjects, items, piece_rows, rule_rows, docs = [], [], [], [], {c: [] for c in CATEGORIES}
    seen = set()

    for order, piece in enumerate(content.PIECES):
        item = piece["item"]
        if item in seen:
            raise SystemExit("item %d is listed twice" % item)
        seen.add(item)
        category = piece["cat"]
        style = piece.get("style", "decor")
        building = style == "building"
        stand = style == "stand"
        name = piece["name"]

        if stand:
            # A stand is a figure the module dresses in the owner's gear: no object at all.
            source = {"type": GO_TYPE_GENERIC, "display": 0, "size": 1.0, "data": [0] * 24}
        elif "go" in piece:
            source = world.gameobject(piece["go"])
        else:
            source = {"type": GO_TYPE_GENERIC, "display": piece["display"], "size": 1.0, "data": [0] * 24}
        size = source["size"] * piece.get("scale", 1.0)
        display = source["display"]

        if style == "keep":
            go_type, data = source["type"], source["data"]
        elif style == "chair":
            go_type, data = GO_TYPE_CHAIR, [piece.get("slots", 1), piece.get("chair_height", 1)] + [0] * 22
        elif building:
            go_type, data = GO_TYPE_GENERIC, [0] * 24
        else:
            go_type, data = GO_TYPE_GOOBER, [0] * 24

        live = 0 if stand else live_entry(item)
        # Pieces that work like the real thing get a clickable copy for decorate mode.
        edit = edit_entry(item) if go_type not in (GO_TYPE_GOOBER, GO_TYPE_GENERIC) and not stand else 0

        def go_row(entry, gtype, gdata):
            return "(%d, %d, %d, %s, '', '', '', %s, %s, '', %s, 0)" % (
                entry, gtype, display, sql_text(name), repr(round(size, 4)), ", ".join(str(v) for v in gdata), sql_text(GO_SCRIPT))

        if live:
            gameobjects.append(go_row(live, go_type, data))
        if edit:
            gameobjects.append(go_row(edit, GO_TYPE_GOOBER, [0] * 24))

        box = boxes.get(display, (0.0,) * 6)
        footprint = max(abs(box[0]), abs(box[1]), abs(box[3]), abs(box[4])) * size
        height = box[5] * size
        footprint = piece.get("footprint", footprint) or (8.0 if building else 0.8)
        height = piece.get("height", height) or (8.0 if building else 1.0)
        if stand:
            footprint, height = 0.5, 2.0

        flags = FLAG_BITS["stand"] if stand else 0
        for flag in piece.get("flags", []):
            flags |= FLAG_BITS[flag]
        groups = piece.get("rules") or []
        if building and any(rule[0] == 3 and rule[2] >= 7 for group in groups for rule in group):
            flags |= FLAG_BITS["per_char"]

        if building:
            cost = FACTION_BUILDING_COST if flags & FLAG_BITS["per_char"] else SHELTER_COST
        else:
            cost = COST.get(category, 0)
        cost = piece.get("cost", cost)
        hint = describe(piece, world, names)

        piece_rows.append("(%d, %d, %d, %s, %d, %d, %s, %s, %s, %d, %d, %d, %s, %d)" % (
            item, 1 if building else 0, CATEGORIES.index(category), sql_text(name), live, edit, repr(piece.get("scale", 1.0)),
            repr(round(footprint, 2)), repr(round(height, 2)), flags, cost, order, sql_text(hint), piece.get("legacy", 0)))
        for group_index, group in enumerate(groups):
            for rule_index, (kind, p1, p2) in enumerate(group):
                rule_rows.append("(%d, %d, %d, %d, %d, %d)" % (item, group_index, rule_index, kind, p1, p2))

        prefix = "Building: " if building else "Furnishing: "
        description = ("Right-click on your island, then click where it should stand." if building
                       else "Right-click on your island, then click where it should go.")
        items.append("(%d, 15, 0, -1, %s, %d, %d, 0, 0, 1, 0, 0, 0, -1, -1, 1, 1, 0, 20, 1, %d, 0, 0, 0, -1, %s, %s, 0)" % (
            item, sql_text(prefix + name), ICON_BUILDING if building else ICON_FURNISHING, QUALITY.get(category, 1),
            circle_spell(footprint), sql_text(description), sql_text(ITEM_SCRIPT)))

        notes = []
        if stand:
            notes.append("wears real gear from your bags: armor, weapons, shields")
        if style in ("keep", "chair"):
            notes.append({"keep": "works like the real thing", "chair": "can be sat on"}[style])
        if flags & FLAG_BITS["surface"]:
            notes.append("things go on top")
        if flags & FLAG_BITS["small"]:
            notes.append("fits on tables")
        if flags & FLAG_BITS["gift"]:
            notes.append("given on first login")
        if flags & FLAG_BITS["wreckage"]:
            notes.append("waiting on the island at the first visit")
        if flags & FLAG_BITS["per_char"]:
            notes.append("per character")
        docs[category].append((name, "building" if building else "furnishing", hint or "Everyone has it",
                               "a figure in your gear" if stand else
                               "%s %d" % ("object" if "go" in piece else "model", piece.get("go", piece.get("display"))),
                               ", ".join(notes)))

    go_columns = "`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`, " + \
        ", ".join("`Data%d`" % i for i in range(24)) + ", `AIName`, `ScriptName`, `VerifiedBuild`"
    item_columns = ("`entry`, `class`, `subclass`, `SoundOverrideSubclass`, `name`, `displayid`, `Quality`, `Flags`, `FlagsExtra`, "
                    "`BuyCount`, `BuyPrice`, `SellPrice`, `InventoryType`, `AllowableClass`, `AllowableRace`, `ItemLevel`, "
                    "`RequiredLevel`, `maxcount`, `stackable`, `bonding`, `spellid_1`, `spelltrigger_1`, `spellcharges_1`, "
                    "`spellcooldown_1`, `spellcategorycooldown_1`, `description`, `ScriptName`, `VerifiedBuild`")

    sql = [
        "-- Generated by tools/content/build_content.py from tools/content/pieces.py. Edit the list",
        "-- there and run the builder again instead of changing this file.",
        "",
        "DELETE FROM `mod_playerhousing_piece_rule`;",
        "DELETE FROM `mod_playerhousing_piece`;",
        "DELETE FROM `item_template` WHERE `entry` BETWEEN 901100 AND 901199 OR `entry` BETWEEN 902001 AND 902999;",
        "DELETE FROM `gameobject_template` WHERE `entry` BETWEEN 911100 AND 922999;",
        "",
        "-- The spells behind the targeting circles, one per circle size.",
        "DELETE FROM `spell_script_names` WHERE `ScriptName` = '%s';" % PLACE_SCRIPT,
        "INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES",
        ",\n".join("(%d, '%s')" % (spell, PLACE_SCRIPT) for _, spell in CIRCLE_SPELLS) + ";",
        "",
        "INSERT INTO `gameobject_template` (%s) VALUES" % go_columns,
        ",\n".join(gameobjects) + ";",
        "",
        "INSERT INTO `item_template` (%s) VALUES" % item_columns,
        ",\n".join(items) + ";",
        "",
        "INSERT INTO `mod_playerhousing_piece` (`item_entry`, `kind`, `category`, `name`, `go_entry`, `edit_go_entry`, `scale`, "
        "`footprint`, `height`, `flags`, `copy_cost`, `sort_order`, `hint`, `legacy_catalog_id`) VALUES",
        ",\n".join(piece_rows) + ";",
        "",
        "INSERT INTO `mod_playerhousing_piece_rule` (`item_entry`, `rule_group`, `rule_index`, `rule_type`, `param1`, `param2`) VALUES",
        ",\n".join(rule_rows) + ";",
        "",
    ]
    with open(os.path.join(MODULE, "sql/db_world/base/mod_playerhousing_world_content.sql"), "w") as f:
        f.write("\n".join(sql))

    md = [
        "# Housing content",
        "",
        "Every piece a player can own and what unlocks it. Generated from `tools/content/pieces.py` by",
        "`tools/content/build_content.py`; edit the list there, not this file.",
        "",
        "Unlocked pieces are in the Collection (House Key, Collection), which hands out copies. Pieces with",
        "no condition are everyone's from the start. Faction buildings need Exalted with their faction and",
        "belong to the character that earned it; everything else is shared across the account.",
        "",
    ]
    for category in CATEGORIES:
        rows = docs[category]
        if not rows:
            continue
        md += ["## %s (%d)" % (category, len(rows)), "", "| Piece | Kind | How to unlock | Model | Notes |", "| --- | --- | --- | --- | --- |"]
        md += ["| %s | %s | %s | %s | %s |" % row for row in rows]
        md.append("")
    with open(os.path.join(MODULE, "docs/UNLOCKS.md"), "w") as f:
        f.write("\n".join(md))

    print("wrote %d pieces, %d objects, %d rules" % (len(piece_rows), len(gameobjects), len(rule_rows)))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dbc", default=os.environ.get("DBC_DIR", os.path.expanduser("~/acore-test-server/data/dbc")))
    parser.add_argument("--mysql", default=os.environ.get("WORLD_MYSQL", "mysql -uacore -pacore acore_world"))
    build(parser.parse_args())
