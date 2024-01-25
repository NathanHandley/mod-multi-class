DROP TABLE IF EXISTS `mod_multi_class_character_reputation`;
CREATE TABLE `mod_multi_class_character_reputation` (
	`guid` INT(10) UNSIGNED NOT NULL DEFAULT '0',
   `class` TINYINT(3) UNSIGNED NOT NULL DEFAULT '0',
	`faction` SMALLINT(5) UNSIGNED NOT NULL DEFAULT '0',
	`standing` INT(10) NOT NULL DEFAULT '0',
	`flags` SMALLINT(5) UNSIGNED NOT NULL DEFAULT '0',
	PRIMARY KEY (`guid`, `class`, `faction`) USING BTREE,
	INDEX `idx_guidclass` (`guid`, `class`) USING BTREE,
	INDEX `idx_guid` (`guid`) USING BTREE
);

ALTER TABLE mod_multi_class_character_class_settings ADD COLUMN `useSharedReputation` TINYINT(3) UNSIGNED NOT NULL DEFAULT '0';
ALTER TABLE mod_multi_class_character_controller ADD COLUMN `activeClassReputation` TINYINT(3) UNSIGNED NOT NULL DEFAULT '0';