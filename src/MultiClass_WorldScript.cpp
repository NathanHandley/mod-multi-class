
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

#include "Configuration/Config.h"
#include "ScriptMgr.h"

#include "MultiClass.h"

#include <set>

using namespace std;

class MultiClass_WorldScript: public WorldScript
{
public:
    MultiClass_WorldScript() : WorldScript("MultiClass_WorldScript") {}

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        // Enabled Flag
        MultiClass->ConfigEnabled = sConfigMgr->GetOption<bool>("MultiClass.Enable", true);

        // Instruction Message
        MultiClass->ConfigDisplayInstructionMessage = sConfigMgr->GetOption<bool>("MultiClass.MultiClass.DisplayInstructionMessage", true);

        // Cross Class Skills
        MultiClass->ConfigCrossClassIncludeSkillIDs = GetSetFromConfigString("MultiClass.CrossClassIncludeSkillIDs");

        // Using Transmog
        MultiClass->ConfigUsingTransmogMod = sConfigMgr->GetOption<bool>("MultiClass.UsingTransmog", true);

        // Master Skills - Enabled
        MultiClass->ConfigEnableMasterSkills = sConfigMgr->GetOption<bool>("MultiClass.MasterSkills.Enable", true);

        // Master Skills - Level Per Token
        MultiClass->ConfigLevelsPerToken = sConfigMgr->GetOption<uint8>("MultiClass.MasterSkills.LevelsPerToken", 10);

        // Master Skills - Bonus Token Levels
        MultiClass->ConfigBonusTokenLevels = GetSetFromConfigString("MultiClass.MasterSkills.BonusTokenLevels");

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

void AddWorldScripts()
{;
    new MultiClass_WorldScript();
}
