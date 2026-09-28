"""Everything a player can place, and what unlocks it.

tools/content/build_content.py turns this list into sql/db_world/base/
mod_playerhousing_world_content.sql and docs/UNLOCKS.md. Edit here, then run the builder.

Fields:
  item      item entry (also decides the object entries, so never renumber a piece)
  name      what the player sees
  cat       Starter, Buildings, Exploration, Dungeons, Raids, Reputation, Professions,
            Holidays or Capstones
  go        existing gameobject to copy the model (and, for "keep", the behavior) from
  display   or a GameObjectDisplayInfo id when no gameobject uses the model
  style     decor     clickable ornament (default)
            chair     can be sat on (chair_height 0 low, 1 medium, 2 high; slots)
            keep      works like the original: mailbox, anvil, forge, cooking fire...
            building  a building: its own limit, seen from farther away
  rules     list of groups; each group is a list of rules that must all be met. Any
            complete group unlocks the piece. No rules: everyone has it.
  flags     surface (things go on top), small (fits on a surface), gift (first login),
            wreckage (standing on the island at the first visit), per_char
  footprint, height  override the size read from the game data (buildings made of
            world models have none recorded)
  legacy    the old catalog id this piece replaces
"""

# Rules: (type, param1, param2)
def level(n): return (1, n, 0)
def ach(n): return (2, n, 0)
def rep(faction, rank=7): return (3, faction, rank)
def quest(n): return (4, n, 0)
def kill(entry): return (5, entry, 0)
def explore(area): return (6, area, 0)
def skill(skill_id, value): return (7, skill_id, value)
NEVER = (8, 0, 0)

EXALTED, REVERED, HONORED, FRIENDLY = 7, 6, 5, 4

# Factions
STORMWIND, IRONFORGE, DARNASSUS, GNOMEREGAN, EXODAR = 72, 47, 69, 54, 930
ORGRIMMAR, THUNDER_BLUFF, UNDERCITY, DARKSPEAR, SILVERMOON = 76, 81, 68, 530, 911
ALLIANCE_VANGUARD, HORDE_EXPEDITION, VALIANCE, WARSONG = 1037, 1052, 1050, 1085
BOOTY_BAY, KEEPERS_OF_TIME, KIRIN_TOR, ARGENT_CRUSADE = 21, 989, 1090, 1106
EBON_BLADE, WYRMREST, CENARION_EXPEDITION = 1098, 1091, 942

# Skills
BLACKSMITHING, MINING, ALCHEMY, COOKING, ENGINEERING, INSCRIPTION, FIRST_AID = 164, 186, 171, 185, 202, 773, 129


def exalted(faction):
    """Faction buildings: Exalted, earned by this character."""
    return [[rep(faction, EXALTED)]]


PIECES = [
    # ------------------------------------------------------------------ Starter
    dict(item=901105, name="Westfall Chair", cat="Starter", go=180047, style="chair", flags=["gift"], legacy=1001),
    dict(item=901106, name="Tiny Table", cat="Starter", go=180885, flags=["surface", "gift"], legacy=1002),
    dict(item=901104, name="Lantern", cat="Starter", go=180765, flags=["small", "gift"]),
    dict(item=901101, name="Campfire", cat="Starter", go=1798, style="keep"),
    dict(item=901102, name="Bedroll", cat="Starter", go=193684),
    dict(item=901103, name="Supply Crate", cat="Starter", go=181302, flags=["surface"]),
    dict(item=902101, name="Barrel", cat="Starter", go=180779, flags=["surface"], legacy=1003),
    dict(item=902102, name="Candle", cat="Starter", go=180338, flags=["small"], legacy=1004),
    dict(item=902103, name="Wooden Bench", cat="Starter", go=24538, style="keep"),
    dict(item=902104, name="Wooden Chair", cat="Starter", go=2413, style="keep"),
    dict(item=902105, name="Camp Table", cat="Starter", go=177724, flags=["surface"]),
    dict(item=902106, name="Camp Mug", cat="Starter", go=181307, flags=["small"]),
    dict(item=902107, name="Stool", cat="Starter", go=193909, style="keep"),
    dict(item=902108, name="Footlocker", cat="Starter", go=183266, flags=["surface"]),
    dict(item=902109, name="Dresser", cat="Starter", go=183267, flags=["surface"]),
    dict(item=902110, name="Book", cat="Starter", go=2695, flags=["small"]),
    dict(item=902111, name="Bottle", cat="Starter", go=2687, flags=["small"]),
    dict(item=902112, name="Inn Barrel", cat="Starter", go=179973, flags=["surface"]),
    dict(item=902113, name="Keg", cat="Starter", go=180575),
    dict(item=902114, name="Rug", cat="Starter", go=181077),
    dict(item=902115, name="Bunk Bed", cat="Starter", go=193167),
    dict(item=902116, name="Scroll", cat="Starter", go=202898, flags=["small"]),
    dict(item=902117, name="Candelabra", cat="Starter", go=2697, flags=["small"]),

    # ------------------------------------------------------------------ Buildings: shelters, by level
    dict(item=902200, name="Broken Cart", cat="Buildings", go=186807, style="building", flags=["wreckage"]),
    dict(item=902201, name="Shredded Tent", cat="Buildings", display=8457, style="building", flags=["wreckage"], footprint=4, height=4),
    dict(item=902202, name="Tarp Lean-to", cat="Buildings", display=7825, style="building"),
    dict(item=902203, name="Wrecked Rowboat", cat="Buildings", go=164909, style="building"),
    dict(item=902204, name="Razorfen Lean-to", cat="Buildings", display=322, style="building", rules=[[level(10)]]),
    dict(item=902205, name="Small Canvas Tent", cat="Buildings", display=8360, style="building", rules=[[level(10)]]),
    dict(item=902206, name="Outhouse", cat="Buildings", display=3332, style="building", rules=[[level(10)]]),
    dict(item=901100, name="Canvas Tent", cat="Buildings", go=184592, style="building", rules=[[level(20)]]),
    dict(item=902207, name="Large Canvas Tent", cat="Buildings", display=7195, style="building", rules=[[level(20)]]),
    dict(item=902208, name="Covered Wagon", cat="Buildings", display=3678, style="building", rules=[[level(20)]]),
    dict(item=902209, name="Water Hut", cat="Buildings", display=7517, style="building", rules=[[level(20)]]),
    dict(item=902210, name="Westfall Shed", cat="Buildings", display=662, style="building", rules=[[level(20)]]),
    dict(item=902211, name="Burnt Westfall Farmhouse", cat="Buildings", display=7464, style="building", rules=[[level(30)]], footprint=16, height=12),
    dict(item=902212, name="Burnt Duskwood Farmhouse", cat="Buildings", display=7436, style="building", rules=[[level(30)]], footprint=16, height=12),
    dict(item=902213, name="Broken House", cat="Buildings", display=7461, style="building", rules=[[level(30)]], footprint=14, height=12),
    dict(item=902214, name="Ruined Guard Tower", cat="Buildings", display=7462, style="building", rules=[[level(30)]], footprint=10, height=20),

    # ------------------------------------------------------------------ Buildings: faction, at Exalted
    dict(item=902220, name="Westfall Farmhouse", cat="Buildings", display=7465, style="building", rules=exalted(STORMWIND), footprint=16, height=12),
    dict(item=902221, name="Duskwood Farmhouse", cat="Buildings", display=7432, style="building", rules=exalted(STORMWIND), footprint=16, height=12),
    dict(item=902222, name="Redridge Farmhouse", cat="Buildings", display=8010, style="building", rules=exalted(STORMWIND), footprint=16, height=12),
    dict(item=902223, name="Redridge Barn", cat="Buildings", display=8013, style="building", rules=exalted(STORMWIND), footprint=16, height=14),
    dict(item=902224, name="Duskwood Stable", cat="Buildings", display=8016, style="building", rules=exalted(STORMWIND), footprint=14, height=10),
    dict(item=902225, name="Duskwood Blacksmith", cat="Buildings", display=7668, style="building", rules=exalted(STORMWIND), footprint=14, height=12),
    dict(item=902226, name="Redridge Lumber Mill", cat="Buildings", display=8012, style="building", rules=exalted(STORMWIND), footprint=20, height=16),
    dict(item=902227, name="Redridge Chapel", cat="Buildings", display=7428, style="building", rules=exalted(STORMWIND), footprint=16, height=20),
    dict(item=902228, name="Duskwood Two-Story House", cat="Buildings", display=7463, style="building", rules=exalted(STORMWIND), footprint=16, height=16),
    dict(item=902229, name="Human Guard Tower", cat="Buildings", display=7595, style="building", rules=exalted(STORMWIND), footprint=14, height=30),
    dict(item=902230, name="Night Elf Tent", cat="Buildings", display=9148, style="building", rules=exalted(DARNASSUS)),
    dict(item=902231, name="Night Elf Druid Tower", cat="Buildings", display=7458, style="building", rules=exalted(DARNASSUS), footprint=14, height=30),
    dict(item=902232, name="Draenei Hut", cat="Buildings", display=7667, style="building", rules=exalted(EXODAR), footprint=12, height=12),
    dict(item=902233, name="Northrend Human House", cat="Buildings", display=7832, style="building", rules=exalted(ALLIANCE_VANGUARD)),
    dict(item=902234, name="Tall Human Tower", cat="Buildings", display=8335, style="building", rules=exalted(ALLIANCE_VANGUARD)),
    dict(item=902235, name="The Skybreaker", cat="Buildings", display=8254, style="building", rules=exalted(ALLIANCE_VANGUARD), footprint=45, height=40),
    dict(item=902240, name="Orc Tent", cat="Buildings", display=8184, style="building", rules=exalted(ORGRIMMAR)),
    dict(item=902241, name="Orc Zeppelin House", cat="Buildings", display=7466, style="building", rules=exalted(ORGRIMMAR)),
    dict(item=902242, name="Orc Great Hall", cat="Buildings", display=7670, style="building", rules=exalted(ORGRIMMAR), footprint=22, height=20),
    dict(item=902243, name="Orc Barracks", cat="Buildings", display=7672, style="building", rules=exalted(ORGRIMMAR), footprint=20, height=18),
    dict(item=902244, name="Tauren Druid Tent", cat="Buildings", display=9127, style="building", rules=exalted(THUNDER_BLUFF), footprint=20, height=20),
    dict(item=902245, name="Forsaken Tent", cat="Buildings", display=7872, style="building", rules=exalted(UNDERCITY)),
    dict(item=902246, name="Troll Watch Tower", cat="Buildings", display=3, style="building", rules=exalted(DARKSPEAR)),
    dict(item=902247, name="Winter Tauren Smoke Hut", cat="Buildings", display=7807, style="building", rules=exalted(HORDE_EXPEDITION), footprint=10, height=10),
    dict(item=902248, name="Orgrim's Hammer", cat="Buildings", display=8253, style="building", rules=exalted(HORDE_EXPEDITION), footprint=45, height=40),
    dict(item=902250, name="Goblin Tent", cat="Buildings", display=7725, style="building", rules=exalted(BOOTY_BAY)),
    dict(item=902251, name="Gypsy Wagon", cat="Buildings", go=180045, style="building", rules=exalted(BOOTY_BAY)),
    dict(item=902252, name="Pirate Ship Run Aground", cat="Buildings", display=7552, style="building", rules=exalted(BOOTY_BAY), footprint=35, height=30),
    dict(item=902253, name="Old Stratholme Farm", cat="Buildings", display=7847, style="building", rules=exalted(KEEPERS_OF_TIME), footprint=18, height=14),
    dict(item=902254, name="Ulduar Tower", cat="Buildings", display=8590, style="building", rules=exalted(KIRIN_TOR)),
    dict(item=902255, name="Wintergrasp Tower", cat="Buildings", display=7878, style="building", rules=exalted(VALIANCE)),
    dict(item=902256, name="Wintergrasp Tower (Horde)", cat="Buildings", display=7878, style="building", rules=exalted(WARSONG)),
    dict(item=902257, name="Blizzard's Test House", cat="Buildings", display=467, style="building", rules=[[NEVER]], footprint=16, height=14,
         hint="Blizzard's own unfinished player housing prototype, still in the game files. GMs only"),

    # ------------------------------------------------------------------ Exploration
    dict(item=902300, name="Stormwind Lamp Post", cat="Exploration", display=5671, rules=[[ach(776)]]),
    dict(item=902301, name="Westfall Harvest", cat="Exploration", go=50490, rules=[[ach(802)]]),
    dict(item=902302, name="Black Candle", cat="Exploration", go=180415, flags=["small"], rules=[[ach(778)]]),
    dict(item=902303, name="Ornate Dwarven Table", cat="Exploration", go=180324, flags=["surface"], rules=[[ach(627)]]),
    dict(item=902304, name="Elven Wooden Table", cat="Exploration", go=180879, flags=["surface"], rules=[[ach(842)]], legacy=1102),
    dict(item=902305, name="Night Elf Stool", cat="Exploration", go=182077, style="chair", chair_height=0, rules=[[ach(845)]]),
    dict(item=902307, name="Blood Elf Table", cat="Exploration", go=182093, flags=["surface"], rules=[[ach(859)]]),
    dict(item=902308, name="Orc Table", cat="Exploration", go=180888, flags=["surface"], rules=[[ach(728)]]),
    dict(item=902309, name="Barrens Brazier Lamp", cat="Exploration", display=730, rules=[[ach(750)]]),
    dict(item=902310, name="Tauren Rug", cat="Exploration", go=188346, rules=[[ach(736)]], legacy=1301),
    dict(item=902311, name="Tauren Lamp Post", cat="Exploration", display=731, rules=[[ach(736)]]),
    dict(item=902312, name="Winterhoof Totem", cat="Exploration", go=50523, rules=[[ach(736)]], legacy=1302),
    dict(item=902313, name="Forsaken Banner", cat="Exploration", go=180432, rules=[[ach(768)]], legacy=1401),
    dict(item=902314, name="Apothecary Bookcase", cat="Exploration", go=190693, rules=[[ach(771)]]),
    dict(item=902315, name="Gnome Maintenance Light", cat="Exploration", go=193586, rules=[[ach(627)]], legacy=1201),
    dict(item=902316, name="Dwarven Workshop Table", cat="Exploration", go=180884, flags=["surface"], rules=[[ach(779)]], legacy=1202),
    dict(item=902317, name="Mag'har Rug", cat="Exploration", go=182257, rules=[[ach(866)]]),
    dict(item=902318, name="Fireworks Barrel", cat="Exploration", go=180878, rules=[[ach(781)]]),
    dict(item=902319, name="Onslaught Table", cat="Exploration", go=190190, flags=["surface"], rules=[[ach(1264)]]),
    dict(item=902320, name="Dalaran Chair", cat="Exploration", go=192842, style="chair", rules=[[ach(1457)]]),
    dict(item=902321, name="Dalaran Fountain", cat="Exploration", go=191446, rules=[[ach(1457)]]),
    dict(item=902322, name="Magnataur Worship Candles", cat="Exploration", go=188436, rules=[[ach(1265)]]),
    dict(item=902323, name="Scourge Weapon Rack", cat="Exploration", go=190576, rules=[[ach(1270)]]),
    dict(item=902324, name="Hellfire Floor Brazier", cat="Exploration", display=7092, rules=[[ach(862)]]),
    dict(item=902325, name="Bogbean Plant", cat="Exploration", go=20939, rules=[[ach(863)]]),

    # ------------------------------------------------------------------ Dungeons
    dict(item=902400, name="Defias Powder Keg", cat="Dungeons", go=193640, rules=[[ach(628)]]),
    dict(item=902401, name="Skull Candle", cat="Dungeons", go=180425, flags=["small"], rules=[[ach(631)]], legacy=1402),
    dict(item=902402, name="Gnome Rocket Cart", cat="Dungeons", go=190227, rules=[[ach(634)]], legacy=1203),
    dict(item=902403, name="Hospital Bed", cat="Dungeons", go=178226, rules=[[ach(637)]], legacy=1502),
    dict(item=902404, name="Scarlet Bookshelf", cat="Dungeons", go=183268, flags=["surface"], rules=[[ach(637)]], legacy=1501),
    dict(item=902405, name="Dark Brazier", cat="Dungeons", go=182014, rules=[[ach(642)]], legacy=1504),
    dict(item=902406, name="Coffin", cat="Dungeons", go=19425, rules=[[ach(646)]], legacy=1403),
    dict(item=902407, name="Musty Coffin", cat="Dungeons", go=190948, rules=[[ach(645)]], legacy=1505),
    dict(item=902408, name="Lab Table", cat="Dungeons", go=190665, flags=["surface"], rules=[[ach(645)]]),
    dict(item=902409, name="Ogre Campfire", cat="Dungeons", display=4611, rules=[[ach(644)]]),
    dict(item=902410, name="Round Table", cat="Dungeons", go=186422, flags=["surface"], rules=[[ach(647)]], legacy=1503),
    dict(item=902411, name="Vrykul Throne", cat="Dungeons", go=192725, style="chair", chair_height=2, rules=[[ach(488)]]),
    dict(item=902412, name="Drakkari Tome", cat="Dungeons", go=202889, flags=["small"], rules=[[ach(484)]]),
    dict(item=902413, name="Ahn'kahet Brazier", cat="Dungeons", go=193057, rules=[[ach(481)]]),
    dict(item=902414, name="Fancy Bed", cat="Dungeons", go=13948, style="keep", rules=[[ach(1283)]]),
    dict(item=902415, name="Arcane Brazier", cat="Dungeons", go=183098, rules=[[ach(1284)]]),
    dict(item=902416, name="Tome of the Light", cat="Dungeons", go=191140, flags=["small"], rules=[[ach(1288)]]),

    # ------------------------------------------------------------------ Raids
    dict(item=902500, name="The Severed Head of Onyxia", cat="Raids", go=179556, rules=[[ach(4396)], [ach(4397)], [kill(10184)]],
         hint="Defeat Onyxia in Onyxia's Lair"),
    dict(item=902501, name="The Severed Head of Nefarian", cat="Raids", go=179881, rules=[[ach(685)]]),
    dict(item=902502, name="Idol of Hakkar", cat="Raids", go=148838, rules=[[ach(688)]]),
    dict(item=902503, name="Lava Shrine", cat="Raids", display=207, rules=[[ach(686)]]),
    dict(item=902504, name="Qiraji Obelisk", cat="Raids", display=7714, rules=[[ach(687)]]),
    dict(item=902505, name="Small Qiraji Obelisk", cat="Raids", display=7715, rules=[[ach(689)]]),
    dict(item=902506, name="Kel'Thuzad's Throne", cat="Raids", go=181640, style="keep", rules=[[ach(576)], [ach(577)]],
         hint="Defeat Kel'Thuzad in Naxxramas"),
    dict(item=902507, name="Thorim's Throne", cat="Raids", go=191647, style="chair", chair_height=2, rules=[[ach(2886)], [ach(2887)]],
         hint="Complete The Siege of Ulduar"),
    dict(item=902508, name="Frostmourne", cat="Raids", go=202302, rules=[[ach(4530)], [ach(4597)]],
         hint="Complete The Frozen Throne in Icecrown Citadel"),
    dict(item=902509, name="Frostmourne Altar", cat="Raids", go=202236, rules=[[ach(4532)], [kill(36597)]],
         hint="Defeat the Lich King"),

    # ------------------------------------------------------------------ Reputation
    dict(item=902600, name="Stormwind Rug", cat="Reputation", go=180334, rules=[[rep(STORMWIND, FRIENDLY)]], legacy=1101),
    dict(item=902601, name="Alliance Banner", cat="Reputation", go=192252, rules=[[rep(STORMWIND, HONORED)]], legacy=1103),
    dict(item=902602, name="Horde Mug", cat="Reputation", go=180049, flags=["small"], rules=[[rep(ORGRIMMAR, FRIENDLY)]]),
    dict(item=902603, name="Horde Table", cat="Reputation", go=191785, flags=["surface"], rules=[[rep(ORGRIMMAR, HONORED)]]),
    dict(item=902604, name="Magna Totem", cat="Reputation", go=187890, rules=[[rep(THUNDER_BLUFF, HONORED)]], legacy=1303),
    dict(item=902605, name="Paladin Shrine", cat="Reputation", display=6873, rules=[[rep(ARGENT_CRUSADE, HONORED)]]),
    dict(item=902606, name="Kirin Tor Orb", cat="Reputation", display=7781, rules=[[rep(KIRIN_TOR, HONORED)]]),
    dict(item=902607, name="Ebon Blade Weapon Rack", cat="Reputation", go=190577, rules=[[rep(EBON_BLADE, HONORED)]]),
    dict(item=902608, name="Dragon Orb", cat="Reputation", display=7800, rules=[[rep(WYRMREST, HONORED)]]),
    dict(item=902609, name="Potted Plant", cat="Reputation", go=181087, rules=[[rep(CENARION_EXPEDITION, HONORED)]]),
    dict(item=902610, name="Tabard Banners", cat="Reputation", go=180773, rules=[[ach(1021)]]),

    # ------------------------------------------------------------------ Professions
    dict(item=902700, name="Anvil", cat="Professions", go=1744, style="keep", rules=[[skill(BLACKSMITHING, 75)]]),
    dict(item=902701, name="Forge", cat="Professions", go=1685, style="keep", rules=[[skill(BLACKSMITHING, 75)], [skill(MINING, 75)]],
         hint="Reach 75 in Blacksmithing or Mining"),
    dict(item=902702, name="Alchemy Lab", cat="Professions", go=191540, style="keep", rules=[[skill(ALCHEMY, 225)]]),
    dict(item=902703, name="Stove", cat="Professions", go=91673, style="keep", rules=[[skill(COOKING, 150)]]),
    dict(item=902704, name="Cooking Table", cat="Professions", go=176463, style="keep", rules=[[skill(COOKING, 225)]]),
    dict(item=902705, name="Gnome Workbench", cat="Professions", go=202564, flags=["surface"], rules=[[skill(ENGINEERING, 150)]]),
    dict(item=902706, name="The Book of the Raven", cat="Professions", go=185581, flags=["small"], rules=[[skill(INSCRIPTION, 150)]]),
    dict(item=902707, name="Surgical Table", cat="Professions", go=192548, rules=[[skill(FIRST_AID, 300)]]),

    # ------------------------------------------------------------------ Holidays
    dict(item=902800, name="Winter Veil Tree", cat="Holidays", go=178425, rules=[[ach(1687)], [ach(1690)]], hint="Earn a Winter Veil achievement"),
    dict(item=902801, name="Winter Veil Gift", cat="Holidays", go=178429, flags=["small"], rules=[[ach(1688)], [ach(1687)]], hint="Earn a Winter Veil achievement"),
    dict(item=902802, name="Headless Horseman's Pumpkin Table", cat="Holidays", go=186327, rules=[[ach(289)], [ach(255)]], hint="Earn a Hallow's End achievement"),
    dict(item=902803, name="Pumpkin", cat="Holidays", go=180405, flags=["small"], rules=[[ach(288)], [ach(289)]], hint="Earn a Hallow's End achievement"),
    dict(item=902804, name="Brewfest Beer Tent", cat="Holidays", go=186682, rules=[[ach(295)], [ach(1683)], [ach(1684)]], hint="Earn a Brewfest achievement"),
    dict(item=902805, name="Standing Brewfest Keg", cat="Holidays", go=186709, rules=[[ach(295)], [ach(1683)], [ach(1684)]], hint="Earn a Brewfest achievement"),

    # ------------------------------------------------------------------ Capstones
    dict(item=902900, name="Mailbox", cat="Capstones", go=32349, style="keep", rules=[[level(80)]]),
    dict(item=902901, name="Imperial Throne", cat="Capstones", go=170592, style="keep", rules=[[ach(2136)]]),
    dict(item=902902, name="Moonglade Fountain", cat="Capstones", go=185493, rules=[[ach(1283)], [ach(1284)], [ach(1288)]],
         hint="Complete Classic, Outland or Northrend Dungeonmaster"),
]
