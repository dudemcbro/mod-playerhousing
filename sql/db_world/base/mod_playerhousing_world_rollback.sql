-- Removes everything mod-playerhousing added to the world database.
DELETE FROM `creature` WHERE `id1` IN (900200, 900201, 900202, 900203);
DELETE FROM `creature_template_movement` WHERE `CreatureId` = 900203;
DELETE FROM `creature_template_model` WHERE `CreatureID` IN (900200, 900201, 900202, 900203);
DELETE FROM `creature_template` WHERE `entry` IN (900200, 900201, 900202, 900203);
DELETE FROM `npc_vendor` WHERE `entry` = 900200;
-- The ghosts' models and displays (curated pieces, then the catalog).
DELETE FROM `creaturedisplayinfo_dbc` WHERE `ID` BETWEEN 61100 AND 62999 OR `ID` BETWEEN 100000 AND 104999;
DELETE FROM `creaturemodeldata_dbc` WHERE `ID` BETWEEN 61100 AND 62999 OR `ID` BETWEEN 100000 AND 104999;
DELETE FROM `creature_model_info` WHERE `DisplayID` BETWEEN 61100 AND 62999 OR `DisplayID` BETWEEN 100000 AND 104999;
DELETE FROM `item_template` WHERE `entry` BETWEEN 901100 AND 901199 OR `entry` BETWEEN 902000 AND 902999 OR `entry` BETWEEN 940000 AND 944999;
DELETE FROM `gameobject_template` WHERE `entry` IN (903990, 903991) OR `entry` BETWEEN 911100 AND 922999 OR `entry` BETWEEN 950000 AND 954999;
-- Figurines
DELETE FROM `creature_template_model` WHERE `CreatureID` BETWEEN 931100 AND 932999;
DELETE FROM `creature_template` WHERE `entry` BETWEEN 931100 AND 932999;
DELETE FROM `npc_text` WHERE `ID` BETWEEN 900300 AND 900319;
DELETE FROM `spell_script_names` WHERE `ScriptName` IN ('spell_playerhousing_place', 'spell_playerhousing_key');
DROP TABLE IF EXISTS `mod_playerhousing_piece_rule`;
DROP TABLE IF EXISTS `mod_playerhousing_piece`;
DROP TABLE IF EXISTS `mod_playerhousing_layout`;

DELETE FROM `creature_queststarter` WHERE `quest` BETWEEN 900400 AND 900404;
DELETE FROM `creature_questender` WHERE `quest` BETWEEN 900400 AND 900404;
DELETE FROM `quest_request_items` WHERE `ID` BETWEEN 900400 AND 900404;
DELETE FROM `quest_offer_reward` WHERE `ID` BETWEEN 900400 AND 900404;
DELETE FROM `quest_template_addon` WHERE `ID` BETWEEN 900400 AND 900404;
DELETE FROM `quest_template` WHERE `ID` BETWEEN 900400 AND 900404;
