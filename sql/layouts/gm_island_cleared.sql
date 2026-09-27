-- Optional layout: GM Island with the guild house removed (see tools/gm-island-cleared).
-- Only use it together with the matching server data (server_data.sh) and client patch;
-- otherwise the campsite sits inside the house walls.
-- Switch back to the guild house by re-applying db_world/base/mod_playerhousing_world_hotfix.sql.
--
-- Apply to the world database after the base/hotfix SQL:
--   mysql acore_world < sql/layouts/gm_island_cleared.sql

-- The campsite stands on the flat ground where the house was, facing north up the island.
UPDATE `mod_playerhousing_style`
SET
  `map_id` = 1,
  `spawn_x` = 16240.0,
  `spawn_y` = 16296.0,
  `spawn_z` = 12.92,
  `spawn_o` = 1.5708,
  `steward_offset_x` = 7.0,
  `steward_offset_y` = 2.0
WHERE `style_id` IN (1, 2, 3, 4);

-- Stage 0 campsite: tent, campfire 9 yd ahead, lantern, bedroll and crate by the tent.
DELETE FROM `mod_playerhousing_style_object` WHERE `style_id` IN (1, 2, 3, 4);
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
