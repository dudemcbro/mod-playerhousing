"""Everything a player can place, and what unlocks it.

tools/content/build_content.py turns this list into sql/db_world/base/
mod_playerhousing_world_content.sql and docs/UNLOCKS.md. Edit here, then run the builder.

Fields:
  item      item entry (also decides the object entries, so never renumber a piece)
  name      what the player sees
  cat       Starter, Buildings, Exploration, Dungeons, Raids, Reputation, Professions,
            Holidays, Capstones or Figurines
  go        existing gameobject to copy the model (and, for "keep", the behavior) from
  display   or a GameObjectDisplayInfo id when no gameobject uses the model
  style     decor     clickable ornament (default)
            chair     can be sat on (chair_height 0 low, 1 medium, 2 high; slots)
            keep      works like the original: mailbox, anvil, forge, cooking fire...
            building  a building: its own limit, seen from farther away
            stand     a mannequin that wears real gear from the owner's bags (no go or
                      display: the figure takes after its owner)
            chest     opens its owner's bank (and House Storage)
            music     a music box: its owner picks the island's music
            figure    a figurine: the creature's model (creature=<entry>), frozen and
                      shrunk to fit on a table
  rules     list of groups; each group is a list of rules that must all be met. Any
            complete group unlocks the piece. No rules: everyone has it.
  flags     surface (things go on top), small (fits on a surface), gift (first login),
            wreckage (standing on the island at the first visit), per_char
  footprint, height  override the size read from the game data. Buildings made of
            world models take theirs from the server's collision data; set these only to
            correct one whose outline includes surrounding pieces (check in game first)
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
SHATAR, ALDOR, SCRYERS, SONS_OF_HODIR = 935, 932, 934, 1119

# Skills
BLACKSMITHING, MINING, ALCHEMY, COOKING, ENGINEERING, INSCRIPTION, FIRST_AID = 164, 186, 171, 185, 202, 773, 129


def exalted(faction):
    """Faction buildings: Exalted, earned by this character."""
    return [[rep(faction, EXALTED)]]


def any_ach(*ids):
    """Any one of these achievements unlocks the piece."""
    return [[ach(n)] for n in ids]


# Holiday achievements (Achievement.dbc), for "earn any achievement of this holiday"
LUNAR_FESTIVAL = any_ach(605, 606, 607, 608, 609, 626, 937, 910, 911, 912, 914, 915, 1281, 1396, 1552, 913)
LOVE_IS_IN_THE_AIR = any_ach(260, 1188, 1279, 1280, 1291, 1694, 1695, 1696, 1697, 1698, 1699, 1700, 1701, 1702, 1703,
                             1704, 4624, 1693, 1707)
NOBLEGARDEN = any_ach(248, 249, 2416, 2417, 2418, 2419, 2420, 2421, 2422, 2436, 2497, 2576, 2676, 2797, 2798)
CHILDRENS_WEEK = any_ach(275, 1786, 1788, 1789, 1790, 1791, 1792, 1793)
MIDSUMMER = any_ach(263, 271, 272, 1145, 1022, 1023, 1024, 1025, 1026, 1027, 1028, 1029, 1030, 1031, 1032, 1033,
                    1034, 1035, 1036, 1037, 1038, 1039)
PILGRIMS_BOUNTY = any_ach(3556, 3557, 3558, 3559, 3576, 3577, 3578, 3579, 3580, 3581, 3582, 3596, 3597, 3478, 3656)


PIECES = [
    # ------------------------------------------------------------------ Starter
    dict(item=901105, name="Westfall Chair", cat="Starter", go=180047, style="chair", flags=["gift"], legacy=1001),
    dict(item=901106, name="Tiny Table", cat="Starter", go=180885, flags=["surface", "gift"], legacy=1002),
    dict(item=901104, name="Lantern", cat="Starter", go=180765, flags=["small", "gift"]),
    dict(item=901101, name="Campfire", cat="Starter", go=1798, style="keep"),
    dict(item=901102, name="Bedroll", cat="Starter", go=193684),
    dict(item=901103, name="Supply Crate", cat="Starter", go=181302, flags=["surface"]),
    dict(item=901107, name="Mannequin", cat="Starter", style="stand"),
    dict(item=901109, name="Music Box", cat="Starter", go=180620, style="music", rules=[[level(10)]]),
    dict(item=901108, name="Bank Chest", cat="Starter", go=2850, style="chest", rules=[[level(20)], [ach(546)]],
         hint="Reach level 20, or buy 7 bank slots (Safe Deposit)"),
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
    dict(item=902211, name="Burnt Westfall Farmhouse", cat="Buildings", display=7464, style="building", rules=[[level(30)]]),
    dict(item=902212, name="Burnt Duskwood Farmhouse", cat="Buildings", display=7436, style="building", rules=[[level(30)]]),
    dict(item=902213, name="Broken House", cat="Buildings", display=7461, style="building", rules=[[level(30)]]),
    dict(item=902214, name="Ruined Guard Tower", cat="Buildings", display=7462, style="building", rules=[[level(30)]]),

    # ------------------------------------------------------------------ Buildings: faction, at Exalted
    dict(item=902220, name="Westfall Farmhouse", cat="Buildings", display=7465, style="building", rules=exalted(STORMWIND)),
    dict(item=902221, name="Duskwood Farmhouse", cat="Buildings", display=7432, style="building", rules=exalted(STORMWIND)),
    dict(item=902222, name="Redridge Farmhouse", cat="Buildings", display=8010, style="building", rules=exalted(STORMWIND)),
    dict(item=902223, name="Redridge Barn", cat="Buildings", display=8013, style="building", rules=exalted(STORMWIND)),
    dict(item=902224, name="Duskwood Stable", cat="Buildings", display=8016, style="building", rules=exalted(STORMWIND)),
    dict(item=902225, name="Duskwood Blacksmith", cat="Buildings", display=7668, style="building", rules=exalted(STORMWIND)),
    dict(item=902226, name="Redridge Lumber Mill", cat="Buildings", display=8012, style="building", rules=exalted(STORMWIND)),
    dict(item=902227, name="Redridge Chapel", cat="Buildings", display=7428, style="building", rules=exalted(STORMWIND)),
    dict(item=902228, name="Duskwood Two-Story House", cat="Buildings", display=7463, style="building", rules=exalted(STORMWIND)),
    dict(item=902229, name="Human Guard Tower", cat="Buildings", display=7595, style="building", rules=exalted(STORMWIND)),
    dict(item=902230, name="Night Elf Tent", cat="Buildings", display=9148, style="building", rules=exalted(DARNASSUS)),
    dict(item=902231, name="Night Elf Druid Tower", cat="Buildings", display=7458, style="building", rules=exalted(DARNASSUS)),
    dict(item=902232, name="Draenei Hut", cat="Buildings", display=7667, style="building", rules=exalted(EXODAR)),
    dict(item=902233, name="Northrend Human House", cat="Buildings", display=7832, style="building", rules=exalted(ALLIANCE_VANGUARD)),
    dict(item=902234, name="Tall Human Tower", cat="Buildings", display=8335, style="building", rules=exalted(ALLIANCE_VANGUARD)),
    dict(item=902235, name="The Skybreaker", cat="Buildings", display=8254, style="building", rules=exalted(ALLIANCE_VANGUARD)),
    dict(item=902240, name="Orc Tent", cat="Buildings", display=8184, style="building", rules=exalted(ORGRIMMAR)),
    dict(item=902241, name="Orc Zeppelin House", cat="Buildings", display=7466, style="building", rules=exalted(ORGRIMMAR)),
    dict(item=902242, name="Orc Great Hall", cat="Buildings", display=7670, style="building", rules=exalted(ORGRIMMAR)),
    dict(item=902243, name="Orc Barracks", cat="Buildings", display=7672, style="building", rules=exalted(ORGRIMMAR)),
    dict(item=902244, name="Tauren Druid Tent", cat="Buildings", display=9127, style="building", rules=exalted(THUNDER_BLUFF)),
    dict(item=902245, name="Forsaken Tent", cat="Buildings", display=7872, style="building", rules=exalted(UNDERCITY)),
    dict(item=902246, name="Troll Watch Tower", cat="Buildings", display=3, style="building", rules=exalted(DARKSPEAR)),
    dict(item=902247, name="Winter Tauren Smoke Hut", cat="Buildings", display=7807, style="building", rules=exalted(HORDE_EXPEDITION)),
    dict(item=902248, name="Orgrim's Hammer", cat="Buildings", display=8253, style="building", rules=exalted(HORDE_EXPEDITION)),
    dict(item=902250, name="Goblin Tent", cat="Buildings", display=7725, style="building", rules=exalted(BOOTY_BAY)),
    dict(item=902251, name="Gypsy Wagon", cat="Buildings", go=180045, style="building", rules=exalted(BOOTY_BAY)),
    dict(item=902252, name="Pirate Ship Run Aground", cat="Buildings", display=7552, style="building", rules=exalted(BOOTY_BAY)),
    dict(item=902253, name="Old Stratholme Farm", cat="Buildings", display=7847, style="building", rules=exalted(KEEPERS_OF_TIME)),
    dict(item=902254, name="Ulduar Tower", cat="Buildings", display=8590, style="building", rules=exalted(KIRIN_TOR)),
    dict(item=902255, name="Wintergrasp Tower", cat="Buildings", display=7878, style="building", rules=exalted(VALIANCE)),
    dict(item=902256, name="Wintergrasp Tower (Horde)", cat="Buildings", display=7878, style="building", rules=exalted(WARSONG)),
    dict(item=902257, name="Blizzard's Test House", cat="Buildings", display=467, style="building", rules=[[NEVER]],
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
    # Eastern Kingdoms
    dict(item=902326, name="Alterac Apple Barrel", cat="Exploration", go=190559, rules=[[ach(760)]]),
    dict(item=902327, name="Arathi Rune Stone", cat="Exploration", go=2711, rules=[[ach(761)]]),
    dict(item=902328, name="Excavation Supply Crate", cat="Exploration", go=2867, flags=["surface"], rules=[[ach(765)]]),
    dict(item=902329, name="Dark Portal Monolith", cat="Exploration", go=153205, rules=[[ach(766)]]),
    dict(item=902330, name="Ambermill Magic Brazier", cat="Exploration", go=1770, rules=[[ach(769)]]),
    dict(item=902331, name="Plaguelands Scourge Banner", cat="Exploration", go=176087, rules=[[ach(770)]]),
    dict(item=902332, name="Southshore Ale Keg", cat="Exploration", go=1727, rules=[[ach(772)]]),
    dict(item=902333, name="Wildhammer Gryphon Roost", cat="Exploration", go=182254, rules=[[ach(773)]]),
    dict(item=902334, name="Dark Iron Chair", cat="Exploration", go=136929, style="chair", chair_height=1, rules=[[ach(774)]]),
    dict(item=902335, name="Blackrock Tool Rack", cat="Exploration", display=1487, rules=[[ach(775)]]),
    dict(item=902336, name="Karazhan Supply Crate", cat="Exploration", display=7489, flags=["surface"], rules=[[ach(777)]]),
    dict(item=902337, name="Lakeshire Bench", cat="Exploration", go=92703, style="chair", slots=3, chair_height=2, rules=[[ach(780)]]),
    dict(item=902338, name="Swamp of Sorrows Reed Plant", cat="Exploration", display=7444, rules=[[ach(782)]]),
    dict(item=902339, name="Dragonmaw Dragon Egg", cat="Exploration", display=277, rules=[[ach(841)]]),
    dict(item=902340, name="Tranquillien Brazier", cat="Exploration", go=184229, rules=[[ach(858)]]),
    dict(item=902341, name="Shattered Sun Banner", cat="Exploration", go=187357, rules=[[ach(868)]]),
    # Kalimdor
    dict(item=902342, name="Darkshore Ancient Flame", cat="Exploration", go=16393, rules=[[ach(844)]]),
    dict(item=902343, name="Highperch Wyvern Egg", cat="Exploration", go=183147, rules=[[ach(846)]]),
    dict(item=902344, name="Venture Co. Shredder", cat="Exploration", go=188697, rules=[[ach(847)]]),
    dict(item=902345, name="Kodo Graveyard Bones", cat="Exploration", go=176751, rules=[[ach(848)]]),
    dict(item=902346, name="Feralas Hippogryph Egg", cat="Exploration", go=186814, rules=[[ach(849)]]),
    dict(item=902347, name="Blackhoof Weapon Rack", cat="Exploration", go=186301, rules=[[ach(850)]]),
    dict(item=902348, name="Steamwheedle Cargo", cat="Exploration", go=142181, flags=["surface"], rules=[[ach(851)]]),
    dict(item=902349, name="Azshara Arcane Crystal", cat="Exploration", go=150140, rules=[[ach(852)]]),
    dict(item=902350, name="Cleansed Songflower", cat="Exploration", go=164882, rules=[[ach(853)]]),
    dict(item=902351, name="Un'Goro Power Crystal", cat="Exploration", go=164838, rules=[[ach(854)]]),
    dict(item=902352, name="Moonglade Dream Catcher", cat="Exploration", go=185504, rules=[[ach(855)]]),
    dict(item=902353, name="Silithus Wind Stone", cat="Exploration", go=180456, rules=[[ach(856)]]),
    dict(item=902354, name="Winterfall Furbolg Totem", cat="Exploration", display=6704, rules=[[ach(857)]]),
    dict(item=902355, name="Azure Watch Cookpot", cat="Exploration", go=181790, rules=[[ach(860)]]),
    dict(item=902356, name="Bloodmyst Impact Crystal", cat="Exploration", go=181779, rules=[[ach(861)]]),
    # Outland
    dict(item=902357, name="Netherstorm Machine Parts", cat="Exploration", go=183771, rules=[[ach(843)]]),
    dict(item=902358, name="Shadowmoon Demonic Crystal", cat="Exploration", go=184731, rules=[[ach(864)]]),
    dict(item=902359, name="Apexis Crystal", cat="Exploration", go=185933, rules=[[ach(865)]]),
    dict(item=902360, name="Arakkoa Alchemy Set", cat="Exploration", go=190689, rules=[[ach(867)]]),
    # Northrend
    dict(item=902361, name="Vrykul Crest Shield", cat="Exploration", go=187386, rules=[[ach(1263)]]),
    dict(item=902362, name="Drakil'jin Pedestal", cat="Exploration", go=190522, rules=[[ach(1266)]]),
    dict(item=902363, name="Zul'Drak Skull Pile", cat="Exploration", go=190594, rules=[[ach(1267)]]),
    dict(item=902364, name="Sholazar Crystal Formation", cat="Exploration", go=190502, rules=[[ach(1268)]]),
    dict(item=902365, name="Titan Control Orb", cat="Exploration", go=192262, rules=[[ach(1269)]]),

    # ------------------------------------------------------------------ Dungeons
    dict(item=902400, name="Defias Powder Keg", cat="Dungeons", go=193640, rules=[[ach(628)]]),
    dict(item=902401, name="Skull Candle", cat="Dungeons", go=180425, flags=["small"], rules=[[ach(631)]], legacy=1402),
    dict(item=902402, name="Gnome Rocket Cart", cat="Dungeons", go=190227, rules=[[ach(634)]], legacy=1203),
    dict(item=902403, name="Hospital Bed", cat="Dungeons", go=178226, rules=[[ach(637)]], legacy=1502),
    dict(item=902404, name="Scarlet Bookshelf", cat="Dungeons", go=183268, flags=["surface"], rules=[[ach(637)]], legacy=1501),
    dict(item=902405, name="Dark Brazier", cat="Dungeons", go=182014, rules=[[ach(642)]], legacy=1504),
    dict(item=902406, name="Ancient Coffin", cat="Dungeons", go=184599, scale=0.3, rules=[[ach(646)]], legacy=1403),
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
    # Classic
    dict(item=902417, name="Searing Blade Brazier", cat="Dungeons", go=175287, rules=[[ach(629)]]),
    dict(item=902418, name="Wailing Caverns Stone Bed", cat="Dungeons", go=185475, rules=[[ach(630)]]),
    dict(item=902419, name="Blackfathom Naga Statue", cat="Dungeons", go=187342, rules=[[ach(632)]]),
    dict(item=902420, name="Stockade Ball and Chain", cat="Dungeons", go=175544, flags=["small"], rules=[[ach(633)]]),
    dict(item=902421, name="Razorfen Cauldron", cat="Dungeons", go=74075, rules=[[ach(635)]]),
    dict(item=902422, name="Razorfen Downs Pyre", cat="Dungeons", go=40199, rules=[[ach(636)]]),
    dict(item=902423, name="Uldaman Titan Urn", cat="Dungeons", go=125477, rules=[[ach(638)]]),
    dict(item=902424, name="Gong of Zul'Farrak", cat="Dungeons", go=141832, rules=[[ach(639)]]),
    dict(item=902425, name="Maraudon Crystal", cat="Dungeons", go=176581, rules=[[ach(640)]]),
    dict(item=902426, name="Atal'ai Eternal Flame", cat="Dungeons", go=148418, rules=[[ach(641)]]),
    dict(item=902427, name="Blackrock Meat Rack", cat="Dungeons", go=176461, rules=[[ach(643)]]),
    dict(item=902428, name="Rookery Egg", cat="Dungeons", go=191840, rules=[[ach(1307)]]),
    # Burning Crusade
    dict(item=902429, name="Blood Furnace War Banner", cat="Dungeons", display=6832, rules=[[ach(648)]]),
    dict(item=902430, name="Shattered Halls Fel Brazier", cat="Dungeons", go=184496, rules=[[ach(657)]]),
    dict(item=902431, name="Slave Pens Cage", cat="Dungeons", go=182094, rules=[[ach(649)]]),
    dict(item=902432, name="Underbog Giant Mushroom", cat="Dungeons", display=6919, rules=[[ach(650)]]),
    dict(item=902433, name="Coilfang Orb Lamp", cat="Dungeons", display=7243, rules=[[ach(656)]]),
    dict(item=902434, name="Ethereal Crate", cat="Dungeons", go=183820, flags=["surface"], rules=[[ach(651)]]),
    dict(item=902435, name="Auchenai Offering Bowl", cat="Dungeons", go=190507, rules=[[ach(666)]]),
    dict(item=902436, name="Sethekk Crystal Ball", cat="Dungeons", go=185554, rules=[[ach(653)]]),
    dict(item=902437, name="Shadow Council Banner", cat="Dungeons", go=185021, rules=[[ach(654)]]),
    dict(item=902438, name="Tarren Mill Chair", cat="Dungeons", go=112318, style="chair", chair_height=2, rules=[[ach(652)]],
         hint="Complete Old Hillsbrad Foothills in the Caverns of Time"),
    dict(item=902439, name="Caverns of Time Hourglass", cat="Dungeons", go=190686, footprint=2.3, height=3.8, rules=[[ach(655)]],
         hint="Complete the Black Morass in the Caverns of Time"),
    dict(item=902440, name="Mechanar Mana Cells", cat="Dungeons", go=187057, rules=[[ach(658)]]),
    dict(item=902441, name="Botanica Exotic Plant", cat="Dungeons", display=6806, rules=[[ach(659)]]),
    dict(item=902442, name="Arcatraz Containment Jar", cat="Dungeons", go=182198, rules=[[ach(660)]]),
    dict(item=902443, name="Orb of the Blue Flight", cat="Dungeons", go=188415, rules=[[ach(661)]]),
    # Wrath of the Lich King
    dict(item=902444, name="Vrykul Chair", cat="Dungeons", go=186695, style="chair", chair_height=1, rules=[[ach(477)]]),
    dict(item=902445, name="Nexus Dragon Egg", cat="Dungeons", go=188457, rules=[[ach(478)]]),
    dict(item=902446, name="Nerubian Egg", cat="Dungeons", go=193051, rules=[[ach(480)]]),
    dict(item=902447, name="Drakkari Skull Pile", cat="Dungeons", go=191347, rules=[[ach(482)]]),
    dict(item=902448, name="Violet Hold Prison Cage", cat="Dungeons", go=144066, rules=[[ach(483)]]),
    dict(item=902449, name="Tribunal Chest", cat="Dungeons", go=113757, flags=["surface"], rules=[[ach(485)]]),
    dict(item=902450, name="Stormforged Brazier", cat="Dungeons", go=192120, rules=[[ach(486)]]),
    dict(item=902451, name="Cache of Eregos", cat="Dungeons", go=180055, flags=["surface"], rules=[[ach(487)]]),
    dict(item=902452, name="Plagued Grain Crate", cat="Dungeons", go=190094, flags=["surface"], rules=[[ach(479)]]),
    dict(item=902453, name="Argent Lance Rack", cat="Dungeons", go=196398, rules=[[ach(3778)], [ach(4296)]],
         hint="Complete the Trial of the Champion"),
    dict(item=902454, name="Soul Crucible Brazier", cat="Dungeons", go=201600, rules=[[ach(4516)]]),
    dict(item=902455, name="Saronite Bar", cat="Dungeons", go=201777, flags=["small"], rules=[[ach(4517)]]),
    dict(item=902456, name="Captain's Chest", cat="Dungeons", go=201710, flags=["surface"], rules=[[ach(4518)]]),

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
    dict(item=902510, name="Karazhan Opera Moon", cat="Raids", go=183494, rules=[[ach(690)]]),
    dict(item=902511, name="High King's Firepit", cat="Raids", go=182573, rules=[[ach(692)]]),
    dict(item=902512, name="Manticron Cube", cat="Raids", go=181713, rules=[[ach(693)]]),
    dict(item=902513, name="Serpentshrine Naga Ark", cat="Raids", go=182082, rules=[[ach(694)]]),
    dict(item=902514, name="Standard of Kael'thas", cat="Raids", go=186983, rules=[[ach(696)]]),
    dict(item=902515, name="Altar of Hyjal", cat="Raids", go=211019, rules=[[ach(695)]]),
    dict(item=902516, name="Naj'entus Spine", cat="Raids", go=185584, rules=[[ach(697)]]),
    dict(item=902517, name="Sunwell Replica", cat="Raids", go=187345, rules=[[ach(698)]]),
    dict(item=902518, name="Amani Eagle Throne", cat="Raids", go=187118, style="chair", chair_height=1, rules=[[ach(691)]]),
    dict(item=902519, name="Obsidian Dragon Egg", cat="Raids", go=177807, rules=[[ach(1876)], [ach(625)]],
         hint="Defeat Sartharion in the Obsidian Sanctum"),
    dict(item=902520, name="Spellweaver's Scrying Orb", cat="Raids", display=7150, rules=[[ach(622)], [ach(623)]],
         hint="Defeat Malygos in the Eye of Eternity"),
    dict(item=902521, name="Stone Watcher's Cache", cat="Raids", go=194307, flags=["surface"], rules=[[ach(1722)], [ach(1721)]],
         hint="Defeat Archavon the Stone Watcher in the Vault of Archavon"),
    dict(item=902522, name="Argent Crusade Tribute Chest", cat="Raids", go=195665, flags=["surface"], rules=[[ach(3917)], [ach(3916)]],
         hint="Complete the Call of the Crusade in the Trial of the Crusader"),
    dict(item=902523, name="Ruby Sanctum Dragon Egg", cat="Raids", go=203003, rules=[[ach(4817)], [ach(4815)]],
         hint="Defeat Halion in the Ruby Sanctum"),

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
    # Capital cities: Exalted is the city's Argent Tournament banner
    dict(item=902611, name="Stormwind Griffon Banner", cat="Reputation", display=624, rules=[[rep(STORMWIND, REVERED)]]),
    dict(item=902612, name="Stormwind Champion's Banner", cat="Reputation", go=194274, rules=[[rep(STORMWIND, EXALTED)]]),
    dict(item=902613, name="Ornate Ironforge Chair", cat="Reputation", go=183753, style="chair", chair_height=1, rules=[[rep(IRONFORGE, HONORED)]]),
    dict(item=902614, name="Ornate Dwarven Wardrobe", cat="Reputation", display=1227, rules=[[rep(IRONFORGE, REVERED)]]),
    dict(item=902615, name="Ironforge Champion's Banner", cat="Reputation", go=194277, rules=[[rep(IRONFORGE, EXALTED)]]),
    dict(item=902616, name="Night Elf Candle", cat="Reputation", go=180213, flags=["small"], rules=[[rep(DARNASSUS, HONORED)]]),
    dict(item=902617, name="Night Elf Dresser", cat="Reputation", go=126158, flags=["surface"], rules=[[rep(DARNASSUS, REVERED)]]),
    dict(item=902618, name="Darnassus Champion's Banner", cat="Reputation", go=194282, rules=[[rep(DARNASSUS, EXALTED)]]),
    dict(item=902619, name="Gnome Chair", cat="Reputation", go=184733, style="chair", chair_height=1, rules=[[rep(GNOMEREGAN, HONORED)]]),
    dict(item=902620, name="Deeprun Tram Bench", cat="Reputation", go=176004, style="chair", slots=3, chair_height=1, rules=[[rep(GNOMEREGAN, REVERED)]]),
    dict(item=902621, name="Gnomeregan Champion's Banner", cat="Reputation", go=194279, rules=[[rep(GNOMEREGAN, EXALTED)]]),
    dict(item=902622, name="Draenei Bench", cat="Reputation", go=184038, style="chair", chair_height=1, rules=[[rep(EXODAR, HONORED)]]),
    dict(item=902623, name="Exodar Brazier", cat="Reputation", go=185543, rules=[[rep(EXODAR, REVERED)]]),
    dict(item=902624, name="Exodar Champion's Banner", cat="Reputation", go=194280, rules=[[rep(EXODAR, EXALTED)]]),
    dict(item=902625, name="Horde Supply Crate", cat="Reputation", go=178442, flags=["surface"], rules=[[rep(ORGRIMMAR, REVERED)]]),
    dict(item=902626, name="Orgrimmar Champion's Banner", cat="Reputation", go=194278, rules=[[rep(ORGRIMMAR, EXALTED)]]),
    dict(item=902627, name="Tauren Log Bench", cat="Reputation", go=126050, style="chair", slots=2, chair_height=1, rules=[[rep(THUNDER_BLUFF, REVERED)]]),
    dict(item=902628, name="Thunder Bluff Champion's Banner", cat="Reputation", go=194283, rules=[[rep(THUNDER_BLUFF, EXALTED)]]),
    dict(item=902629, name="Lordaeron Brazier", cat="Reputation", display=754, rules=[[rep(UNDERCITY, HONORED)]]),
    dict(item=902630, name="Forsaken Chemistry Set", cat="Reputation", go=193407, rules=[[rep(UNDERCITY, REVERED)]]),
    dict(item=902631, name="Undercity Champion's Banner", cat="Reputation", go=194276, rules=[[rep(UNDERCITY, EXALTED)]]),
    dict(item=902632, name="Darkspear Drum", cat="Reputation", go=185304, rules=[[rep(DARKSPEAR, HONORED)]]),
    dict(item=902633, name="Darkspear Gong", cat="Reputation", go=180386, rules=[[rep(DARKSPEAR, REVERED)]]),
    dict(item=902634, name="Darkspear Champion's Banner", cat="Reputation", go=194281, rules=[[rep(DARKSPEAR, EXALTED)]]),
    dict(item=902635, name="Silvermoon Chair", cat="Reputation", go=184671, style="chair", chair_height=1, rules=[[rep(SILVERMOON, HONORED)]]),
    dict(item=902636, name="Silvermoon Lantern", cat="Reputation", display=7084, rules=[[rep(SILVERMOON, REVERED)]]),
    dict(item=902637, name="Silvermoon Champion's Banner", cat="Reputation", go=194275, rules=[[rep(SILVERMOON, EXALTED)]]),
    # Northrend and Outland
    dict(item=902638, name="Argent Tome", cat="Reputation", go=191312, flags=["small"], rules=[[rep(ARGENT_CRUSADE, REVERED)]]),
    dict(item=902639, name="Argent Crusade Banner", cat="Reputation", go=191614, rules=[[rep(ARGENT_CRUSADE, EXALTED)]]),
    dict(item=902640, name="Dalaran Bench", cat="Reputation", go=191476, style="chair", slots=2, chair_height=2, rules=[[rep(KIRIN_TOR, REVERED)]]),
    dict(item=902641, name="Globe of Scrying", cat="Reputation", go=178439, rules=[[rep(KIRIN_TOR, EXALTED)]]),
    dict(item=902642, name="Eye of Acherus", cat="Reputation", go=191612, rules=[[rep(EBON_BLADE, REVERED)]]),
    dict(item=902643, name="Ebon Blade Blood Orb", cat="Reputation", go=192933, rules=[[rep(EBON_BLADE, EXALTED)]]),
    dict(item=902644, name="Chromatic Dragon Egg", cat="Reputation", display=7188, rules=[[rep(WYRMREST, REVERED)]]),
    dict(item=902645, name="Wyrmrest Dragon Egg", cat="Reputation", go=188133, rules=[[rep(WYRMREST, EXALTED)]]),
    dict(item=902646, name="Glowing Zangar Mushroom", cat="Reputation", display=6977, rules=[[rep(CENARION_EXPEDITION, REVERED)]]),
    dict(item=902647, name="Cenarion Blue Lantern", cat="Reputation", display=6666, rules=[[rep(CENARION_EXPEDITION, EXALTED)]]),
    dict(item=902648, name="Shattrath Standing Lamp", cat="Reputation", go=185967, rules=[[rep(SHATAR, HONORED)]]),
    dict(item=902649, name="Sha'tari Banner", cat="Reputation", display=7603, rules=[[rep(SHATAR, REVERED)]]),
    dict(item=902650, name="Naaru Crystal", cat="Reputation", go=182036, rules=[[rep(SHATAR, EXALTED)]]),
    dict(item=902651, name="Aldor Banner", cat="Reputation", go=181917, rules=[[rep(ALDOR, HONORED)]]),
    dict(item=902652, name="Aldor Brazier", cat="Reputation", go=185979, rules=[[rep(ALDOR, REVERED)]]),
    dict(item=902653, name="Aldor Fountain", cat="Reputation", go=182563, rules=[[rep(ALDOR, EXALTED)]]),
    dict(item=902654, name="Scryer Banner", cat="Reputation", go=185106, rules=[[rep(SCRYERS, HONORED)]]),
    dict(item=902655, name="Scryer Chair", cat="Reputation", go=182598, style="chair", chair_height=1, rules=[[rep(SCRYERS, REVERED)]]),
    dict(item=902656, name="Scryer Power Orb", cat="Reputation", go=190675, rules=[[rep(SCRYERS, EXALTED)]]),
    dict(item=902657, name="Brunnhildar Brazier", cat="Reputation", go=187105, rules=[[rep(SONS_OF_HODIR, HONORED)]]),
    dict(item=902658, name="Brunnhildar Shield", cat="Reputation", display=7441, rules=[[rep(SONS_OF_HODIR, REVERED)]]),
    dict(item=902659, name="Brunnhildar Rug", cat="Reputation", go=186933, rules=[[rep(SONS_OF_HODIR, EXALTED)]]),
    dict(item=902660, name="Pirate Flag", cat="Reputation", go=187083, rules=[[rep(BOOTY_BAY, HONORED)]]),
    dict(item=902661, name="Booty Bay Fish Rack", cat="Reputation", go=181252, rules=[[rep(BOOTY_BAY, REVERED)]]),
    dict(item=902662, name="Booty Bay Cannon", cat="Reputation", go=113531, rules=[[rep(BOOTY_BAY, EXALTED)]]),

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
    dict(item=902806, name="Lunar Festival Lantern", cat="Holidays", go=180766, rules=LUNAR_FESTIVAL, hint="Earn a Lunar Festival achievement"),
    dict(item=902807, name="Lunar Festival Firecrackers", cat="Holidays", go=180764, rules=[[ach(913)]],
         hint="Earn To Honor One's Elders, the Lunar Festival meta achievement"),
    dict(item=902808, name="Heart Wreath", cat="Holidays", go=181016, rules=LOVE_IS_IN_THE_AIR, hint="Earn a Love is in the Air achievement"),
    dict(item=902809, name="Valentine Arch", cat="Holidays", go=181086, rules=[[ach(1693)], [ach(1707)]],
         hint="Earn Fool For Love, the Love is in the Air meta achievement"),
    dict(item=902810, name="Noblegarden Egg", cat="Holidays", go=177272, flags=["small"], rules=NOBLEGARDEN, hint="Earn a Noblegarden achievement"),
    dict(item=902811, name="Orphan's Rag Doll", cat="Holidays", go=186621, flags=["small"], rules=CHILDRENS_WEEK,
         hint="Earn a Children's Week achievement"),
    dict(item=902812, name="Leather Kickball", cat="Holidays", display=7528, flags=["small"], rules=[[ach(1793)]],
         hint="Earn For The Children, the Children's Week meta achievement"),
    dict(item=902813, name="Midsummer Brazier", cat="Holidays", go=181355, rules=MIDSUMMER, hint="Earn a Midsummer achievement"),
    dict(item=902814, name="Ribbon Pole", cat="Holidays", go=181605, rules=[[ach(1038)], [ach(1039)]],
         hint="Earn The Flame Warden or The Flame Keeper, the Midsummer meta achievement"),
    dict(item=902815, name="Basket of Corn", cat="Holidays", go=195192, rules=PILGRIMS_BOUNTY, hint="Earn a Pilgrim's Bounty achievement"),
    dict(item=902816, name="Cornucopia", cat="Holidays", go=195303, rules=[[ach(3478)], [ach(3656)]],
         hint="Earn Pilgrim, the Pilgrim's Bounty meta achievement"),
    dict(item=902817, name="Orange Marigolds", cat="Holidays", go=195063, rules=[[ach(3456)]],
         hint="Earn Dead Man's Party during the Day of the Dead"),
    dict(item=902818, name="Candy Skulls", cat="Holidays", go=195069, flags=["small"], rules=[[ach(3456)]],
         hint="Earn Dead Man's Party during the Day of the Dead"),
    dict(item=902819, name="Pirate Treasure Chest", cat="Holidays", go=179125, flags=["surface"], rules=[[ach(3457)]],
         hint="Earn The Captain's Booty on Pirates' Day"),
    dict(item=902820, name="Pirate Cannonball Stack", cat="Holidays", go=180054, rules=[[ach(3457)]],
         hint="Earn The Captain's Booty on Pirates' Day"),

    # ------------------------------------------------------------------ Capstones
    dict(item=902900, name="Mailbox", cat="Capstones", go=32349, style="keep", rules=[[level(80)]]),
    dict(item=902901, name="Imperial Throne", cat="Capstones", go=170592, style="keep", rules=[[ach(2136)]]),
    dict(item=902902, name="Moonglade Fountain", cat="Capstones", go=185493, rules=[[ach(1283)], [ach(1284)], [ach(1288)]],
         hint="Complete Classic, Outland or Northrend Dungeonmaster"),
    dict(item=902903, name="Barber Chair", cat="Capstones", go=191817, style="keep", rules=[[ach(1681)], [ach(1682)]],
         hint="Earn The Loremaster"),
    dict(item=902904, name="Guild Vault", cat="Capstones", go=187299, style="keep", rules=[[ach(1180)]],
         hint="Loot 10,000 gold (Got My Mind On My Money)"),

    # ------------------------------------------------------------------ Figurines
    # Defeat the boss (or hold its achievement) for a figurine of it, small enough for a table.
    dict(item=902950, name="Kobold Figurine", cat="Figurines", creature=6, style="figure", rules=[[kill(6)]],
         hint="Defeat a Kobold Vermin (you no take candle)"),
    dict(item=902951, name="Murloc Figurine", cat="Figurines", creature=46, style="figure", rules=[[kill(46)]],
         hint="Defeat a Murloc Forager"),
    dict(item=902952, name="Hogger Figurine", cat="Figurines", creature=448, style="figure", rules=[[kill(448)]]),
    dict(item=902953, name="Edwin VanCleef Figurine", cat="Figurines", creature=639, style="figure", rules=[[kill(639)], [ach(628)]]),
    dict(item=902954, name="Onyxia Figurine", cat="Figurines", creature=10184, style="figure",
         rules=[[kill(10184)]] + any_ach(684, 4396, 4397), hint="Defeat Onyxia (or hold an Onyxia's Lair achievement)"),
    dict(item=902955, name="Ragnaros Figurine", cat="Figurines", creature=11502, style="figure", rules=[[kill(11502)], [ach(686)]]),
    dict(item=902956, name="Nefarian Figurine", cat="Figurines", creature=11583, style="figure", rules=[[kill(11583)], [ach(685)]]),
    dict(item=902957, name="Hakkar Figurine", cat="Figurines", creature=14834, style="figure", rules=[[kill(14834)], [ach(688)]]),
    dict(item=902958, name="C'Thun Figurine", cat="Figurines", creature=15727, style="figure", rules=[[kill(15727)], [ach(687)]]),
    dict(item=902959, name="Prince Malchezaar Figurine", cat="Figurines", creature=15690, style="figure", rules=[[kill(15690)], [ach(690)]]),
    dict(item=902960, name="Lady Vashj Figurine", cat="Figurines", creature=21212, style="figure", rules=[[kill(21212)], [ach(694)]]),
    dict(item=902961, name="Kael'thas Sunstrider Figurine", cat="Figurines", creature=19622, style="figure",
         rules=[[kill(19622)], [ach(696)]]),
    dict(item=902962, name="Archimonde Figurine", cat="Figurines", creature=17968, style="figure", rules=[[kill(17968)], [ach(695)]]),
    dict(item=902963, name="Illidan Stormrage Figurine", cat="Figurines", creature=22917, style="figure", rules=[[kill(22917)], [ach(697)]]),
    dict(item=902964, name="Kil'jaeden Figurine", cat="Figurines", creature=25315, style="figure", rules=[[kill(25315)], [ach(698)]]),
    dict(item=902965, name="Ingvar the Plunderer Figurine", cat="Figurines", creature=23954, style="figure",
         rules=[[kill(23954)], [ach(477)]]),
    dict(item=902966, name="Kel'Thuzad Figurine", cat="Figurines", creature=15990, style="figure",
         rules=[[kill(15990)]] + any_ach(574, 575), hint="Defeat Kel'Thuzad (or hold Kel'Thuzad's Defeat)"),
    dict(item=902967, name="Sartharion Figurine", cat="Figurines", creature=28860, style="figure",
         rules=[[kill(28860)]] + any_ach(1876, 625), hint="Defeat Sartharion (or hold Besting the Black Dragonflight)"),
    dict(item=902968, name="Malygos Figurine", cat="Figurines", creature=28859, style="figure", rules=[[kill(28859)]] + any_ach(622, 623),
         hint="Defeat Malygos (or hold The Spellweaver's Downfall)"),
    dict(item=902969, name="Yogg-Saron Figurine", cat="Figurines", creature=33288, style="figure",
         rules=[[kill(33288)]] + any_ach(2894, 2895), hint="Defeat Yogg-Saron (or hold The Secrets of Ulduar)"),
    dict(item=902970, name="The Lich King Figurine", cat="Figurines", creature=36597, style="figure",
         rules=[[kill(36597)]] + any_ach(4530, 4597), hint="Defeat the Lich King (or hold The Frozen Throne)"),
]
