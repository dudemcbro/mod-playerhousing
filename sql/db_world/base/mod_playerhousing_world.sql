SET @PH_STEWARD_ENTRY := 900200;
SET @PH_ITEM_TENT := 901100;
SET @PH_ITEM_CAMPFIRE := 901101;
SET @PH_ITEM_BEDROLL := 901102;
SET @PH_ITEM_CRATE := 901103;
SET @PH_ITEM_LANTERN := 901104;
SET @PH_ITEM_CHAIR := 901105;
SET @PH_ITEM_TABLE := 901106;
SET @PH_FURNITURE_ITEM_DISPLAY := COALESCE((SELECT `displayid` FROM `item_template` WHERE `entry` = 6948 LIMIT 1), 6291);

CREATE TABLE IF NOT EXISTS `mod_playerhousing_style` (
  `style_id` tinyint unsigned NOT NULL,
  `style_code` varchar(16) NOT NULL,
  `display_name` varchar(32) NOT NULL,
  `map_id` int unsigned NOT NULL DEFAULT 0,
  `spawn_x` float NOT NULL,
  `spawn_y` float NOT NULL,
  `spawn_z` float NOT NULL,
  `spawn_o` float NOT NULL DEFAULT 0,
  PRIMARY KEY (`style_id`),
  UNIQUE KEY `uq_mod_playerhousing_style_code` (`style_code`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `mod_playerhousing_stage` (
  `stage` tinyint unsigned NOT NULL,
  `upgrade_cost_copper` int unsigned NOT NULL DEFAULT 0,
  `max_items` int unsigned NOT NULL DEFAULT 0,
  `place_radius` float NOT NULL DEFAULT 0,
  PRIMARY KEY (`stage`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `mod_playerhousing_catalog` (
  `catalog_id` int unsigned NOT NULL,
  `display_name` varchar(80) NOT NULL,
  `gameobject_entry` int unsigned NOT NULL,
  `unlock_cost_copper` int unsigned NOT NULL DEFAULT 0,
  `min_stage` tinyint unsigned NOT NULL DEFAULT 0,
  `style_mask` int unsigned NOT NULL DEFAULT 0,
  `is_default` tinyint unsigned NOT NULL DEFAULT 0,
  `active` tinyint unsigned NOT NULL DEFAULT 1,
  `sort_order` int unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`catalog_id`),
  KEY `idx_mod_playerhousing_catalog_stage` (`min_stage`),
  KEY `idx_mod_playerhousing_catalog_active` (`active`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `mod_playerhousing_style_default_unlock` (
  `style_id` tinyint unsigned NOT NULL,
  `catalog_id` int unsigned NOT NULL,
  PRIMARY KEY (`style_id`,`catalog_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `mod_playerhousing_style_object` (
  `style_id` tinyint unsigned NOT NULL,
  `min_stage` tinyint unsigned NOT NULL DEFAULT 0,
  `object_index` tinyint unsigned NOT NULL,
  `gameobject_entry` int unsigned NOT NULL,
  `offset_x` float NOT NULL DEFAULT 0,
  `offset_y` float NOT NULL DEFAULT 0,
  `offset_z` float NOT NULL DEFAULT 0,
  `orientation_offset` float NOT NULL DEFAULT 0,
  PRIMARY KEY (`style_id`,`min_stage`,`object_index`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `mod_playerhousing_furniture_item` (
  `item_entry` int unsigned NOT NULL,
  `catalog_id` int unsigned NOT NULL DEFAULT 0,
  `display_name` varchar(80) NOT NULL,
  `spawn_type` tinyint unsigned NOT NULL DEFAULT 0,
  `spawn_entry` int unsigned NOT NULL,
  `display_id` int unsigned NOT NULL DEFAULT 0,
  `scale` float NOT NULL DEFAULT 1,
  `collision_radius` float NOT NULL DEFAULT 1,
  `min_distance` float NOT NULL DEFAULT 1.5,
  `orientation_offset` float NOT NULL DEFAULT 0,
  `required_stage` tinyint unsigned NOT NULL DEFAULT 0,
  `style_mask` int unsigned NOT NULL DEFAULT 0,
  `consume_on_place` tinyint unsigned NOT NULL DEFAULT 1,
  `active` tinyint unsigned NOT NULL DEFAULT 1,
  `sort_order` int unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`item_entry`),
  KEY `idx_mod_playerhousing_furniture_item_stage` (`required_stage`),
  KEY `idx_mod_playerhousing_furniture_item_active` (`active`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DELETE FROM `mod_playerhousing_style`;
INSERT INTO `mod_playerhousing_style` (`style_id`, `style_code`, `display_name`, `map_id`, `spawn_x`, `spawn_y`, `spawn_z`, `spawn_o`) VALUES
(1, 'human', 'Human Cottage',  658, 435.743, 212.413, 528.709, 6.25646),
(2, 'gnome', 'Gnome Workshop', 658, 435.743, 212.413, 528.709, 6.25646),
(3, 'tauren', 'Tauren Lodge',  658, 435.743, 212.413, 528.709, 6.25646),
(4, 'undead', 'Undead Crypt',  658, 435.743, 212.413, 528.709, 6.25646);

DELETE FROM `mod_playerhousing_stage`;
INSERT INTO `mod_playerhousing_stage` (`stage`, `upgrade_cost_copper`, `max_items`, `place_radius`) VALUES
(0,         0,  5, 50.0),
(1,    500000, 10, 55.0),
(2,   1500000, 18, 60.0),
(3,   4500000, 28, 65.0),
(4,  13500000, 40, 70.0),
(5,  40500000, 55, 75.0),
(6, 121500000, 72, 80.0);

DELETE FROM `mod_playerhousing_catalog`;
INSERT INTO `mod_playerhousing_catalog` (`catalog_id`, `display_name`, `gameobject_entry`, `unlock_cost_copper`, `min_stage`, `style_mask`, `is_default`, `active`, `sort_order`) VALUES
(1001, 'Starter Chair',             180047,      0, 0, 0, 1, 1,  10),
(1002, 'Starter Table',             180885,      0, 0, 0, 1, 1,  20),
(1003, 'Starter Barrel',            180779,      0, 0, 0, 1, 1,  30),
(1004, 'Starter Candle',            180338,      0, 0, 0, 1, 1,  40),
(1101, 'Stormwind Rug',             180334,  75000, 1, 1, 0, 1, 100),
(1102, 'Elven Wooden Table',        180879, 180000, 2, 1, 0, 1, 110),
(1103, 'Alliance Banner',           192252, 360000, 3, 1, 0, 1, 120),
(1201, 'Gnome Maintenance Light',   193586,  90000, 1, 2, 0, 1, 200),
(1202, 'Dwarven Workshop Table',    180884, 210000, 2, 2, 0, 1, 210),
(1203, 'Gnome Rocket Cart',         190227, 420000, 3, 2, 0, 1, 220),
(1301, 'Tauren Rug',                188346,  90000, 1, 4, 0, 1, 300),
(1302, 'Winterhoof Totem',           50523, 210000, 2, 4, 0, 1, 310),
(1303, 'Magna Totem',               187890, 420000, 3, 4, 0, 1, 320),
(1401, 'Forsaken Banner',           180432,  90000, 1, 8, 0, 1, 400),
(1402, 'Skull Candle',              180425, 210000, 2, 8, 0, 1, 410),
(1403, 'Coffin',                     19425, 540000, 4, 8, 0, 1, 420),
(1501, 'Bookshelf',                 183268, 260000, 2, 0, 0, 1, 500),
(1502, 'Hospital Bed',              178226, 360000, 3, 0, 0, 1, 510),
(1503, 'Round Table',               186422, 480000, 4, 0, 0, 1, 520),
(1504, 'Dark Brazier',              182014, 640000, 5, 0, 0, 1, 530),
(1505, 'Musty Coffin',              190948, 960000, 6, 8, 0, 1, 540);

DELETE FROM `mod_playerhousing_style_default_unlock`;
INSERT INTO `mod_playerhousing_style_default_unlock` (`style_id`, `catalog_id`) VALUES
(1, 1101),
(2, 1201),
(3, 1301),
(4, 1401);

DELETE FROM `mod_playerhousing_style_object`;
INSERT INTO `mod_playerhousing_style_object` (`style_id`, `min_stage`, `object_index`, `gameobject_entry`, `offset_x`, `offset_y`, `offset_z`, `orientation_offset`) VALUES
(1, 0, 0, 184592,  0.0,  0.0, 0.0, 0.0),
(1, 0, 1,   1798,  9.0,  0.0, 0.0, 0.0),
(1, 0, 2, 193684,  0.3, -0.7, 0.0, 0.0),
(1, 0, 3, 181302, -0.8,  0.6, 0.0, 0.0),
(1, 0, 4, 179977, -0.1,  0.0, 0.0, 0.0),
(1, 1, 1, 180334,  0.0,  0.0, 0.0, 0.0),
(1, 2, 2, 192252,  3.0,  0.0, 0.0, 0.0),
(2, 0, 0, 184592,  0.0,  0.0, 0.0, 0.0),
(2, 0, 1,   1798,  9.0,  0.0, 0.0, 0.0),
(2, 0, 2, 193684,  0.3, -0.7, 0.0, 0.0),
(2, 0, 3, 181302, -0.8,  0.6, 0.0, 0.0),
(2, 0, 4, 179977, -0.1,  0.0, 0.0, 0.0),
(2, 1, 1, 193586,  0.0,  0.0, 0.0, 0.0),
(2, 3, 2, 190227,  3.0,  0.0, 0.0, 0.0),
(3, 0, 0, 184592,  0.0,  0.0, 0.0, 0.0),
(3, 0, 1,   1798,  9.0,  0.0, 0.0, 0.0),
(3, 0, 2, 193684,  0.3, -0.7, 0.0, 0.0),
(3, 0, 3, 181302, -0.8,  0.6, 0.0, 0.0),
(3, 0, 4, 179977, -0.1,  0.0, 0.0, 0.0),
(3, 1, 1, 188346,  0.0,  0.0, 0.0, 0.0),
(3, 2, 2,  50523,  3.0,  0.0, 0.0, 0.0),
(4, 0, 0, 184592,  0.0,  0.0, 0.0, 0.0),
(4, 0, 1,   1798,  9.0,  0.0, 0.0, 0.0),
(4, 0, 2, 193684,  0.3, -0.7, 0.0, 0.0),
(4, 0, 3, 181302, -0.8,  0.6, 0.0, 0.0),
(4, 0, 4, 179977, -0.1,  0.0, 0.0, 0.0),
(4, 1, 1,  19425,  0.5,  0.0, 0.0, 0.0),
(4, 2, 2, 180432,  3.0,  0.0, 0.0, 0.0);

DELETE FROM `mod_playerhousing_furniture_item`;
INSERT INTO `mod_playerhousing_furniture_item`
(`item_entry`, `catalog_id`, `display_name`, `spawn_type`, `spawn_entry`, `display_id`, `scale`, `collision_radius`, `min_distance`, `orientation_offset`, `required_stage`, `style_mask`, `consume_on_place`, `active`, `sort_order`) VALUES
(@PH_ITEM_TENT,      0,    'Canvas Tent Kit',      0, 184592, 0, 1.00, 2.8, 3.0, 0.0, 0, 0, 1, 1, 10),
(@PH_ITEM_CAMPFIRE,  0,    'Campfire Kit',         0,   1798, 0, 1.00, 1.4, 2.0, 0.0, 0, 0, 1, 1, 20),
(@PH_ITEM_BEDROLL,   0,    'Bedroll Kit',          0, 181302, 0, 1.00, 0.8, 1.2, 0.0, 0, 0, 1, 1, 30),
(@PH_ITEM_CRATE,     0,    'Supply Crate Kit',     0, 179977, 0, 1.00, 0.9, 1.2, 0.0, 0, 0, 1, 1, 40),
(@PH_ITEM_LANTERN,   0,    'Hanging Lantern Kit',  0, 193684, 0, 1.00, 0.6, 1.0, 0.0, 0, 0, 1, 1, 50),
(@PH_ITEM_CHAIR,  1001,    'Cozy Chair Kit',       0, 180047, 0, 1.00, 0.9, 1.4, 0.0, 1, 0, 1, 1, 60),
(@PH_ITEM_TABLE,  1002,    'Cozy Table Kit',       0, 180885, 0, 1.00, 1.3, 1.8, 0.0, 1, 0, 1, 1, 70);

DELETE FROM `creature` WHERE `id1` = @PH_STEWARD_ENTRY;
DELETE FROM `creature_template_model` WHERE `CreatureID` = @PH_STEWARD_ENTRY;
DELETE FROM `creature_template` WHERE `entry` = @PH_STEWARD_ENTRY;

INSERT INTO `creature_template`
(`entry`, `name`, `subname`, `gossip_menu_id`, `minlevel`, `maxlevel`, `faction`, `npcflag`, `unit_class`, `type`, `AIName`, `MovementType`, `RegenHealth`, `ScriptName`, `VerifiedBuild`) VALUES
(@PH_STEWARD_ENTRY, 'Housing Steward', 'Krook''s Cranny', 0, 80, 80, 35, 129, 1, 7, '', 0, 1, 'npc_playerhousing_steward', 0);

INSERT INTO `creature_template_model`
(`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(@PH_STEWARD_ENTRY, 0, 25384, 1.0, 1.0, 0);

DELETE FROM `item_template`
WHERE `entry` IN (@PH_ITEM_TENT, @PH_ITEM_CAMPFIRE, @PH_ITEM_BEDROLL, @PH_ITEM_CRATE, @PH_ITEM_LANTERN, @PH_ITEM_CHAIR, @PH_ITEM_TABLE);

INSERT INTO `item_template`
(`entry`, `class`, `subclass`, `SoundOverrideSubclass`, `name`, `displayid`, `Quality`, `Flags`, `FlagsExtra`, `BuyCount`, `BuyPrice`, `SellPrice`, `InventoryType`, `AllowableClass`, `AllowableRace`, `ItemLevel`, `RequiredLevel`, `maxcount`, `stackable`, `bonding`, `spellid_1`, `spelltrigger_1`, `spellcharges_1`, `spellcooldown_1`, `spellcategorycooldown_1`, `description`, `ScriptName`, `VerifiedBuild`) VALUES
(@PH_ITEM_TENT,      15, 0, -1, 'Housing: Canvas Tent Kit',     @PH_FURNITURE_ITEM_DISPLAY, 1, 0, 0, 1, 50000, 12500, 0, -1, -1, 1, 1, 0, 20, 1, 1543, 0, 0, -1, -1, 'Use: Select a location in your house to place this furniture.', 'item_playerhousing_furniture', 0),
(@PH_ITEM_CAMPFIRE,  15, 0, -1, 'Housing: Campfire Kit',        @PH_FURNITURE_ITEM_DISPLAY, 1, 0, 0, 1, 15000,  3750, 0, -1, -1, 1, 1, 0, 20, 1, 1543, 0, 0, -1, -1, 'Use: Select a location in your house to place this furniture.', 'item_playerhousing_furniture', 0),
(@PH_ITEM_BEDROLL,   15, 0, -1, 'Housing: Bedroll Kit',         @PH_FURNITURE_ITEM_DISPLAY, 1, 0, 0, 1, 20000,  5000, 0, -1, -1, 1, 1, 0, 20, 1, 1543, 0, 0, -1, -1, 'Use: Select a location in your house to place this furniture.', 'item_playerhousing_furniture', 0),
(@PH_ITEM_CRATE,     15, 0, -1, 'Housing: Supply Crate Kit',    @PH_FURNITURE_ITEM_DISPLAY, 1, 0, 0, 1, 12000,  3000, 0, -1, -1, 1, 1, 0, 20, 1, 1543, 0, 0, -1, -1, 'Use: Select a location in your house to place this furniture.', 'item_playerhousing_furniture', 0),
(@PH_ITEM_LANTERN,   15, 0, -1, 'Housing: Hanging Lantern Kit', @PH_FURNITURE_ITEM_DISPLAY, 1, 0, 0, 1, 18000,  4500, 0, -1, -1, 1, 1, 0, 20, 1, 1543, 0, 0, -1, -1, 'Use: Select a location in your house to place this furniture.', 'item_playerhousing_furniture', 0),
(@PH_ITEM_CHAIR,     15, 0, -1, 'Housing: Cozy Chair Kit',      @PH_FURNITURE_ITEM_DISPLAY, 2, 0, 0, 1, 30000,  7500, 0, -1, -1, 1, 1, 0, 20, 1, 1543, 0, 0, -1, -1, 'Use: Select a location in your house to place this furniture.', 'item_playerhousing_furniture', 0),
(@PH_ITEM_TABLE,     15, 0, -1, 'Housing: Cozy Table Kit',      @PH_FURNITURE_ITEM_DISPLAY, 2, 0, 0, 1, 45000, 11250, 0, -1, -1, 1, 1, 0, 20, 1, 1543, 0, 0, -1, -1, 'Use: Select a location in your house to place this furniture.', 'item_playerhousing_furniture', 0);

DELETE FROM `npc_vendor`
WHERE `entry` = @PH_STEWARD_ENTRY
  AND `item` IN (@PH_ITEM_TENT, @PH_ITEM_CAMPFIRE, @PH_ITEM_BEDROLL, @PH_ITEM_CRATE, @PH_ITEM_LANTERN, @PH_ITEM_CHAIR, @PH_ITEM_TABLE);

INSERT INTO `npc_vendor`
(`entry`, `slot`, `item`, `maxcount`, `incrtime`, `ExtendedCost`, `VerifiedBuild`) VALUES
(@PH_STEWARD_ENTRY, 0, @PH_ITEM_TENT, 0, 0, 0, 0),
(@PH_STEWARD_ENTRY, 0, @PH_ITEM_CAMPFIRE, 0, 0, 0, 0),
(@PH_STEWARD_ENTRY, 0, @PH_ITEM_BEDROLL, 0, 0, 0, 0),
(@PH_STEWARD_ENTRY, 0, @PH_ITEM_CRATE, 0, 0, 0, 0),
(@PH_STEWARD_ENTRY, 0, @PH_ITEM_LANTERN, 0, 0, 0, 0),
(@PH_STEWARD_ENTRY, 0, @PH_ITEM_CHAIR, 0, 0, 0, 0),
(@PH_STEWARD_ENTRY, 0, @PH_ITEM_TABLE, 0, 0, 0, 0);
