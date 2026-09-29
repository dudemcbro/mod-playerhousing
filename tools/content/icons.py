"""Which bag icon each housing item gets, from its name (see build_content.py)."""

import re

# Each rule: words to look for at the start of a word in the name, and icons to use, the
# first one the client has wins. Earlier rules win over later ones.
FIGURES = [
    (["kobold"], ["INV_Misc_Head_Kobold_01"]),
    (["murloc"], ["INV_Misc_Head_Murloc_01"]),
    (["hogger"], ["INV_Misc_Head_Gnoll_01"]),
    (["vancleef"], ["Achievement_Boss_EdwinVancleef"]),
    (["onyxia"], ["Achievement_Boss_Onyxia"]),
    (["ragnaros"], ["Achievement_Boss_Ragnaros"]),
    (["nefarian"], ["Achievement_Boss_Nefarion"]),
    (["hakkar"], ["Achievement_Boss_Hakkar"]),
    (["c'thun"], ["Achievement_Boss_CThun"]),
    (["malchezaar"], ["Achievement_Boss_PrinceMalchezaar_02"]),
    (["vashj"], ["Achievement_Boss_LadyVashj"]),
    (["kael'thas"], ["Achievement_Boss_Kael'thasSunstrider_01"]),
    (["archimonde"], ["Achievement_Boss_Archimonde "]),  # the client's own name ends in a space
    (["illidan"], ["Achievement_Boss_Illidan"]),
    (["kil'jaeden"], ["Achievement_Boss_Kiljaedan"]),
    (["ingvar"], ["Achievement_Dungeon_UtgardeKeep_Normal"]),
    (["kel'thuzad"], ["Achievement_Boss_KelThuzad_01"]),
    (["sartharion"], ["Achievement_Dungeon_CoABlackDragonflight"]),
    (["malygos"], ["Achievement_Dungeon_NexusRaid"]),
    (["yogg"], ["Achievement_Boss_YoggSaron_01"]),
    (["lich king"], ["Achievement_Boss_LichKing"]),
]

BUILDINGS = [
    (["skybreaker"], ["Achievement_Zone_IceCrown_01"]),
    (["orgrim's hammer", "zeppelin"], ["Achievement_Dungeon_HordeAirship"]),
    (["pirate", "rowboat"], ["Achievement_Zone_Stranglethorn_01"]),
    (["wintergrasp"], ["Achievement_Win_Wintergrasp"]),
    (["ulduar"], ["Achievement_Dungeon_Ulduar77_Normal"]),
    (["stratholme"], ["Achievement_Dungeon_CoTStratholme_Normal"]),
    (["broken", "wrecked", "shredded", "burnt", "ruined"], ["Spell_Shadow_UnsummonBuilding"]),
    (["westfall"], ["Achievement_Zone_WestFall_01"]),
    (["duskwood"], ["Achievement_Zone_Duskwood"]),
    (["redridge"], ["Achievement_Zone_RedridgeMountains"]),
    (["razorfen"], ["Achievement_Zone_Barrens_01"]),
    (["northrend"], ["Achievement_Zone_HowlingFjord_01"]),
    (["night elf"], ["Spell_Arcane_PortalDarnassus"]),
    (["draenei"], ["Spell_Arcane_PortalExodar"]),
    (["tauren"], ["Spell_Arcane_PortalThunderBluff"]),
    (["forsaken"], ["Spell_Arcane_PortalUnderCity"]),
    (["orc"], ["Spell_Arcane_PortalOrgrimmar"]),
    (["troll"], ["Achievement_Character_Troll_Male"]),
    (["human"], ["Spell_Arcane_PortalStormWind"]),
    (["brewfest"], ["INV_Holiday_BrewfestBuff_01"]),
]

BANNER_WORDS = ["banner", "standard", "flag", "tabard"]
BANNERS = [
    (["stormwind", "alliance", "human"], ["INV_Misc_Tournaments_Banner_Human"]),
    (["ironforge", "dwarven", "wildhammer"], ["INV_Misc_Tournaments_Banner_Dwarf"]),
    (["darnassus", "night elf"], ["INV_Misc_Tournaments_Banner_NightElf"]),
    (["gnomeregan", "gnome"], ["INV_Misc_Tournaments_Banner_Gnome"]),
    (["exodar", "draenei"], ["INV_Misc_Tournaments_Banner_Draenei"]),
    (["orgrimmar", "horde", "orc", "blood furnace"], ["INV_Misc_Tournaments_Banner_Orc"]),
    (["thunder bluff", "tauren"], ["INV_Misc_Tournaments_Banner_Tauren"]),
    (["darkspear", "troll"], ["INV_Misc_Tournaments_Banner_Troll"]),
    (["silvermoon", "blood elf", "kael'thas", "shattered sun"], ["INV_Misc_Tournaments_Banner_BloodElf"]),
    (["undercity", "forsaken", "scourge", "plaguelands", "shadow council"], ["INV_Misc_Tournaments_Banner_Scourge"]),
    (["pirate"], ["INV_BannerPVP_03"]),
]

GENERAL = [
    (["mannequin"], ["INV_Chest_Chain_05"]),
    (["music box", "jukebox"], ["INV_Misc_Flute_01"]),
    (["bank chest", "guild vault", "vault"], ["INV_Box_02"]),
    (["mailbox"], ["INV_Letter_02"]),
    (["barber"], ["INV_Misc_Comb_01"]),
    (["anvil", "forge", "furnace"], ["Trade_BlackSmithing"]),
    (["alchemy", "chemistry", "lab table", "containment jar"], ["Trade_Alchemy"]),
    (["stove", "cooking", "cookpot", "meat rack"], ["INV_Misc_Food_15"]),
    (["workbench", "workshop", "shredder", "rocket cart", "machine", "mana cell", "maintenance", "cube"],
     ["Trade_Engineering"]),
    (["surgical", "hospital"], ["INV_Misc_Bandage_15"]),
    (["winter veil tree"], ["INV_Holiday_Christmas_Present_01"]),
    (["gift", "present"], ["INV_Holiday_Christmas_Present_02"]),
    (["pumpkin"], ["Achievement_Halloween_Smiley_01"]),
    (["candy skull"], ["INV_Misc_CandySkull"]),
    (["noblegarden"], ["Achievement_Noblegarden_Chocolate_Egg"]),
    (["heart ", "hearts", "valentine"], ["Achievement_WorldEvent_Valentine"]),
    (["doll"], ["INV_Misc_Toy_07"]),
    (["kickball", "ball and chain"], ["INV_Misc_ThrowingBall_01"]),
    (["firecracker", "firework"], ["INV_Misc_MissileSmall_Red"]),
    (["powder keg", "cannonball", "bomb"], ["INV_Misc_Bomb_05"]),
    (["cannon"], ["Ability_Vehicle_SiegeEngineCannon"]),
    (["ribbon"], ["INV_Misc_Ribbon_01"]),
    (["cornucopia"], ["INV_Holiday_Thanksgiving_Cornucopia"]),
    (["crate", "box", "supply", "supplies", "cargo", "sack"], ["INV_Crate_01"]),
    (["basket", "corn", "grain", "harvest"], ["INV_Misc_Basket_01"]),
    (["marigold"], ["INV_Misc_Marrigolds_01"]),
    (["keg", "beer", "brew", "ale ", "barrel"], ["INV_Holiday_BrewfestBuff_01"]),
    (["mug", "tankard"], ["INV_Misc_Mug"]),
    (["bottle", "wine"], ["INV_Wine_01"]),
    (["lantern", "lamp"], ["INV_Misc_Lantern_01"]),
    (["candle", "candelabra"], ["INV_Misc_Candle_01"]),
    (["fel "], ["Inv_Misc_SummerFest_BrazierGreen"]),
    (["brazier", "campfire", "fire", "flame", "pyre", "hearth"], ["Inv_Misc_SummerFest_BrazierOrange"]),
    (["torch"], ["INV_Torch_Lit"]),
    (["book", "tome"], ["INV_Misc_Book_09"]),
    (["scroll"], ["INV_Scroll_03"]),
    (["bed", "bunk", "cot "], ["Spell_Nature_Sleep"]),
    (["rug", "carpet", "mat "], ["INV_Fabric_Wool_03"]),
    (["throne"], ["Achievement_Dungeon_FrozenThrone"]),
    (["chair", "stool", "bench", "seat"], ["INV_FishingChair"]),
    (["coffin", "sarcophagus"], ["Spell_Shadow_AnimateDead"]),
    (["cage", "jar"], ["INV_Box_Birdcage_01"]),
    (["dresser", "wardrobe", "footlocker", "chest", "coffer", "cache", "trunk", "ark"], ["INV_Box_02"]),
    (["totem", "dream catcher"], ["INV_Relics_TotemofLife"]),
    (["idol"], ["INV_Misc_Idol_02"]),
    (["frostmourne"], ["Achievement_Dungeon_Icecrown_Frostmourne"]),
    (["statue", "figure", "pedestal", "shrine", "altar"], ["INV_Misc_Statue_01"]),
    (["rune stone", "wind stone", "obelisk", "monolith", "tablet"], ["INV_Misc_StoneTablet_05"]),
    (["egg"], ["INV_Egg_03"]),
    (["head of"], ["INV_Misc_Head_Dragon_Black"]),
    (["orb", "globe", "crystal ball", "scrying", "eye of"], ["INV_Misc_Orb_01"]),
    (["crystal", "shard", "naaru", "gem"], ["INV_Misc_Gem_Crystal_02"]),
    (["mushroom", "shroom"], ["INV_Mushroom_11"]),
    (["plant", "flower", "songflower", "reed", "bogbean", "bush", "shrub", "fern", "vine", "tree", "grass", "hay"], ["INV_Misc_Herb_07"]),
    (["skull", "bone", "skeleton"], ["INV_Misc_Bone_HumanSkull_01"]),
    (["spine"], ["INV_Misc_Bone_06"]),
    (["fish"], ["INV_Misc_Fish_02"]),
    (["rack", "weapon", "sword", "axe", "spear", "lance"], ["INV_Sword_04"]),
    (["shield", "crest"], ["INV_Shield_06"]),
    (["drum"], ["INV_Misc_Drum_01"]),
    (["gong", "bell"], ["INV_Misc_Bell_01"]),
    (["cauldron", "urn", "pot "], ["INV_Misc_Cauldron_Nature"]),
    (["bowl"], ["INV_Misc_Bowl_01"]),
    (["fountain", "well"], ["Spell_Frost_SummonWaterElemental_2"]),
    (["hourglass", "clock"], ["INV_Misc_PocketWatch_01"]),
    (["moon"], ["Spell_Nature_MoonGlow"]),
    (["gear", "parts", "pipe", "valve"], ["INV_Misc_Gear_01"]),
    (["rock", "boulder", "stone"], ["INV_Stone_15"]),
    (["sign", "notice", "letter", "map"], ["INV_Misc_Note_01"]),
]

FIGURE_DEFAULT = ["INV_Misc_Statue_01"]
BUILDING_DEFAULT = ["INV_Misc_Rune_01"]   # the Hearthstone: home
FURNISHING_DEFAULT = BUILDING_DEFAULT
MOVER = ["INV_Gauntlets_04"]


def _text(name):
    return " " + re.sub(r"[^a-z0-9' ]+", " ", name.lower()) + " "


def _match(text, rules):
    for words, icons in rules:
        if any(" " + word in text for word in words):
            return icons
    return None


def icons_for(name, style="decor", building=False):
    """Icons to try for a piece, best first."""
    text = _text(name)
    if style == "figure":
        return _match(text, FIGURES) or FIGURE_DEFAULT
    if building:
        found = _match(text, BUILDINGS) or _match(text, GENERAL)
        return found or BUILDING_DEFAULT
    if any(" " + word in text for word in BANNER_WORDS):
        return _match(text, BANNERS) or ["INV_Banner_02"]
    return _match(text, GENERAL) or FURNISHING_DEFAULT
