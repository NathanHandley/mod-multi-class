
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
#include "Player.h"
#include "ScriptMgr.h"

#include "MultiClass.h"

using namespace std;

class MultiClass_PlayerScript : public PlayerScript
{
public:
    MultiClass_PlayerScript() : PlayerScript("MultiClass_PlayerScript") {}

    // TODO: Implement CLASS_CONTEXT_PET and CLASS_CONTEXT_PET_CHARM
    // Note: Client Binary Changes are needed for the following to function:
    // - Rune abilities reactivating after rune depletion and recharge
    // - Show combo points on non-druid and non-rogue
    Optional<bool> OnPlayerIsClass(Player const* player, Classes playerClass, ClassContext context)
    {
        switch (context)
        {
            // If in a druid combat shapeshift form, then use stat logic for druid forms even if not a druid
        case CLASS_CONTEXT_STATS:
        {
            if (player->GetShapeshiftForm() == FORM_CAT
                || player->GetShapeshiftForm() == FORM_BEAR
                || player->GetShapeshiftForm() == FORM_DIREBEAR)
            {
                if (playerClass == CLASS_DRUID)
                {
                    return true;
                }
                else
                {
                    return false;
                }
            }
        } break;
        // Any class can use any base ability
        //  - Note: Required for resource initialization and recharging for DK Runes
        case CLASS_CONTEXT_ABILITY:
        // Also enable reactives on all classes, but do special logic for Overpower (TODO)
        case CLASS_CONTEXT_ABILITY_REACTIVE:
        // Any class can loot (and should be able to roll on) and use any equipment identified in DBC
        case CLASS_CONTEXT_EQUIP_RELIC:
        case CLASS_CONTEXT_EQUIP_SHIELDS:
        case CLASS_CONTEXT_EQUIP_ARMOR_CLASS:
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

    Optional<bool> OnPlayerHasActivePowerType(Player const* /*player*/, Powers /*power*/)
    {
        // For enable all powers for all classes
        return true;
    }

    void OnLogin(Player* player)
    {
        if (MultiClass->ConfigEnabled == false)
            return;

        if (MultiClass->ConfigEnableMasterSkills)
        {
            MultiClass->PerformKnownSpellUpdateFromMasterSkills(player);
            MultiClass->PerformTokenIssuesForPlayerClass(player, player->getClass());
        }

        if (MultiClass->ConfigDisplayInstructionMessage)
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
        if (MultiClass->ConfigEnabled == false)
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
        if (MultiClass->ConfigEnabled == false)
            return;
        MultiClass->PerformPlayerDelete(guid);        
    }

    void OnLevelChanged(Player* player, uint8 /*oldLevel*/)
    {
        if (MultiClass->ConfigEnabled == false)
            return;
        if (MultiClass->ConfigEnableMasterSkills)
        {
            MultiClass->PerformKnownSpellUpdateFromMasterSkills(player);
            MultiClass->PerformTokenIssuesForPlayerClass(player, player->getClass());
        }
    }

    void OnLearnSpell(Player* player, uint32 spellID)
    {
        if (MultiClass->ConfigEnabled == false)
            return;

        // Only take action if a master skill was learned
        if (MultiClass->ConfigEnableMasterSkills && MultiClass->IsSpellAMasterSkill(spellID))
        {
            MultiClass->PerformKnownSpellUpdateFromMasterSkills(player);
        }
    }

    void OnForgotSpell(Player* player, uint32 spellID)
    {
        if (MultiClass->ConfigEnabled == false)
            return;

        // Only take action if a master skill was forgotten
        if (MultiClass->ConfigEnableMasterSkills && MultiClass->IsSpellAMasterSkill(spellID))
        {
            MultiClass->PerformKnownSpellUpdateFromMasterSkills(player);
        }
    }
};

void AddMultiClassPlayerScripts()
{
    new MultiClass_PlayerScript();
}
