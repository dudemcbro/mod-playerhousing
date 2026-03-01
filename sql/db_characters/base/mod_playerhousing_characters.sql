CREATE TABLE IF NOT EXISTS `mod_playerhousing_house` (
  `owner_guid` int unsigned NOT NULL,
  `style_id` tinyint unsigned NOT NULL DEFAULT 1,
  `stage` tinyint unsigned NOT NULL DEFAULT 0,
  `is_private` tinyint unsigned NOT NULL DEFAULT 1,
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`owner_guid`),
  KEY `idx_mod_playerhousing_house_style` (`style_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `mod_playerhousing_acl` (
  `owner_guid` int unsigned NOT NULL,
  `guest_guid` int unsigned NOT NULL,
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`owner_guid`,`guest_guid`),
  KEY `idx_mod_playerhousing_acl_guest` (`guest_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `mod_playerhousing_unlock` (
  `owner_guid` int unsigned NOT NULL,
  `catalog_id` int unsigned NOT NULL,
  `unlocked_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`owner_guid`,`catalog_id`),
  KEY `idx_mod_playerhousing_unlock_catalog` (`catalog_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `mod_playerhousing_placement` (
  `owner_guid` int unsigned NOT NULL,
  `placement_id` int unsigned NOT NULL,
  `catalog_id` int unsigned NOT NULL DEFAULT 0,
  `source_item_entry` int unsigned NOT NULL DEFAULT 0,
  `map_id` int unsigned NOT NULL DEFAULT 0,
  `spawn_type` tinyint unsigned NOT NULL DEFAULT 0,
  `spawn_entry` int unsigned NOT NULL DEFAULT 0,
  `display_id` int unsigned NOT NULL DEFAULT 0,
  `scale` float NOT NULL DEFAULT 1,
  `collision_radius` float NOT NULL DEFAULT 1,
  `min_distance` float NOT NULL DEFAULT 1.5,
  `pos_x` float NOT NULL,
  `pos_y` float NOT NULL,
  `pos_z` float NOT NULL,
  `orientation` float NOT NULL,
  `placed_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`owner_guid`,`placement_id`),
  KEY `idx_mod_playerhousing_placement_catalog` (`catalog_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

SET @ph_stmt = (
  SELECT IF(COUNT(*) = 0,
    'ALTER TABLE `mod_playerhousing_placement` ADD COLUMN `source_item_entry` int unsigned NOT NULL DEFAULT 0 AFTER `catalog_id`',
    'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'mod_playerhousing_placement' AND column_name = 'source_item_entry'
);
PREPARE ph_stmt FROM @ph_stmt;
EXECUTE ph_stmt;
DEALLOCATE PREPARE ph_stmt;

SET @ph_stmt = (
  SELECT IF(COUNT(*) = 0,
    'ALTER TABLE `mod_playerhousing_placement` ADD COLUMN `map_id` int unsigned NOT NULL DEFAULT 0 AFTER `source_item_entry`',
    'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'mod_playerhousing_placement' AND column_name = 'map_id'
);
PREPARE ph_stmt FROM @ph_stmt;
EXECUTE ph_stmt;
DEALLOCATE PREPARE ph_stmt;

SET @ph_stmt = (
  SELECT IF(COUNT(*) = 0,
    'ALTER TABLE `mod_playerhousing_placement` ADD COLUMN `spawn_type` tinyint unsigned NOT NULL DEFAULT 0 AFTER `map_id`',
    'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'mod_playerhousing_placement' AND column_name = 'spawn_type'
);
PREPARE ph_stmt FROM @ph_stmt;
EXECUTE ph_stmt;
DEALLOCATE PREPARE ph_stmt;

SET @ph_stmt = (
  SELECT IF(COUNT(*) = 0,
    'ALTER TABLE `mod_playerhousing_placement` ADD COLUMN `spawn_entry` int unsigned NOT NULL DEFAULT 0 AFTER `spawn_type`',
    'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'mod_playerhousing_placement' AND column_name = 'spawn_entry'
);
PREPARE ph_stmt FROM @ph_stmt;
EXECUTE ph_stmt;
DEALLOCATE PREPARE ph_stmt;

SET @ph_stmt = (
  SELECT IF(COUNT(*) = 0,
    'ALTER TABLE `mod_playerhousing_placement` ADD COLUMN `display_id` int unsigned NOT NULL DEFAULT 0 AFTER `spawn_entry`',
    'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'mod_playerhousing_placement' AND column_name = 'display_id'
);
PREPARE ph_stmt FROM @ph_stmt;
EXECUTE ph_stmt;
DEALLOCATE PREPARE ph_stmt;

SET @ph_stmt = (
  SELECT IF(COUNT(*) = 0,
    'ALTER TABLE `mod_playerhousing_placement` ADD COLUMN `scale` float NOT NULL DEFAULT 1 AFTER `display_id`',
    'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'mod_playerhousing_placement' AND column_name = 'scale'
);
PREPARE ph_stmt FROM @ph_stmt;
EXECUTE ph_stmt;
DEALLOCATE PREPARE ph_stmt;

SET @ph_stmt = (
  SELECT IF(COUNT(*) = 0,
    'ALTER TABLE `mod_playerhousing_placement` ADD COLUMN `collision_radius` float NOT NULL DEFAULT 1 AFTER `scale`',
    'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'mod_playerhousing_placement' AND column_name = 'collision_radius'
);
PREPARE ph_stmt FROM @ph_stmt;
EXECUTE ph_stmt;
DEALLOCATE PREPARE ph_stmt;

SET @ph_stmt = (
  SELECT IF(COUNT(*) = 0,
    'ALTER TABLE `mod_playerhousing_placement` ADD COLUMN `min_distance` float NOT NULL DEFAULT 1.5 AFTER `collision_radius`',
    'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'mod_playerhousing_placement' AND column_name = 'min_distance'
);
PREPARE ph_stmt FROM @ph_stmt;
EXECUTE ph_stmt;
DEALLOCATE PREPARE ph_stmt;
