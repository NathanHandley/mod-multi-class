
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

bool MultiClassMod::PerformAnyQueuedClassSwitch(Player* player)
{
    // Only take action if there's a class to switch to
    QueryResult queryResult = CharacterDatabase.Query("SELECT nextclass FROM `mod_multi_class_next_switch_class` WHERE guid = {}", player->GetGUID().GetCounter());
    if (!queryResult || queryResult->GetRowCount() == 0)
        return true;
    Field* fields = queryResult->Fetch();
    uint8 nextClass = fields[0].Get<uint8>();
    CharacterDatabase.Execute("DELETE FROM `mod_multi_class_next_switch_class` WHERE guid = {}", player->GetGUID().GetCounter());
    if (nextClass == player->getClass())
        return true;

    // Save old class values
    if (!SavePlayerCurrentClassData(player))
    {
        LOG_ERROR("module", "multiclass: Could not successfuly save the current player class data for class {} for player named {} with guid {}", nextClass, player->GetName(), player->GetGUID().GetCounter());
        return false;
    }

    // Set to the new class
    if (!ChangeActivePlayerClass(player, nextClass))
    {
        LOG_ERROR("module", "multiclass: Could not set the new class of {} for player named {} with guid {}", nextClass, player->GetName(), player->GetGUID().GetCounter());
        return false;
    }

    return true;
}

bool MultiClassMod::SavePlayerCurrentClassData(Player* player)
{
    uint8 curClass = player->getClass();

    // Talent data
    CharacterDatabase.Execute("DELETE FROM `mod_multi_class_character_talent` WHERE guid = {} and class = {}", player->GetGUID().GetCounter(), curClass);
    CharacterDatabase.Execute("INSERT INTO mod_multi_class_character_talent (guid, class, spell, specMask) SELECT {}, {}, spell, specMask FROM character_talent WHERE guid = {}", player->GetGUID().GetCounter(), curClass, player->GetGUID().GetCounter());

    // Aura Data
    CharacterDatabase.Execute("DELETE FROM `mod_multi_class_character_aura` WHERE guid = {} and class = {}", player->GetGUID().GetCounter(), curClass);
    CharacterDatabase.Execute("INSERT INTO mod_multi_class_character_aura (guid, class, casterGuid, itemGuid, spell, effectMask, recalculateMask, stackCount, amount0, amount1, amount2, base_amount0, base_amount1, base_amount2, maxDuration, remainTime, remainCharges) SELECT {}, {}, casterGuid, itemGuid, spell, effectMask, recalculateMask, stackCount, amount0, amount1, amount2, base_amount0, base_amount1, base_amount2, maxDuration, remainTime, remainCharges FROM character_aura WHERE guid = {}", player->GetGUID().GetCounter(), curClass, player->GetGUID().GetCounter());

    // Spell Data
    CharacterDatabase.Execute("DELETE FROM `mod_multi_class_character_spell` WHERE guid = {} and class = {}", player->GetGUID().GetCounter(), curClass);
    CharacterDatabase.Execute("INSERT INTO mod_multi_class_character_spell (guid, class, spell, specMask) SELECT {}, {}, spell, specMask FROM character_spell WHERE GUID = {}", player->GetGUID().GetCounter(), curClass, player->GetGUID().GetCounter());

    // Skill Data
    CharacterDatabase.Execute("DELETE FROM `mod_multi_class_character_skills` WHERE guid = {} and class = {}", player->GetGUID().GetCounter(), curClass);
    CharacterDatabase.Execute("INSERT INTO mod_multi_class_character_skills (guid, class, skill, value, max) SELECT {}, {}, skill, value, max FROM character_skills WHERE GUID = {}", player->GetGUID().GetCounter(), curClass, player->GetGUID().GetCounter());

    // Base Character Data
    CharacterDatabase.Execute("DELETE FROM `mod_multi_class_characters` WHERE guid = {} and class = {}", player->GetGUID().GetCounter(), curClass);
    CharacterDatabase.Execute("INSERT INTO mod_multi_class_characters (guid, class, `level`, xp, restState, leveltime, rest_bonus, resettalents_cost, resettalents_time, talentGroupsCount, activeTalentGroup) SELECT {}, {}, `level`, xp, restState, leveltime, rest_bonus, resettalents_cost, resettalents_time, talentGroupsCount, activeTalentGroup FROM characters WHERE guid = {}", player->GetGUID().GetCounter(), curClass, player->GetGUID().GetCounter());

    return true;
}

bool MultiClassMod::ChangeActivePlayerClass(Player* player, uint8 newClass)
{
    // If the class is a new class, then create new class data

    // If the class data is a saved class, then replace current class data with saved class data
        // Delete the old copy of the saved class data

    return true;
}

bool MultiClassMod::DoesSavedClassDataExistForPlayer(Player* player, uint8 lookupClass)
{
    QueryResult queryResult = CharacterDatabase.Query("SELECT guid, class FROM mod_multi_class_characters WHERE guid = {} AND class = {}", player->GetGUID().GetCounter(), lookupClass);
    if (!queryResult || queryResult->GetRowCount() == 0)
        return false;
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

	    ChatHandler(player->GetSession()).SendSysMessage("This server is running the |cff4CFF00Multi Class |rmodule.");
    }

    void OnLogout(Player* player)
    {
        if (!MultiClass->PerformAnyQueuedClassSwitch(player))
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

//class MultiClass_CommandScript : public CommandScript
//{
// TODO: Add command to 'reload from database'
//public:
//    MultiClass_CommandScript() : CommandScript("MultiClass_CommandScript") { }
//
//    ChatCommandTable GetCommands() const override
//    {
//        static ChatCommandTable acaCommandTable =
//        {
//            { "test",     HandleACATestCommand,   SEC_PLAYER, Console::Yes }
//        };
//        static ChatCommandTable commandTable =
//        {
//            { "mct", acaCommandTable }
//        };
//        return commandTable;
//    }
//
//    static bool HandleACATestCommand(ChatHandler* handler, const char* args)
//    {
//        Player* player = handler->GetPlayer();
//        ChatHandler(player->GetSession()).SendSysMessage("This is a test");
//        return true;
//    }
//};

class MultiClass_CommandScript : public CommandScript
{
public:
    MultiClass_CommandScript() : CommandScript("MultiClass_CommandScript") { }

    std::vector<ChatCommand> GetCommands() const
    {
        static std::vector<ChatCommand> ABCommandTable =
        {
            { "changeclass",        SEC_PLAYER,                            true, &HandleMultiClassChangeClass,              "Changes a class to the passed class" },
        };

        static std::vector<ChatCommand> commandTable =
        {
            { "multiclass",       SEC_PLAYER,                             false, NULL,                      "", ABCommandTable },
        };
        return commandTable;
    }

    static bool HandleMultiClassChangeClass(ChatHandler* handler, const char* args)
    {
        if (!*args)
        {
            handler->PSendSysMessage(".multiclass changeclass 'class'");
            handler->PSendSysMessage("Changes the player class on next logout.  Example: '.multiclass changeclass warrior'");
            handler->PSendSysMessage("Valid Class Values: warrior, paladin, hunter, rogue, priest, deathknight, shaman, mage warlock, druid");
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
            handler->PSendSysMessage(".multiclass changeclass 'class'");
            handler->PSendSysMessage("Changes the player class.  Example: '.multiclass changeclass warrior'");
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
    new MultiClass_PlayerScript();
    new MultiClass_WorldScript();
    //new MultiClass_CommandScript();
    new MultiClass_CommandScript();
}
