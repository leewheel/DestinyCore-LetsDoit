/*
 * Copyright (C) 2019 MagicStorm.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ScriptMgr.h"
#include "Cell.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "SpellScript.h"
#include "SpellAuras.h"
#include "ScriptedGossip.h"
#include "MotionMaster.h"
#include <G3D/Quat.h>

enum DarkmoonFaireYells
{
    // Selina
    SAY_SELINA_WELCOME           = 0,
};

enum SelinaDourmanEvent
{
    EVENT_RENEW_SELINA_TEXT = 1,
};

class npc_selina_dourman : public CreatureScript
{
public:
    npc_selina_dourman() : CreatureScript("npc_selina_dourman") { }

    CreatureAI* GetAI(Creature* creature) const
    {
        return new npc_selina_dourmanAI (creature);
    }

    struct npc_selina_dourmanAI : public ScriptedAI
    {
        npc_selina_dourmanAI(Creature* creature) : ScriptedAI(creature) { }

        EventMap events;

        bool Talked;

        void Reset()
        {
            Talked = false;
        }

        void MoveInLineOfSight(Unit* who)
        {
            if (who->GetTypeId() != TYPEID_PLAYER)
                return;

            if (who->GetExactDist(me) <= 20.0f && !Talked)
            {
                Talked = true;
                Talk(SAY_SELINA_WELCOME);
                events.ScheduleEvent(EVENT_RENEW_SELINA_TEXT, 60000);
            }
        }

        void UpdateAI(uint32 diff)
        {
            events.Update(diff);

            while (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_RENEW_SELINA_TEXT:
                        Talked = false;
                        break;
                }
            }
        }
    };

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (creature->IsQuestGiver())
            player->PrepareQuestMenu(creature->GetGUID());

        char const* GOSSIP_BUTTON_1;
        char const* GOSSIP_BUTTON_2;
        char const* GOSSIP_BUTTON_3;
        char const* GOSSIP_BUTTON_4;
        char const* GOSSIP_BUTTON_5;

        switch (LocaleConstant currentlocale = player->GetSession()->GetSessionDbcLocale())
        {
            default:
                GOSSIP_BUTTON_1         = "暗月冒险者指南？";
                GOSSIP_BUTTON_2         = "我可以买什么？";
                GOSSIP_BUTTON_3         = "暗月马戏团奖券？";
                GOSSIP_BUTTON_4         = "暗月卡片？";
                GOSSIP_BUTTON_5         = "娱乐项目？";
                break;
        };

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_BUTTON_1, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_BUTTON_2, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 2);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_BUTTON_3, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 3);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_BUTTON_4, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 4);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_BUTTON_5, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 5);

        SendGossipMenuFor(player, 23004, creature->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 uiSender, uint32 action)
    {
        char const* GOSSIP_BUTTON_1;
        char const* GOSSIP_BUTTON_2;
        char const* GOSSIP_BUTTON_3;
        char const* GOSSIP_BUTTON_4;
        char const* GOSSIP_BUTTON_5;
        char const* GOSSIP_BUTTON_6;
        char const* GOSSIP_BUTTON_7;
        char const* GOSSIP_BUTTON_8;
        char const* GOSSIP_BUTTON_9;
        char const* GOSSIP_BUTTON_10;
        char const* GOSSIP_BUTTON_11;
        char const* GOSSIP_BUTTON_12;
        char const* GOSSIP_BUTTON_13;

        switch (LocaleConstant currentlocale = player->GetSession()->GetSessionDbcLocale())
        {
            default:
                GOSSIP_BUTTON_1         = "暗月冒险者指南？";
                GOSSIP_BUTTON_2         = "我可以买什么？";
                GOSSIP_BUTTON_3         = "暗月马戏团奖券？";
                GOSSIP_BUTTON_4         = "暗月卡片？";
                GOSSIP_BUTTON_5         = "娱乐项目？";
                GOSSIP_BUTTON_6         = "可以给我一本暗月冒险者指南吗？";
                GOSSIP_BUTTON_7         = "详细说说。";
                GOSSIP_BUTTON_8         = "坦克大战？";
                GOSSIP_BUTTON_9         = "人间大炮？";
                GOSSIP_BUTTON_10         = "痛扁豺狼人？";
                GOSSIP_BUTTON_11         = "套圈圈？";
                GOSSIP_BUTTON_12         = "射击场？";
                GOSSIP_BUTTON_13         = "先知？";
                break;
        };

        player->PlayerTalkClass->ClearMenus();

        if (action == GOSSIP_ACTION_INFO_DEF + 1)
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_BUTTON_6, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 6);
            SendGossipMenuFor(player, 23005, creature->GetGUID());
        }

        if (action == GOSSIP_ACTION_INFO_DEF + 2)
            SendGossipMenuFor(player, 23006, creature->GetGUID());

        if (action == GOSSIP_ACTION_INFO_DEF + 3)
            SendGossipMenuFor(player, 23007, creature->GetGUID());

        if (action == GOSSIP_ACTION_INFO_DEF + 4)
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_BUTTON_7, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 7);
            SendGossipMenuFor(player, 23008, creature->GetGUID());
        }

        if (action == GOSSIP_ACTION_INFO_DEF + 5)
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_BUTTON_8, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 8);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_BUTTON_9, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 9);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_BUTTON_10, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 10);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_BUTTON_11, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 11);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_BUTTON_12, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 12);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_BUTTON_13, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 13);
            SendGossipMenuFor(player, 23010, creature->GetGUID());
        }

        if (action == GOSSIP_ACTION_INFO_DEF + 6)
        {
            player->PlayerTalkClass->ClearMenus();
            CloseGossipMenuFor(player);
            player->AddItem(71634, 1);
        }

        if (action == GOSSIP_ACTION_INFO_DEF + 7)
            SendGossipMenuFor(player, 23009, creature->GetGUID());

        if (action == GOSSIP_ACTION_INFO_DEF + 8)
        {
            SendGossipMenuFor(player, 23011, creature->GetGUID());
            //player->PlayerTalkClass->SendPointOfInterest();
        }

        if (action == GOSSIP_ACTION_INFO_DEF + 9)
        {
            SendGossipMenuFor(player, 23012, creature->GetGUID());
            //player->PlayerTalkClass->SendPointOfInterest();
        }

        if (action == GOSSIP_ACTION_INFO_DEF + 10)
        {
            SendGossipMenuFor(player, 23013, creature->GetGUID());
            //player->PlayerTalkClass->SendPointOfInterest();
        }

        if (action == GOSSIP_ACTION_INFO_DEF + 11)
        {
            SendGossipMenuFor(player, 23014, creature->GetGUID());
            //player->PlayerTalkClass->SendPointOfInterest();
        }

        if (action == GOSSIP_ACTION_INFO_DEF + 12)
        {
            SendGossipMenuFor(player, 23015, creature->GetGUID());
            //player->PlayerTalkClass->SendPointOfInterest();
        }

        if (action == GOSSIP_ACTION_INFO_DEF + 13)
        {
            SendGossipMenuFor(player, 23016, creature->GetGUID());
            //player->PlayerTalkClass->SendPointOfInterest();
        }

        return true;
    }
};

void AddSC_darkmoon_selina()
{
    new npc_selina_dourman();
};
