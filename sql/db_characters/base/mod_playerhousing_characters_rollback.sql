-- Removes everything mod-playerhousing added to the characters database. Run it with the
-- worldserver stopped: the mail below takes the next free mail ids, which a running server
-- also hands out.
DROP TABLE IF EXISTS `mod_playerhousing_meta`;
DROP TABLE IF EXISTS `mod_playerhousing_guestbook`;
DROP TABLE IF EXISTS `mod_playerhousing_set_piece`;
DROP TABLE IF EXISTS `mod_playerhousing_set`;
DROP TABLE IF EXISTS `mod_playerhousing_saved_piece`;
DROP TABLE IF EXISTS `mod_playerhousing_report`;
DROP TABLE IF EXISTS `mod_playerhousing_like`;
DROP TABLE IF EXISTS `mod_playerhousing_visit_log`;
DROP TABLE IF EXISTS `mod_playerhousing_saved_layout`;
DROP TABLE IF EXISTS `mod_playerhousing_character`;
DROP TABLE IF EXISTS `mod_playerhousing_collection`;
DROP TABLE IF EXISTS `mod_playerhousing_storage`;
-- Gear still on stands goes back to its owners by mail (one letter per item, from Krook), so
-- nothing is lost. Its owner is the item's own owner_guid: a roommate's gear on someone
-- else's island goes to the roommate, not the island's owner. The table is created first in
-- case this install never had stands.
CREATE TABLE IF NOT EXISTS `mod_playerhousing_placement_gear` (
  `owner_guid` int unsigned NOT NULL,
  `placement_id` int unsigned NOT NULL,
  `slot` tinyint unsigned NOT NULL,
  `item_guid` int unsigned NOT NULL,
  `item_entry` int unsigned NOT NULL,
  PRIMARY KEY (`owner_guid`, `placement_id`, `slot`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
SET @ph_mail := (SELECT COALESCE(MAX(`id`), 0) FROM `mail`);
INSERT INTO `mail` (`id`, `messageType`, `stationery`, `mailTemplateId`, `sender`, `receiver`, `subject`, `body`, `has_items`, `expire_time`, `deliver_time`, `money`, `cod`, `checked`)
SELECT @ph_mail + ROW_NUMBER() OVER (ORDER BY g.`item_guid`), 3, 41, 0, 900200, ii.`owner_guid`, 'Gear from your mannequin', '', 1,
       UNIX_TIMESTAMP() + 30 * 86400, UNIX_TIMESTAMP(), 0, 0, 0
FROM `mod_playerhousing_placement_gear` g JOIN `item_instance` ii ON ii.`guid` = g.`item_guid`;
INSERT INTO `mail_items` (`mail_id`, `item_guid`, `receiver`)
SELECT @ph_mail + ROW_NUMBER() OVER (ORDER BY g.`item_guid`), g.`item_guid`, ii.`owner_guid`
FROM `mod_playerhousing_placement_gear` g JOIN `item_instance` ii ON ii.`guid` = g.`item_guid`;
DROP TABLE IF EXISTS `mod_playerhousing_placement_gear`;
DROP TABLE IF EXISTS `mod_playerhousing_placement`;
DROP TABLE IF EXISTS `mod_playerhousing_unlock`;
DROP TABLE IF EXISTS `mod_playerhousing_acl`;
DROP TABLE IF EXISTS `mod_playerhousing_house`;
