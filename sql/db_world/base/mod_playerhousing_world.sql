-- mod-playerhousing: world data.
--
-- The pieces themselves (furnishings and buildings, their items, objects and unlock rules)
-- are generated into mod_playerhousing_world_content.sql by tools/content/build_content.py.
-- Apply this file first, then the content file. Both can be re-applied at any time.

SET @STEWARD := 900200;
SET @MANNEQUIN := 900201;
SET @CHEST_BANKER := 900202;
SET @GHOST := 900203;
SET @HOUSE_KEY := 902000;
SET @MARKER := 903990;
SET @RING := 903991;

-- Tables from before furnishings became items and house levels were removed.
DROP TABLE IF EXISTS `mod_playerhousing_style_object`;
DROP TABLE IF EXISTS `mod_playerhousing_style_default_unlock`;
DROP TABLE IF EXISTS `mod_playerhousing_stage`;
DROP TABLE IF EXISTS `mod_playerhousing_style`;
DROP TABLE IF EXISTS `mod_playerhousing_furniture_item`;
DROP TABLE IF EXISTS `mod_playerhousing_catalog`;

-- Where players land on the island and where Krook stands, per island layout. The
-- PlayerHousing.Layout setting picks one; the server data has to match (see
-- tools/gm-island-cleared). center/radius: the private copy around the island.
DROP TABLE IF EXISTS `mod_playerhousing_layout`;
CREATE TABLE `mod_playerhousing_layout` (
  `layout` varchar(16) NOT NULL,
  `map_id` int unsigned NOT NULL,
  `landing_x` float NOT NULL,
  `landing_y` float NOT NULL,
  `landing_z` float NOT NULL,
  `landing_o` float NOT NULL,
  `steward_offset_x` float NOT NULL DEFAULT 7,
  `steward_offset_y` float NOT NULL DEFAULT 2,
  `center_x` float NOT NULL,
  `center_y` float NOT NULL,
  `radius` float NOT NULL DEFAULT 230,
  `description` varchar(120) NOT NULL DEFAULT '',
  PRIMARY KEY (`layout`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT INTO `mod_playerhousing_layout` (`layout`, `map_id`, `landing_x`, `landing_y`, `landing_z`, `landing_o`, `steward_offset_x`, `steward_offset_y`, `center_x`, `center_y`, `radius`, `description`) VALUES
('cleared',    1, 16240.0, 16296.0, 12.92, 1.5708, 5.0, 3.0, 16250.0, 16334.0, 230.0, 'GM Island with the guild hall removed: land on the plateau where it stood'),
('guildhouse', 1, 16224.5, 16283.5, 13.18, 1.5708, 3.0, 2.5, 16250.0, 16334.0, 230.0, 'GM Island with its guild hall: land in the hall''s entry room');

-- Everything a player can place: one row per item.
--   kind      0 furnishing, 1 building
--   category  0 Starter, 1 Buildings, 2 Exploration, 3 Dungeons, 4 Raids, 5 Reputation,
--             6 Professions, 7 Holidays, 8 Capstones
--   go_entry  the object spawned normally; edit_go_entry a clickable copy used while
--             decorating (only for pieces that work like the real thing: chairs, stations)
--   footprint radius in yards (targeting circle, distance to a building); height of the top
--             surface; outline the rectangle on the ground in the piece's own frame (x forward),
--             which is what counts as inside a building
--   flags     1 surface, 2 small, 4 unlock per character, 8 first-login gift, 16 starter wreckage,
--             32 mannequin, 64 Bank Chest, 128 Music Box, 256 figurine
DROP TABLE IF EXISTS `mod_playerhousing_piece`;
CREATE TABLE `mod_playerhousing_piece` (
  `item_entry` int unsigned NOT NULL,
  `kind` tinyint unsigned NOT NULL DEFAULT 0,
  `category` tinyint unsigned NOT NULL DEFAULT 0,
  `name` varchar(80) NOT NULL,
  `go_entry` int unsigned NOT NULL,
  `edit_go_entry` int unsigned NOT NULL DEFAULT 0,
  `creature_entry` int unsigned NOT NULL DEFAULT 0,  -- figurines: the creature shown
  `scale` float NOT NULL DEFAULT 1,
  `footprint` float NOT NULL DEFAULT 1,
  `height` float NOT NULL DEFAULT 1,
  `flags` int unsigned NOT NULL DEFAULT 0,
  `copy_cost` int unsigned NOT NULL DEFAULT 0,
  `sort_order` int unsigned NOT NULL DEFAULT 0,
  `hint` varchar(255) NOT NULL DEFAULT '',
  `legacy_catalog_id` int unsigned NOT NULL DEFAULT 0,
  `outline_min_x` float NOT NULL DEFAULT 0,
  `outline_min_y` float NOT NULL DEFAULT 0,
  `outline_max_x` float NOT NULL DEFAULT 0,
  `outline_max_y` float NOT NULL DEFAULT 0,
  PRIMARY KEY (`item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- What unlocks a piece. Rules in the same rule_group must all be met; any complete group
-- unlocks the piece. No rules at all: everyone has it.
--   rule_type 1 level (param1), 2 achievement (param1), 3 reputation (param1 faction,
--             param2 rank, 7 = Exalted), 4 quest rewarded (param1), 5 kill (param1 creature),
--             6 explore area or zone (param1), 7 skill (param1 skill, param2 value),
--             8 never (GM or UnlockAll only)
DROP TABLE IF EXISTS `mod_playerhousing_piece_rule`;
CREATE TABLE `mod_playerhousing_piece_rule` (
  `item_entry` int unsigned NOT NULL,
  `rule_group` tinyint unsigned NOT NULL DEFAULT 0,
  `rule_index` tinyint unsigned NOT NULL,
  `rule_type` tinyint unsigned NOT NULL,
  `param1` int unsigned NOT NULL DEFAULT 0,
  `param2` int unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`item_entry`, `rule_group`, `rule_index`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Krook, the housing steward. No vendor window any more: everything comes from the
-- Collection.
DELETE FROM `npc_vendor` WHERE `entry` = @STEWARD;
DELETE FROM `creature` WHERE `id1` = @STEWARD;
DELETE FROM `creature_template_model` WHERE `CreatureID` = @STEWARD;
DELETE FROM `creature_template` WHERE `entry` = @STEWARD;

INSERT INTO `creature_template`
(`entry`, `name`, `subname`, `gossip_menu_id`, `minlevel`, `maxlevel`, `faction`, `npcflag`, `unit_class`, `type`, `AIName`, `MovementType`, `RegenHealth`, `ScriptName`, `VerifiedBuild`) VALUES
(@STEWARD, 'Krook', 'Housing Steward', 0, 80, 80, 35, 3, 1, 7, '', 0, 1, 'npc_playerhousing_steward', 0);

INSERT INTO `creature_template_model`
(`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(@STEWARD, 0, 25384, 1.0, 1.0, 0);

-- The mannequin: the figure a stand shows. The module gives it a player body and dresses
-- it in the gear on the stand; the model here is only a placeholder.
DELETE FROM `creature_template_model` WHERE `CreatureID` = @MANNEQUIN;
DELETE FROM `creature_template` WHERE `entry` = @MANNEQUIN;
INSERT INTO `creature_template`
(`entry`, `name`, `subname`, `gossip_menu_id`, `minlevel`, `maxlevel`, `faction`, `npcflag`, `unit_class`, `unit_flags`, `type`, `AIName`, `MovementType`, `RegenHealth`, `ScriptName`, `VerifiedBuild`) VALUES
(@MANNEQUIN, 'Mannequin', '', 0, 1, 1, 35, 1, 1, 770, 7, '', 0, 1, 'npc_playerhousing_mannequin', 0);
INSERT INTO `creature_template_model`
(`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(@MANNEQUIN, 0, 49, 1.0, 1.0, 0);

-- A Bank Chest's banker: unseen (the invisible stalker's model), unselectable, standing at
-- the chest for a few minutes after its owner opens it, so the bank works the usual way
-- (only within reach, and only while it's there).
DELETE FROM `creature_template_model` WHERE `CreatureID` = @CHEST_BANKER;
DELETE FROM `creature_template` WHERE `entry` = @CHEST_BANKER;
INSERT INTO `creature_template`
(`entry`, `name`, `subname`, `gossip_menu_id`, `minlevel`, `maxlevel`, `faction`, `npcflag`, `unit_class`, `unit_flags`, `type`, `AIName`, `MovementType`, `RegenHealth`, `ScriptName`, `VerifiedBuild`) VALUES
(@CHEST_BANKER, 'Bank Chest', '', 0, 1, 1, 35, 131072, 1, 33554434, 10, '', 0, 1, '', 0);
INSERT INTO `creature_template_model`
(`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(@CHEST_BANKER, 0, 11686, 1.0, 1.0, 0);

-- A ghost: a see-through copy of a piece following its player until it's set down. The
-- module gives it the piece's ghost model (tools/content/build_content.py adds those, and the
-- client patch adds them to the client); the model here is only a placeholder.
DELETE FROM `creature_template_model` WHERE `CreatureID` = @GHOST;
DELETE FROM `creature_template_movement` WHERE `CreatureId` = @GHOST;
DELETE FROM `creature_template` WHERE `entry` = @GHOST;
INSERT INTO `creature_template`
(`entry`, `name`, `subname`, `gossip_menu_id`, `minlevel`, `maxlevel`, `faction`, `npcflag`, `unit_class`, `unit_flags`, `type`, `AIName`, `MovementType`, `RegenHealth`, `ScriptName`, `VerifiedBuild`) VALUES
(@GHOST, 'Ghost of a piece', '', 0, 1, 1, 35, 0, 1, 33555202, 10, '', 0, 1, '', 0);
INSERT INTO `creature_template_model`
(`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(@GHOST, 0, 11686, 1.0, 1.0, 0);
INSERT INTO `creature_template_movement` (`CreatureId`, `Ground`, `Swim`, `Flight`, `Rooted`, `Chase`, `Random`, `InteractionPauseTimer`) VALUES
(@GHOST, 1, 1, 1, 0, 0, 0, 0);

-- Krook in the capital cities, beside each innkeeper.
SET @GUID := (SELECT COALESCE(MAX(`guid`), 0) FROM `creature`);
INSERT INTO `creature` (`guid`, `id1`, `map`, `spawnMask`, `phaseMask`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecs`, `wander_distance`, `MovementType`, `Comment`) VALUES
(@GUID + 1,  @STEWARD,   0, 1, 1, -8864.85,   672.40,   97.99, 5.201, 300, 0, 0, 'Krook: Stormwind'),
(@GUID + 2,  @STEWARD,   0, 1, 1, -4838.30,  -859.25,  502.00, 4.869, 300, 0, 0, 'Krook: Ironforge'),
(@GUID + 3,  @STEWARD,   1, 1, 1, 10124.80,  2225.58, 1328.81, 2.217, 300, 0, 0, 'Krook: Darnassus'),
(@GUID + 4,  @STEWARD, 530, 1, 1, -3748.86, -11698.11, -105.77, 3.146, 300, 0, 0, 'Krook: Exodar'),
(@GUID + 5,  @STEWARD,   1, 1, 1,  1630.92, -4440.29,   15.76, 2.758, 300, 0, 0, 'Krook: Orgrimmar'),
(@GUID + 6,  @STEWARD,   1, 1, 1, -1299.22,    41.51,  129.29, 0.559, 300, 0, 0, 'Krook: Thunder Bluff'),
(@GUID + 7,  @STEWARD,   0, 1, 1,  1632.42,   222.15,  -43.02, 2.845, 300, 0, 0, 'Krook: Undercity'),
(@GUID + 8,  @STEWARD, 530, 1, 1,  9685.48, -7366.49,   12.01, 4.485, 300, 0, 0, 'Krook: Silvermoon'),
(@GUID + 9,  @STEWARD, 530, 1, 1, -2185.12,  5402.64,   51.97, 1.239, 300, 0, 0, 'Krook: Shattrath'),
(@GUID + 10, @STEWARD, 571, 1, 1,  5718.80,   683.71,  645.83, 6.231, 300, 0, 0, 'Krook: Dalaran');

-- The House Key. Its spell does nothing by itself: using the key opens the Home menu.
DELETE FROM `item_template` WHERE `entry` = @HOUSE_KEY;
INSERT INTO `item_template`
(`entry`, `class`, `subclass`, `SoundOverrideSubclass`, `name`, `displayid`, `Quality`, `Flags`, `FlagsExtra`, `BuyCount`, `BuyPrice`, `SellPrice`, `InventoryType`, `AllowableClass`, `AllowableRace`, `ItemLevel`, `RequiredLevel`, `maxcount`, `stackable`, `bonding`, `spellid_1`, `spelltrigger_1`, `spellcharges_1`, `spellcooldown_1`, `spellcategorycooldown_1`, `description`, `ScriptName`, `VerifiedBuild`) VALUES
(@HOUSE_KEY, 15, 0, -1, 'House Key', 22071, 1, 0, 0, 1, 0, 0, 0, -1, -1, 1, 1, 1, 1, 1, 18282, 0, 0, 0, -1,
 'Right-click: go home, visit an island, decorate, your Collection.', 'item_playerhousing_key', 0);

-- The key's spell is caught before it casts, like the targeting circles of the pieces (whose
-- spells, one per circle size, are listed in the content file).
DELETE FROM `spell_script_names` WHERE `ScriptName` = 'spell_playerhousing_key';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(18282, 'spell_playerhousing_key');

-- The blue rune on tables and shelves while decorating: click it to put something on top.
DELETE FROM `gameobject_template` WHERE `entry` = @MARKER;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`, `ScriptName`, `VerifiedBuild`) VALUES
(@MARKER, 10, 7658, 'Put something here', '', '', '', 0.35, 'go_playerhousing_piece', 0);

-- Edit mode: the green rune under the selected piece (scaled to the piece when spawned).
DELETE FROM `gameobject_template` WHERE `entry` = @RING;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`, `ScriptName`, `VerifiedBuild`) VALUES
(@RING, 5, 674, 'Selected', '', '', '', 1, '', 0);

-- Krook's welcome tour: five short quests that walk through housing. Each completes the
-- moment the player does the thing (the module reports it as an event), and Krook anywhere,
-- in a city or on the island, hands them out and takes them back. The last one unlocks
-- Krook's Picnic Basket (a quest rule in the content list).
DELETE FROM `creature_queststarter` WHERE `quest` BETWEEN 900400 AND 900404;
DELETE FROM `creature_questender` WHERE `quest` BETWEEN 900400 AND 900404;
DELETE FROM `quest_request_items` WHERE `ID` BETWEEN 900400 AND 900404;
DELETE FROM `quest_offer_reward` WHERE `ID` BETWEEN 900400 AND 900404;
DELETE FROM `quest_template_addon` WHERE `ID` BETWEEN 900400 AND 900404;
DELETE FROM `quest_template` WHERE `ID` BETWEEN 900400 AND 900404;
INSERT INTO `quest_template`
(`ID`, `QuestType`, `QuestLevel`, `MinLevel`, `QuestSortID`, `RewardNextQuest`, `RewardMoney`, `Flags`,
 `LogTitle`, `LogDescription`, `QuestDescription`, `QuestCompletionLog`, `VerifiedBuild`) VALUES
(900400, 2, -1, 1, 876, 900401, 500, 0, 'Home Sweet Island',
 'Use your House Key to go to your island.',
 'Psst, $N! You''ve got an island now. All yours! Use the House Key in your bags and it takes you right there. I''ll meet you on the beach and show you around, promise.',
 'Talk to Krook on your island.', 0),
(900401, 2, -1, 1, 876, 900402, 500, 0, 'Making It Yours',
 'Place a furnishing: right-click one in your bags, then click where it should go.',
 'An empty island is a sad island. You''ve got a chair, a table and a lantern in your bags. Right-click one and the ground lights up: click where it should go. It turns to face you, too!',
 'Talk to Krook.', 0),
(900402, 2, -1, 1, 876, 900403, 500, 0, 'A Fresh Look',
 'Start decorating (House Key, Start decorating), then click a piece and turn it, nudge it or move it.',
 'Not quite right? Easy. Ask me to start decorating, then click any piece: turn it, nudge it, move it with the circle, make it bigger. Go on, move something!',
 'Talk to Krook.', 0),
(900403, 2, -1, 1, 876, 900404, 500, 0, 'Nothing Is Ever Lost',
 'Undo a change: House Key, Undo.',
 'Here''s the best secret, $N: nothing is ever lost. Didn''t like what you did? Undo, and it''s back. Picked something up? It goes back to your bags. Try it: undo something.',
 'Talk to Krook.', 0),
(900404, 2, -1, 1, 876, 0, 1000, 0, 'Open House',
 'Invite a guest (Island settings, Guests), or open your island to friends or to everyone (Island settings, Privacy).',
 'An island is better with friends. Invite someone in, or let your friends and guild visit. Or everyone! You decide who comes over, and you can change it whenever you like.',
 'Talk to Krook.', 0);
INSERT INTO `quest_template_addon` (`ID`, `PrevQuestID`, `SpecialFlags`) VALUES
(900400, 0, 2), (900401, 900400, 2), (900402, 900401, 2), (900403, 900402, 2), (900404, 900403, 2);
INSERT INTO `quest_request_items` (`ID`, `EmoteOnComplete`, `EmoteOnIncomplete`, `CompletionText`, `VerifiedBuild`) VALUES
(900400, 1, 1, 'Found it? The island, I mean.', 0),
(900401, 1, 1, 'Placed something yet?', 0),
(900402, 1, 1, 'Moved something?', 0),
(900403, 1, 1, 'Tried the undo?', 0),
(900404, 1, 1, 'Anyone coming over?', 0);
INSERT INTO `quest_offer_reward` (`ID`, `Emote1`, `RewardText`, `VerifiedBuild`) VALUES
(900400, 4, 'You made it! Sand, sea, and nobody telling you where to put your chairs. Welcome home.', 0),
(900401, 4, 'Now that''s a start! Every piece you place can be picked up again, so don''t be shy.', 0),
(900402, 4, 'Much better. You''ve got an eye for this.', 0),
(900403, 4, 'See? Nothing lost. Mistakes are free here.', 0),
(900404, 4, 'Company! Here, take this basket. For picnics. With your friends. Welcome home, $N.', 0);
INSERT INTO `creature_queststarter` (`id`, `quest`) VALUES
(@STEWARD, 900400), (@STEWARD, 900401), (@STEWARD, 900402), (@STEWARD, 900403), (@STEWARD, 900404);
INSERT INTO `creature_questender` (`id`, `quest`) VALUES
(@STEWARD, 900400), (@STEWARD, 900401), (@STEWARD, 900402), (@STEWARD, 900403), (@STEWARD, 900404);

-- Texts heading the menus.
DELETE FROM `npc_text` WHERE `ID` BETWEEN 900300 AND 900319;
INSERT INTO `npc_text` (`ID`, `text0_0`, `text0_1`, `Probability0`) VALUES
(900300, 'Your island, your rules. Everything you place can be picked up again, and every change can be undone.', '', 1),
(900301, 'Everything you can own. Click an unlocked piece to get copies; locked ones tell you how to earn them.', '', 1),
(900302, 'What should happen to this piece?', '', 1),
(900303, 'Whose island would you like to visit?', '', 1),
(900304, 'Who can visit, and what they see when they arrive.', '', 1),
(900305, 'Pieces that came back while your bags were full wait here.', '', 1),
(900306, 'How housing works:$B$B1. Right-click a furnishing in your bags, then click where it should go. It turns to face you.$B$B2. House Key, Start decorating. Click any piece to turn it, nudge it, move it or pick it up. Blue runes on tables take small pieces.$B$B3. Made a mistake? House Key, Undo. Picked-up pieces go back to your bags (or House Storage when your bags are full). Nothing is ever lost.$B$B4. Your Collection grows as you explore, run dungeons and raids, earn reputation and level professions. Faction buildings need Exalted with their faction.$B$B5. Island settings: who can visit, your guest list and a greeting for visitors.$B$B6. A Mannequin (starter set) shows off armor and weapons: click it to dress it from your bags.', '', 1),
(900307, 'What should go on top?', '', 1),
(900308, 'Guests can always visit, whatever your privacy setting.', '', 1),
(900309, 'Pieces near you, closest first.', '', 1),
(900310, 'A mannequin, showing off its owner''s gear.', '', 1),
(900311, 'A sturdy chest. Your bank is in here, and so is House Storage.', '', 1),
(900312, 'Weather, time of day and music: everyone on your island sees and hears them.', '', 1);
