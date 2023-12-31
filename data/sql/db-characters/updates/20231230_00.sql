ALTER TABLE character_action ADD INDEX `idx_guid` (`guid`);
ALTER TABLE character_glyphs ADD INDEX `idx_guid` (`guid`);
ALTER TABLE character_aura ADD INDEX `idx_guid` (`guid`);
ALTER TABLE character_spell ADD INDEX `idx_guid` (`guid`);
ALTER TABLE character_skills ADD INDEX `idx_guid` (`guid`);
ALTER TABLE character_talent ADD INDEX `idx_guid` (`guid`);