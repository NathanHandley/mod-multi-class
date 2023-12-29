
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

using namespace Acore::ChatCommands;
using namespace std;

static bool ConfigEnabled = true;
static uint8 CrossClassAbilityLevelGap = 10; // TODO: Load from config

MultiClassMod::MultiClassMod() //: mIsInitialized(false)
{

}

MultiClassMod::~MultiClassMod()
{

}

// (Re)populates the ability data for the classes
bool MultiClassMod::LoadClassAbilityData()
{
    // Clear old
    ClassSpellsByClass.clear();

    // Cache the mod for talent level calculations
    float talentRateMod = 1.0f;
    if (sWorld->getRate(RATE_TALENT) > 1.0f)
        talentRateMod = 1.0f / sWorld->getRate(RATE_TALENT);

    // Pull in all the new data
    QueryResult queryResult = WorldDatabase.Query("SELECT `SpellID`, `SpellName`, `SpellSubText`, `DefaultReqLevel`, `Class`, `Side`, `IsTalent`, `IsLearnedByTalent` FROM mod_multi_class_spells ORDER BY `Class`, `SpellID`");
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
        curClassData.DefaultReqLevel = fields[3].Get<uint16>();
        uint16 curClass = fields[4].Get<uint16>();
        std:string curFactionAllowed = fields[5].Get<std::string>();
        curClassData.IsTalent = fields[6].Get<bool>();
        curClassData.IsLearnedByTalent = fields[7].Get<bool>();

        // Calculate the level gap for knowing a spell cross-class
        if (curClassData.IsLearnedByTalent && curClassData.DefaultReqLevel >= 11 && sWorld->getRate(RATE_TALENT) > 1.0f)
        {
            curClassData.ModifiedReqLevel = (uint16)((float)(curClassData.DefaultReqLevel - 10) * talentRateMod) + CrossClassAbilityLevelGap + 10;
        }
        else
            curClassData.ModifiedReqLevel = curClassData.DefaultReqLevel + CrossClassAbilityLevelGap;

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
            LOG_ERROR("module", "multiclass: Could not interpret the race, value passed was {}", curClass);
            curClassData.AllowAlliance = false;
            curClassData.AllowHorde = false;
        }

        // Add to the appropriate class trainer list
        // TODO: Is this needed?
        ///if (ClassTrainerDataByClass.find(curClass) == ClassTrainerDataByClass.end())
        //ClassTrainerDataByClass[curClass] = std::list<MultiClassTrainerClassData>();
        ClassSpellsByClass[curClass].push_back(curClassData);
    } while (queryResult->NextRow());
    return true;
}

bool MultiClassMod::MarkClassChangeOnNextLogout(ChatHandler* handler, Player* player, uint8 newClass)
{
    //uint32 curRaceClassgender = player->GetUInt32Value(UNIT_FIELD_BYTES_0);
    //player->SetUInt32Value(UNIT_FIELD_BYTES_0, (curRaceClassgender | (newClass << 8)));
    //uint32 RaceClassGender = (RACE_HUMAN) | (newClass << 8) | (GENDER_FEMALE << 16);
    //player->SetUInt32Value(UNIT_FIELD_BYTES_0, RaceClassGender);

   /* uint8 curRace = player->getRace();
    uint8 curGender = player->getGender();
    uint32 RaceClassGender = curRace | (newClass << 8) | (curGender << 16);
    player->SetUInt32Value(UNIT_FIELD_BYTES_0, RaceClassGender);*/

 //   player->SaveToDB(false, false);

    
//    QueryResult queryResult = CharacterDatabase.Query("SELECT 'nextclass' FROM `mod_multi_class_next_switch_class` WHERE 'guid' = {}", player->GetGUID().GetCounter());
//    if (queryResult && queryResult->GetRowCount() > 0)
    // Delete the switch row if it's already there
    CharacterDatabase.Execute("DELETE FROM `mod_multi_class_next_switch_class` WHERE guid = {}", player->GetGUID().GetCounter());

    // Don't do anything if we're already that class
    if (newClass == player->getClass())
    {
        handler->PSendSysMessage("Class change requested is the current class, so taking no action on the next login.");
        return true;
    }

    // Add the switch event
    CharacterDatabase.Execute("INSERT INTO `mod_multi_class_next_switch_class` (guid, nextclass) VALUES ({}, {})", player->GetGUID().GetCounter(), newClass);
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

bool MultiClassMod::PerformQueuedClassSwitch(Player* player)
{
    // Only take action if there's a class switch queued
    QueryResult queryResult = CharacterDatabase.Query("SELECT nextclass FROM `mod_multi_class_next_switch_class` WHERE guid = {}", player->GetGUID().GetCounter());
    if (!queryResult || queryResult->GetRowCount() == 0)
        return true;
    Field* fields = queryResult->Fetch();
    uint8 nextClass = fields[0].Get<uint8>();
    CharacterDatabase.Execute("DELETE FROM `mod_multi_class_next_switch_class` WHERE guid = {}", player->GetGUID().GetCounter());
    if (nextClass == player->getClass())
        return true;

    // Gather the current class
    uint8 curClass = player->getClass();

    // Determine if this is a new class or not
    bool isNewClass = true;
    if (DoesSavedClassDataExistForPlayer(player, nextClass))
        isNewClass = false;

    // Switch the various types of data
    if (!SwitchClassCoreData(player, curClass, nextClass, isNewClass))
    {
        LOG_ERROR("module", "multiclass: Could not switch core class data {} for player named {} with guid {}", nextClass, player->GetName(), player->GetGUID().GetCounter());
        return false;
    }
    if (!SwitchClassSkillData(player, curClass, nextClass, isNewClass))
    {
        LOG_ERROR("module", "multiclass: Could not switch skill class data for class {} for player named {} with guid {}", nextClass, player->GetName(), player->GetGUID().GetCounter());
        return false;
    }
    if (!SwitchClassTalentData(player, curClass, nextClass, isNewClass))
    {
        LOG_ERROR("module", "multiclass: Could not switch talent class data for class {} for player named {} with guid {}", nextClass, player->GetName(), player->GetGUID().GetCounter());
        return false;
    }
    if (!SwitchClassSpellData(player, curClass, nextClass, isNewClass))
    {
        LOG_ERROR("module", "multiclass: Could not switch spell class data for class {} for player named {} with guid {}", nextClass, player->GetName(), player->GetGUID().GetCounter());
        return false;
    }
    if (!SwitchClassActionBarData(player, curClass, nextClass, isNewClass))
    {
        LOG_ERROR("module", "multiclass: Could not switch action bar class data for class {} for player named {} with guid {}", nextClass, player->GetName(), player->GetGUID().GetCounter());
        return false;
    }
    if (!SwitchClassGlyphData(player, curClass, nextClass, isNewClass))
    {
        LOG_ERROR("module", "multiclass: Could not switch glyph class data for class {} for player named {} with guid {}", nextClass, player->GetName(), player->GetGUID().GetCounter());
        return false;
    }
    if (!SwitchClassAuraData(player, curClass, nextClass, isNewClass))
    {
        LOG_ERROR("module", "multiclass: Could not switch aura class data for class {} for player named {} with guid {}", nextClass, player->GetName(), player->GetGUID().GetCounter());
        return false;
    }
    if (!SwitchClassEquipmentData(player, curClass, nextClass, isNewClass))
    {
        LOG_ERROR("module", "multiclass: Could not switch equipment class data for class {} for player named {} with guid {}", nextClass, player->GetName(), player->GetGUID().GetCounter());
        return false;
    }
    
    return true;
}

bool MultiClassMod::DoesSavedClassDataExistForPlayer(Player* player, uint8 lookupClass)
{
    QueryResult queryResult = CharacterDatabase.Query("SELECT guid, class FROM mod_multi_class_characters WHERE guid = {} AND class = {}", player->GetGUID().GetCounter(), lookupClass);
    if (!queryResult || queryResult->GetRowCount() == 0)
        return false;
    return true;
}

bool MultiClassMod::SwitchClassCoreData(Player* player, uint8 oldClass, uint8 newClass, bool isNew)
{
    // Clear out any old version of the data for this class in the mod table, and copy in fresh
    CharacterDatabase.Execute("DELETE FROM `mod_multi_class_characters` WHERE guid = {} and class = {}", player->GetGUID().GetCounter(), oldClass);
    CharacterDatabase.Execute("INSERT INTO mod_multi_class_characters (guid, class, `level`, xp, leveltime, rest_bonus, resettalents_cost, resettalents_time, health, power1, power2, power3, power4, power5, power6, power7, talentGroupsCount, activeTalentGroup) SELECT {}, {}, `level`, xp, leveltime, rest_bonus, resettalents_cost, resettalents_time, health, power1, power2, power3, power4, power5, power6, power7, talentGroupsCount, activeTalentGroup FROM characters WHERE guid = {}", player->GetGUID().GetCounter(), oldClass, player->GetGUID().GetCounter());

    // New
    if (isNew)
    {
        // Level
        uint32 startLevel = newClass != CLASS_DEATH_KNIGHT
            ? sWorld->getIntConfig(CONFIG_START_PLAYER_LEVEL)
            : sWorld->getIntConfig(CONFIG_START_HEROIC_PLAYER_LEVEL);

        // For health and mana
        PlayerClassLevelInfo classInfo;
        sObjectMgr->GetPlayerClassLevelInfo(newClass, startLevel, &classInfo);

        // Set the new character data
        CharacterDatabase.Execute("UPDATE characters SET `class` = {},	`level` = {}, `xp` = 0, `leveltime` = 0, `rest_bonus` = 0, `resettalents_cost` = 0, `resettalents_time` = 0, health = {}, power1 = {}, power2 = 0, power3 = 0, power4 = 100, power5 = 0, power6 = 0, power7 = 0, `talentGroupsCount` = 1, `activeTalentGroup` = 0 WHERE guid = {}", newClass, startLevel, classInfo.basehealth, classInfo.basemana, player->GetGUID().GetCounter());
    }
    // Existing
    else
    {
        // Copy in the stored version for existing
        CharacterDatabase.Execute("UPDATE characters, mod_multi_class_characters SET characters.`class` = mod_multi_class_characters.`class`, characters.`level` = mod_multi_class_characters.`level`, characters.`xp` = mod_multi_class_characters.`xp`, characters.`leveltime` = mod_multi_class_characters.`leveltime`, characters.`rest_bonus` = mod_multi_class_characters.`rest_bonus`, characters.`resettalents_cost` = mod_multi_class_characters.`resettalents_cost`, characters.`resettalents_time` = mod_multi_class_characters.`resettalents_time`, characters.`health` = mod_multi_class_characters.`health`, characters.`power1` = mod_multi_class_characters.`power1`, characters.`power2` = mod_multi_class_characters.`power2`, characters.`power3` = mod_multi_class_characters.`power3`, characters.`power4` = mod_multi_class_characters.`power4`, characters.`power5` = mod_multi_class_characters.`power5`, characters.`power6` = mod_multi_class_characters.`power6`, characters.`power7` = mod_multi_class_characters.`power7`, characters.`talentGroupsCount` = mod_multi_class_characters.`talentGroupsCount`, characters.`activeTalentGroup` = mod_multi_class_characters.`activeTalentGroup` WHERE characters.`guid` = mod_multi_class_characters.`guid` AND mod_multi_class_characters.`class` = {} AND mod_multi_class_characters.`guid` = {}", newClass, player->GetGUID().GetCounter());
    }

    return true;
}

bool MultiClassMod::SwitchClassTalentData(Player* player, uint8 oldClass, uint8 newClass, bool isNew)
{
    // Clear out any old version of the data for this class in the mod table, and copy in fresh
    CharacterDatabase.Execute("DELETE FROM `mod_multi_class_character_talent` WHERE guid = {} and class = {}", player->GetGUID().GetCounter(), oldClass);
    CharacterDatabase.Execute("INSERT INTO mod_multi_class_character_talent (guid, class, spell, specMask) SELECT guid, {}, spell, specMask FROM character_talent WHERE guid = {}", oldClass, player->GetGUID().GetCounter());

    // Delete the active version of talent data
    CharacterDatabase.Execute("DELETE FROM `character_talent` WHERE guid = {}", player->GetGUID().GetCounter());

    // New
    if (isNew)
    {
        player->InitTalentForLevel();
    }
    // Existing
    else
    {
        // Copy in the stored version for existing
        CharacterDatabase.Execute("INSERT INTO character_talent (guid, spell, specMask) SELECT guid, spell, specMask FROM mod_multi_class_character_talent WHERE guid = {} AND class = {}", player->GetGUID().GetCounter(), newClass);
    }

    return true;
}

bool MultiClassMod::SwitchClassSpellData(Player* player, uint8 oldClass, uint8 newClass, bool isNew)
{
    // Clear out any old version of the data for this class in the mod table, and copy in fresh
    CharacterDatabase.Execute("DELETE FROM `mod_multi_class_character_spell` WHERE guid = {} and class = {}", player->GetGUID().GetCounter(), oldClass);
    CharacterDatabase.Execute("INSERT INTO mod_multi_class_character_spell (guid, class, spell, specMask) SELECT guid, {}, spell, specMask FROM character_spell WHERE GUID = {}", oldClass, player->GetGUID().GetCounter());

    // Clear out the list of spells
    // TODO: Make this more dynamic with a lookup to allow cross-class abilities
    CharacterDatabase.Execute("DELETE FROM `character_spell` WHERE guid = {}", player->GetGUID().GetCounter(), oldClass);

    // New
    if (isNew)
    {
        // Generate a new list of spells for this class
        player->LearnCustomSpells();
    }
    // Existing
    else
    {
        // Copy in the existing spells
        CharacterDatabase.Execute("INSERT INTO character_spell (guid, spell, specMask) SELECT guid, spell, specMask FROM mod_multi_class_character_spell WHERE GUID = {} AND class = {}", player->GetGUID().GetCounter(), newClass);
    }

    return true;
}

bool MultiClassMod::SwitchClassSkillData(Player* player, uint8 oldClass, uint8 newClass, bool isNew)
{
    // Clear out any old version of the data for this class in the mod table, and copy in fresh
    CharacterDatabase.Execute("DELETE FROM `mod_multi_class_character_skills` WHERE guid = {} and class = {}", player->GetGUID().GetCounter(), oldClass);
    CharacterDatabase.Execute("INSERT INTO mod_multi_class_character_skills (guid, class, skill, value, max) SELECT guid, {}, skill, value, max FROM character_skills WHERE GUID = {}", oldClass, player->GetGUID().GetCounter());

    // Delete the active version of skill data
    CharacterDatabase.Execute("DELETE FROM `character_skills` WHERE guid = {}", player->GetGUID().GetCounter());

    // New
    if (isNew)
    {
        // Generate a new list of skills for this class
        player->LearnDefaultSkills();
        player->UpdateSkillsForLevel();
    }
    // Existing
    else
    {
        // Copy the stored version in
        CharacterDatabase.Execute("INSERT INTO character_skills (guid, skill, `value`, `max`) SELECT guid, skill, `value`, `max` FROM mod_multi_class_character_skills WHERE GUID = {} AND class = {}", player->GetGUID().GetCounter(), newClass);
    }

    return true;
}

bool MultiClassMod::SwitchClassActionBarData(Player* player, uint8 oldClass, uint8 newClass, bool isNew)
{
    // Clear out any old version of the data for this class in the mod table, and copy in fresh
    CharacterDatabase.Execute("DELETE FROM `mod_multi_class_character_action` WHERE guid = {} and class = {}", player->GetGUID().GetCounter(), oldClass);
    CharacterDatabase.Execute("INSERT INTO mod_multi_class_character_action (guid, class, spec, button, `action`, `type`) SELECT guid, {}, spec, button, `action`, `type` FROM character_action WHERE guid = {}", oldClass, player->GetGUID().GetCounter());

    // Delete the active version of action bar data
    CharacterDatabase.Execute("DELETE FROM `character_action` WHERE guid = {}", player->GetGUID().GetCounter());

    // New
    if (isNew)
    {
        // Set up new action bars
        PlayerInfo const* info = sObjectMgr->GetPlayerInfo(player->getRace(), newClass);
        if (!info)
        {
            LOG_ERROR("module", "multiclass: Could not pull PlayerInfo for race: {} class: {} for guid {} in order to set up action bars", player->getRace(), newClass, player->GetGUID().GetCounter());
        }
        else
        {
            for (PlayerCreateInfoActions::const_iterator action_itr = info->action.begin(); action_itr != info->action.end(); ++action_itr)
                player->addActionButton(action_itr->button, action_itr->action, action_itr->type);
        }
    }
    // Existing
    else
    {
        // Copy the stored version in
        CharacterDatabase.Execute("INSERT INTO character_action (guid, spec, button, `action`, `type`) SELECT guid, spec, button, `action`, `type` FROM mod_multi_class_character_action WHERE guid = {} AND class = {}", player->GetGUID().GetCounter(), newClass);
    }

    return true;
}

bool MultiClassMod::SwitchClassGlyphData(Player* player, uint8 oldClass, uint8 newClass, bool isNew)
{
    // Clear out any old version of the data for this class in the mod table, and copy in fresh
    CharacterDatabase.Execute("DELETE FROM `mod_multi_class_character_glyphs` WHERE guid = {} and class = {}", player->GetGUID().GetCounter(), oldClass);
    CharacterDatabase.Execute("INSERT INTO mod_multi_class_character_glyphs (guid, class, talentGroup, glyph1, glyph2, glyph3, glyph4, glyph5, glyph6) SELECT guid, {}, talentGroup, glyph1, glyph2, glyph3, glyph4, glyph5, glyph6 FROM character_glyphs WHERE guid = {}", oldClass, player->GetGUID().GetCounter());

    // Delete the active version of glyph data
    CharacterDatabase.Execute("DELETE FROM `character_glyphs` WHERE guid = {}", player->GetGUID().GetCounter());

    // New
    if (isNew)
    {
        player->InitGlyphsForLevel();
    }
    else
    {
        // Copy the stored version in
        CharacterDatabase.Execute("INSERT INTO character_glyphs (guid, talentGroup, glyph1, glyph2, glyph3, glyph4, glyph5, glyph6) SELECT guid, talentGroup, glyph1, glyph2, glyph3, glyph4, glyph5, glyph6 FROM mod_multi_class_character_glyphs WHERE guid = {} AND class = {}", player->GetGUID().GetCounter(), newClass);
    }

    return true;
}

bool MultiClassMod::SwitchClassAuraData(Player* player, uint8 oldClass, uint8 newClass, bool isNew)
{
    // Clear out any old version of the data for this class in the mod table, and copy in fresh
    CharacterDatabase.Execute("DELETE FROM `mod_multi_class_character_aura` WHERE guid = {} and class = {}", player->GetGUID().GetCounter(), oldClass);
    CharacterDatabase.Execute("INSERT INTO mod_multi_class_character_aura (guid, class, casterGuid, itemGuid, spell, effectMask, recalculateMask, stackCount, amount0, amount1, amount2, base_amount0, base_amount1, base_amount2, maxDuration, remainTime, remainCharges) SELECT guid, {}, casterGuid, itemGuid, spell, effectMask, recalculateMask, stackCount, amount0, amount1, amount2, base_amount0, base_amount1, base_amount2, maxDuration, remainTime, remainCharges FROM character_aura WHERE guid = {}", oldClass, player->GetGUID().GetCounter());

    // Delete the active version
    CharacterDatabase.Execute("DELETE FROM `character_aura` WHERE guid = {}", player->GetGUID().GetCounter());

    // Existing
    if (!isNew)
    {
        // Copy in existing auras
        CharacterDatabase.Execute("INSERT INTO character_aura (guid, casterGuid, itemGuid, spell, effectMask, recalculateMask, stackCount, amount0, amount1, amount2, base_amount0, base_amount1, base_amount2, maxDuration, remainTime, remainCharges) SELECT guid, casterGuid, itemGuid, spell, effectMask, recalculateMask, stackCount, amount0, amount1, amount2, base_amount0, base_amount1, base_amount2, maxDuration, remainTime, remainCharges FROM mod_multi_class_character_aura WHERE guid = {} AND class = {}", player->GetGUID().GetCounter(), newClass);
    }    

    return true;
}

bool MultiClassMod::SwitchClassEquipmentData(Player* player, uint8 oldClass, uint8 newClass, bool isNew)
{
    // New
    if (isNew)
    {


    }
    // Existing
    else
    {

    }

    // TODO

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
    }

    void OnLogout(Player* player)
    {
        if (ConfigEnabled == false)
            return;

        if (!MultiClass->PerformQueuedClassSwitch(player))
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
        ConfigEnabled = sConfigMgr->GetOption<bool>("MultiClass.Enable", true);
        if (!MultiClass->LoadClassAbilityData())
        {
            LOG_ERROR("module", "multiclass: Could not load the class ability data after the config load");
        }
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
        if (!MultiClass->MarkClassChangeOnNextLogout(handler, player, classInt))
        {
            LOG_ERROR("module", "multiclass: Could not change class to {}", classInt);
            handler->PSendSysMessage("ERROR CHANGING CLASS");
        }

        return true;
    }
};

void AddMultiClassScripts()
{
    new MultiClass_CommandScript();
    new MultiClass_PlayerScript();
    new MultiClass_WorldScript();
}
