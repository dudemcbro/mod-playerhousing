SET @PH_STEWARD_ENTRY := 900200;
SET @PH_ITEM_TENT := 901100;
SET @PH_ITEM_CAMPFIRE := 901101;
SET @PH_ITEM_BEDROLL := 901102;
SET @PH_ITEM_CRATE := 901103;
SET @PH_ITEM_LANTERN := 901104;
SET @PH_ITEM_CHAIR := 901105;
SET @PH_ITEM_TABLE := 901106;
SET @PH_FURNITURE_ITEM_DISPLAY := COALESCE((SELECT `displayid` FROM `item_template` WHERE `entry` = 6948 LIMIT 1), 6291);

UPDATE `mod_playerhousing_style`
SET
  `map_id` = 658,
  `spawn_x` = 435.743,
  `spawn_y` = 212.413,
  `spawn_z` = 528.709,
  `spawn_o` = 6.25646
WHERE `style_id` IN (1, 2, 3, 4);

UPDATE `mod_playerhousing_style_object`
SET `offset_x` = 9.0
WHERE `style_id` IN (1, 2, 3, 4)
  AND `min_stage` = 0
  AND `object_index` = 1
  AND `gameobject_entry` = 1798;

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

REPLACE INTO `mod_playerhousing_furniture_item`
(`item_entry`, `catalog_id`, `display_name`, `spawn_type`, `spawn_entry`, `display_id`, `scale`, `collision_radius`, `min_distance`, `orientation_offset`, `required_stage`, `style_mask`, `consume_on_place`, `active`, `sort_order`) VALUES
(@PH_ITEM_TENT,      0,    'Canvas Tent Kit',      0, 184592, 0, 1.00, 2.8, 3.0, 0.0, 0, 0, 1, 1, 10),
(@PH_ITEM_CAMPFIRE,  0,    'Campfire Kit',         0,   1798, 0, 1.00, 1.4, 2.0, 0.0, 0, 0, 1, 1, 20),
(@PH_ITEM_BEDROLL,   0,    'Bedroll Kit',          0, 181302, 0, 1.00, 0.8, 1.2, 0.0, 0, 0, 1, 1, 30),
(@PH_ITEM_CRATE,     0,    'Supply Crate Kit',     0, 179977, 0, 1.00, 0.9, 1.2, 0.0, 0, 0, 1, 1, 40),
(@PH_ITEM_LANTERN,   0,    'Hanging Lantern Kit',  0, 193684, 0, 1.00, 0.6, 1.0, 0.0, 0, 0, 1, 1, 50),
(@PH_ITEM_CHAIR,  1001,    'Cozy Chair Kit',       0, 180047, 0, 1.00, 0.9, 1.4, 0.0, 1, 0, 1, 1, 60),
(@PH_ITEM_TABLE,  1002,    'Cozy Table Kit',       0, 180885, 0, 1.00, 1.3, 1.8, 0.0, 1, 0, 1, 1, 70);

INSERT INTO `creature_template`
(`entry`, `name`, `subname`, `gossip_menu_id`, `minlevel`, `maxlevel`, `faction`, `npcflag`, `unit_class`, `type`, `AIName`, `MovementType`, `RegenHealth`, `ScriptName`, `VerifiedBuild`)
SELECT @PH_STEWARD_ENTRY, 'Housing Steward', 'Krook''s Cranny', 0, 80, 80, 35, 129, 1, 7, '', 0, 1, 'npc_playerhousing_steward', 0
WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = @PH_STEWARD_ENTRY);

UPDATE `creature_template`
SET
  `subname` = 'Krook''s Cranny',
  `npcflag` = (`npcflag` | 129),
  `ScriptName` = 'npc_playerhousing_steward'
WHERE `entry` = @PH_STEWARD_ENTRY;

DELETE FROM `creature_template_model`
WHERE `CreatureID` = @PH_STEWARD_ENTRY
  AND `Idx` <> 0;

REPLACE INTO `creature_template_model`
(`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`)
VALUES (@PH_STEWARD_ENTRY, 0, 25384, 1.0, 1.0, 0);

INSERT INTO `item_template`
(`entry`, `class`, `subclass`, `SoundOverrideSubclass`, `name`, `displayid`, `Quality`, `Flags`, `FlagsExtra`, `BuyCount`, `BuyPrice`, `SellPrice`, `InventoryType`, `AllowableClass`, `AllowableRace`, `ItemLevel`, `RequiredLevel`, `maxcount`, `stackable`, `bonding`, `spellid_1`, `spelltrigger_1`, `spellcharges_1`, `spellcooldown_1`, `spellcategorycooldown_1`, `description`, `ScriptName`, `VerifiedBuild`) VALUES
(@PH_ITEM_TENT,      15, 0, -1, 'Housing: Canvas Tent Kit',     @PH_FURNITURE_ITEM_DISPLAY, 1, 0, 0, 1, 50000, 12500, 0, -1, -1, 1, 1, 0, 20, 1, 1543, 0, 0, -1, -1, 'Use: Select a location in your house to place this furniture.', 'item_playerhousing_furniture', 0),
(@PH_ITEM_CAMPFIRE,  15, 0, -1, 'Housing: Campfire Kit',        @PH_FURNITURE_ITEM_DISPLAY, 1, 0, 0, 1, 15000,  3750, 0, -1, -1, 1, 1, 0, 20, 1, 1543, 0, 0, -1, -1, 'Use: Select a location in your house to place this furniture.', 'item_playerhousing_furniture', 0),
(@PH_ITEM_BEDROLL,   15, 0, -1, 'Housing: Bedroll Kit',         @PH_FURNITURE_ITEM_DISPLAY, 1, 0, 0, 1, 20000,  5000, 0, -1, -1, 1, 1, 0, 20, 1, 1543, 0, 0, -1, -1, 'Use: Select a location in your house to place this furniture.', 'item_playerhousing_furniture', 0),
(@PH_ITEM_CRATE,     15, 0, -1, 'Housing: Supply Crate Kit',    @PH_FURNITURE_ITEM_DISPLAY, 1, 0, 0, 1, 12000,  3000, 0, -1, -1, 1, 1, 0, 20, 1, 1543, 0, 0, -1, -1, 'Use: Select a location in your house to place this furniture.', 'item_playerhousing_furniture', 0),
(@PH_ITEM_LANTERN,   15, 0, -1, 'Housing: Hanging Lantern Kit', @PH_FURNITURE_ITEM_DISPLAY, 1, 0, 0, 1, 18000,  4500, 0, -1, -1, 1, 1, 0, 20, 1, 1543, 0, 0, -1, -1, 'Use: Select a location in your house to place this furniture.', 'item_playerhousing_furniture', 0),
(@PH_ITEM_CHAIR,     15, 0, -1, 'Housing: Cozy Chair Kit',      @PH_FURNITURE_ITEM_DISPLAY, 2, 0, 0, 1, 30000,  7500, 0, -1, -1, 1, 1, 0, 20, 1, 1543, 0, 0, -1, -1, 'Use: Select a location in your house to place this furniture.', 'item_playerhousing_furniture', 0),
(@PH_ITEM_TABLE,     15, 0, -1, 'Housing: Cozy Table Kit',      @PH_FURNITURE_ITEM_DISPLAY, 2, 0, 0, 1, 45000, 11250, 0, -1, -1, 1, 1, 0, 20, 1, 1543, 0, 0, -1, -1, 'Use: Select a location in your house to place this furniture.', 'item_playerhousing_furniture', 0)
ON DUPLICATE KEY UPDATE
  `name` = VALUES(`name`),
  `displayid` = VALUES(`displayid`),
  `BuyPrice` = VALUES(`BuyPrice`),
  `SellPrice` = VALUES(`SellPrice`),
  `spellid_1` = VALUES(`spellid_1`),
  `spelltrigger_1` = VALUES(`spelltrigger_1`),
  `spellcharges_1` = VALUES(`spellcharges_1`),
  `spellcooldown_1` = VALUES(`spellcooldown_1`),
  `spellcategorycooldown_1` = VALUES(`spellcategorycooldown_1`),
  `description` = VALUES(`description`),
  `ScriptName` = VALUES(`ScriptName`);

REPLACE INTO `npc_vendor`
(`entry`, `slot`, `item`, `maxcount`, `incrtime`, `ExtendedCost`, `VerifiedBuild`) VALUES
(@PH_STEWARD_ENTRY, 0, @PH_ITEM_TENT, 0, 0, 0, 0),
(@PH_STEWARD_ENTRY, 0, @PH_ITEM_CAMPFIRE, 0, 0, 0, 0),
(@PH_STEWARD_ENTRY, 0, @PH_ITEM_BEDROLL, 0, 0, 0, 0),
(@PH_STEWARD_ENTRY, 0, @PH_ITEM_CRATE, 0, 0, 0, 0),
(@PH_STEWARD_ENTRY, 0, @PH_ITEM_LANTERN, 0, 0, 0, 0),
(@PH_STEWARD_ENTRY, 0, @PH_ITEM_CHAIR, 0, 0, 0, 0),
(@PH_STEWARD_ENTRY, 0, @PH_ITEM_TABLE, 0, 0, 0, 0);
