
/*
** Made by Nathan Handley https://github.com/NathanHandley
** AzerothCore 2019 http://www.azerothcore.org/
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU Affero General Public License as published by the
* Free Software Foundation; either version 3 of the License, or (at your
* option) any later version.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
* more details.
*
* You should have received a copy of the GNU General Public License along
* with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "Chat.h"
#include "Configuration/Config.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "World.h"

#include "MultiClass.h"

#include <set>
#include <list>

using namespace Acore::ChatCommands;
using namespace std;

static bool ConfigEnabled = true;
static uint8 ConfigCrossClassAbilityLevelGap = 10; // TODO: Load from config
static uint32 ConfigMaxSkillIDCheck = 1000;         // The highest level of skill ID it will look for when doing copies
static set<uint32> ConfigCrossClassIncludeSkillIDs;

MultiClassMod::MultiClassMod()
{

}

MultiClassMod::~MultiClassMod()
{

}

bool MultiClassMod::DoesSavedClassDataExistForPlayer(Player* player, uint8 lookupClass)
{
    QueryResult queryResult = CharacterDatabase.Query("SELECT guid, class FROM mod_multi_class_characters WHERE guid = {} AND class = {}", player->GetGUID().GetCounter(), lookupClass);
    if (!queryResult || queryResult->GetRowCount() == 0)
        return false;
    return true;
}

bool MultiClassMod::IsValidRaceClassCombo(uint8 lookupClass, uint8 lookupRace)
{
    PlayerInfo const* info = sObjectMgr->GetPlayerInfo(lookupRace, lookupClass);
    if (!info)
        return false;
    else
        return true;
}

void MultiClassMod::QueueClassSwitch(Player* player, uint8 nextClass)
{
    bool isNewClass = !DoesSavedClassDataExistForPlayer(player, nextClass);
    CharacterDatabase.DirectExecute("INSERT INTO `mod_multi_class_next_switch_class` (guid, nextclass, isnew) VALUES ({}, {}, {})", player->GetGUID().GetCounter(), nextClass, isNewClass);
}

QueuedClassSwitch MultiClassMod::GetQueuedClassSwitch(Player* player)
{
    QueuedClassSwitch queuedClassSwitch;
    QueryResult queryResult = CharacterDatabase.Query("SELECT nextclass, IsNew FROM `mod_multi_class_next_switch_class` WHERE guid = {}", player->GetGUID().GetCounter());
    if (!queryResult || queryResult->GetRowCount() == 0)
    {
        queuedClassSwitch.classID = CLASS_NONE;
        queuedClassSwitch.isNew = false;
    }
    else
    {
        Field* fields = queryResult->Fetch();
        queuedClassSwitch.classID = fields[0].Get<uint8>();
        queuedClassSwitch.isNew = fields[1].Get<bool>();
    }
    return queuedClassSwitch;
}

void MultiClassMod::DeleteQueuedClassSwitch(Player* player)
{
    CharacterDatabase.DirectExecute("DELETE FROM `mod_multi_class_next_switch_class` WHERE guid = {}", player->GetGUID().GetCounter());
}

void MultiClassMod::CopyCharacterDataIntoModCharacterTable(Player* player, CharacterDatabaseTransaction& transaction)
{
    transaction->Append("DELETE FROM `mod_multi_class_characters` WHERE guid = {} and class = {}", player->GetGUID().GetCounter(), player->getClass());
    QueryResult queryResult = CharacterDatabase.Query("SELECT leveltime, rest_bonus, resettalents_cost, resettalents_time FROM characters WHERE guid = {}", player->GetGUID().GetCounter());
    if (!queryResult)
    {
        LOG_ERROR("module", "multiclass: Error pulling character data for guid {}", player->GetGUID().GetCounter());
    }
    else
    {
        Field* fields = queryResult->Fetch();
        uint32 spellID = fields[0].Get<uint32>();
        auto finiteAlways = [](float f) { return std::isfinite(f) ? f : 0.0f; };

        transaction->Append("INSERT INTO mod_multi_class_characters (guid, class, `level`, xp, leveltime, rest_bonus, resettalents_cost, resettalents_time, health, power1, power2, power3, power4, power5, power6, power7, talentGroupsCount, activeTalentGroup) VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
            player->GetGUID().GetCounter(),
            player->getClass(),
            player->getLevel(),
            player->GetUInt32Value(PLAYER_XP),
            fields[0].Get<uint32>(),                // leveltime
            finiteAlways(fields[1].Get<float>()),   // rest_bonus
            fields[2].Get<uint32>(),                //resettalents_cost - m_resetTalentsCost,
            fields[3].Get<uint32>(),                //resettalents_time - uint32(m_resetTalentsTime),
            player->GetHealth(),
            player->GetPower(Powers(0)),
            player->GetPower(Powers(1)),
            player->GetPower(Powers(2)),
            player->GetPower(Powers(3)),
            player->GetPower(Powers(4)),
            player->GetPower(Powers(5)),
            player->GetPower(Powers(6)),
            player->GetSpecsCount(),
            player->GetActiveSpec()
        );
    }
}

void MultiClassMod::MoveTalentsToModTalentsTable(Player* player, CharacterDatabaseTransaction& transaction)
{
    transaction->Append("DELETE FROM `mod_multi_class_character_talent` WHERE guid = {} and class = {}", player->GetGUID().GetCounter(), player->getClass());

    for (auto& curTalent : player->GetTalentMap())
    {
        transaction->Append("INSERT INTO mod_multi_class_character_talent (guid, class, spell, specMask) VALUES ({}, {}, {}, {})",
            player->GetGUID().GetCounter(),
            player->getClass(),
            curTalent.second->talentID,
            (uint32)curTalent.second->specMask);
    }

    // Delete the previous talents in the character table
    transaction->Append("DELETE FROM `character_talent` WHERE guid = {}", player->GetGUID().GetCounter());
}
void MultiClassMod::MoveClassSpellsToModSpellsTable(Player* player, CharacterDatabaseTransaction& transaction)
{
    // Purge old spell list in mod table
    transaction->Append("DELETE FROM `mod_multi_class_character_spell` WHERE guid = {} and class = {}", player->GetGUID().GetCounter(), player->getClass());

    // Move class spells from the character table into the mod table
    for (auto& curSpell : player->GetSpellMap())
    {
        // Skip non-class spells
        if (ClassSpellsBySpellID.find(curSpell.first) == ClassSpellsBySpellID.end())
            continue;

        // Insert into Mod
        transaction->Append("INSERT INTO mod_multi_class_character_spell (guid, class, spell, specMask) VALUES ({}, {}, {}, {})",
            player->GetGUID().GetCounter(),
            player->getClass(),
            curSpell.first,
            (uint32)(curSpell.second->specMask));

        // Delete from character
        transaction->Append("DELETE FROM character_spell WHERE guid = {} and spell = {}",
            player->GetGUID().GetCounter(),
            curSpell.first);
    }
}

void MultiClassMod::MoveClassSkillsToModSkillsTable(Player* player, CharacterDatabaseTransaction& transaction)
{
    // Purge old skill list in mod table
    transaction->Append("DELETE FROM `mod_multi_class_character_skills` WHERE guid = {} and class = {}", player->GetGUID().GetCounter(), player->getClass());

    // Get all of the known player skills
    // TODO: This REALLY needs to be done somehow better
    set<uint32> playerKnownSkills;
    for (uint32 i = 0; i < ConfigMaxSkillIDCheck; ++i)
    {
        if (player->HasSkill(i))
            playerKnownSkills.insert(i);
    }

    // Go through all known skills on this player to move them
    for (uint32 curSkillID : playerKnownSkills)
    {
        // Ignore shared skills
        if (ConfigCrossClassIncludeSkillIDs.find(curSkillID) != ConfigCrossClassIncludeSkillIDs.end())
        {
            continue;
        }

        // Add to the mod table
        transaction->Append("INSERT INTO mod_multi_class_character_skills (guid, class, skill, value, max) VALUES ({}, {}, {}, {}, {})",
            player->GetGUID().GetCounter(),
            player->getClass(),
            curSkillID,
            player->GetPureSkillValue(curSkillID),
            player->GetPureMaxSkillValue(curSkillID));

        // Remove from the character skill table
        transaction->Append("DELETE FROM character_skills WHERE guid = {} AND skill = {}",
            player->GetGUID().GetCounter(),
            curSkillID);
    }
}

void MultiClassMod::ReplaceModClassActionCopy(Player* player, CharacterDatabaseTransaction& transaction)
{
    // Delete the old action entries
    transaction->Append("DELETE FROM `mod_multi_class_character_action` WHERE guid = {} and class = {}", player->GetGUID().GetCounter(), player->getClass());

    // Save current action buttons
    // This is closer to a better approach, but can't be done unless action buttons are made available to the mod
    //QueryResult queryResult = CharacterDatabase.Query("SELECT spec, button, `action`, `type` FROM character_action WHERE guid = {}", player->GetGUID().GetCounter());
    //if (queryResult)
    //{
    //    do
    //    {
    //        // Pull the data out
    //        Field* fields = queryResult->Fetch();
    //        uint8 actionSpec = fields[0].Get<uint8>();
    //        uint8 actionButton = fields[1].Get<uint8>();
    //        uint32 actionAction = fields[2].Get<uint32>();
    //        uint8 actionType = fields[3].Get<uint8>();

    //        transaction->Append("INSERT INTO `mod_multi_class_character_action` (`guid`, class, `spec`, `button`, `action`, `type`) VALUES ({}, {}, {}, {}, {}, {})",
    //            player->GetGUID().GetCounter(),
    //            player->getClass(),
    //            (uint32)actionSpec,
    //            (uint32)actionButton,
    //            actionAction,
    //            (uint32)actionType);

    //    } while (queryResult->NextRow());
    //}
    // Less ideal approach, as it causes a table scan on character_action
    transaction->Append("INSERT INTO mod_multi_class_character_action (guid, class, spec, button, `action`, `type`) SELECT guid, {}, spec, button, `action`, `type` FROM character_action WHERE guid = {}", player->getClass(), player->GetGUID().GetCounter());
}

void MultiClassMod::MoveGlyphsToModGlyhpsTable(Player* player, CharacterDatabaseTransaction& transaction)
{
    transaction->Append("DELETE FROM `mod_multi_class_character_glyphs` WHERE guid = {} and class = {}", player->GetGUID().GetCounter(), player->getClass());
    transaction->Append("INSERT INTO mod_multi_class_character_glyphs (guid, class, talentGroup, glyph1, glyph2, glyph3, glyph4, glyph5, glyph6) SELECT guid, {}, talentGroup, glyph1, glyph2, glyph3, glyph4, glyph5, glyph6 FROM character_glyphs WHERE guid = {}", player->getClass(), player->GetGUID().GetCounter());
    transaction->Append("DELETE FROM `character_glyphs` WHERE guid = {}", player->GetGUID().GetCounter());
}

void MultiClassMod::MoveAuraToModAuraTable(Player* player, CharacterDatabaseTransaction& transaction)
{
    transaction->Append("DELETE FROM `mod_multi_class_character_aura` WHERE guid = {} and class = {}", player->GetGUID().GetCounter(), player->getClass());
    transaction->Append("INSERT INTO mod_multi_class_character_aura (guid, class, casterGuid, itemGuid, spell, effectMask, recalculateMask, stackCount, amount0, amount1, amount2, base_amount0, base_amount1, base_amount2, maxDuration, remainTime, remainCharges) SELECT guid, {}, casterGuid, itemGuid, spell, effectMask, recalculateMask, stackCount, amount0, amount1, amount2, base_amount0, base_amount1, base_amount2, maxDuration, remainTime, remainCharges FROM character_aura WHERE guid = {}", player->getClass(), player->GetGUID().GetCounter());
    transaction->Append("DELETE FROM `character_aura` WHERE guid = {}", player->GetGUID().GetCounter());
}

void MultiClassMod::MoveEquipToModInventoryTable(Player* player, CharacterDatabaseTransaction& transaction)
{
    transaction->Append("DELETE FROM `mod_multi_class_character_inventory` WHERE guid = {} AND class = {} AND `bag` = 0 AND `slot` <= 18;", player->GetGUID().GetCounter(), player->getClass());
    transaction->Append("INSERT INTO `mod_multi_class_character_inventory` (`guid`, `class`, `bag`, `slot`, `item`) SELECT `guid`, {}, `bag`, `slot`, `item` FROM character_inventory WHERE guid = {} AND `bag` = 0 AND `slot` <= 18", player->getClass(), player->GetGUID().GetCounter());
    transaction->Append("DELETE FROM `character_inventory` WHERE guid = {} AND `bag` = 0 AND `slot` <= 18", player->GetGUID().GetCounter());
}

void MultiClassMod::UpdateCharacterFromModCharacterTable(uint32 playerGUID, uint8 pullClassID, CharacterDatabaseTransaction& transaction)
{
    QueryResult queryResult = CharacterDatabase.Query("SELECT `level`, `xp`, `leveltime`, `rest_bonus`, `resettalents_cost`, `resettalents_time`, `health`, `power1`, `power2`, `power3`, `power4`, `power5`, `power6`, `power7`, `talentGroupsCount`, `activeTalentGroup` FROM mod_multi_class_characters WHERE guid = {} AND class = {}", playerGUID, pullClassID);
    if (!queryResult)
    {
        LOG_ERROR("module", "multiclass: Error pulling character data for guid {} class {}", playerGUID, pullClassID);
    }
    else
    {
        Field* fields = queryResult->Fetch();
        uint32 spellID = fields[0].Get<uint32>();
        auto finiteAlways = [](float f) { return std::isfinite(f) ? f : 0.0f; };

        transaction->Append("UPDATE characters SET `class` = {}, `level` = {}, `xp` = {}, `leveltime` = {}, `rest_bonus` = {}, `resettalents_cost` = {}, `resettalents_time` = {}, `health` = {}, `power1` = {}, `power2` = {}, `power3` = {}, `power4` = {}, `power5` = {}, `power6` = {}, `power7` = {}, `talentGroupsCount` = {}, `activeTalentGroup` = {} WHERE `guid` = {}",
            pullClassID,                            // class
            fields[0].Get<uint8>(),                 // level
            fields[1].Get<uint32>(),                // xp
            fields[2].Get<uint32>(),                // leveltime
            finiteAlways(fields[3].Get<float>()),   // rest_bonus
            fields[4].Get<uint32>(),                // resettalents_cost
            fields[5].Get<uint32>(),                // resettalents_time
            fields[6].Get<uint32>(),                // health
            fields[7].Get<uint32>(),                // power1
            fields[8].Get<uint32>(),                // power2
            fields[9].Get<uint32>(),                // power3
            fields[10].Get<uint32>(),               // power4
            fields[11].Get<uint32>(),               // power5
            fields[12].Get<uint32>(),               // power6
            fields[13].Get<uint32>(),               // power7
            fields[14].Get<uint8>(),                // talentGroupsCount
            fields[15].Get<uint8>(),                // activeTalentGroup
            playerGUID
        );
    }
}

void MultiClassMod::CopyModSpellTableIntoCharacterSpells(uint32 playerGUID, uint8 pullClassID, CharacterDatabaseTransaction& transaction)
{
    // Create inserts for all of the coming class spells
    QueryResult queryResult = CharacterDatabase.Query("SELECT spell, specMask FROM mod_multi_class_character_spell WHERE guid = {} and class = {}", playerGUID, (uint32)pullClassID);
    if (!queryResult)
    {
        LOG_ERROR("module", "multiclass: Error pulling class spell data from the mod table for class {} on guid {}, so the class will have no spells...", (uint32)pullClassID, playerGUID);
    }
    else
    {
        do
        {
            // Pull the data out
            Field* fields = queryResult->Fetch();
            uint32 spellID = fields[0].Get<uint32>();
            uint8 specMask = fields[1].Get<uint8>();

            // Only add if it's a valid spell
            if (ClassSpellsBySpellID.find(spellID) != ClassSpellsBySpellID.end())
            {
                transaction->Append("INSERT INTO character_spell (guid, spell, specMask) VALUES ({}, {}, {})",
                    playerGUID,
                    spellID,
                    (uint32)specMask);
            }

        } while (queryResult->NextRow());
    }
}

void MultiClassMod::CopyModActionTableIntoCharacterAction(uint32 playerGUID, uint8 pullClassID, CharacterDatabaseTransaction& transaction)
{
    // Delete the old data
    transaction->Append("DELETE FROM `character_action` WHERE guid = {}", playerGUID);

    // Create inserts for all of the coming class action bar buttons
    QueryResult queryResult = CharacterDatabase.Query("SELECT spec, button, `action`, `type` FROM mod_multi_class_character_action WHERE guid = {} and class = {}", playerGUID, (uint32)pullClassID);
    if (!queryResult)
    {
        LOG_ERROR("module", "multiclass: Error pulling action bar data from the mod table for class {} on guid {}, so the class will have action bar buttons...", (uint32)pullClassID, playerGUID);
    }
    else
    {
        do
        {
            // Pull the data out
            Field* fields = queryResult->Fetch();
            uint8 actionSpec = fields[0].Get<uint8>();
            uint8 actionButton = fields[1].Get<uint8>();
            uint32 actionAction = fields[2].Get<uint32>();
            uint8 actionType = fields[3].Get<uint8>();

            transaction->Append("INSERT INTO `character_action` (`guid`, `spec`, `button`, `action`, `type`) VALUES ({}, {}, {}, {}, {})",
                playerGUID,
                (uint32)actionSpec,
                (uint32)actionButton,
                actionAction,
                (uint32)actionType);

        } while (queryResult->NextRow());
    }
}

void MultiClassMod::CopyModSkillTableIntoCharacterSkills(uint32 playerGUID, uint8 pullClassID, CharacterDatabaseTransaction& transaction)
{
    // Create inserts for all of the coming class skills
    QueryResult queryResult = CharacterDatabase.Query("SELECT skill, value, max FROM mod_multi_class_character_skills WHERE guid = {} and class = {}", playerGUID, (uint32)pullClassID);
    if (!queryResult)
    {
        LOG_ERROR("module", "multiclass: Error pulling class skill data from the mod table for class {} on guid {}, so the class will have no non-shared skills...", (uint32)pullClassID, playerGUID);
    }
    else
    {
        do
        {
            // Pull the data out
            Field* fields = queryResult->Fetch();
            uint32 skillID = fields[0].Get<uint32>();
            uint32 value = fields[1].Get<uint32>();
            uint32 max = fields[2].Get<uint32>();

            // Insert it
            transaction->Append("INSERT INTO `character_skills` (`guid`, `skill`, `value`, `max`) VALUES ({}, {}, {}, {})",
                playerGUID,
                skillID,
                value,
                max);

        } while (queryResult->NextRow());
    }
}

// (Re)populates the ability data for the classes
bool MultiClassMod::LoadClassAbilityData()
{
    // Clear old
    ClassSpellsByClassAndSpellID.clear();

    // Cache the mod for talent level calculations
    float talentRateMod = 1.0f;
    if (sWorld->getRate(RATE_TALENT) > 1.0f)
        talentRateMod = 1.0f / sWorld->getRate(RATE_TALENT);

    // Pull in all the new data
    QueryResult queryResult = WorldDatabase.Query("SELECT `SpellID`, `SpellName`, `SpellSubText`, `ReqSpellID`, `ReqLevel`, `Class`, `Side`, `IsTalent`, `IsLearnedByTalent` FROM mod_multi_class_spells ORDER BY `Class`, `SpellID`");
    if (!queryResult)
    {
        LOG_ERROR("module", "multiclass: Error pulling class spell data from the database.  Does the 'mod_multi_class_spells' table exist in the world database and is populated?");
        return false;
    }
    do
    {
        // Pull the data out
        Field* fields = queryResult->Fetch();
        MultiClassSpells curClassData;
        curClassData.SpellID = fields[0].Get<uint32>();
        curClassData.SpellName = fields[1].Get<std::string>();
        curClassData.SpellSubText = fields[2].Get<std::string>();
        curClassData.ReqSpellID = fields[3].Get<uint32>();
        curClassData.ReqLevel = fields[4].Get<uint16>();
        uint16 curClass = fields[5].Get<uint16>();
        std:string curFactionAllowed = fields[6].Get<std::string>();
        curClassData.IsTalent = fields[7].Get<bool>();
        curClassData.IsLearnedByTalent = fields[8].Get<bool>();

        // Calculate the level gap for knowing a spell cross-class
        if (curClassData.IsLearnedByTalent && curClassData.ReqLevel >= 11 && sWorld->getRate(RATE_TALENT) > 1.0f)
        {
            curClassData.ModifiedReqLevel = (uint16)((float)(curClassData.ReqLevel - 10) * talentRateMod) + ConfigCrossClassAbilityLevelGap + 10;
        }
        else
            curClassData.ModifiedReqLevel = curClassData.ReqLevel + ConfigCrossClassAbilityLevelGap;

        // Determine the faction
        if (curFactionAllowed == "ALLIANCE" || curFactionAllowed == "Alliance" || curFactionAllowed == "alliance")
        {
            curClassData.AllowAlliance = true;
            curClassData.AllowHorde = false;
        }
        else if (curFactionAllowed == "HORDE" || curFactionAllowed == "Horde" || curFactionAllowed == "horde")
        {
            curClassData.AllowAlliance = false;
            curClassData.AllowHorde = true;
        }
        else if (curFactionAllowed == "BOTH" || curFactionAllowed == "Both" || curFactionAllowed == "both")
        {
            curClassData.AllowAlliance = true;
            curClassData.AllowHorde = true;
        }
        else
        {
            LOG_ERROR("module", "multiclass: Could not interpret the class faction, value passed was {}", curClass);
            curClassData.AllowAlliance = false;
            curClassData.AllowHorde = false;
        }

        // Add to class spell list
        if (ClassSpellsByClassAndSpellID[curClass].find(curClassData.SpellID) != ClassSpellsByClassAndSpellID[curClass].end())
        {
            LOG_ERROR("module", "multiclass: SpellID with ID {} for class {} already in the class spells by class map, skipping...", curClassData.SpellID, curClass);
        }
        else
        {
            ClassSpellsByClassAndSpellID[curClass].insert(pair<uint32, MultiClassSpells>(curClassData.SpellID, curClassData));
        }

        // Add to the full list
        if (ClassSpellsBySpellID.find(curClassData.SpellID) == ClassSpellsBySpellID.end())
        {
            ClassSpellsBySpellID.insert(pair<uint32, MultiClassSpells>(curClassData.SpellID, curClassData));
        }
    } while (queryResult->NextRow());
    return true;
}

bool MultiClassMod::MarkClassChangeOnNextLogout(ChatHandler* handler, Player* player, uint8 newClass)
{
    // Delete the switch row if it's already there
    DeleteQueuedClassSwitch(player);

    // Don't do anything if we're already that class
    if (newClass == player->getClass())
    {
        handler->PSendSysMessage("Class change requested is the current class, so taking no action on the next login.");
        return false;
    }
    else if (!IsValidRaceClassCombo(newClass, player->getRace()))
    {
        handler->PSendSysMessage("Class change could not be completed because this class and race combo is not enabled on the server.");
        return false;
    }

    // Add the switch event
    QueueClassSwitch(player, newClass);
    switch (newClass)
    {
    case CLASS_WARRIOR: handler->PSendSysMessage("You will become a Warrior on the next login"); break;
    case CLASS_PALADIN: handler->PSendSysMessage("You will become a Paladin on the next login"); break;
    case CLASS_HUNTER: handler->PSendSysMessage("You will become a Hunter on the next login"); break;
    case CLASS_ROGUE: handler->PSendSysMessage("You will become a Rogue on the next login"); break;
    case CLASS_PRIEST: handler->PSendSysMessage("You will become a Priest on the next login"); break;
    case CLASS_DEATH_KNIGHT: handler->PSendSysMessage("You will become a Death Knight on the next login"); break;
    case CLASS_SHAMAN: handler->PSendSysMessage("You will become a Shaman on the next login"); break;
    case CLASS_MAGE: handler->PSendSysMessage("You will become a Mage on the next login"); break;
    case CLASS_WARLOCK: handler->PSendSysMessage("You will become a Warlock on the next login"); break;
    case CLASS_DRUID: handler->PSendSysMessage("You will become a Druid on the next login"); break;
    default: break;
    }
    return true;
}

bool MultiClassMod::PerformQueuedClassSwitchOnLogout(Player* player)
{
    // Only take action if there's a class switch queued
    QueuedClassSwitch queuedClassSwitch = GetQueuedClassSwitch(player);
    if (queuedClassSwitch.classID == CLASS_NONE)
        return true;

    uint8 oldClass = player->getClass();
    uint8 nextClass = queuedClassSwitch.classID;
    bool isNew = queuedClassSwitch.isNew;

    // Set up the transaction
    CharacterDatabaseTransaction transaction = CharacterDatabase.BeginTransaction();

    // Perform moves into the mod tables to reflect this character's class
    CopyCharacterDataIntoModCharacterTable(player, transaction);
    MoveTalentsToModTalentsTable(player, transaction);
    MoveClassSpellsToModSpellsTable(player, transaction);
    MoveClassSkillsToModSkillsTable(player, transaction);
    ReplaceModClassActionCopy(player, transaction);
    MoveGlyphsToModGlyhpsTable(player, transaction);
    MoveAuraToModAuraTable(player, transaction);
    MoveEquipToModInventoryTable(player, transaction);

    // New
    if (isNew)
    {
        // Pull needed metadata
        uint32 startLevel;
        PlayerClassLevelInfo classInfo;
        if (isNew)
        {
            // For start level
            startLevel = nextClass != CLASS_DEATH_KNIGHT
                ? sWorld->getIntConfig(CONFIG_START_PLAYER_LEVEL)
                : sWorld->getIntConfig(CONFIG_START_HEROIC_PLAYER_LEVEL);

            // For health and mana    
            sObjectMgr->GetPlayerClassLevelInfo(nextClass, startLevel, &classInfo);
        }

        // Update the character core table to reflect the switch
        transaction->Append("UPDATE characters SET `class` = {}, `level` = {}, `xp` = 0, `leveltime` = 0, `rest_bonus` = 0, `resettalents_cost` = 0, `resettalents_time` = 0, health = {}, power1 = {}, power2 = 0, power3 = 0, power4 = 100, power5 = 0, power6 = 0, power7 = 0, `talentGroupsCount` = 1, `activeTalentGroup` = 0 WHERE guid = {}", nextClass, startLevel, classInfo.basehealth, classInfo.basemana, player->GetGUID().GetCounter());
    }
    // Existing
    else
    {
        // Copy in the stored version for existing
        UpdateCharacterFromModCharacterTable(player->GetGUID().GetCounter(), nextClass, transaction);
        CopyModSpellTableIntoCharacterSpells(player->GetGUID().GetCounter(), nextClass, transaction);
        CopyModActionTableIntoCharacterAction(player->GetGUID().GetCounter(), nextClass, transaction);
        CopyModSkillTableIntoCharacterSkills(player->GetGUID().GetCounter(), nextClass, transaction);

        transaction->Append("INSERT INTO character_talent (guid, spell, specMask) SELECT guid, spell, specMask FROM mod_multi_class_character_talent WHERE guid = {} AND class = {}", player->GetGUID().GetCounter(), nextClass);
        transaction->Append("INSERT INTO character_glyphs (guid, talentGroup, glyph1, glyph2, glyph3, glyph4, glyph5, glyph6) SELECT guid, talentGroup, glyph1, glyph2, glyph3, glyph4, glyph5, glyph6 FROM mod_multi_class_character_glyphs WHERE guid = {} AND class = {}", player->GetGUID().GetCounter(), nextClass);
        transaction->Append("INSERT INTO character_aura (guid, casterGuid, itemGuid, spell, effectMask, recalculateMask, stackCount, amount0, amount1, amount2, base_amount0, base_amount1, base_amount2, maxDuration, remainTime, remainCharges) SELECT guid, casterGuid, itemGuid, spell, effectMask, recalculateMask, stackCount, amount0, amount1, amount2, base_amount0, base_amount1, base_amount2, maxDuration, remainTime, remainCharges FROM mod_multi_class_character_aura WHERE guid = {} AND class = {}", player->GetGUID().GetCounter(), nextClass);
        transaction->Append("INSERT INTO `character_inventory` (`guid`, `bag`, `slot`, `item`) SELECT `guid`, `bag`, `slot`, `item` FROM mod_multi_class_character_inventory WHERE guid = {} AND class = {}", player->GetGUID().GetCounter(), nextClass);
    }

    // Commit the transaction
    CharacterDatabase.CommitTransaction(transaction);

    return true;
}

bool MultiClassMod::PerformQueuedClassSwitchOnLogin(Player* player)
{
    // Only take action if there's a class switch queued
    QueuedClassSwitch queuedClassSwitch = GetQueuedClassSwitch(player);
    if (queuedClassSwitch.classID == CLASS_NONE)
        return true;

    // Learn new abilities

    // Unlearn invalid abilities


    // Clear the class switch
    DeleteQueuedClassSwitch(player);
    return true;
}

class MultiClass_PlayerScript : public PlayerScript
{
public:
    MultiClass_PlayerScript() : PlayerScript("MultiClass_PlayerScript") {}

    void OnLogin(Player* player)
    {
        if (ConfigEnabled == false)
            return;

	    ChatHandler(player->GetSession()).SendSysMessage("This server is running the |cff4CFF00MultiClass |rmodule.");

        if (!MultiClass->PerformQueuedClassSwitchOnLogin(player))
        {
            LOG_ERROR("module", "multiclass: Could not successfully complete the class switch on login for player {} with GUID {}", player->GetName(), player->GetGUID().GetCounter());
        }
    }

    void OnLogout(Player* player)
    {
        if (ConfigEnabled == false)
            return;

        if (!MultiClass->PerformQueuedClassSwitchOnLogout(player))
        {
            LOG_ERROR("module", "multiclass: Could not successfully complete the class switch on logout for player {} with GUID {}", player->GetName(), player->GetGUID().GetCounter());
        }
    }
};

class MultiClass_WorldScript: public WorldScript
{
public:
    MultiClass_WorldScript() : WorldScript("MultiClass_WorldScript") {}

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        // Enabled Flag
        ConfigEnabled = sConfigMgr->GetOption<bool>("MultiClass.Enable", true);

        // Class Ability Data
        if (!MultiClass->LoadClassAbilityData())
        {
            LOG_ERROR("module", "multiclass: Could not load the class ability data after the config load");
        }

        // Cross Class Skills
        ConfigCrossClassIncludeSkillIDs = GetSetFromConfigString("MultiClass.CrossClassIncludeSkillIDs");
    }

    set<uint32> GetSetFromConfigString(string configStringName)
    {
        string configString = sConfigMgr->GetOption<std::string>(configStringName, "");

        std::string delimitedValue;
        std::stringstream delimetedValueStream;
        std::set<uint32> generatedSet;

        delimetedValueStream.str(configString);
        while (std::getline(delimetedValueStream, delimitedValue, ','))
        {
            std::string curValue;
            std::stringstream delimetedPairStream(delimitedValue);
            delimetedPairStream >> curValue;
            auto itemId = atoi(curValue.c_str());
            if (generatedSet.find(itemId) != generatedSet.end())
            {
                LOG_ERROR("module", "multiclass: Duplicate value found in config string named {}",configString);
            }
            else
            {
                generatedSet.insert(itemId);
            }
        }

        return generatedSet;
    }
};

class MultiClass_CommandScript : public CommandScript
{
public:
    MultiClass_CommandScript() : CommandScript("MultiClass_CommandScript") { }

    std::vector<ChatCommand> GetCommands() const
    {
        static std::vector<ChatCommand> CommandTable =
        {
            { "change",        SEC_PLAYER,                            true, &HandleMultiClassChangeClass,              "Changes your class" },
        };

        static std::vector<ChatCommand> commandTable =
        {
            { "class",       SEC_PLAYER,                             false, NULL,                      "", CommandTable },
        };
        return commandTable;
    }

    static bool HandleMultiClassChangeClass(ChatHandler* handler, const char* args)
    {
        if (ConfigEnabled == false)
            return true;

        if (!*args)
        {
            handler->PSendSysMessage(".class change 'class'");
            handler->PSendSysMessage("Changes the player class on next logout.  Example: '.class change warrior'");
            handler->PSendSysMessage("Valid Class Values: warrior, paladin, hunter, rogue, priest, deathknight, shaman, mage, warlock, druid");
            return true;
        }

        uint8 classInt = CLASS_NONE;
        std::string className = strtok((char*)args, " ");
        if (className.starts_with("Warr") || className.starts_with("warr") || className.starts_with("WARR"))
            classInt = CLASS_WARRIOR;
        else if (className.starts_with("Pa") || className.starts_with("pa") || className.starts_with("PA"))
            classInt = CLASS_PALADIN;
        else if (className.starts_with("H") || className.starts_with("h"))
            classInt = CLASS_HUNTER;
        else if (className.starts_with("R") || className.starts_with("r"))
            classInt = CLASS_ROGUE;
        else if (className.starts_with("Pr") || className.starts_with("pr") || className.starts_with("PR"))
            classInt = CLASS_PRIEST;
        else if (className.starts_with("De") || className.starts_with("de") || className.starts_with("DE"))
            classInt = CLASS_DEATH_KNIGHT;
        else if (className.starts_with("S") || className.starts_with("s"))
            classInt = CLASS_SHAMAN;
        else if (className.starts_with("M") || className.starts_with("m"))
            classInt = CLASS_MAGE;
        else if (className.starts_with("Warl") || className.starts_with("warl") || className.starts_with("WARL"))
            classInt = CLASS_WARLOCK;
        else if (className.starts_with("Dr") || className.starts_with("dr") || className.starts_with("DR"))
            classInt = CLASS_DRUID;
        else
        {
            handler->PSendSysMessage(".class change 'class'");
            handler->PSendSysMessage("Changes the player class.  Example: '.class change warrior'");
            handler->PSendSysMessage("Valid Class Values: warrior, paladin, hunter, rogue, priest, deathknight, shaman, mage warlock, druid");
            std::string enteredValueLine = "Entered Value was ";
            enteredValueLine.append(className);
            handler->PSendSysMessage(enteredValueLine.c_str());
            return true;
        }

        Player* player = handler->GetPlayer();
        MultiClass->MarkClassChangeOnNextLogout(handler, player, classInt);
        
        // Class change accepted
        return true;
    }
};

std::string GenerateCommaDelimitedStringFromSet(std::set<uint32> intSet)
{
    bool isFirstElement = true;
    stringstream generatedStringStream;
    for (auto element : intSet)
    {
        if (!isFirstElement)
            generatedStringStream << ", ";
        generatedStringStream << element;
        isFirstElement = false;
    }
    return generatedStringStream.str();
}

void AddMultiClassScripts()
{
    new MultiClass_CommandScript();
    new MultiClass_PlayerScript();
    new MultiClass_WorldScript();
}
