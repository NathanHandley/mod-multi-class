
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

// TODO: Restrict DK unless level 55 (sConfigMgr->GetOption<int32>("CharacterCreating.MinLevelForHeroicCharacter", 55);)

#include "Chat.h"
#include "Configuration/Config.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "StringConvert.h"
#include "Tokenize.h"
#include "Unit.h"
#include "World.h"

#include "MultiClass.h"

#include <set>
#include <list>

using namespace Acore::ChatCommands;
using namespace std;

static bool ConfigEnabled;
static bool ConfigDisplayInstructionMessage;
static set<uint32> ConfigCrossClassIncludeSkillIDs;
static bool ConfigUsingTransmogMod;                     // If true, factor for the transmog fakeEntry table records
static bool ConfigEnableMasterSkills;                   // If true, the player can learn spells from other classes
static uint8 ConfigLevelsPerToken;                      // How many levels per token issued
static set<uint32> ConfigBonusTokenLevels;              // Levels where an extra token is awarded

static uint32 ConfigMaxSkillIDCheck = 1000;             // The highest level of skill ID it will look for when doing copies

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

// Returns a list of MasterSkills that the player knows
list<MasterSkill> MultiClassMod::GetKnownMasterSkillsForPlayer(Player* player)
{
    list<MasterSkill> knownMasterSkills;
    for (auto& curSpell : player->GetSpellMap())
    {
        if (curSpell.second->State == PLAYERSPELL_REMOVED)
            continue;
        if (MasterSkillsBySpellID.find(curSpell.first) != MasterSkillsBySpellID.end())
            knownMasterSkills.push_back(MasterSkillsBySpellID[curSpell.first]);
    }
    return knownMasterSkills;
}

list<MasterSkill> MultiClassMod::GetKnownMasterSkillsForPlayerForClass(Player* player, uint8 classID)
{
    list<MasterSkill> knownMasterSkills;
    for (auto& curSpell : player->GetSpellMap())
    {
        if (curSpell.second->State == PLAYERSPELL_REMOVED)
            continue;

        if (MasterSkillsBySpellID.find(curSpell.first) != MasterSkillsBySpellID.end())
        {
            if (MasterSkillsBySpellID[curSpell.first].ClassID == classID)
                knownMasterSkills.push_back(MasterSkillsBySpellID[curSpell.first]);
        }
    }
    return knownMasterSkills;
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
        auto finiteAlways = [](float f) { return std::isfinite(f) ? f : 0.0f; };

        transaction->Append("INSERT IGNORE INTO mod_multi_class_characters (guid, class, `level`, xp, leveltime, rest_bonus, resettalents_cost, resettalents_time, health, power1, power2, power3, power4, power5, power6, power7, talentGroupsCount, activeTalentGroup) VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
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
    transaction->Append("INSERT IGNORE INTO mod_multi_class_character_talent (guid, class, spell, specMask) SELECT guid, {}, spell, specMask FROM character_talent WHERE guid = {}", player->getClass(), player->GetGUID().GetCounter());
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
        if (ClassSpellIDs.find(curSpell.first) == ClassSpellIDs.end())
            continue;

        // Skip deleting spells
        if (curSpell.second->State == PLAYERSPELL_REMOVED)
            continue;

        // INSERT IGNORE INTO Mod
        transaction->Append("INSERT IGNORE INTO mod_multi_class_character_spell (guid, class, spell, specMask) VALUES ({}, {}, {}, {})",
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
        transaction->Append("INSERT IGNORE INTO mod_multi_class_character_skills (guid, class, skill, value, max) VALUES ({}, {}, {}, {}, {})",
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

    // Less ideal approach, as it causes a table scan on character_action
    transaction->Append("INSERT IGNORE INTO mod_multi_class_character_action (guid, class, spec, button, `action`, `type`) SELECT guid, {}, spec, button, `action`, `type` FROM character_action WHERE guid = {}", player->getClass(), player->GetGUID().GetCounter());
}

void MultiClassMod::MoveGlyphsToModGlyhpsTable(Player* player, CharacterDatabaseTransaction& transaction)
{
    transaction->Append("DELETE FROM `mod_multi_class_character_glyphs` WHERE guid = {} and class = {}", player->GetGUID().GetCounter(), player->getClass());
    transaction->Append("INSERT IGNORE INTO mod_multi_class_character_glyphs (guid, class, talentGroup, glyph1, glyph2, glyph3, glyph4, glyph5, glyph6) SELECT guid, {}, talentGroup, glyph1, glyph2, glyph3, glyph4, glyph5, glyph6 FROM character_glyphs WHERE guid = {}", player->getClass(), player->GetGUID().GetCounter());
    transaction->Append("DELETE FROM `character_glyphs` WHERE guid = {}", player->GetGUID().GetCounter());
}

void MultiClassMod::MoveAuraToModAuraTable(Player* player, CharacterDatabaseTransaction& transaction)
{
    transaction->Append("DELETE FROM `mod_multi_class_character_aura` WHERE guid = {} and class = {}", player->GetGUID().GetCounter(), player->getClass());
    transaction->Append("INSERT IGNORE INTO mod_multi_class_character_aura (guid, class, casterGuid, itemGuid, spell, effectMask, recalculateMask, stackCount, amount0, amount1, amount2, base_amount0, base_amount1, base_amount2, maxDuration, remainTime, remainCharges) SELECT guid, {}, casterGuid, itemGuid, spell, effectMask, recalculateMask, stackCount, amount0, amount1, amount2, base_amount0, base_amount1, base_amount2, maxDuration, remainTime, remainCharges FROM character_aura WHERE guid = {}", player->getClass(), player->GetGUID().GetCounter());
    transaction->Append("DELETE FROM `character_aura` WHERE guid = {}", player->GetGUID().GetCounter());
}

void MultiClassMod::MoveEquipToModInventoryTable(Player* player, CharacterDatabaseTransaction& transaction)
{
    transaction->Append("DELETE FROM `mod_multi_class_character_inventory` WHERE guid = {} AND class = {} AND `bag` = 0 AND `slot` <= 18;", player->GetGUID().GetCounter(), player->getClass());
    transaction->Append("INSERT IGNORE INTO `mod_multi_class_character_inventory` (`guid`, `class`, `bag`, `slot`, `item`) SELECT `guid`, {}, `bag`, `slot`, `item` FROM character_inventory WHERE guid = {} AND `bag` = 0 AND `slot` <= 18", player->getClass(), player->GetGUID().GetCounter());
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
    if (queryResult)
    {
        do
        {
            // Pull the data out
            Field* fields = queryResult->Fetch();
            uint32 spellID = fields[0].Get<uint32>();
            uint8 specMask = fields[1].Get<uint8>();

            // Skip if not valid
            if (ClassSpellIDs.find(spellID) == ClassSpellIDs.end())
                continue;

            // Add it
            transaction->Append("INSERT IGNORE INTO character_spell (guid, spell, specMask) VALUES ({}, {}, {})",
                playerGUID,
                spellID,
                (uint32)specMask);
        } while (queryResult->NextRow());
    }
}

void MultiClassMod::CopyModActionTableIntoCharacterAction(uint32 playerGUID, uint8 pullClassID, CharacterDatabaseTransaction& transaction)
{
    // Delete the old data
    transaction->Append("DELETE FROM `character_action` WHERE guid = {}", playerGUID);

    // Create inserts for all of the coming class action bar buttons
    QueryResult queryResult = CharacterDatabase.Query("SELECT spec, button, `action`, `type` FROM mod_multi_class_character_action WHERE guid = {} and class = {}", playerGUID, (uint32)pullClassID);
    if (queryResult)
    {
        do
        {
            // Pull the data out
            Field* fields = queryResult->Fetch();
            uint8 actionSpec = fields[0].Get<uint8>();
            uint8 actionButton = fields[1].Get<uint8>();
            uint32 actionAction = fields[2].Get<uint32>();
            uint8 actionType = fields[3].Get<uint8>();

            transaction->Append("INSERT IGNORE INTO `character_action` (`guid`, `spec`, `button`, `action`, `type`) VALUES ({}, {}, {}, {}, {})",
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
            transaction->Append("INSERT IGNORE INTO `character_skills` (`guid`, `skill`, `value`, `max`) VALUES ({}, {}, {}, {})",
                playerGUID,
                skillID,
                value,
                max);

        } while (queryResult->NextRow());
    }
}

// Modifies a list of spell learn and unlearns for the passed gathering skills
void MultiClassMod::AddSpellLearnAndUnlearnsForGatheringSkillForPlayer(Player* player, uint16 skillID, array<uint32, 6> skillSpellIDs, list<int32>& inOutSpellUnlearns, list<int32>& inOutSpellLearns)
{
    // Get level
    uint8 playerLevel = player->GetLevel();

    if (player->HasSkill(skillID))
    {
        uint16 skillValue = player->GetSkillValue(skillID);

        // Learn the one you should know.  Have to unlearn the current rank first for it to reflect properly
        if (playerLevel >= 55 && skillValue >= 450)
        {
            inOutSpellUnlearns.push_back(skillSpellIDs[0]);
            inOutSpellLearns.push_back(skillSpellIDs[0]);
        }
        else if (playerLevel >= 40 && skillValue >= 375)
        {
            inOutSpellUnlearns.push_back(skillSpellIDs[1]);
            inOutSpellLearns.push_back(skillSpellIDs[1]);
        }
        else if (playerLevel >= 25 && skillValue >= 300)
        {
            inOutSpellUnlearns.push_back(skillSpellIDs[2]);
            inOutSpellLearns.push_back(skillSpellIDs[2]);
        }
        else if (playerLevel >= 10 && skillValue >= 225)
        {
            inOutSpellUnlearns.push_back(skillSpellIDs[3]);
            inOutSpellLearns.push_back(skillSpellIDs[3]);
        }
        else if (skillValue >= 150)
        {
            inOutSpellUnlearns.push_back(skillSpellIDs[4]);
            inOutSpellLearns.push_back(skillSpellIDs[4]);
        }
        else if (skillValue >= 75)
        {
            inOutSpellUnlearns.push_back(skillSpellIDs[5]);
            inOutSpellLearns.push_back(skillSpellIDs[5]);
        }
    }
}

// Returns a list of what spells should be learned and unlearned for a class
void MultiClassMod::GetSpellLearnAndUnlearnsForPlayer(Player* player, list<int32>& outSpellUnlearns, list<int32>& outSpellLearns)
{
    // Clear out parameters
    outSpellUnlearns.clear();
    outSpellLearns.clear();

    // Get the known Master Skills
    list<MasterSkill> knownMasterSkills = GetKnownMasterSkillsForPlayer(player);

    // Get level
    uint8 playerLevel = player->GetLevel();

    // Go through the master skills and see what the player should know from them now
    set<uint32> shouldKnowSpellIDsFromMasterSkills;
    for (auto& curMasterSkill : knownMasterSkills)
    {
        for (auto& curMasterSkillSpell : curMasterSkill.Spells)
        {
            // Skip out of level spells
            if (curMasterSkillSpell.ReqLevel > player->GetLevel())
                continue;

            // This is a spell the player should know
            shouldKnowSpellIDsFromMasterSkills.insert(curMasterSkillSpell.SpellID);

            // If the player doesn't know it, mark it to learn
            if (player->HasSpell(curMasterSkillSpell.SpellID) == false)
                outSpellLearns.push_back(curMasterSkillSpell.SpellID);
        }
    }

    // If player has a gathering skill, determine what rank they should have and set mark accordingly
    
    // Herbalism - Lifeblood
    //AddSpellLearnAndUnlearnsForGatheringSkillForPlayer(player, 182, { 55503, 55502, 55501, 55500, 55480, 55428 }, outSpellUnlearns, outSpellLearns);

    // Mining - Toughness
    AddSpellLearnAndUnlearnsForGatheringSkillForPlayer(player, 186, { 53040, 53124, 53123, 53122, 53121, 53120 }, outSpellUnlearns, outSpellLearns);
    
    // Skinning - Master of Anatomy
    AddSpellLearnAndUnlearnsForGatheringSkillForPlayer(player, 393, { 53666, 53665, 53664, 53663, 53662, 53125 }, outSpellUnlearns, outSpellLearns);

    // Go through what class spells the player does know, and mark removal for any that don't belong to the player's class, aren't in the master skill list, or are invalid profession spells
    for (auto& curSpell : player->GetSpellMap())
    {
        // Skip already deleted spells
        if (curSpell.second->State == PLAYERSPELL_REMOVED)
            continue;

        // Skip non class spells
        if (ClassSpellIDs.find(curSpell.first) == ClassSpellIDs.end())
            continue;

        // Skip these class spells
        // TODO: Talent awareness?
        if (ClassSpellsByClassAndSpellID[player->getClass()].find(curSpell.first) != ClassSpellsByClassAndSpellID[player->getClass()].end())
            continue;

        // Mark removal if the player knows a class spell, but it's not in the master list pulled earlier
        if (shouldKnowSpellIDsFromMasterSkills.find(curSpell.first) == shouldKnowSpellIDsFromMasterSkills.end())
            outSpellUnlearns.push_back(curSpell.first);        
    }
}

uint8 MultiClassMod::GetTokenCountToIssueForPlayer(Player* player, uint8 classID)
{
    // If disabled, skip
    if (ConfigLevelsPerToken == 0)
        return 0;

    // Ignore if death knight
    if (player->IsClass(CLASS_DEATH_KNIGHT))
        return 0;

    // Get this class level
    uint8 playersIssueClassLevel = player->GetLevel();
    if (player->getClass() == classID)
        playersIssueClassLevel = player->GetLevel();
    else
    {
        map<uint8, uint8> levelsByClassForPlayer = GetClassLevelsByClassForPlayer(player);
        if (levelsByClassForPlayer.find(classID) != levelsByClassForPlayer.end())
            playersIssueClassLevel = levelsByClassForPlayer[classID];
    }

    // get the tokens issued for the class
    uint8 tokensIssued = 0;
    QueryResult tokenQueryResult = CharacterDatabase.Query("SELECT `tokensIssued` FROM mod_multi_class_character_tokens WHERE guid = {} AND class = {}", player->GetGUID().GetCounter(), classID);
    if (tokenQueryResult && tokenQueryResult->GetRowCount() > 0)
    {
        Field* fields = tokenQueryResult->Fetch();
        tokensIssued = fields[0].Get<uint8>();
    }
    else
    {
        // No record found, so make a blank one
        CharacterDatabase.DirectExecute("INSERT IGNORE INTO mod_multi_class_character_tokens (guid, class, tokensIssued) VALUES ({}, {}, 0)",
            player->GetGUID().GetCounter(),
            classID);
    }

    // Calculate the number of tokens to issue
    uint8 tokensToIssueTotal = playersIssueClassLevel / ConfigLevelsPerToken;
    for (auto& bonusLevel : ConfigBonusTokenLevels)
    {
        if (playersIssueClassLevel >= bonusLevel)
            tokensToIssueTotal++;
    }
    if (tokensToIssueTotal > tokensIssued)
        return tokensToIssueTotal - tokensIssued;
    else
        return 0;
}

bool MultiClassMod::RefundTokenCountForPlayerClass(Player* player, uint8 classID, uint8 tokenCountToRefund)
{
    // Ignore if death knight
    if (player->IsClass(CLASS_DEATH_KNIGHT))
        return true;

    // Do nothing if nothing is refunded
    if (tokenCountToRefund == 0)
        return true;

    // Get the tokens issued so far
    uint8 tokensIssued = 0;
    QueryResult tokenQueryResult = CharacterDatabase.Query("SELECT `tokensIssued` FROM mod_multi_class_character_tokens WHERE guid = {} AND class = {}", player->GetGUID().GetCounter(), classID);
    if (tokenQueryResult && tokenQueryResult->GetRowCount() > 0)
    {
        Field* fields = tokenQueryResult->Fetch();
        tokensIssued = fields[0].Get<uint8>();
    }
    else
    {
        // No record found, so make a blank one
        CharacterDatabase.DirectExecute("INSERT IGNORE INTO mod_multi_class_character_tokens (guid, class, tokensIssued) VALUES ({}, {}, 0)",
            player->GetGUID().GetCounter(),
            player->getClass());
    }

    // Refund the tokens
    if (tokenCountToRefund > tokensIssued)
    {
        LOG_ERROR("module", "multiclass: Error refunding tokens for player guid {}, Class {} had {} tokens issued, but requested to refund {}", player->GetGUID().GetCounter(), classID, tokensIssued, tokenCountToRefund);
        return false;
    }
    uint8 newTokenCount = tokensIssued - tokenCountToRefund;
    CharacterDatabase.DirectExecute("UPDATE mod_multi_class_character_tokens SET tokensIssued = {} WHERE guid = {} AND class = {}", newTokenCount, player->GetGUID().GetCounter(), classID);
    return true;
}

void MultiClassMod::UpdateTokenIssueCountForPlayerClass(Player* player, uint8 tokenCount, uint8 classID)
{
    // Ignore if death knight
    if (classID == CLASS_DEATH_KNIGHT)
        return;

    QueryResult tokenQueryResult = CharacterDatabase.Query("SELECT `tokensIssued` FROM mod_multi_class_character_tokens WHERE guid = {} AND class = {}", player->GetGUID().GetCounter(), classID);
    if (!tokenQueryResult || tokenQueryResult->GetRowCount() == 0)
    {
        CharacterDatabase.DirectExecute("INSERT IGNORE INTO mod_multi_class_character_tokens (guid, class, tokensIssued) VALUES ({}, {}, {})",
            player->GetGUID().GetCounter(),
            classID,
            tokenCount);
    }
    else
    {
        // Add to the existing and save that
        Field* fields = tokenQueryResult->Fetch();
        uint8 tokensIssuedAlready = fields[0].Get<uint8>();
        CharacterDatabase.DirectExecute("UPDATE mod_multi_class_character_tokens SET tokensIssued = {} WHERE guid = {} AND class = {}",
            tokenCount + tokensIssuedAlready,
            player->GetGUID().GetCounter(),
            classID);
    }
}

// (Re)populates the ability data for the classes
bool MultiClassMod::LoadClassAbilityData()
{
    // Pull in all the new spell data
    ClassSpellsByClassAndSpellID.clear();
    ClassSpellIDs.clear();
    QueryResult spellQueryResult = WorldDatabase.Query("SELECT `SpellID`, `SpellName`, `SpellSubText`, `ReqSpellID`, `ReqLevel`, `Class`, `Side`, `IsTalent`, `IsLearnedByTalent` FROM mod_multi_class_spells ORDER BY `Class`, `SpellID`");
    if (!spellQueryResult)
    {
        LOG_ERROR("module", "multiclass: Error pulling class spell data from the database.  Does the 'mod_multi_class_spells' table exist in the world database and is populated?");
        return false;
    }
    do
    {
        // Pull the data out
        Field* fields = spellQueryResult->Fetch();
        MultiClassSpell curSpellData;
        curSpellData.SpellID = fields[0].Get<uint32>();
        curSpellData.SpellName = fields[1].Get<std::string>();
        curSpellData.SpellSubText = fields[2].Get<std::string>();
        curSpellData.ReqSpellID = fields[3].Get<uint32>();
        curSpellData.ReqLevel = fields[4].Get<uint8>();
        curSpellData.ClassID = fields[5].Get<uint8>();
        std::string curFactionAllowed = fields[6].Get<std::string>();
        curSpellData.IsTalent = fields[7].Get<bool>();
        curSpellData.IsLearnedByTalent = fields[8].Get<bool>();

        // Determine the faction
        if (curFactionAllowed == "ALLIANCE" || curFactionAllowed == "Alliance" || curFactionAllowed == "alliance")
        {
            curSpellData.AllowAlliance = true;
            curSpellData.AllowHorde = false;
        }
        else if (curFactionAllowed == "HORDE" || curFactionAllowed == "Horde" || curFactionAllowed == "horde")
        {
            curSpellData.AllowAlliance = false;
            curSpellData.AllowHorde = true;
        }
        else if (curFactionAllowed == "BOTH" || curFactionAllowed == "Both" || curFactionAllowed == "both")
        {
            curSpellData.AllowAlliance = true;
            curSpellData.AllowHorde = true;
        }
        else
        {
            LOG_ERROR("module", "multiclass: Could not interpret the class faction, value passed was {}", curSpellData.ClassID);
            curSpellData.AllowAlliance = false;
            curSpellData.AllowHorde = false;
        }

        // Add to the full list
        if (ClassSpellsByClassAndSpellID[curSpellData.ClassID].find(curSpellData.SpellID) != ClassSpellsByClassAndSpellID[curSpellData.ClassID].end())
        {
            LOG_ERROR("module", "multiclass: SpellID with ID {} for class {} already in the class spells by class map, skipping...", curSpellData.SpellID, curSpellData.ClassID);
        }
        else
        {
            ClassSpellsByClassAndSpellID[curSpellData.ClassID].insert(pair<uint32, MultiClassSpell>(curSpellData.SpellID, curSpellData));
        }

        // Add a unique entry to the spell ID tracker
        if (ClassSpellIDs.find(curSpellData.SpellID) == ClassSpellIDs.end())
        {
            ClassSpellIDs.insert(curSpellData.SpellID);
        }
    } while (spellQueryResult->NextRow());

    // Pull in the master skills
    MasterSkillsBySpellID.clear();
    QueryResult masterQueryResult = WorldDatabase.Query("SELECT MasterSkillID, LearnSpellClassID, LearnSpellID FROM mod_multi_class_master_skill_spells ORDER BY MasterSkillID");
    if (!masterQueryResult)
    {
        LOG_ERROR("module", "multiclass: Error pulling master skill data from the database.  Does the 'mod_multi_class_master_skill_spells' table exist in the world database and is populated?");
        return false;
    }
    do
    {
        // Pull the data out
        Field* fields = masterQueryResult->Fetch();
        uint32 masterSkillSpellID = fields[0].Get<uint32>();
        uint32 learnSpellClass = fields[1].Get<uint8>();
        uint32 learnSpellID = fields[2].Get<uint32>();

        // Get the spell
        MultiClassSpell curSpell;
        if (ClassSpellsByClassAndSpellID.find(learnSpellClass) == ClassSpellsByClassAndSpellID.end() ||
            ClassSpellsByClassAndSpellID[learnSpellClass].find(learnSpellID) == ClassSpellsByClassAndSpellID[learnSpellClass].end())
        {
            LOG_ERROR("module", "multiclass: Could not find multi class spell with id {} in class {}, so skipping add of master skill {}", learnSpellID, learnSpellClass, masterSkillSpellID);
            continue;
        }
        curSpell = ClassSpellsByClassAndSpellID[learnSpellClass][learnSpellID];

        // Add the spell to the a master skill, creating a new master skill if one doesn't exist
        if (MasterSkillsBySpellID.find(masterSkillSpellID) == MasterSkillsBySpellID.end())
        {
            MasterSkill newMasterSkill;
            newMasterSkill.SpellID = masterSkillSpellID;
            newMasterSkill.ClassID = learnSpellClass;
            newMasterSkill.Spells.push_back(curSpell);
            MasterSkillsBySpellID.insert(pair<uint32, MasterSkill>(masterSkillSpellID, newMasterSkill));
        }
        else
        {
            MasterSkillsBySpellID[masterSkillSpellID].Spells.push_back(curSpell);
        }
    } while (masterQueryResult->NextRow());
    return true;
}

bool MultiClassMod::MarkClassChangeOnNextLogout(ChatHandler* handler, Player* player, uint8 newClass)
{
    if (!IsValidRaceClassCombo(newClass, player->getRace()))
    {
        handler->PSendSysMessage("Class change could not be completed because this class and race combo is not enabled on the server.");
        return false;
    }

    // Add the switch event
    PlayerControllerData controllerData = GetPlayerControllerData(player);
    controllerData.NextClass = newClass;
    SetPlayerControllerData(controllerData);
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

// Enables or Disables quest sharing for the current player class
bool MultiClassMod::MarkChangeQuestShareForCurrentClassOnNextLogout(Player* player, bool useSharedQuests)
{
    PlayerClassSettings classSettings = GetPlayerClassSettings(player, player->getClass());
    if (classSettings.UseSharedQuests == useSharedQuests)
        return false;
    else
    {
        classSettings.UseSharedQuests = useSharedQuests;
        SetPlayerClassSettings(classSettings);
        return true;
    }
}

// Enables or Disables reputation sharing for the current player class
bool MultiClassMod::MarkChangeReputationShareForCurrentClassOnNextLogout(Player* player, bool useSharedReputation)
{
    PlayerClassSettings classSettings = GetPlayerClassSettings(player, player->getClass());
    if (classSettings.UseSharedReputation == useSharedReputation)
        return false;
    else
    {
        classSettings.UseSharedReputation = useSharedReputation;
        SetPlayerClassSettings(classSettings);
        return true;
    }
}

bool MultiClassMod::PerformClassSwitch(Player* player, PlayerControllerData controllerData)
{
    uint8 nextClass = controllerData.NextClass;
    bool isNew = !DoesSavedClassDataExistForPlayer(player, controllerData.NextClass);

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

    // Update pet references
    transaction->Append("UPDATE character_pet SET multi_class_owner = {}, multi_class_class = {} WHERE owner = {}", player->GetGUID().GetCounter(), player->getClass(), player->GetGUID().GetCounter());
    transaction->Append("UPDATE character_pet SET owner = 0 WHERE multi_class_owner = {} AND multi_class_class = {}", player->GetGUID().GetCounter(), player->getClass());
    transaction->Append("UPDATE character_pet SET owner = {} WHERE multi_class_owner = {} AND multi_class_class = {}", player->GetGUID().GetCounter(), player->GetGUID().GetCounter(), nextClass);

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

        // Give blank action mappings
        transaction->Append("DELETE FROM `character_action` WHERE guid = {}", player->GetGUID().GetCounter());
    }
    // Existing
    else
    {
        // Copy in the stored version for existing
        UpdateCharacterFromModCharacterTable(player->GetGUID().GetCounter(), nextClass, transaction);
        CopyModSpellTableIntoCharacterSpells(player->GetGUID().GetCounter(), nextClass, transaction);
        CopyModActionTableIntoCharacterAction(player->GetGUID().GetCounter(), nextClass, transaction);
        CopyModSkillTableIntoCharacterSkills(player->GetGUID().GetCounter(), nextClass, transaction);

        transaction->Append("INSERT IGNORE INTO character_talent (guid, spell, specMask) SELECT guid, spell, specMask FROM mod_multi_class_character_talent WHERE guid = {} AND class = {}", player->GetGUID().GetCounter(), nextClass);
        transaction->Append("INSERT IGNORE INTO character_glyphs (guid, talentGroup, glyph1, glyph2, glyph3, glyph4, glyph5, glyph6) SELECT guid, talentGroup, glyph1, glyph2, glyph3, glyph4, glyph5, glyph6 FROM mod_multi_class_character_glyphs WHERE guid = {} AND class = {}", player->GetGUID().GetCounter(), nextClass);
        transaction->Append("INSERT IGNORE INTO character_aura (guid, casterGuid, itemGuid, spell, effectMask, recalculateMask, stackCount, amount0, amount1, amount2, base_amount0, base_amount1, base_amount2, maxDuration, remainTime, remainCharges) SELECT guid, casterGuid, itemGuid, spell, effectMask, recalculateMask, stackCount, amount0, amount1, amount2, base_amount0, base_amount1, base_amount2, maxDuration, remainTime, remainCharges FROM mod_multi_class_character_aura WHERE guid = {} AND class = {}", player->GetGUID().GetCounter(), nextClass);
        transaction->Append("INSERT IGNORE INTO `character_inventory` (`guid`, `bag`, `slot`, `item`) SELECT `guid`, `bag`, `slot`, `item` FROM mod_multi_class_character_inventory WHERE guid = {} AND class = {}", player->GetGUID().GetCounter(), nextClass);
    }

    // Commit the transaction
    CharacterDatabase.CommitTransaction(transaction);

    return true;
}

bool MultiClassMod::PerformQuestDataSwitch(uint32 playerGUID, uint8 prevQuestDataClass, uint8 nextQuestDataClass)
{
    // Set up the transaction
    CharacterDatabaseTransaction transaction = CharacterDatabase.BeginTransaction();

    // Delete the old mod quest data at the target
    transaction->Append("DELETE FROM `mod_multi_class_character_queststatus` WHERE guid = {} AND class = {}", playerGUID, prevQuestDataClass);
    transaction->Append("DELETE FROM `mod_multi_class_character_queststatus_rewarded` WHERE guid = {} AND class = {}", playerGUID, prevQuestDataClass);

    // Copy this quest data into the mod quest data
    transaction->Append("INSERT INTO `mod_multi_class_character_queststatus` (`guid`, `class`, `quest`, `status`, `explored`, `timer`, `mobcount1`, `mobcount2`, `mobcount3`, `mobcount4`, `itemcount1`, `itemcount2`, `itemcount3`, `itemcount4`, `itemcount5`, `itemcount6`, `playercount`) SELECT {}, {}, `quest`, `status`, `explored`, `timer`, `mobcount1`, `mobcount2`, `mobcount3`, `mobcount4`, `itemcount1`, `itemcount2`, `itemcount3`, `itemcount4`, `itemcount5`, `itemcount6`, `playercount` FROM character_queststatus WHERE guid = {}", playerGUID, prevQuestDataClass, playerGUID);
    transaction->Append("INSERT INTO `mod_multi_class_character_queststatus_rewarded` (`guid`, `class`, `quest`, `active`) SELECT {}, {}, `quest`, `active` FROM character_queststatus_rewarded WHERE guid = {}", playerGUID, prevQuestDataClass, playerGUID);
    
    // Delete the active quest data
    transaction->Append("DELETE FROM `character_queststatus` WHERE guid = {}", playerGUID);
    transaction->Append("DELETE FROM `character_queststatus_rewarded` WHERE guid = {}", playerGUID);

    // Insert in the related quest data from mod
    transaction->Append("INSERT INTO `character_queststatus` (`guid`, `quest`, `status`, `explored`, `timer`, `mobcount1`, `mobcount2`, `mobcount3`, `mobcount4`, `itemcount1`, `itemcount2`, `itemcount3`, `itemcount4`, `itemcount5`, `itemcount6`, `playercount`) SELECT {}, `quest`, `status`, `explored`, `timer`, `mobcount1`, `mobcount2`, `mobcount3`, `mobcount4`, `itemcount1`, `itemcount2`, `itemcount3`, `itemcount4`, `itemcount5`, `itemcount6`, `playercount` FROM mod_multi_class_character_queststatus WHERE guid = {} AND class = {}", playerGUID, playerGUID, nextQuestDataClass);
    transaction->Append("INSERT INTO `character_queststatus_rewarded` (`guid`, `quest`, `active`) SELECT {}, `quest`, `active` FROM mod_multi_class_character_queststatus_rewarded WHERE guid = {} AND class = {}", playerGUID, playerGUID, nextQuestDataClass);

    // Commit the transaction
    CharacterDatabase.CommitTransaction(transaction);
    return true;
}

bool MultiClassMod::PerformReputationDataSwitch(uint32 playerGUID, uint8 prevReputationDataClass, uint8 nextReputationDataClass)
{
    // Set up the transaction
    CharacterDatabaseTransaction transaction = CharacterDatabase.BeginTransaction();

    // Delete the old mod reputation data at the target
    transaction->Append("DELETE FROM `mod_multi_class_character_reputation` WHERE guid = {} AND class = {}", playerGUID, prevReputationDataClass);

    // Copy this quest data into the mod quest data
    transaction->Append("INSERT INTO `mod_multi_class_character_reputation` (`guid`, `class`, `faction`, `standing`, `flags`) SELECT {}, {}, `faction`, `standing`, `flags` FROM character_reputation WHERE guid = {}", playerGUID, prevReputationDataClass, playerGUID);

    // Delete the active quest data
    transaction->Append("DELETE FROM `character_reputation` WHERE guid = {}", playerGUID);

    // Insert in the related quest data from mod
    transaction->Append("INSERT INTO `character_reputation` (`guid`, `faction`, `standing`, `flags`) SELECT {}, `faction`, `standing`, `flags` FROM mod_multi_class_character_reputation WHERE guid = {} AND class = {}", playerGUID, playerGUID, nextReputationDataClass);

    // Commit the transaction
    CharacterDatabase.CommitTransaction(transaction);
    return true;
}

bool MultiClassMod::PerformPlayerDelete(ObjectGuid guid)
{
    // Delete every mod table record with this player guid
    uint32 playerGUID = guid.GetCounter();

    CharacterDatabaseTransaction transaction = CharacterDatabase.BeginTransaction();
    transaction->Append("DELETE FROM mod_multi_class_characters WHERE guid = {}", playerGUID);
    transaction->Append("DELETE FROM mod_multi_class_character_talent WHERE guid = {}", playerGUID);
    transaction->Append("DELETE FROM mod_multi_class_character_aura WHERE guid = {}", playerGUID);
    transaction->Append("DELETE FROM mod_multi_class_character_spell WHERE guid = {}", playerGUID);
    transaction->Append("DELETE FROM mod_multi_class_character_skills WHERE guid = {}", playerGUID);
    transaction->Append("DELETE FROM mod_multi_class_character_action WHERE guid = {}", playerGUID);
    transaction->Append("DELETE FROM mod_multi_class_character_glyphs WHERE guid = {}", playerGUID);
    transaction->Append("DELETE FROM mod_multi_class_character_inventory WHERE guid = {}", playerGUID);
    transaction->Append("DELETE FROM mod_multi_class_character_tokens WHERE guid = {}", playerGUID);
    transaction->Append("DELETE FROM mod_multi_class_character_queststatus WHERE guid = {}", playerGUID);
    transaction->Append("DELETE FROM mod_multi_class_character_queststatus_rewarded WHERE guid = {}", playerGUID);
    transaction->Append("DELETE FROM mod_multi_class_character_reputation WHERE guid = {}", playerGUID);
    transaction->Append("DELETE FROM mod_multi_class_character_controller WHERE guid = {}", playerGUID);
    transaction->Append("DELETE FROM mod_multi_class_character_class_settings WHERE guid = {}", playerGUID);
    transaction->Append("DELETE FROM character_pet WHERE owner = 0 AND multi_class_owner = {}", playerGUID);
    CharacterDatabase.CommitTransaction(transaction);
    return true;
}

void MultiClassMod::PerformKnownSpellUpdateFromMasterSkills(Player* player)
{
    list<int32> spellsToUnlearn;
    list<int32> spellsToLearn;
    GetSpellLearnAndUnlearnsForPlayer(player, spellsToUnlearn, spellsToLearn);

    // Perform unlearns
    for (auto& spellToUnlearn : spellsToUnlearn)
    {
        if (player->HasSpell(spellToUnlearn))
            player->removeSpell(spellToUnlearn, SPEC_MASK_ALL, false);
    }

    // Perform learns, but only temporary ones if it's a gather profession buff spell
    set<uint32> gatherProfSpells{ 55503, 55502, 55501, 55500, 55480, 55428, 53040, 53124, 53123, 53122, 53121, 53120, 53666, 53665, 53664, 53663, 53662, 53125 };
    for (auto& spellToLearn : spellsToLearn)
    {
        if (!player->HasSpell(spellToLearn))
        {
            if (gatherProfSpells.find(spellToLearn) != gatherProfSpells.end())
                player->learnSpell(spellToLearn, true);
            else
                player->learnSpell(spellToLearn);
                
        }
    }
}

bool MultiClassMod::PerformTokenIssuesForPlayerClass(Player* player, uint8 classID)
{
    // Give tokens if there are any to give
    uint8 tokenCountToIssue = GetTokenCountToIssueForPlayer(player, classID);
    if (tokenCountToIssue > 0)
    {
        uint32 itemIDofToken = GetTokenItemIDForClass(classID);
        if (itemIDofToken == 0)
        {
            LOG_ERROR("module", "multiclass: Unable to determine class token to use for class {} on player {}", classID, player->GetGUID().GetCounter());
            return false;
        }

        // Issue the token
        if (!player->AddItem(itemIDofToken, tokenCountToIssue))
        {
            LOG_ERROR("module", "multiclass: Unable to give item with id {} to player with GUID {}", classID, player->GetGUID().GetCounter());
            return false;
        }

        // Update the token issue amount in the database
        UpdateTokenIssueCountForPlayerClass(player, tokenCountToIssue, classID);
    }

    return true;
}

// Clears any class-specific master skills for the player, and returns the tokens
void MultiClassMod::ResetMasterSkillsForPlayerClass(Player* player, uint8 playerClass)
{
    // Get a list of master skills to delete
    list<MasterSkill> knownMasterSkillsForClass = GetKnownMasterSkillsForPlayerForClass(player, playerClass);

    // Unlearn the master skills
    for (auto& masterSkill : knownMasterSkillsForClass)
    {
        if (player->HasSpell(masterSkill.SpellID))
            player->removeSpell(masterSkill.SpellID, SPEC_MASK_ALL, false);
    }

    // Perform learn and unlearns
    PerformKnownSpellUpdateFromMasterSkills(player);

    // Update token issue counts
    RefundTokenCountForPlayerClass(player, playerClass, (uint8)knownMasterSkillsForClass.size());

    // Issue any new tokens
    PerformTokenIssuesForPlayerClass(player, playerClass);
}

map<uint8, PlayerEquipedItemData> MultiClassMod::GetVisibleItemsBySlotForPlayerClass(Player* player, uint8 classID)
{
    // Start with a list of blank inventory display slots
    map<uint8, PlayerEquipedItemData> visibleItems;
    for (uint8 i = 0; i < 18; ++i)
    {
        PlayerEquipedItemData curItem;
        curItem.ItemID = 0;
        curItem.PermEnchant = 0;
        curItem.Slot = i;
        curItem.TempEnchant = 0;
        curItem.ItemInstanceGUID = 0;
        visibleItems.insert(pair<uint8, PlayerEquipedItemData>(i, curItem));
    }

    // If current class, grab those items
    if (player->getClass() == classID)
    {
        LOG_ERROR("module", "multiclass: Getting visible item list for current player is unimplemented");
    }
    // Otherwise, retrieve from the database
    else
    {
        QueryResult queryResult = CharacterDatabase.Query("SELECT CI.`slot`, II.`itemEntry`, II.`enchantments`, II.`guid` FROM `mod_multi_class_character_inventory` CI INNER JOIN `item_instance` II on II.guid = CI.item WHERE CI.`bag` = 0 AND CI.`slot` <= 18 AND CI.`guid` = {} AND `class` = {}", player->GetGUID().GetCounter(), classID);
        if (queryResult && queryResult->GetRowCount() > 0)
        {
            do
            {
                Field* fields = queryResult->Fetch();
                uint8 slot = fields[0].Get<uint8>();
                uint32 itemID = fields[1].Get<uint32>();
                string enchantString = fields[2].Get<string>();
                uint32 itemInstanceGUID = fields[3].Get<uint32>();

                // Break out enchant values
                std::vector<std::string_view> tokens = Acore::Tokenize(enchantString, ' ', false);
                uint32 permEnchant = *Acore::StringTo<uint32>(tokens[PERM_ENCHANTMENT_SLOT * MAX_ENCHANTMENT_OFFSET]);
                uint32 tempEnchant = *Acore::StringTo<uint32>(tokens[TEMP_ENCHANTMENT_SLOT * MAX_ENCHANTMENT_OFFSET]);

                // Store
                visibleItems[slot].Slot = slot;
                visibleItems[slot].ItemID = itemID;
                visibleItems[slot].PermEnchant = permEnchant;
                visibleItems[slot].TempEnchant = tempEnchant;
                visibleItems[slot].ItemInstanceGUID = itemInstanceGUID;
            } while (queryResult->NextRow());
        }
    }

    // If we're using the transmog mod, factor for that by pulling those visuals too
    if (ConfigUsingTransmogMod)
    {
        QueryResult queryResult = CharacterDatabase.Query("SELECT `GUID`, `FakeEntry` FROM custom_transmogrification WHERE `Owner` = {}", player->GetGUID().GetCounter());
        if (queryResult && queryResult->GetRowCount() > 0)
        {
            do
            {
                Field* fields = queryResult->Fetch();
                uint32 itemInstanceGUID = fields[0].Get<uint32>();
                uint32 fakeItemID = fields[1].Get<uint32>();

                // Replace any matches
                for (auto& visibleItem : visibleItems)
                {
                    if (visibleItem.second.ItemInstanceGUID == itemInstanceGUID)
                        visibleItem.second.ItemID = fakeItemID;
                }
            } while (queryResult->NextRow());
        }
    }

    return visibleItems;
}

// Returns any class levels for classes that the player is not
map<uint8, uint8> MultiClassMod::GetClassLevelsByClassForPlayer(Player* player)
{
    // Pull the other class levels first
    map<uint8, uint8> levelsByClass;
    QueryResult classQueryResult = CharacterDatabase.Query("SELECT `class`, `level` FROM mod_multi_class_characters WHERE guid = {} AND class <> {}", player->GetGUID().GetCounter(), player->getClass());
    if (classQueryResult)
    {
        do
        {
            Field* fields = classQueryResult->Fetch();
            uint8 returnedClass = fields[0].Get<uint8>();
            uint8 returnedLevel = fields[1].Get<uint8>();
            levelsByClass.insert(pair<uint8, uint8>(returnedClass, returnedLevel));
        } while (classQueryResult->NextRow());

    }

    // Add this class level
    levelsByClass.insert(pair<uint8, uint8>(player->getClass(), player->GetLevel()));

    return levelsByClass;
}

// Returns the full class info set for the player
map<string, PlayerClassInfoItem> MultiClassMod::GetPlayerClassInfoByClassNameForPlayer(Player* player)
{
    map<string, PlayerClassInfoItem> playerClassInfoByClass;

    // Get levels for classes first, and populate the base list
    map<uint8, uint8> classLevelsByClass = GetClassLevelsByClassForPlayer(player);
    for (auto& curClassLevel : classLevelsByClass)
    {
        PlayerClassInfoItem curClassInfo;
        curClassInfo.ClassID = curClassLevel.first;
        curClassInfo.ClassName = GetClassStringFromID(curClassInfo.ClassID);
        curClassInfo.Level = curClassLevel.second;
        playerClassInfoByClass.insert(pair<string, PlayerClassInfoItem>(curClassInfo.ClassName, curClassInfo));
    }

    // Get the settings to populate the remaining data
    for (auto& playerClassInfoItem : playerClassInfoByClass)
    {
        PlayerClassSettings curClassSettings = GetPlayerClassSettings(player, playerClassInfoItem.second.ClassID);
        playerClassInfoItem.second.UseSharedQuests = curClassSettings.UseSharedQuests;
        playerClassInfoItem.second.UseSharedReputation = curClassSettings.UseSharedReputation;
    }

    return playerClassInfoByClass;
}

// Returns true if the passed spellID is a master skill
bool MultiClassMod::IsSpellAMasterSkill(uint32 spellID)
{
    if (MasterSkillsBySpellID.find(spellID) == MasterSkillsBySpellID.end())
        return false;
    else
        return true;
}

PlayerControllerData MultiClassMod::GetPlayerControllerData(Player* player)
{
    PlayerControllerData controllerData;
    controllerData.GUID = player->GetGUID().GetCounter();
    QueryResult queryResult = CharacterDatabase.Query("SELECT nextClass, activeClassQuests, activeClassReputation FROM mod_multi_class_character_controller WHERE guid = {}", player->GetGUID().GetCounter());
    if (!queryResult || queryResult->GetRowCount() == 0)
    {
        controllerData.NextClass = player->getClass();
        controllerData.ActiveClassQuests = CLASS_NONE;
        controllerData.ActiveClassReputation = CLASS_NONE;
    }
    else
    {
        Field* fields = queryResult->Fetch();
        controllerData.NextClass = fields[0].Get<uint8>();
        controllerData.ActiveClassQuests = fields[1].Get<uint8>();
        controllerData.ActiveClassReputation = fields[2].Get<uint8>();
    }
    return controllerData;
}

void MultiClassMod::SetPlayerControllerData(PlayerControllerData controllerData)
{
    CharacterDatabase.DirectExecute("REPLACE INTO `mod_multi_class_character_controller` (`guid`, `nextClass`, `activeClassQuests`, activeClassReputation) VALUES ({}, {}, {}, {})",
        controllerData.GUID,
        controllerData.NextClass,
        controllerData.ActiveClassQuests,
        controllerData.ActiveClassReputation);
}

PlayerClassSettings MultiClassMod::GetPlayerClassSettings(Player* player, uint8 classID)
{
    PlayerClassSettings classSettings;
    classSettings.GUID = player->GetGUID().GetCounter();
    classSettings.ClassID = classID;
    QueryResult queryResult = CharacterDatabase.Query("SELECT useSharedQuests, useSharedReputation FROM mod_multi_class_character_class_settings WHERE guid = {} AND class = {}", player->GetGUID().GetCounter(), classID);
    if (!queryResult || queryResult->GetRowCount() == 0)
    {
        classSettings.UseSharedQuests = true;
        classSettings.UseSharedReputation = true;
    }
    else
    {
        Field* fields = queryResult->Fetch();
        classSettings.UseSharedQuests = fields[0].Get<uint8>() == 1 ? true : false;
        classSettings.UseSharedReputation = fields[1].Get<uint8>() == 1 ? true : false;
    }
    return classSettings;
}

void MultiClassMod::SetPlayerClassSettings(PlayerClassSettings classSettings)
{
    CharacterDatabase.DirectExecute("REPLACE INTO `mod_multi_class_character_class_settings` (`guid`, `class`, `useSharedQuests`, useSharedReputation) VALUES ({}, {}, {}, {})",
        classSettings.GUID,
        classSettings.ClassID,
        classSettings.UseSharedQuests == true ? 1 : 0,
        classSettings.UseSharedReputation == true ? 1 : 0);
}

static int cntone = 0;
static int cnttwo = 0;
static int cntthree = 0;

class MultiClass_UnitScript : public UnitScript
{
public:
    MultiClass_UnitScript() : UnitScript("MultiClass_UnitScript") {}

    // TODO: Implement CLASS_CONTEXT_PET
    // Note: Client Binary Changes are needed for the following to function:
    // - Rune abilities reactivating after rune depletion and recharge
    // - Show combo points on non-druid and non-rogue
    Optional<bool> IsClass(Unit const* unit, Classes unitClass, ClassContext context)
    {
        // Ignore if not a player
        if (unit->GetTypeId() != TYPEID_PLAYER)
            return std::nullopt;

        switch (context)
        {
            // If in a druid combat shapeshift form, then use stat logic only for druid
            case CLASS_CONTEXT_STATS:
            {
                if (unit->GetShapeshiftForm() == FORM_CAT
                    || unit->GetShapeshiftForm() == FORM_BEAR
                    || unit->GetShapeshiftForm() == FORM_DIREBEAR)
                {
                    if (unitClass == CLASS_DRUID)
                        return true;
                    else
                        return std::nullopt;
                }
            } break;
            // Any class can use any base ability
            case CLASS_CONTEXT_ABILITY:
            // Any class can loot or use any equipment
            case CLASS_CONTEXT_EQUIP_RELIC:
            case CLASS_CONTEXT_EQUIP_SHIELDS:
            case CLASS_CONTEXT_EQUIP_ARMOR_CLASS:
            case CLASS_CONTEXT_EQUIP_WEAPON:
            {    
                return true;
            }
            default:
            {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }
};

class MultiClass_PlayerScript : public PlayerScript
{
public:
    MultiClass_PlayerScript() : PlayerScript("MultiClass_PlayerScript") {}

    void OnLogin(Player* player)
    {
        if (ConfigEnabled == false)
            return;

        if (ConfigEnableMasterSkills)
        {
            MultiClass->PerformKnownSpellUpdateFromMasterSkills(player);
            MultiClass->PerformTokenIssuesForPlayerClass(player, player->getClass());
        }

        if (ConfigDisplayInstructionMessage)
        {
            ChatHandler(player->GetSession()).SendSysMessage("Type |cff4CFF00.class |rto change or edit classes.");
        }
    }

    void OnBeforeLogout(Player* player)
    {
        // If a class change is in progress, update the item visuals
        PlayerControllerData controllerData = MultiClass->GetPlayerControllerData(player);
        if (controllerData.NextClass != player->getClass())
        {
            map<uint8, PlayerEquipedItemData> visibleItemsBySlot = MultiClass->GetVisibleItemsBySlotForPlayerClass(player, controllerData.NextClass);
            for (uint8 i = 0; i < 18; ++i)
            {
                if (visibleItemsBySlot[i].ItemID == 0)
                    player->SetVisibleItemSlot(i, NULL);
                else
                {
                    player->SetUInt32Value(PLAYER_VISIBLE_ITEM_1_ENTRYID + (i * 2), visibleItemsBySlot[i].ItemID);
                    player->SetUInt16Value(PLAYER_VISIBLE_ITEM_1_ENCHANTMENT + (i * 2), 0, visibleItemsBySlot[i].PermEnchant);
                    player->SetUInt16Value(PLAYER_VISIBLE_ITEM_1_ENCHANTMENT + (i * 2), 1, visibleItemsBySlot[i].TempEnchant);
                }
            }
        }
    }

    void OnLogout(Player* player)
    {
        if (ConfigEnabled == false)
            return;

        PlayerControllerData controllerData = MultiClass->GetPlayerControllerData(player);
        PlayerClassSettings nextClassSettings = MultiClass->GetPlayerClassSettings(player, controllerData.NextClass);

        // Class switch
        if (controllerData.NextClass != player->getClass())
        {
            if (!MultiClass->PerformClassSwitch(player, controllerData))
            {
                LOG_ERROR("module", "multiclass: Could not successfully complete the class switch on logout for player {} with GUID {}", player->GetName(), player->GetGUID().GetCounter());
            }
        }

        // Quests Change
        if (nextClassSettings.UseSharedQuests == true && controllerData.ActiveClassQuests != CLASS_NONE)
        {
            if (!MultiClass->PerformQuestDataSwitch(player->GetGUID().GetCounter(), controllerData.ActiveClassQuests, CLASS_NONE))
            {
                LOG_ERROR("module", "multiclass: Could not successfully perform quest data switch on logout for player {} with GUID {}", player->GetName(), player->GetGUID().GetCounter());
            }
            controllerData.ActiveClassQuests = CLASS_NONE;
            MultiClass->SetPlayerControllerData(controllerData);
        }
        else if (nextClassSettings.UseSharedQuests == false && controllerData.ActiveClassQuests != controllerData.NextClass)
        {
            if (!MultiClass->PerformQuestDataSwitch(player->GetGUID().GetCounter(), controllerData.ActiveClassQuests, controllerData.NextClass))
            {
                LOG_ERROR("module", "multiclass: Could not successfully perform quest data switch on logout for player {} with GUID {}", player->GetName(), player->GetGUID().GetCounter());
            }
            controllerData.ActiveClassQuests = controllerData.NextClass;
            MultiClass->SetPlayerControllerData(controllerData);
        }

        // Reputation Change
        if (nextClassSettings.UseSharedReputation == true && controllerData.ActiveClassReputation != CLASS_NONE)
        {
            if (!MultiClass->PerformReputationDataSwitch(player->GetGUID().GetCounter(), controllerData.ActiveClassReputation, CLASS_NONE))
            {
                LOG_ERROR("module", "multiclass: Could not successfully perform reputation data switch on logout for player {} with GUID {}", player->GetName(), player->GetGUID().GetCounter());
            }
            controllerData.ActiveClassReputation = CLASS_NONE;
            MultiClass->SetPlayerControllerData(controllerData);
        }
        else if (nextClassSettings.UseSharedReputation == false && controllerData.ActiveClassReputation != controllerData.NextClass)
        {
            if (!MultiClass->PerformReputationDataSwitch(player->GetGUID().GetCounter(), controllerData.ActiveClassReputation, controllerData.NextClass))
            {
                LOG_ERROR("module", "multiclass: Could not successfully perform reputation data switch on logout for player {} with GUID {}", player->GetName(), player->GetGUID().GetCounter());
            }
            controllerData.ActiveClassReputation = controllerData.NextClass;
            MultiClass->SetPlayerControllerData(controllerData);
        }
    }

    void OnDelete(ObjectGuid guid, uint32 /*accountId*/)
    {
        if (ConfigEnabled == false)
            return;
        MultiClass->PerformPlayerDelete(guid);        
    }

    void OnLevelChanged(Player* player, uint8 /*oldLevel*/)
    {
        if (ConfigEnabled == false)
            return;
        if (ConfigEnableMasterSkills)
        {
            MultiClass->PerformKnownSpellUpdateFromMasterSkills(player);
            MultiClass->PerformTokenIssuesForPlayerClass(player, player->getClass());
        }
    }

    void OnLearnSpell(Player* player, uint32 spellID)
    {
        if (ConfigEnabled == false)
            return;

        // Only take action if a master skill was learned
        if (ConfigEnableMasterSkills && MultiClass->IsSpellAMasterSkill(spellID))
        {
            MultiClass->PerformKnownSpellUpdateFromMasterSkills(player);
        }
    }

    void OnForgotSpell(Player* player, uint32 spellID)
    {
        if (ConfigEnabled == false)
            return;

        // Only take action if a master skill was forgotten
        if (ConfigEnableMasterSkills && MultiClass->IsSpellAMasterSkill(spellID))
        {
            MultiClass->PerformKnownSpellUpdateFromMasterSkills(player);
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

        // Instruction Message
        ConfigDisplayInstructionMessage = sConfigMgr->GetOption<bool>("MultiClass.MultiClass.DisplayInstructionMessage", true);

        // Cross Class Skills
        ConfigCrossClassIncludeSkillIDs = GetSetFromConfigString("MultiClass.CrossClassIncludeSkillIDs");

        // Using Transmog
        ConfigUsingTransmogMod = sConfigMgr->GetOption<bool>("MultiClass.UsingTransmog", true);

        // Master Skills - Enabled
        ConfigEnableMasterSkills = sConfigMgr->GetOption<bool>("MultiClass.MasterSkills.Enable", true);

        // Master Skills - Level Per Token
        ConfigLevelsPerToken = sConfigMgr->GetOption<uint8>("MultiClass.MasterSkills.LevelsPerToken", 10);

        // Master Skills - Bonus Token Levels
        ConfigBonusTokenLevels = GetSetFromConfigString("MultiClass.MasterSkills.BonusTokenLevels");

        // Class Ability Data
        if (!MultiClass->LoadClassAbilityData())
        {
            LOG_ERROR("module", "multiclass: Could not load the class ability data after the config load");
        }
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
            { "change",             SEC_PLAYER,     true, &HandleMultiClassChangeClass,             "Changes your class" },
            { "info",               SEC_PLAYER,     true, &HandleMultiClassInfo,                    "Shows all your classes, their level, and other properties" },
            { "sharequests",        SEC_PLAYER,     true, &HandleMultiClassShareQuests,             "Toggle between sharing or not sharing quests on the current class" },
            { "sharereputation",        SEC_PLAYER,     true, &HandleMultiClassShareReputation,     "Toggle between sharing or not sharing reputation on the current class" },
            { "resetmasterskills",  SEC_PLAYER,     true, &HandleMultiClassMasterSkillReset,        "Resets spent master tokens for a class" },
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
        player->SaveToDB(false, false);
        
        // Class change accepted
        return true;
    }

    static bool HandleMultiClassInfo(ChatHandler* handler, const char* /*args*/)
    {
        if (ConfigEnabled == false)
            return true;

        handler->PSendSysMessage("Class List:");

        // Get the player data
        Player* player = handler->GetPlayer();
        map<string, PlayerClassInfoItem> playerClassInfoItems = MultiClass->GetPlayerClassInfoByClassNameForPlayer(player);

        // Write the information out
        for (auto& playerClassInfoItem : playerClassInfoItems)
        {
            string currentLine = " - " + playerClassInfoItem.second.ClassName + "(" + std::to_string(playerClassInfoItem.second.Level) + "), Shared: Quests(";
            if (playerClassInfoItem.second.UseSharedQuests)
                currentLine += "|cff4CFF00ON|r";
            else
                currentLine += "|cffff0000OFF|r";
            currentLine += "), Reputation(";
            if (playerClassInfoItem.second.UseSharedReputation)
                currentLine += "|cff4CFF00ON|r";
            else
                currentLine += "|cffff0000OFF|r";
            currentLine += ")";
            handler->PSendSysMessage(currentLine.c_str());
        }

        return true;
    }

    static bool HandleMultiClassShareQuests(ChatHandler* handler, const char* args)
    {
        if (ConfigEnabled == false)
            return true;

        if (!*args)
        {
            handler->PSendSysMessage(".class sharequests 'on/off'.  Default is ON");
            handler->PSendSysMessage("Toggles on/off if the currently played class has its own quest log.  Example: '.class sharequests off'");
            handler->PSendSysMessage("Requires logging out to take effect");
            return true;
        }

        std::string enteredValue = strtok((char*)args, " ");
        if (enteredValue.starts_with("ON") || enteredValue.starts_with("on") || enteredValue.starts_with("On"))
        {
            Player* player = handler->GetPlayer();
            if (MultiClass->MarkChangeQuestShareForCurrentClassOnNextLogout(player, true) == true)
            {
                handler->PSendSysMessage("Success. Shared quests will be used on this class next login");
                return true;
            }
            else
            {
                handler->PSendSysMessage("Share Quests is already 'on' for this class, so no action is taken");
                return true;
            }
        }
        else if (enteredValue.starts_with("OF") || enteredValue.starts_with("Of") || enteredValue.starts_with("of"))
        {
            Player* player = handler->GetPlayer();
            if (MultiClass->MarkChangeQuestShareForCurrentClassOnNextLogout(player, false) == true)
            {
                handler->PSendSysMessage("Success. Shared quests will no longer be used on this class next login");
                return true;
            }
            else
            {
                handler->PSendSysMessage("Share Quests is already 'off' for this class, so no action is taken");
                return true;
            }
        }
        else
        {
            handler->PSendSysMessage(".class sharequests 'on/off'.  Default is ON");
            handler->PSendSysMessage("Toggles on/off if the currently played class has its own quest log.  Example: '.class sharequests off'");
            handler->PSendSysMessage("Valid Values: on, off.");
            std::string enteredValueLine = "Entered Value was ";
            enteredValueLine.append(enteredValue);
            handler->PSendSysMessage(enteredValueLine.c_str());
            return true;
        }
    }

    static bool HandleMultiClassShareReputation(ChatHandler* handler, const char* args)
    {
        if (ConfigEnabled == false)
            return true;

        if (!*args)
        {
            handler->PSendSysMessage(".class sharereputation 'on/off'.  Default is ON");
            handler->PSendSysMessage("Toggles on/off if the currently played class has its own reputations.  Example: '.class sharereputation off'");
            handler->PSendSysMessage("Requires logging out to take effect");
            return true;
        }

        std::string enteredValue = strtok((char*)args, " ");
        if (enteredValue.starts_with("ON") || enteredValue.starts_with("on") || enteredValue.starts_with("On"))
        {
            Player* player = handler->GetPlayer();
            if (MultiClass->MarkChangeReputationShareForCurrentClassOnNextLogout(player, true) == true)
            {
                handler->PSendSysMessage("Success. Shared reputation will be used on this class next login");
                return true;
            }
            else
            {
                handler->PSendSysMessage("Share reputation is already 'on' for this class, so no action is taken");
                return true;
            }
        }
        else if (enteredValue.starts_with("OF") || enteredValue.starts_with("Of") || enteredValue.starts_with("of"))
        {
            Player* player = handler->GetPlayer();
            if (MultiClass->MarkChangeReputationShareForCurrentClassOnNextLogout(player, false) == true)
            {
                handler->PSendSysMessage("Success. Shared reputation will no longer be used on this class next login");
                return true;
            }
            else
            {
                handler->PSendSysMessage("Share reputation is already 'off' for this class, so no action is taken");
                return true;
            }
        }
        else
        {
            handler->PSendSysMessage(".class sharereputation 'on/off'.  Default is ON");
            handler->PSendSysMessage("Toggles on/off if the currently played class has its own reputations.  Example: '.class sharereputation off'");
            handler->PSendSysMessage("Requires logging out to take effect");
            std::string enteredValueLine = "Entered Value was ";
            enteredValueLine.append(enteredValue);
            handler->PSendSysMessage(enteredValueLine.c_str());
            return true;
        }
    }

    static bool HandleMultiClassMasterSkillReset(ChatHandler* handler, const char* args)
    {
        if (ConfigEnabled == false)
            return true;
        if (!ConfigEnableMasterSkills)
        {
            handler->PSendSysMessage("MasterSkills are not enabled");
            return true;
        }

        if (!*args)
        {
            handler->PSendSysMessage(".class resetmasterskills 'class'");
            handler->PSendSysMessage("Removes and learned master skills for the passed class, refunding the tokens. Example: '.class resetmasterskills warrior'");
            handler->PSendSysMessage("Valid Class Values: warrior, paladin, hunter, rogue, priest, shaman, mage, warlock, druid");
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
        //else if (className.starts_with("De") || className.starts_with("de") || className.starts_with("DE"))
        //    classInt = CLASS_DEATH_KNIGHT;
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
            handler->PSendSysMessage(".class resetmasterskills 'class'");
            handler->PSendSysMessage("Removes and learned master skills for the passed class, refunding the tokens. Example: '.class resetmasterskills warrior'");
            handler->PSendSysMessage("Valid Class Values: warrior, paladin, hunter, rogue, priest, shaman, mage, warlock, druid");
            std::string enteredValueLine = "Entered Value was ";
            enteredValueLine.append(className);
            handler->PSendSysMessage(enteredValueLine.c_str());
            return true;
        }

        Player* player = handler->GetPlayer();
        MultiClass->ResetMasterSkillsForPlayerClass(player, classInt);
        player->SaveToDB(false, false);

        // Class change accepted
        return true;
    }
};

std::string GetClassStringFromID(uint8 classID)
{
    switch (classID)
    {
    case CLASS_WARRIOR:     return "Warrior";
    case CLASS_PALADIN:     return "Paladin";
    case CLASS_HUNTER:      return "Hunter";
    case CLASS_ROGUE:       return "Rogue";
    case CLASS_PRIEST:      return "Priest";
    case CLASS_DEATH_KNIGHT:return "Death Knight";
    case CLASS_SHAMAN:      return "Shaman";
    case CLASS_MAGE:        return "Mage";
    case CLASS_WARLOCK:     return "Warlock";
    case CLASS_DRUID:       return "Druid";
    default:                return "Unknown";
    }
}

uint32 GetTokenItemIDForClass(uint8 classID)
{
    switch (classID)
    {
    case CLASS_WARRIOR:     return 81207;
    case CLASS_PALADIN:     return 81203;
    case CLASS_HUNTER:      return 81201;
    case CLASS_ROGUE:       return 81205;
    case CLASS_PRIEST:      return 81208;
    case CLASS_DEATH_KNIGHT:return 81204;
    case CLASS_SHAMAN:      return 81206;
    case CLASS_MAGE:        return 81202;
    case CLASS_WARLOCK:     return 81210;
    case CLASS_DRUID:       return 81209;
    default:                return 0;
    }
}

void AddMultiClassScripts()
{
    new MultiClass_CommandScript();
    new MultiClass_PlayerScript();
    new MultiClass_WorldScript();
    new MultiClass_UnitScript();
}
