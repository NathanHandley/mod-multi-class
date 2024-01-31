
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
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"

#include "MultiClass.h"

using namespace Acore::ChatCommands;
using namespace std;

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
        if (MultiClass->ConfigEnabled == false)
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
        if (MultiClass->ConfigEnabled == false)
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
        if (MultiClass->ConfigEnabled == false)
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
        if (MultiClass->ConfigEnabled == false)
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
        if (MultiClass->ConfigEnabled == false)
            return true;
        if (!MultiClass->ConfigEnableMasterSkills)
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

void AddCommandScripts()
{
    new MultiClass_CommandScript();
}
