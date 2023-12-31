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

#include <set>
#include <map>

struct MultiClassSpell
{
    uint8 ClassID;
    uint32 SpellID;
    std::string SpellName;
    std::string SpellSubText;
    uint32 ReqSpellID;
    uint8 ReqLevel;
    uint8 ModifiedReqLevel;
    bool AllowHorde;
    bool AllowAlliance;
    bool IsTalent;
    bool IsLearnedByTalent;
};

struct QueuedClassSwitch
{
    uint8 classID;
    bool isNew;
};

class MultiClassMod
{
private:
    MultiClassMod();

    std::map<uint32, MultiClassSpell> ClassSpellsBySpellID;

    bool DoesSavedClassDataExistForPlayer(Player* player, uint8 lookupClass);
    bool IsValidRaceClassCombo(uint8 lookupClass, uint8 lookupRace);
    void QueueClassSwitch(Player* player, uint8 nextClass);
    QueuedClassSwitch GetQueuedClassSwitch(Player* player);
    void DeleteQueuedClassSwitch(Player* player);

    void CopyCharacterDataIntoModCharacterTable(Player* player, CharacterDatabaseTransaction& transaction);
    void MoveTalentsToModTalentsTable(Player* player, CharacterDatabaseTransaction& transaction);
    void MoveClassSpellsToModSpellsTable(Player* player, CharacterDatabaseTransaction& transaction);
    void MoveClassSkillsToModSkillsTable(Player* player, CharacterDatabaseTransaction& transaction);
    void ReplaceModClassActionCopy(Player* player, CharacterDatabaseTransaction& transaction);
    void MoveGlyphsToModGlyhpsTable(Player* player, CharacterDatabaseTransaction& transaction);
    void MoveAuraToModAuraTable(Player* player, CharacterDatabaseTransaction& transaction);
    void MoveEquipToModInventoryTable(Player* player, CharacterDatabaseTransaction& transaction);

    void UpdateCharacterFromModCharacterTable(uint32 playerGUID, uint8 pullClassID, CharacterDatabaseTransaction& transaction);
    void CopyModSpellTableIntoCharacterSpells(uint32 playerGUID, uint8 pullClassID, CharacterDatabaseTransaction& transaction);
    void CopyModActionTableIntoCharacterAction(uint32 playerGUID, uint8 pullClassID, CharacterDatabaseTransaction& transaction);
    void CopyModSkillTableIntoCharacterSkills(uint32 playerGUID, uint8 pullClassID, CharacterDatabaseTransaction& transaction);

    void GetSpellLearnAndUnlearnsForPlayer(Player* player, std::list<int32>& outSpellUnlearns, std::list<int32>& outSpellLearns);

public:
    static MultiClassMod* instance()
    {
        static MultiClassMod instance;
        return &instance;
    }
    ~MultiClassMod();

    bool LoadClassAbilityData();

    bool MarkClassChangeOnNextLogout(ChatHandler* handler, Player* player, uint8 newClass);
    bool PerformQueuedClassSwitchOnLogout(Player* player);
    bool PerformQueuedClassSwitchOnLogin(Player* player);
    bool PerformPlayerDelete(ObjectGuid guid);
};

std::string GenerateCommaDelimitedStringFromSet(std::set<uint32> intSet); // Can delete?

#define MultiClass MultiClassMod::instance()

#endif //MASTER_CLASS_TRAINERS_H
