DROP TABLE IF EXISTS `mod_multi_class_character_talent`;
CREATE TABLE `mod_multi_class_character_talent` (
	`guid` INT(10) UNSIGNED NOT NULL,
	`spell` INT(10) UNSIGNED NOT NULL,
	`class` TINYINT(3) UNSIGNED NOT NULL DEFAULT '0',
	`specMask` TINYINT(3) UNSIGNED NOT NULL DEFAULT '0',
	PRIMARY KEY (`guid`, `spell`) USING BTREE
);

DROP TABLE IF EXISTS `mod_multi_class_character_aura`;
CREATE TABLE `mod_multi_class_character_aura` (
	`guid` INT(10) UNSIGNED NOT NULL DEFAULT '0' COMMENT 'Global Unique Identifier',
	`casterGuid` BIGINT(20) UNSIGNED NOT NULL DEFAULT '0' COMMENT 'Full Global Unique Identifier',
	`itemGuid` BIGINT(20) UNSIGNED NOT NULL DEFAULT '0',
	`spell` INT(10) UNSIGNED NOT NULL DEFAULT '0',
	`effectMask` TINYINT(3) UNSIGNED NOT NULL DEFAULT '0',
	`class` TINYINT(3) UNSIGNED NOT NULL DEFAULT '0',
	`recalculateMask` TINYINT(3) UNSIGNED NOT NULL DEFAULT '0',
	`stackCount` TINYINT(3) UNSIGNED NOT NULL DEFAULT '1',
	`amount0` INT(10) NOT NULL DEFAULT '0',
	`amount1` INT(10) NOT NULL DEFAULT '0',
	`amount2` INT(10) NOT NULL DEFAULT '0',
	`base_amount0` INT(10) NOT NULL DEFAULT '0',
	`base_amount1` INT(10) NOT NULL DEFAULT '0',
	`base_amount2` INT(10) NOT NULL DEFAULT '0',
	`maxDuration` INT(10) NOT NULL DEFAULT '0',
	`remainTime` INT(10) NOT NULL DEFAULT '0',
	`remainCharges` TINYINT(3) UNSIGNED NOT NULL DEFAULT '0',
	PRIMARY KEY (`guid`, `casterGuid`, `itemGuid`, `spell`, `effectMask`) USING BTREE
);

DROP TABLE IF EXISTS `mod_multi_class_character_spell`;
CREATE TABLE `mod_multi_class_character_spell` (
	`guid` INT(10) UNSIGNED NOT NULL DEFAULT '0' COMMENT 'Global Unique Identifier',
	`spell` INT(10) UNSIGNED NOT NULL DEFAULT '0' COMMENT 'Spell Identifier',
	`class` TINYINT(3) UNSIGNED NOT NULL DEFAULT '0',
	`specMask` TINYINT(3) UNSIGNED NOT NULL DEFAULT '1',
	PRIMARY KEY (`guid`, `spell`) USING BTREE
);

DROP TABLE IF EXISTS `mod_multi_class_character_skills`;
CREATE TABLE `mod_multi_class_character_skills` (
	`guid` INT(10) UNSIGNED NOT NULL COMMENT 'Global Unique Identifier',
	`skill` SMALLINT(5) UNSIGNED NOT NULL,
	`value` SMALLINT(5) UNSIGNED NOT NULL,
	`class` TINYINT(3) UNSIGNED NOT NULL DEFAULT '0',
	`max` SMALLINT(5) UNSIGNED NOT NULL,
	PRIMARY KEY (`guid`, `skill`) USING BTREE
);

DROP TABLE IF EXISTS `mod_multi_class_characters`;
CREATE TABLE `mod_multi_class_characters` (
	`guid` INT(10) UNSIGNED NOT NULL DEFAULT '0' COMMENT 'Global Unique Identifier',
	`class` TINYINT(3) UNSIGNED NOT NULL DEFAULT '0',
	`level` TINYINT(3) UNSIGNED NOT NULL DEFAULT '0',
	`xp` INT(10) UNSIGNED NOT NULL DEFAULT '0',
	`restState` TINYINT(3) UNSIGNED NOT NULL DEFAULT '0',
	`leveltime` INT(10) UNSIGNED NOT NULL DEFAULT '0',
	`rest_bonus` FLOAT NOT NULL DEFAULT '0',
	`resettalents_cost` INT(10) UNSIGNED NOT NULL DEFAULT '0',
	`resettalents_time` INT(10) UNSIGNED NOT NULL DEFAULT '0',
	`talentGroupsCount` TINYINT(3) UNSIGNED NOT NULL DEFAULT '1',
	`activeTalentGroup` TINYINT(3) UNSIGNED NOT NULL DEFAULT '0',
	PRIMARY KEY (`guid`) USING BTREE
);

DROP TABLE IF EXISTS `mod_multi_class_next_switch_class`;
CREATE TABLE `mod_multi_class_next_switch_class` (
	`guid` INT(10) UNSIGNED NOT NULL DEFAULT '0' COMMENT 'Global Unique Identifier',
	`nextclass` TINYINT(3) UNSIGNED NOT NULL DEFAULT '0',
	PRIMARY KEY (`guid`) USING BTREE
);