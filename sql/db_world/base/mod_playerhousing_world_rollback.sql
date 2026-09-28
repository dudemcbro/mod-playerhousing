-- Removes everything mod-playerhousing added to the world database.
DELETE FROM `creature` WHERE `id1` = 900200;
DELETE FROM `creature_template_model` WHERE `CreatureID` = 900200;
DELETE FROM `creature_template` WHERE `entry` = 900200;
DELETE FROM `npc_vendor` WHERE `entry` = 900200;
DELETE FROM `item_template` WHERE `entry` BETWEEN 901100 AND 901199 OR `entry` BETWEEN 902000 AND 902999;
DELETE FROM `gameobject_template` WHERE `entry` = 903990 OR `entry` BETWEEN 911100 AND 922999;
DELETE FROM `npc_text` WHERE `ID` BETWEEN 900300 AND 900309;
DELETE FROM `spell_script_names` WHERE `ScriptName` IN ('spell_playerhousing_place', 'spell_playerhousing_key');
DROP TABLE IF EXISTS `mod_playerhousing_piece_rule`;
DROP TABLE IF EXISTS `mod_playerhousing_piece`;
DROP TABLE IF EXISTS `mod_playerhousing_layout`;
