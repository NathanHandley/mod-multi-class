
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

#include "MasterClassTrainers.h"

using namespace Acore::ChatCommands;
using namespace std;

static bool ConfigEnabled;

MasterClassTrainersMod::MasterClassTrainersMod() //: mIsInitialized(false)
{

}

MasterClassTrainersMod::~MasterClassTrainersMod()
{

}

// (Re)populates the master class trainer data
bool MasterClassTrainersMod::LoadClassTrainerData()
{
    // Clear old
    ClassTrainerDataByClass.clear();

    // Pull in all the new data
    QueryResult queryResult = WorldDatabase.Query("SELECT `SpellID`, `SpellName`, `SpellSubText`, `ReqSpellID`, `ReqSkillLine`, `ReqSkillRank`, `ReqLevel`, `Class`, `Side`, `DefaultCost`, `IsTalent` FROM mod_master_class_trainers_abilities ORDER BY `Class`, `SpellID`");
    if (!queryResult)
    {
        LOG_ERROR("module", "MasterClassTrainers: Error pulling class trainer data from the database.  Does the 'mod_master_class_trainers_abilities' table exist in the world database?");
        return false;
    }
    do
    {
        // Pull the data out
        Field* fields = queryResult->Fetch();
        MasterClassTrainerClassData curClassData;
        curClassData.SpellID = fields[0].Get<uint32>();
        curClassData.SpellName = fields[1].Get<std::string>();
        curClassData.SpellSubText = fields[2].Get<std::string>();
        curClassData.ReqSpellID = fields[3].Get<uint32>();
        curClassData.ReqSkillLine = fields[4].Get<uint32>();
        curClassData.ReqSkillRank = fields[5].Get<uint16>();
        curClassData.ReqLevel = fields[6].Get<uint16>();
        uint16 curClass = fields[7].Get<uint16>();
        std:string curFactionAllowed = fields[8].Get<std::string>();
        curClassData.DefaultCost = fields[9].Get<uint32>();
        curClassData.IsTalent = fields[10].Get<uint32>();

        // TODO Calculate a modified cost
        curClassData.ModifiedCost = curClassData.DefaultCost;

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
            LOG_ERROR("module", "MasterClassTrainers: Could not interpret the race, value passed was {}", curClass);
            curClassData.AllowAlliance = false;
            curClassData.AllowHorde = false;
        }

        // Add to the appropriate class trainer list
        // TODO: Is this needed?
        if (ClassTrainerDataByClass.find(curClass) == ClassTrainerDataByClass.end())
            ClassTrainerDataByClass[curClass] = std::list<MasterClassTrainerClassData>();
    } while (queryResult->NextRow());
    return true;
}

class MasterClassTrainers_PlayerScript : public PlayerScript
{
public:
    MasterClassTrainers_PlayerScript() : PlayerScript("MasterClassTrainers_PlayerScript") {}

    void OnLogin(Player* player)
    {
        if (ConfigEnabled == false)
            return;

	    ChatHandler(player->GetSession()).SendSysMessage("This server is running the |cff4CFF00Master Class Trainer |rmodule.");
    }

    bool OnPrepareGossipMenu(Player* player, WorldObject* source, uint32 menuId /*= 0*/, bool showQuests /*= false*/)
    {
        if (ConfigEnabled == false)
            return true;

        if (Creature* creature = source->ToCreature())
        {
            if (const CreatureTemplate* creatureTemplate = creature->GetCreatureTemplate())
            {
                if (creatureTemplate->trainer_type == TRAINER_TYPE_CLASS)
                {
                    ChatHandler(player->GetSession()).SendSysMessage("Boop");
                    //return false;
                    if (MasterClassTrainer->LoadClassTrainerData() == true)
                    {
                        ChatHandler(player->GetSession()).SendSysMessage("Bop");
                    }
                }
            }
        }
        return true;
    }
};

class MasterClassTrainers_WorldScript: public WorldScript
{
public:
    MasterClassTrainers_WorldScript() : WorldScript("MasterClassTrainers_WorldScript") {}

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        ConfigEnabled = sConfigMgr->GetOption<bool>("MasterClassTrainers.Enable", true);
    }
};

//class MasterClassTrainers_CommandScript : public CommandScript
//{
// TODO: Add command to 'reload from database'
//public:
//    MasterClassTrainers_CommandScript() : CommandScript("MasterClassTrainers_CommandScript") { }
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

class MasterClass_CommandScript : public CommandScript
{
public:
    MasterClass_CommandScript() : CommandScript("MasterClass_CommandScript") { }

    std::vector<ChatCommand> GetCommands() const
    {
        static std::vector<ChatCommand> ABCommandTable =
        {
            { "changeclass",        SEC_PLAYER,                            true, &HandleMasterClassChangeClass,              "Changes a class to the passed class" },
        };

        static std::vector<ChatCommand> commandTable =
        {
            { "masterclass",       SEC_PLAYER,                             false, NULL,                      "", ABCommandTable },
        };
        return commandTable;
    }

    static bool HandleMasterClassChangeClass(ChatHandler* handler, const char* args)
    {
        if (!*args)
        {
            handler->PSendSysMessage(".masterclass changeclass 'class'");
            handler->PSendSysMessage("Changes the player class.  Example: '.masterclass changeclass warrior'");
            return false;
        }

        handler->PSendSysMessage("Boopie");
    }
};

void AddMasterClassTrainerScripts()
{
    new MasterClassTrainers_PlayerScript();
    new MasterClassTrainers_WorldScript();
    //new MasterClassTrainers_CommandScript();
    new MasterClass_CommandScript();
}
