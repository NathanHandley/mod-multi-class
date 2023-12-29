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

#ifndef MASTER_CLASS_TRAINERS_H
#define MASTER_CLASS_TRAINERS_H

#include "Common.h"

#include <list>
#include <map>

struct MultiClassSpells
{
    uint32 SpellID;
    std::string SpellName;
    std::string SpellSubText;
    uint16 DefaultReqLevel;
    uint16 ModifiedReqLevel;
    bool AllowHorde;
    bool AllowAlliance;
    bool IsTalent;
    bool IsLearnedByTalent;
};

class MultiClassMod
{
private:
    MultiClassMod();

    //bool mIsInitialized;
    std::map<uint16, std::list<MultiClassSpells>> ClassSpellsByClass;

public:
    static MultiClassMod* instance()
    {
        static MultiClassMod instance;
        return &instance;
    }

    ~MultiClassMod();

    bool LoadClassAbilityData();

    bool MarkClassChangeOnNextLogout(ChatHandler* handler, Player* player, uint8 newClass);
    bool PerformQueuedClassSwitch(Player* player);

private:
    bool DoesSavedClassDataExistForPlayer(Player* player, uint8 lookupClass);
    bool IsValidRaceClassCombo(uint8 lookupClass, uint8 lookupRace);

    bool SwitchClassCoreData(Player* player, uint8 oldClass, uint8 newClass, bool isNew);
    bool SwitchClassTalentData(Player* player, uint8 oldClass, uint8 newClass, bool isNew);
    bool SwitchClassSpellData(Player* player, uint8 oldClass, uint8 newClass, bool isNew);
    bool SwitchClassSkillData(Player* player, uint8 oldClass, uint8 newClass, bool isNew);
    bool SwitchClassActionBarData(Player* player, uint8 oldClass, uint8 newClass, bool isNew);
    bool SwitchClassGlyphData(Player* player, uint8 oldClass, uint8 newClass, bool isNew);
    bool SwitchClassAuraData(Player* player, uint8 oldClass, uint8 newClass, bool isNew);
    bool SwitchClassEquipmentData(Player* player, uint8 oldClass, uint8 newClass, bool isNew);

    //bool GetIsInitialized() { return mIsInitialized; }
};

#define MultiClass MultiClassMod::instance()

#endif //MASTER_CLASS_TRAINERS_H
