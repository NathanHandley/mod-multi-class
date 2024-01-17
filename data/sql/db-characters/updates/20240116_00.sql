ALTER TABLE character_pet ADD COLUMN `multi_class_owner` INT(10) UNSIGNED NOT NULL DEFAULT '0';
ALTER TABLE character_pet ADD COLUMN `multi_class_class` TINYINT(3) UNSIGNED NOT NULL DEFAULT '0';
ALTER TABLE character_pet ADD INDEX `idx_mcowner` (`multi_class_owner`);
ALTER TABLE character_pet ADD INDEX `idx_mcclass` (`multi_class_class`);