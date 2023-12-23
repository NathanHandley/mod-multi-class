/*
** Made by Nathan Handley https://github.com/NathanHandley
** AzerothCore 2019 http://www.azerothcore.org/
*/

#include "Chat.h"
#include "Configuration/Config.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "World.h"

using namespace Acore::ChatCommands;

class MasterClassTrainers_PlayerScript : public PlayerScript
{
public:
    MasterClassTrainers_PlayerScript() : PlayerScript("MasterClassTrainers_PlayerScript") {}

    void OnLogin(Player* player)
    {
	    ChatHandler(player->GetSession()).SendSysMessage("This server is running the |cff4CFF00Master Class Trainer |rmodule.");
    }

    bool CanPrepareGossipMenu(Player* player, WorldObject* source, uint32 menuId /*= 0*/, bool showQuests /*= false*/)
    {
        ChatHandler(player->GetSession()).SendSysMessage("Boop");
        return false;
    }
};

class MasterClassTrainers_WorldScript: public WorldScript
{
public:
    MasterClassTrainers_WorldScript() : WorldScript("MasterClassTrainers_WorldScript") {}

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        //if (sConfigMgr->GetOption<bool>("something", true) != somevalue)
        //{
        //    do something
       // }
    }
};

class MasterClassTrainers_CommandScript : public CommandScript
{
public:
    MasterClassTrainers_CommandScript() : CommandScript("MasterClassTrainers_CommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable acaCommandTable =
        {
            { "test",     HandleACATestCommand,   SEC_PLAYER, Console::Yes }
        };
        static ChatCommandTable commandTable =
        {
            { "mct", acaCommandTable }
        };
        return commandTable;
    }

    static bool HandleACATestCommand(ChatHandler* handler, const char* args)
    {
        Player* player = handler->GetPlayer();
        ChatHandler(player->GetSession()).SendSysMessage("This is a test");
        return true;
    }
};

void AddMasterClassTrainerScripts()
{
    new MasterClassTrainers_PlayerScript();
    new MasterClassTrainers_WorldScript();
    new MasterClassTrainers_CommandScript();
}
