#!/usr/bin/env python3
"""Builds the housing content from tools/content/pieces.py:

  sql/db_world/base/mod_playerhousing_world_content.sql   items, objects, pieces, rules
  sql/db_world/base/mod_playerhousing_world_catalog.sql   every other object model (Catalog = everything)
  docs/UNLOCKS.md                                          the same list for people
  tools/gm-island-cleared/client_items.tsv                 bag icons, for the client patch

Needs the world database (to copy models and behavior from existing gameobjects) and the
client data's dbc folder (for model sizes, achievement, faction and creature names):

  python3 tools/content/build_content.py [--dbc DIR] [--mysql "mysql -uacore -pacore acore_world"]
"""

import argparse
import os
import re
import shlex
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MODULE = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import pieces as content  # noqa: E402
from icons import MOVER, icons_for  # noqa: E402

CATEGORIES = ["Starter", "Buildings", "Exploration", "Dungeons", "Raids", "Reputation", "Professions", "Holidays", "Capstones",
              "Figurines", "Catalog"]
# The catalog of every object (PlayerHousing.Catalog = everything): its own item and object
# ranges, so the curated content never touches it.
CATALOG_FIRST, CATALOG_LAST = 940000, 944999
CATALOG_COST = 1000         # copper a copy; FreeMode makes it free
CATALOG_LARGEST = 40.0      # yards: bigger models (whole areas, transports) are left out
FLAG_BITS = {"surface": 0x01, "small": 0x02, "per_char": 0x04, "gift": 0x08, "wreckage": 0x10, "stand": 0x20, "chest": 0x40, "music": 0x80,
             "figure": 0x100}
FIGURINE_SIZE = 0.9   # yards, the longest side of a figurine
FIGURE_SCRIPT = "npc_playerhousing_figurine"
RANKS = ["Hated", "Hostile", "Unfriendly", "Neutral", "Friendly", "Honored", "Revered", "Exalted"]
SKILLS = {164: "Blacksmithing", 186: "Mining", 171: "Alchemy", 185: "Cooking", 202: "Engineering", 773: "Inscription",
          129: "First Aid", 197: "Tailoring", 165: "Leatherworking", 333: "Enchanting", 755: "Jewelcrafting",
          182: "Herbalism", 393: "Skinning", 356: "Fishing"}

GO_TYPE_CHAIR, GO_TYPE_GENERIC, GO_TYPE_GOOBER = 7, 5, 10
# The client draws a world model (.wmo) only for a few object types (destructible buildings,
# transports); as a generic object it shows its error cube instead.
GO_TYPE_DESTRUCTIBLE_BUILDING = 33
GO_SCRIPT = "go_playerhousing_piece"
ITEM_SCRIPT = "item_playerhousing_piece"
# The targeting circle of each piece comes from its item's spell. These ground-target spells
# have circles from 1 to 20 yards, so the circle shows how much room a piece takes. None of
# them ever casts: spell_playerhousing_place stops the cast as soon as the circle is clicked.
# Each is an unused creature or quest spell with no description (the item's tooltip shows a
# spell's description as "Use: ..."), no cost, cooldown or global cooldown, and no script or
# condition of its own on the server. The game has no such spell with a 4 or 6 yard circle.
CIRCLE_SPELLS = [(1.0, 61736), (2.0, 52923), (3.0, 53261), (5.0, 68316), (8.0, 45959),
                 (10.0, 54686), (15.0, 48431), (18.0, 55295), (20.0, 32150)]
PLACE_SCRIPT = "spell_playerhousing_place"
MOVER_FIRST = 901190  # Housing::MOVER_ITEM_FIRST, one "Move a Piece" item per circle


def circle_spell(footprint):
    """The spell whose circle best matches a piece of this radius (yards)."""
    for radius, spell in CIRCLE_SPELLS:
        if footprint <= radius * 1.1:
            return spell
    return CIRCLE_SPELLS[-1][1]

# Copy cost in copper on live servers (FreeMode makes everything free).
COST = {"Starter": 0, "Exploration": 5000, "Dungeons": 10000, "Raids": 50000, "Reputation": 10000,
        "Professions": 20000, "Holidays": 5000, "Capstones": 100000, "Figurines": 20000}
SHELTER_COST, FACTION_BUILDING_COST = 2000, 50000
QUALITY = {"Starter": 1, "Buildings": 2, "Exploration": 2, "Dungeons": 3, "Raids": 4, "Reputation": 3,
           "Professions": 2, "Holidays": 2, "Capstones": 4, "Figurines": 3}
# Bag icons: an icon some item already uses keeps that item's display; any other icon (spell
# and achievement icons) gets a display of its own, added by the client patch.
CUSTOM_DISPLAY_BASE = 190000   # plus the icon's SpellIcon.dbc id, so ids never move
HOUSE_KEY, HOUSE_KEY_DISPLAY = 902000, 22071   # as in mod_playerhousing_world.sql
CLIENT_ITEMS = os.path.join(MODULE, "tools/gm-island-cleared/client_items.tsv")
# Ghosts: the see-through copy of a piece that follows a player placing or moving it. Its
# creature display (and model) is Housing::GHOST_DISPLAY_BASE plus the item's offset.
GHOST_DISPLAY_BASE = 60000
GHOST_ALPHA = 150            # 0 (unseen) to 255 (solid)
MANNEQUIN_DISPLAY = 49       # the mannequin's figure, as in mod_playerhousing_world.sql
# A building's ghost: a see-through block its size, a model the client patch writes
# (tools/gm-island-cleared/make_ghost_blocks.py).
GHOST_BLOCK_PATH = "World\\PlayerHousing\\GhostBlock%d.m2"


def ghost_id(item):
    return GHOST_DISPLAY_BASE + item - 900000


def live_entry(item):
    return 910000 + (item - 900000)


def edit_entry(item):
    return 920000 + (item - 900000)


def figure_entry(item):
    """The creature a figurine shows (a copy of its boss, named after the figurine)."""
    return 930000 + (item - 900000)


def load_creature_boxes(dbc):
    """Bounding box of each creature display at scale 1, from CreatureDisplayInfo.dbc and
    CreatureModelData.dbc: low x, y, z, high x, y, z."""
    rows, _ = read_dbc(os.path.join(dbc, "CreatureModelData.dbc"))
    models = {ints[0]: tuple(v * floats[4] for v in floats[17:23]) for ints, floats in rows}
    rows, _ = read_dbc(os.path.join(dbc, "CreatureDisplayInfo.dbc"))
    boxes = {}
    for ints, floats in rows:
        box = models.get(ints[1])
        if box:
            boxes[ints[0]] = tuple(v * (floats[4] or 1.0) for v in box)
    return boxes


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

    def creature_display(self, entry):
        rows = self.query("SELECT CreatureDisplayID, DisplayScale FROM creature_template_model WHERE CreatureID=%d ORDER BY Idx LIMIT 1" % entry)
        if not rows:
            raise SystemExit("creature %d has no model in the world database" % entry)
        return int(rows[0][0]), float(rows[0][1] or 1.0)

    def creature_name(self, entry):
        rows = self.query("SELECT name FROM creature_template WHERE entry=%d" % entry)
        return rows[0][0] if rows else "creature %d" % entry

    def quest_title(self, entry):
        rows = self.query("SELECT LogTitle FROM quest_template WHERE ID=%d" % entry)
        return rows[0][0] if rows else "quest %d" % entry


def load_world_model_bounds(dbc):
    """Bounding boxes of object models from the server's collision data (vmaps), which,
    unlike GameObjectDisplayInfo.dbc, has them for world models (.wmo) too. Empty when the
    data isn't next to the dbc folder."""
    bounds = {}
    path = os.path.join(os.path.dirname(os.path.normpath(dbc)), "vmaps", "GameObjectModels.dtree")
    if not os.path.exists(path):
        return bounds
    with open(path, "rb") as f:
        data = f.read()
    pos = 8  # magic
    while pos + 9 <= len(data):
        display, is_wmo, name_length = struct.unpack_from("<IBI", data, pos)
        pos += 9 + name_length
        box = struct.unpack_from("<6f", data, pos)
        pos += 24
        bounds[display] = box  # low x, y, z, high x, y, z
    return bounds


def model_frame(box):
    """How to frame a model in the addon's preview: its longest side and the middle of its
    bounds (x, y, z), in the model's own units. Zeros when the dbc has no bounds for it."""
    if not box or not any(box):
        return (0.0, 0.0, 0.0, 0.0)
    return (max(box[3] - box[0], box[4] - box[1], box[5] - box[2]),
            (box[0] + box[3]) / 2, (box[1] + box[4]) / 2, (box[2] + box[5]) / 2)


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


def load_models(dbc):
    """Model file of each gameobject display, for the addon's preview."""
    models = {}
    rows, text = read_dbc(os.path.join(dbc, "GameObjectDisplayInfo.dbc"))
    for ints, _ in rows:
        models[ints[0]] = text(ints[1])
    return models


class Ghosts:
    """The ghosts' creature models and displays. A piece's own model gets a creature model of
    its own; a figurine or a mannequin, already a creature, gets a see-through copy of its
    display. The server reads them from its *_dbc tables; the client patch adds the same rows
    to the client's files (tools/gm-island-cleared/dbc_add_ghosts.py)."""

    MODEL_COLUMNS = ("`ID`, `Flags`, `ModelName`, `SizeClass`, `ModelScale`, `BloodID`, `FootprintTextureID`, `FootprintTextureLength`, "
                     "`FootprintTextureWidth`, `FootprintParticleScale`, `FoleyMaterialID`, `FootstepShakeSize`, `DeathThudShakeSize`, "
                     "`SoundID`, `CollisionWidth`, `CollisionHeight`, `MountHeight`, `GeoBoxMinX`, `GeoBoxMinY`, `GeoBoxMinZ`, "
                     "`GeoBoxMaxX`, `GeoBoxMaxY`, `GeoBoxMaxZ`, `WorldEffectScale`, `AttachedEffectScale`, `MissileCollisionRadius`, "
                     "`MissileCollisionPush`, `MissileCollisionRaise`")
    DISPLAY_COLUMNS = ("`ID`, `ModelID`, `SoundID`, `ExtendedDisplayInfoID`, `CreatureModelScale`, `CreatureModelAlpha`, "
                       "`TextureVariation_1`, `TextureVariation_2`, `TextureVariation_3`, `PortraitTextureName`, `BloodLevel`, "
                       "`BloodID`, `NPCSoundID`, `ParticleColorID`, `CreatureGeosetData`, `ObjectEffectPackageID`")

    def __init__(self, dbc):
        rows, text = read_dbc(os.path.join(dbc, "CreatureDisplayInfo.dbc"))
        # Every field of each display, strings read, for copies (-1 stays -1: the server's
        # columns are signed).
        signed = lambda value: value - (1 << 32) if value >= 1 << 31 else value
        self.displays = {ints[0]: [signed(v) for v in ints[:4]] + [floats[4], signed(ints[5])] + [text(ints[i]) for i in range(6, 10)]
                         + [signed(v) for v in ints[10:16]] for ints, floats in rows}
        self.models = []   # (ghost id, model path, box)
        self.copies = []   # (ghost id, the display copied)
        self.blocks = []   # (ghost id, model path, box): the models the client patch writes

    def model(self, item, path, box):
        # The server's creaturemodeldata_dbc keeps 100 characters of the path: a model with a
        # longer one gets no ghost (it's carried as it is instead).
        if path.lower().endswith((".m2", ".mdx")) and len(path) <= 100:
            self.models.append((ghost_id(item), path, tuple(box or (0.0,) * 6)))

    def block(self, item, box):
        # A building: a block from its bounds (in its model's units), for the patch to write.
        if box[3] > box[0] and box[4] > box[1] and box[5] > box[2]:
            entry = (ghost_id(item), GHOST_BLOCK_PATH % item, tuple(box))
            self.models.append(entry)
            self.blocks.append(entry)

    def copy(self, item, display):
        if display in self.displays:
            self.copies.append((ghost_id(item), display))

    def sql(self, first_item, last_item):
        """The server's rows for the items first_item to last_item."""
        low, high = ghost_id(first_item), ghost_id(last_item)
        models = [m for m in self.models if low <= m[0] <= high]
        copies = [c for c in self.copies if low <= c[0] <= high]
        sql = [
            "-- Ghosts: see-through copies of the pieces, for placing and moving them (the client patch",
            "-- adds the same models and displays to the client).",
            "DELETE FROM `creaturedisplayinfo_dbc` WHERE `ID` BETWEEN %d AND %d;" % (low, high),
            "DELETE FROM `creaturemodeldata_dbc` WHERE `ID` BETWEEN %d AND %d;" % (low, high),
            "DELETE FROM `creature_model_info` WHERE `DisplayID` BETWEEN %d AND %d;" % (low, high),
        ]
        model_rows = ["(%d, 0, %s, 0, 1.0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, %s, 1.0, 1.0, 0, 0, 0)" % (
            ghost, sql_text(path), ", ".join(repr(round(v, 3)) for v in box)) for ghost, path, box in models]
        display_rows = ["(%d, %d, 0, 0, 1.0, %d, '', '', '', '', 0, 0, 0, 0, 0, 0)" % (ghost, ghost, GHOST_ALPHA) for ghost, _, _ in models]
        for ghost, source in copies:
            row = self.displays[source]
            display_rows.append("(%d, %d, %d, %d, %s, %d, %s, %s, %s, %s, %d, %d, %d, %d, %d, %d)" % (
                (ghost,) + tuple(row[1:4]) + (repr(round(row[4], 4)), GHOST_ALPHA) + tuple(sql_text(t) for t in row[6:10]) + tuple(row[10:16])))
        info_rows = ["(%d, 0.5, 1.0, 2, 0, 0)" % ghost for ghost in sorted([m[0] for m in models] + [c[0] for c in copies])]
        for start in range(0, len(model_rows), 500):
            sql += ["INSERT INTO `creaturemodeldata_dbc` (%s) VALUES" % self.MODEL_COLUMNS, ",\n".join(model_rows[start:start + 500]) + ";"]
        for start in range(0, len(display_rows), 500):
            sql += ["INSERT INTO `creaturedisplayinfo_dbc` (%s) VALUES" % self.DISPLAY_COLUMNS, ",\n".join(display_rows[start:start + 500]) + ";"]
        for start in range(0, len(info_rows), 500):
            sql += ["INSERT INTO `creature_model_info` (`DisplayID`, `BoundingRadius`, `CombatReach`, `Gender`, `DisplayID_Other_Gender`, "
                    "`VerifiedBuild`) VALUES", ",\n".join(info_rows[start:start + 500]) + ";"]
        return sql + [""]

    def client_lines(self):
        lines = ["ghostmodel\t%d\t%s\t%s" % (ghost, path, " ".join(repr(round(v, 3)) for v in box)) for ghost, path, box in self.models]
        lines += ["ghostcopy\t%d\t%d" % (ghost, source) for ghost, source in self.copies]
        lines += ["ghostblock\t%d\t%s\t%s" % (ghost, path, " ".join(repr(round(v, 3)) for v in box)) for ghost, path, box in self.blocks]
        return lines


class Icons:
    """Turns icon names into item display ids, remembering the displays the patch must add."""

    def __init__(self, dbc):
        self.items, self.spells, self.added, self.names = {}, {}, {}, {}
        rows, text = read_dbc(os.path.join(dbc, "ItemDisplayInfo.dbc"))
        for ints, _ in rows:
            icon = text(ints[5])
            if icon and (icon.lower() not in self.items or ints[0] < self.items[icon.lower()]):
                self.items[icon.lower()] = ints[0]
                self.names[ints[0]] = icon
        rows, text = read_dbc(os.path.join(dbc, "SpellIcon.dbc"))
        for ints, _ in rows:
            icon = text(ints[1]).replace("\\", "/").split("/")[-1]
            if icon:
                self.spells.setdefault(icon.lower(), (ints[0], icon))

    def display(self, candidates):
        for icon in candidates:
            if icon.lower() in self.items:
                return self.items[icon.lower()]
            if icon.lower() in self.spells:
                spell_icon, name = self.spells[icon.lower()]
                self.added[CUSTOM_DISPLAY_BASE + spell_icon] = name
                self.names[CUSTOM_DISPLAY_BASE + spell_icon] = name
                return CUSTOM_DISPLAY_BASE + spell_icon
        raise SystemExit("the client has none of these icons: %s" % ", ".join(candidates))

    def name(self, display):
        return self.names[display]


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
    models = load_models(args.dbc)
    world_bounds = load_world_model_bounds(args.dbc)
    creature_boxes = load_creature_boxes(args.dbc)
    icons = Icons(args.dbc)
    ghosts = Ghosts(args.dbc)
    client_items = [(HOUSE_KEY, HOUSE_KEY_DISPLAY)]
    infos = []   # the addon's piece list
    creatures = []
    previews = []
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
        figure = style == "figure"
        name = piece["name"]
        creature_display = figure_box = None

        if stand:
            # A stand is a figure the module dresses in the owner's gear: no object at all.
            source = {"type": GO_TYPE_GENERIC, "display": 0, "size": 1.0, "data": [0] * 24}
        elif figure:
            # A figurine is its creature's model, frozen and shrunk to fit on a table.
            creature_display, display_scale = world.creature_display(piece["creature"])
            figure_box = tuple(v * display_scale for v in creature_boxes.get(creature_display, (-0.5, -0.5, 0.0, 0.5, 0.5, 1.0)))
            extent = max(figure_box[3] - figure_box[0], figure_box[4] - figure_box[1], figure_box[5] - figure_box[2], 0.1)
            source = {"type": GO_TYPE_GENERIC, "display": 0, "size": FIGURINE_SIZE / extent, "data": [0] * 24}
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
        elif building and models.get(display, "").lower().endswith(".wmo"):
            go_type, data = GO_TYPE_DESTRUCTIBLE_BUILDING, [0] * 24
        elif building:
            go_type, data = GO_TYPE_GENERIC, [0] * 24
        else:
            go_type, data = GO_TYPE_GOOBER, [0] * 24

        live = 0 if stand or figure else live_entry(item)
        # Pieces that work like the real thing, and every building, get a clickable goober copy
        # while the housing window is open. Generic/destructible building objects otherwise do
        # not deliver clicks to the script, which made buildings movable only from the Placed tab.
        edit = edit_entry(item) if (building or go_type not in (GO_TYPE_GOOBER, GO_TYPE_GENERIC, GO_TYPE_DESTRUCTIBLE_BUILDING)) and not stand and not figure else 0

        def go_row(entry, gtype, gdata):
            return "(%d, %d, %d, %s, '', '', '', %s, %s, '', %s, 0)" % (
                entry, gtype, display, sql_text(name), repr(round(size, 4)), ", ".join(str(v) for v in gdata), sql_text(GO_SCRIPT))

        if live:
            gameobjects.append(go_row(live, go_type, data))
        if edit:
            gameobjects.append(go_row(edit, GO_TYPE_GOOBER, [0] * 24))

        box = figure_box if figure else boxes.get(display, (0.0,) * 6)
        footprint = max(abs(box[0]), abs(box[1]), abs(box[3]), abs(box[4])) * size
        height = box[5] * size
        # Length and depth, for the addon's floor plan and size text.
        length, depth = (box[3] - box[0]) * size, (box[4] - box[1]) * size
        footprint = piece.get("footprint", footprint) or (8.0 if building else 0.8)
        height = piece.get("height", height) or (8.0 if building else 1.0)
        if stand:
            footprint, height = 0.5, 2.0
        # The outline on the ground, in the piece's own frame (x forward): what counts as
        # inside a building.
        outline = (box[0] * size, box[1] * size, box[3] * size, box[4] * size)
        if length <= 0 or depth <= 0 or stand or "footprint" in piece:
            # No bounds recorded, or overridden: the footprint is all there is.
            length = depth = 2 * footprint
            outline = (-footprint, -footprint, footprint, footprint)

        if figure:
            creatures.append("(%d, %s, %s, %d, %d, %s)" % (figure_entry(item), sql_text(name), sql_text(FIGURE_SCRIPT), creature_display,
                                                            piece["creature"], sql_text(name)))
        elif not stand and display not in models:
            raise SystemExit("%s (%d) uses model %d, which isn't in GameObjectDisplayInfo.dbc: it would be invisible"
                             % (name, item, display))

        # What the addon shows before placing. Model frames can't draw world models (.wmo), so
        # those buildings get a floor plan, shaped by the server's collision data where it has
        # the model.
        model = models.get(display, "")
        preview_size = (length, depth, height)
        world_box = world_bounds.get(display)
        if world_box and model.lower().endswith(".wmo"):
            preview_size = tuple((world_box[i + 3] - world_box[i]) * size for i in range(3))
            # The same outline sizes the building itself (its targeting circle, and what
            # counts as inside it), unless pieces.py says otherwise. Some models include
            # surrounding pieces and come out too big: check those in game and set their
            # footprint in pieces.py.
            if "footprint" not in piece:
                footprint = max(abs(world_box[0]), abs(world_box[1]), abs(world_box[3]), abs(world_box[4])) * size
                outline = (world_box[0] * size, world_box[1] * size, world_box[3] * size, world_box[4] * size)
            if "height" not in piece and world_box[5] > 0:
                height = world_box[5] * size
        if stand:
            model = "player"
        elif figure:
            model = "creature:%d" % piece["creature"]
        elif not model or model.lower().endswith(".wmo"):
            model = None
        else:
            model = os.path.splitext(model)[0] + ".m2"
        framing = model_frame(boxes.get(display)) if model and model != "player" and not model.startswith("creature:") else model_frame(None)
        previews.append((item, model) + preview_size + framing)
        # Its ghost, for placing and moving it. M2 buildings use the same exact translucent
        # model as furnishings; only world-model (.wmo) buildings need a different fallback,
        # because the client cannot display a WMO as a creature.
        if stand:
            ghosts.copy(item, MANNEQUIN_DISPLAY)
        elif figure:
            ghosts.copy(item, creature_display)
        elif not building or not models.get(display, "").lower().endswith(".wmo"):
            ghosts.model(item, models.get(display, ""), boxes.get(display))
        # WMO buildings deliberately have no creature ghost: the server carries their real
        # collisionless game object, so their doors, porches and front are visible.

        flags = FLAG_BITS["stand"] if stand else 0
        if figure:
            flags |= FLAG_BITS["figure"] | FLAG_BITS["small"]
        if style in ("chest", "music"):
            flags |= FLAG_BITS[style]
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

        piece_rows.append("(%d, %d, %d, %s, %d, %d, %d, %s, %s, %s, %d, %d, %d, %s, %d, %s)" % (
            item, 1 if building else 0, CATEGORIES.index(category), sql_text(name), live, edit, figure_entry(item) if figure else 0,
            repr(round(size, 4) if figure else piece.get("scale", 1.0)),
            repr(round(footprint, 2)), repr(round(height, 2)), flags, cost, order, sql_text(hint), piece.get("legacy", 0),
            ", ".join(repr(round(v, 2)) for v in outline)))
        for group_index, group in enumerate(groups):
            for rule_index, (kind, p1, p2) in enumerate(group):
                rule_rows.append("(%d, %d, %d, %d, %d, %d)" % (item, group_index, rule_index, kind, p1, p2))

        prefix = "Building: " if building else "Furnishing: "
        description = ("Right-click on your island, then click where it should stand." if building
                       else "Right-click on your island, then click where it should go.")
        icon = icons.display(piece["icon"] if "icon" in piece else icons_for(name, style, building))
        client_items.append((item, icon))
        infos.append((item, name, CATEGORIES.index(category) + 1, 1 if building else 0, cost, icons.name(icon), hint))
        items.append("(%d, 15, 0, -1, %s, %d, %d, 0, 0, 1, 0, 0, 0, -1, -1, 1, 1, 0, 20, 1, %d, 0, 0, 0, -1, %s, %s, 0)" % (
            item, sql_text(prefix + name), icon, QUALITY.get(category, 1),
            circle_spell(footprint), sql_text(description), sql_text(ITEM_SCRIPT)))

        notes = []
        if stand:
            notes.append("wears real gear from your bags: armor, weapons, shields")
        if figure:
            notes.append("a figurine of %s" % world.creature_name(piece["creature"]))
        if style == "chest":
            notes.append("opens your bank and House Storage")
        if style == "music":
            notes.append("plays the island's music")
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
                               "a figure in your gear" if stand else "creature %d" % piece["creature"] if figure else
                               "%s %d" % ("object" if "go" in piece else "model", piece.get("go", piece.get("display"))),
                               ", ".join(notes)))

    # "Move a Piece": one per circle size, handed out to move a placed piece with the circle
    # (PlayerHousingMgr::StartMove). Not pieces themselves.
    mover_icon = icons.display(MOVER)
    for index, (radius, spell) in enumerate(CIRCLE_SPELLS):
        client_items.append((MOVER_FIRST + index, mover_icon))
        items.append("(%d, 15, 0, -1, %s, %d, 1, 0, 0, 1, 0, 0, 0, -1, -1, 1, 1, 1, 1, 1, %d, 0, 0, 0, -1, %s, %s, 0)" % (
            MOVER_FIRST + index, sql_text("Move a Piece"), mover_icon, spell,
            sql_text("Right-click, then click where the piece should go. Gone once used."), sql_text(ITEM_SCRIPT)))

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
        "DELETE FROM `mod_playerhousing_piece_rule` WHERE `item_entry` < %d;" % CATALOG_FIRST,
        "DELETE FROM `mod_playerhousing_piece` WHERE `item_entry` < %d;" % CATALOG_FIRST,
        "DELETE FROM `item_template` WHERE `entry` BETWEEN 901100 AND 901199 OR `entry` BETWEEN 902001 AND 902999;",
        "DELETE FROM `gameobject_template` WHERE `entry` BETWEEN 911100 AND 922999;",
        "DELETE FROM `creature_template_model` WHERE `CreatureID` BETWEEN 931100 AND 932999;",
        "DELETE FROM `creature_template` WHERE `entry` BETWEEN 931100 AND 932999;",
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
        "-- Figurines: each one a creature of its own, with its boss's model (frozen and shrunk by",
        "-- the module when placed).",
        "CREATE TEMPORARY TABLE `ph_figure` (`entry` int unsigned, `name` varchar(80), `script` varchar(64), `display` int unsigned,",
        "  `source` int unsigned, `subname` varchar(80));",
        "INSERT INTO `ph_figure` VALUES",
        (",\n".join(creatures) if creatures else "(0, '', '', 0, 0, '')") + ";",
        "INSERT INTO `creature_template` (`entry`, `name`, `subname`, `gossip_menu_id`, `minlevel`, `maxlevel`, `faction`, `npcflag`,",
        "  `unit_class`, `unit_flags`, `type`, `AIName`, `MovementType`, `RegenHealth`, `ScriptName`, `VerifiedBuild`)",
        "SELECT `entry`, `name`, 'Figurine', 0, 1, 1, 35, 1, 1, 770, 10, '', 0, 1, `script`, 0 FROM `ph_figure` WHERE `entry` <> 0;",
        "INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`)",
        "SELECT `entry`, 0, `display`, 1.0, 1.0, 0 FROM `ph_figure` WHERE `entry` <> 0;",
        "DROP TEMPORARY TABLE `ph_figure`;",
        "",
        "INSERT INTO `mod_playerhousing_piece` (`item_entry`, `kind`, `category`, `name`, `go_entry`, `edit_go_entry`, `creature_entry`, `scale`, "
        "`footprint`, `height`, `flags`, `copy_cost`, `sort_order`, `hint`, `legacy_catalog_id`, "
        "`outline_min_x`, `outline_min_y`, `outline_max_x`, `outline_max_y`) VALUES",
        ",\n".join(piece_rows) + ";",
        "",
        "INSERT INTO `mod_playerhousing_piece_rule` (`item_entry`, `rule_group`, `rule_index`, `rule_type`, `param1`, `param2`) VALUES",
        ",\n".join(rule_rows) + ";",
        "",
    ]
    sql += ghosts.sql(901100, 902999)
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

    curated_displays = {int(world.gameobject(p["go"])["display"]) for p in content.PIECES if "go" in p}
    curated_displays |= {p["display"] for p in content.PIECES if "display" in p}
    catalog_count = build_catalog(world, models, boxes, curated_displays, previews, icons, client_items, infos, ghosts)
    write_client_items(icons, client_items, ghosts)
    write_piece_info(infos)
    md += ["## Catalog (%d)" % catalog_count, "",
           "With `PlayerHousing.Catalog = everything`, every other object model in the game is a piece too, one per",
           "model, everyone's from the start (search the Collection for them). They come from",
           "`sql/db_world/base/mod_playerhousing_world_catalog.sql`.", ""]
    with open(os.path.join(MODULE, "docs/UNLOCKS.md"), "w") as f:
        f.write("\n".join(md))

    lua = ["-- Generated by tools/content/build_content.py from tools/content/pieces.py: each piece's",
           "-- model and size (yards long, deep and tall) for the addon's preview. Edit the list there.",
           "-- false: no model to show (world model buildings get a floor plan); \"player\": a mannequin.",
           "-- Then how to frame the model: its longest side and the middle of its bounds (x, y, z),",
           "-- in the model's own units (0 when unknown).",
           "PlayerHousing_Models = {"]
    for item, model, length, depth, height, fit, mid_x, mid_y, mid_z in previews:
        shown = "false" if model is None else '"%s"' % model.replace("\\", "\\\\")
        lua.append("    [%d] = { %s, %.1f, %.1f, %.1f, %.2f, %.2f, %.2f, %.2f }," % (item, shown, length, depth, height, fit, mid_x, mid_y,
                                                                                   mid_z))
    lua.append("}")
    with open(os.path.join(MODULE, "client-addon/PlayerHousing/PieceModels.lua"), "w") as f:
        f.write("\n".join(lua) + "\n")

    print("wrote %d pieces, %d objects, %d rules" % (len(piece_rows), len(gameobjects), len(rule_rows)))


def lua_text(value):
    return '"' + str(value).replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def write_piece_info(infos):
    """The addon's list of every piece, for its Collection tab."""
    lua = ["-- Generated by tools/content/build_content.py from tools/content/pieces.py: every piece, in the",
           "-- Collection's order, for the addon's window. Edit the list there.",
           "-- { item, name, category (PlayerHousing_Categories), building (1 or 0), copper a copy, icon, how to unlock }",
           "PlayerHousing_Categories = { %s }" % ", ".join(lua_text(c) for c in CATEGORIES),
           "PlayerHousing_Pieces = {"]
    for item, name, category, building, cost, icon, hint in infos:
        lua.append("    { %d, %s, %d, %d, %d, %s, %s }," % (item, lua_text(name), category, building, cost, lua_text(icon), lua_text(hint)))
    lua.append("}")
    with open(os.path.join(MODULE, "client-addon/PlayerHousing/PieceInfo.lua"), "w") as f:
        f.write("\n".join(lua) + "\n")


def write_client_items(icons, client_items, ghosts):
    """The rows make_client_patch.sh adds to the client: the housing items, so they show their
    icons, and the ghosts' models."""
    lines = ["# Generated by tools/content/build_content.py: what make_client_patch.sh adds to the",
             "# client so housing items show their icons, and ghosts (see-through pieces) can be seen.",
             "#   item       <entry> <display>        a row of Item.dbc (class 15, miscellaneous)",
             "#   display    <id> <icon>              a row of ItemDisplayInfo.dbc with only an icon",
             "#   ghostmodel <id> <model> <box>       rows of CreatureModelData.dbc and CreatureDisplayInfo.dbc (see-through)",
             "#   ghostcopy  <id> <display>           a see-through copy of a CreatureDisplayInfo.dbc row",
             "#   alpha      <0-255>                  how solid ghosts look"]
    lines += ["alpha\t%d" % GHOST_ALPHA]
    lines += ["display\t%d\t%s" % (display, name) for display, name in sorted(icons.added.items())]
    lines += ["item\t%d\t%d" % (item, display) for item, display in sorted(client_items)]
    lines += ghosts.client_lines()
    with open(CLIENT_ITEMS, "w") as f:
        f.write("\n".join(lines) + "\n")


def catalog_name(go_name, model):
    """A readable name: the object's own, unless it's an internal one, then the model's."""
    bad = ("[", "DND", "Doodad", "doodad", "_", "TEST", "Test ", "zz", "PH ", "(", "Dummy", "Trigger", "Bunny", "Visual", "Effect",
           " - ", ",")
    name = go_name.strip()
    # Internal names are often all capitals or lists of parts; those get the model's name.
    if name and len(name) <= 60 and not any(part in name for part in bad) and not (name.isupper() and len(name) > 3):
        return name
    base = os.path.splitext(model.replace("\\", "/").split("/")[-1])[0]
    base = re.sub(r"(?<=[a-z])(?=[A-Z])|_", " ", base)
    base = re.sub(r"\s*\d+$", "", base).strip()
    words = base.split()
    if len(words) > 1 and words[0] in ("G", "ND", "DNR"):  # "generic" and similar prefixes
        words = words[1:]
    return " ".join(word.capitalize() if word.islower() or (word.isupper() and len(word) > 2) else word for word in words) or "Object"


def build_catalog(world, models, boxes, curated_displays, previews, icons, client_items, infos, ghosts):
    """Every other object model in the game, one piece each: the catalog. Returns how many."""
    rows = world.query("SELECT entry, displayId, name, size FROM gameobject_template WHERE displayId > 0 ORDER BY entry")
    chosen = {}
    for entry, display, name, size in rows:
        display, entry = int(display), int(entry)
        model = models.get(display, "")
        # Small and medium models from the world folders; buildings and whole areas (.wmo),
        # spell effects and creatures stay out.
        lowered = (model + " " + name).lower()
        if display in curated_displays or not model.lower().endswith((".m2", ".mdx")) or not model.lower().startswith("world"):
            continue
        # Collision shapes, blockers and markers: nothing to see.
        if any(word in lowered for word in ("collision", "invisible", "blocker", "occluder", "bounding", "camerashake", "camera shake",
                                             "trigger")):
            continue
        current = chosen.get(display)
        good = catalog_name(name, model) == name.strip()
        if current is None or (good and not current[3]):
            chosen[display] = (entry, name, float(size or 1.0), good, model)

    gameobjects, items, piece_rows, used = [], [], [], {}
    item = CATALOG_FIRST
    for display, (entry, go_name, size, _, model) in sorted(chosen.items(), key=lambda kv: catalog_name(kv[1][1], kv[1][4]).lower()):
        box = boxes.get(display)
        if not box:
            continue
        length, depth, height = ((box[3] - box[0]) * size, (box[4] - box[1]) * size, (box[5] - box[2]) * size)
        if max(length, depth, height) > CATALOG_LARGEST or max(length, depth, height) < 0.05:
            continue
        if item > CATALOG_LAST:
            break

        name = catalog_name(go_name, model)
        used[name] = used.get(name, 0) + 1
        if used[name] > 1:
            name = "%s (%d)" % (name, used[name])
        footprint = max(0.3, max(abs(box[0]), abs(box[1]), abs(box[3]), abs(box[4])) * size)
        flags = FLAG_BITS["small"] if height < 1.0 and footprint < 0.6 else 0
        outline = (box[0] * size, box[1] * size, box[3] * size, box[4] * size)

        gameobjects.append("(%d, %d, %d, %s, '', '', '', %s, %s, '', %s, 0)" % (
            live_entry(item), GO_TYPE_GOOBER, display, sql_text(name), repr(round(size, 4)), ", ".join("0" for _ in range(24)),
            sql_text(GO_SCRIPT)))
        icon = icons.display(icons_for(name))
        client_items.append((item, icon))
        infos.append((item, name, CATEGORIES.index("Catalog") + 1, 0, CATALOG_COST, icons.name(icon), ""))
        items.append("(%d, 15, 0, -1, %s, %d, 1, 0, 0, 1, 0, 0, 0, -1, -1, 1, 1, 0, 20, 1, %d, 0, 0, 0, -1, %s, %s, 0)" % (
            item, sql_text("Furnishing: " + name), icon, circle_spell(footprint),
            sql_text("Right-click on your island, then click where it should go."), sql_text(ITEM_SCRIPT)))
        piece_rows.append("(%d, 0, %d, %s, %d, 0, 0, 1.0, %s, %s, %d, %d, %d, '', 0, %s)" % (
            item, CATEGORIES.index("Catalog"), sql_text(name), live_entry(item), repr(round(footprint, 2)), repr(round(max(height, 0.1), 2)),
            flags, CATALOG_COST, 100000 + item - CATALOG_FIRST, ", ".join(repr(round(v, 2)) for v in outline)))
        previews.append((item, os.path.splitext(model)[0] + ".m2", length, depth, height) + model_frame(box))
        ghosts.model(item, model, box)
        item += 1

    go_columns = "`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`, " + \
        ", ".join("`Data%d`" % i for i in range(24)) + ", `AIName`, `ScriptName`, `VerifiedBuild`"
    item_columns = ("`entry`, `class`, `subclass`, `SoundOverrideSubclass`, `name`, `displayid`, `Quality`, `Flags`, `FlagsExtra`, "
                    "`BuyCount`, `BuyPrice`, `SellPrice`, `InventoryType`, `AllowableClass`, `AllowableRace`, `ItemLevel`, "
                    "`RequiredLevel`, `maxcount`, `stackable`, `bonding`, `spellid_1`, `spelltrigger_1`, `spellcharges_1`, "
                    "`spellcooldown_1`, `spellcategorycooldown_1`, `description`, `ScriptName`, `VerifiedBuild`")
    sql = [
        "-- Generated by tools/content/build_content.py: every object model in the game that isn't a",
        "-- curated piece, one piece each. Only used with PlayerHousing.Catalog = everything; safe to",
        "-- apply either way (the module leaves these pieces out otherwise).",
        "",
        "DELETE FROM `mod_playerhousing_piece_rule` WHERE `item_entry` BETWEEN %d AND %d;" % (CATALOG_FIRST, CATALOG_LAST),
        "DELETE FROM `mod_playerhousing_piece` WHERE `item_entry` BETWEEN %d AND %d;" % (CATALOG_FIRST, CATALOG_LAST),
        "DELETE FROM `item_template` WHERE `entry` BETWEEN %d AND %d;" % (CATALOG_FIRST, CATALOG_LAST),
        "DELETE FROM `gameobject_template` WHERE `entry` BETWEEN %d AND %d;" % (live_entry(CATALOG_FIRST), live_entry(CATALOG_LAST)),
        "",
    ]
    for start in range(0, len(items), 500):
        sql += ["INSERT INTO `gameobject_template` (%s) VALUES" % go_columns, ",\n".join(gameobjects[start:start + 500]) + ";", ""]
        sql += ["INSERT INTO `item_template` (%s) VALUES" % item_columns, ",\n".join(items[start:start + 500]) + ";", ""]
        sql += ["INSERT INTO `mod_playerhousing_piece` (`item_entry`, `kind`, `category`, `name`, `go_entry`, `edit_go_entry`, "
                "`creature_entry`, `scale`, `footprint`, `height`, `flags`, `copy_cost`, `sort_order`, `hint`, `legacy_catalog_id`, "
                "`outline_min_x`, `outline_min_y`, `outline_max_x`, `outline_max_y`) VALUES",
                ",\n".join(piece_rows[start:start + 500]) + ";", ""]
    sql += ghosts.sql(CATALOG_FIRST, CATALOG_LAST)
    with open(os.path.join(MODULE, "sql/db_world/base/mod_playerhousing_world_catalog.sql"), "w") as f:
        f.write("\n".join(sql))
    return len(piece_rows)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dbc", default=os.environ.get("DBC_DIR", os.path.expanduser("~/acore-test-server/data/dbc")))
    parser.add_argument("--mysql", default=os.environ.get("WORLD_MYSQL", "mysql -uacore -pacore acore_world"))
    build(parser.parse_args())
