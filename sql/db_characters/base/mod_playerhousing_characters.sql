-- mod-playerhousing: character data. Re-applying is safe; for databases created by older
-- versions, mod_playerhousing_characters_hotfix.sql adds the columns they lack.

-- One island per character. is_private holds the privacy level: 0 public, 1 private,
-- 2 friends and guild. style_id and stage are unused since house levels were removed.
CREATE TABLE IF NOT EXISTS `mod_playerhousing_house` (
  `owner_guid` int unsigned NOT NULL,
  `style_id` tinyint unsigned NOT NULL DEFAULT 1,
  `stage` tinyint unsigned NOT NULL DEFAULT 0,
  `is_private` tinyint unsigned NOT NULL DEFAULT 1,
  `flags` int unsigned NOT NULL DEFAULT 0,
  `greeting` varchar(255) NOT NULL DEFAULT '',
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`owner_guid`),
  KEY `idx_mod_playerhousing_house_style` (`style_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Guest lists.
CREATE TABLE IF NOT EXISTS `mod_playerhousing_acl` (
  `owner_guid` int unsigned NOT NULL,
  `guest_guid` int unsigned NOT NULL,
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`owner_guid`,`guest_guid`),
  KEY `idx_mod_playerhousing_acl_guest` (`guest_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Old catalog unlocks. The worldserver converts them into Collection unlocks at startup.
CREATE TABLE IF NOT EXISTS `mod_playerhousing_unlock` (
  `owner_guid` int unsigned NOT NULL,
  `catalog_id` int unsigned NOT NULL,
  `unlocked_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`owner_guid`,`catalog_id`),
  KEY `idx_mod_playerhousing_unlock_catalog` (`catalog_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Placed pieces. source_item_entry is the piece (its item); the spawn_* columns are unused
-- leftovers of the old catalog.
CREATE TABLE IF NOT EXISTS `mod_playerhousing_placement` (
  `owner_guid` int unsigned NOT NULL,
  `placement_id` int unsigned NOT NULL,
  `catalog_id` int unsigned NOT NULL DEFAULT 0,  -- old catalog entry, for converting old placements
  `source_item_entry` int unsigned NOT NULL DEFAULT 0,
  `map_id` int unsigned NOT NULL DEFAULT 0,
  `scale` float NOT NULL DEFAULT 1,
  `pos_x` float NOT NULL,
  `pos_y` float NOT NULL,
  `pos_z` float NOT NULL,
  `orientation` float NOT NULL,
  `look` int unsigned NOT NULL DEFAULT 0,
  `parent_id` int unsigned NOT NULL DEFAULT 0,  -- the surface it stands on
  `pitch` float NOT NULL DEFAULT 0,             -- tilt, radians
  `roll` float NOT NULL DEFAULT 0,
  PRIMARY KEY (`owner_guid`,`placement_id`),
  KEY `idx_mod_playerhousing_placement_catalog` (`catalog_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Gear on stands (mannequins). The items stay in item_instance, out of the owner's
-- inventory, the way mail keeps its items; taking them off puts them back in the bags.
CREATE TABLE IF NOT EXISTS `mod_playerhousing_placement_gear` (
  `owner_guid` int unsigned NOT NULL,
  `placement_id` int unsigned NOT NULL,
  `slot` tinyint unsigned NOT NULL,
  `item_guid` int unsigned NOT NULL,
  `item_entry` int unsigned NOT NULL,
  PRIMARY KEY (`owner_guid`, `placement_id`, `slot`),
  UNIQUE KEY `idx_mod_playerhousing_gear_item` (`item_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- House Storage: pieces that came back while the owner's bags were full.
CREATE TABLE IF NOT EXISTS `mod_playerhousing_storage` (
  `owner_guid` int unsigned NOT NULL,
  `item_entry` int unsigned NOT NULL,
  `count` int unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`owner_guid`,`item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- The Collection: what an account (guid 0) or a single character has unlocked.
CREATE TABLE IF NOT EXISTS `mod_playerhousing_collection` (
  `account_id` int unsigned NOT NULL,
  `guid` int unsigned NOT NULL DEFAULT 0,
  `item_entry` int unsigned NOT NULL,
  `unlocked_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `seen` tinyint unsigned NOT NULL DEFAULT 0,  -- 0: shown as new in the Collection
  PRIMARY KEY (`account_id`,`guid`,`item_entry`),
  KEY `idx_mod_playerhousing_collection_guid` (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Per character: flags (1 key given, 2 past progress credited, 4 greeted by Krook) and
-- one-time tips already shown.
CREATE TABLE IF NOT EXISTS `mod_playerhousing_character` (
  `guid` int unsigned NOT NULL,
  `flags` int unsigned NOT NULL DEFAULT 0,
  `tips` int unsigned NOT NULL DEFAULT 0,
  `grid` tinyint unsigned NOT NULL DEFAULT 0,   -- grid snapping, in quarter yards (0: off)
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- One-off conversions already done.
CREATE TABLE IF NOT EXISTS `mod_playerhousing_meta` (
  `meta_key` varchar(32) NOT NULL,
  `meta_value` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`meta_key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
