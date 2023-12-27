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

MasterClassTrainersMod::MasterClassTrainersMod()
{

}

MasterClassTrainersMod::~MasterClassTrainersMod()
{

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
                    return false;
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

void AddMasterClassTrainerScripts()
{
    new MasterClassTrainers_PlayerScript();
    new MasterClassTrainers_WorldScript();
    //new MasterClassTrainers_CommandScript();
}
