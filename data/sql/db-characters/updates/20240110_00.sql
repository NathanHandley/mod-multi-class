DROP TABLE IF EXISTS `mod_multi_class_character_controller`;
CREATE TABLE `mod_multi_class_character_controller` (
	`guid` INT(10) UNSIGNED NOT NULL DEFAULT '0',
	`nextClass` TINYINT(3) UNSIGNED NOT NULL DEFAULT '0',
	`activeClassQuests` TINYINT(3) UNSIGNED NOT NULL DEFAULT '0',
	PRIMARY KEY (`guid`) USING BTREE
);

DROP TABLE IF EXISTS `mod_multi_class_character_class_settings`;
CREATE TABLE `mod_multi_class_character_class_settings` (
	`guid` INT(10) UNSIGNED NOT NULL DEFAULT '0',
   `class` TINYINT(3) UNSIGNED NOT NULL DEFAULT '0',
	`useSharedQuests` TINYINT(3) UNSIGNED NOT NULL DEFAULT '1',	
	PRIMARY KEY (`guid`, `class`) USING BTREE,
	INDEX `idx_guid` (`guid`) USING BTREE
);
