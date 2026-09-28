-- Brings databases created by older versions up to date. Safe to re-apply.

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
    'ALTER TABLE `mod_playerhousing_placement` ADD COLUMN `scale` float NOT NULL DEFAULT 1 AFTER `map_id`',
    'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'mod_playerhousing_placement' AND column_name = 'scale'
);
PREPARE ph_stmt FROM @ph_stmt;
EXECUTE ph_stmt;
DEALLOCATE PREPARE ph_stmt;

SET @ph_stmt = (
  SELECT IF(COUNT(*) = 0,
    'ALTER TABLE `mod_playerhousing_house` ADD COLUMN `flags` int unsigned NOT NULL DEFAULT 0 AFTER `is_private`',
    'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'mod_playerhousing_house' AND column_name = 'flags'
);
PREPARE ph_stmt FROM @ph_stmt;
EXECUTE ph_stmt;
DEALLOCATE PREPARE ph_stmt;

SET @ph_stmt = (
  SELECT IF(COUNT(*) = 0,
    'ALTER TABLE `mod_playerhousing_house` ADD COLUMN `greeting` varchar(255) NOT NULL DEFAULT \'\' AFTER `flags`',
    'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'mod_playerhousing_house' AND column_name = 'greeting'
);
PREPARE ph_stmt FROM @ph_stmt;
EXECUTE ph_stmt;
DEALLOCATE PREPARE ph_stmt;

SET @ph_stmt = (
  SELECT IF(COUNT(*) = 0,
    'ALTER TABLE `mod_playerhousing_placement` ADD COLUMN `look` int unsigned NOT NULL DEFAULT 0 AFTER `orientation`',
    'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'mod_playerhousing_placement' AND column_name = 'look'
);
PREPARE ph_stmt FROM @ph_stmt;
EXECUTE ph_stmt;
DEALLOCATE PREPARE ph_stmt;

-- Columns from the house levels version that nothing uses any more.

SET @ph_stmt = (
  SELECT IF(COUNT(*) = 1,
    'ALTER TABLE `mod_playerhousing_placement` DROP COLUMN `spawn_type`',
    'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'mod_playerhousing_placement' AND column_name = 'spawn_type'
);
PREPARE ph_stmt FROM @ph_stmt;
EXECUTE ph_stmt;
DEALLOCATE PREPARE ph_stmt;

SET @ph_stmt = (
  SELECT IF(COUNT(*) = 1,
    'ALTER TABLE `mod_playerhousing_placement` DROP COLUMN `spawn_entry`',
    'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'mod_playerhousing_placement' AND column_name = 'spawn_entry'
);
PREPARE ph_stmt FROM @ph_stmt;
EXECUTE ph_stmt;
DEALLOCATE PREPARE ph_stmt;

SET @ph_stmt = (
  SELECT IF(COUNT(*) = 1,
    'ALTER TABLE `mod_playerhousing_placement` DROP COLUMN `display_id`',
    'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'mod_playerhousing_placement' AND column_name = 'display_id'
);
PREPARE ph_stmt FROM @ph_stmt;
EXECUTE ph_stmt;
DEALLOCATE PREPARE ph_stmt;

SET @ph_stmt = (
  SELECT IF(COUNT(*) = 1,
    'ALTER TABLE `mod_playerhousing_placement` DROP COLUMN `collision_radius`',
    'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'mod_playerhousing_placement' AND column_name = 'collision_radius'
);
PREPARE ph_stmt FROM @ph_stmt;
EXECUTE ph_stmt;
DEALLOCATE PREPARE ph_stmt;

SET @ph_stmt = (
  SELECT IF(COUNT(*) = 1,
    'ALTER TABLE `mod_playerhousing_placement` DROP COLUMN `min_distance`',
    'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'mod_playerhousing_placement' AND column_name = 'min_distance'
);
PREPARE ph_stmt FROM @ph_stmt;
EXECUTE ph_stmt;
DEALLOCATE PREPARE ph_stmt;

SET @ph_stmt = (
  SELECT IF(COUNT(*) = 1,
    'ALTER TABLE `mod_playerhousing_placement` DROP COLUMN `placed_at`',
    'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'mod_playerhousing_placement' AND column_name = 'placed_at'
);
PREPARE ph_stmt FROM @ph_stmt;
EXECUTE ph_stmt;
DEALLOCATE PREPARE ph_stmt;

SET @ph_stmt = (
  SELECT IF(COUNT(*) = 1,
    'ALTER TABLE `mod_playerhousing_placement` DROP COLUMN `updated_at`',
    'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'mod_playerhousing_placement' AND column_name = 'updated_at'
);
PREPARE ph_stmt FROM @ph_stmt;
EXECUTE ph_stmt;
DEALLOCATE PREPARE ph_stmt;
