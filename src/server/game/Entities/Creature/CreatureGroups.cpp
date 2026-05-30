/*
 * Copyright (C) 2008-2018 TrinityCore <https://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
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

#include "CreatureGroups.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "DatabaseEnv.h"
#include "FormationMovementGenerator.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "Util.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

FormationMgr::~FormationMgr()
{
    for (CreatureGroupInfoType::iterator itr = CreatureGroupMap.begin(); itr != CreatureGroupMap.end(); ++itr)
        delete itr->second;
}

FormationMgr* FormationMgr::instance()
{
    static FormationMgr instance;
    return &instance;
}

void FormationMgr::AddCreatureToGroup(ObjectGuid::LowType leaderGuid, Creature* creature)
{
    Map* map = creature->FindMap();
    if (!map)
        return;

    CreatureGroupHolderType::iterator itr = map->CreatureGroupHolder.find(leaderGuid);

    //Add member to an existing group
    if (itr != map->CreatureGroupHolder.end())
    {
        TC_LOG_DEBUG("entities.unit", "Group found: " UI64FMTD ", inserting %s, Group InstanceID %u", leaderGuid, creature->GetGUID().ToString().c_str(), creature->GetInstanceId());
        itr->second->AddMember(creature);
    }
    //Create new group
    else
    {
        TC_LOG_DEBUG("entities.unit", "Group not found: " UI64FMTD ". Creating new group.", leaderGuid);
        CreatureGroup* group = new CreatureGroup(leaderGuid);
        map->CreatureGroupHolder[leaderGuid] = group;
        group->AddMember(creature);
    }
}

void FormationMgr::RemoveCreatureFromGroup(CreatureGroup* group, Creature* member)
{
    TC_LOG_DEBUG("entities.unit", "Deleting member pointer to GUID: " UI64FMTD " from group " UI64FMTD, group->GetId(), member->GetSpawnId());
    group->RemoveMember(member);

    if (group->isEmpty())
    {
        Map* map = member->FindMap();
        if (!map)
            return;

        TC_LOG_DEBUG("entities.unit", "Deleting group with InstanceID %u", member->GetInstanceId());
        map->CreatureGroupHolder.erase(group->GetId());
        delete group;
    }
}

void FormationMgr::LoadCreatureFormations()
{
    uint32 oldMSTime = getMSTime();

    for (CreatureGroupInfoType::iterator itr = CreatureGroupMap.begin(); itr != CreatureGroupMap.end(); ++itr) // for reload case
        delete itr->second;
    CreatureGroupMap.clear();

    //Get group data
    QueryResult result = WorldDatabase.Query("SELECT leaderGUID, memberGUID, dist, angle, groupAI, point_1, point_2 FROM creature_formations ORDER BY leaderGUID");

    if (!result)
    {
        TC_LOG_ERROR("server.loading", ">>  Loaded 0 creatures in formations. DB table `creature_formations` is empty!");
        return;
    }

    uint32 count = 0;
    Field* fields;
    FormationInfo* group_member;

    do
    {
        fields = result->Fetch();

        //Load group member data
        group_member                        = new FormationInfo();
        group_member->leaderGUID            = fields[0].GetUInt64();
        ObjectGuid::LowType memberGUID      = fields[1].GetUInt64();
        group_member->groupAI               = fields[4].GetUInt32();
        group_member->point_1               = fields[5].GetUInt16();
        group_member->point_2               = fields[6].GetUInt16();
        //If creature is group leader we may skip loading of dist/angle
        if (group_member->leaderGUID != memberGUID)
        {
            group_member->follow_dist       = fields[2].GetFloat();
            group_member->follow_angle      = fields[3].GetFloat() * float(M_PI) / 180;
        }
        else
        {
            group_member->follow_dist       = 0;
            group_member->follow_angle      = 0;
        }

        // check data correctness
        {
            if (!sObjectMgr->GetCreatureData(group_member->leaderGUID))
            {
                TC_LOG_ERROR("sql.sql", "creature_formations table leader guid " UI64FMTD " incorrect (not exist)", group_member->leaderGUID);
                delete group_member;
                continue;
            }

            if (!sObjectMgr->GetCreatureData(memberGUID))
            {
                TC_LOG_ERROR("sql.sql", "creature_formations table member guid " UI64FMTD " incorrect (not exist)", memberGUID);
                delete group_member;
                continue;
            }
        }

        CreatureGroupMap[memberGUID] = group_member;
        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded %u creatures in formations in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void CreatureGroup::AddMember(Creature* member)
{
    TC_LOG_DEBUG("entities.unit", "CreatureGroup::AddMember: Adding %s.", member->GetGUID().ToString().c_str());

    //Check if it is a leader
    if (member->GetSpawnId() == m_groupID)
    {
        TC_LOG_DEBUG("entities.unit", "%s is formation leader. Adding group.", member->GetGUID().ToString().c_str());
        m_leader = member;
    }

    m_members[member] = sFormationMgr->CreatureGroupMap.find(member->GetSpawnId())->second;
    member->SetFormation(this);
}

void CreatureGroup::RemoveMember(Creature* member)
{
    if (m_leader == member)
        m_leader = NULL;

    m_members.erase(member);
    member->SetFormation(NULL);
}

void CreatureGroup::MemberAttackStart(Creature* member, Unit* target)
{
    uint8 groupAI = sFormationMgr->CreatureGroupMap[member->GetSpawnId()]->groupAI;
    if (!groupAI)
        return;

    if (member == m_leader)
    {
        if (!(groupAI & FLAG_MEMBERS_ASSIST_LEADER))
            return;
    }
    else if (!(groupAI & FLAG_LEADER_ASSISTS_MEMBER))
        return;

    for (CreatureGroupMemberType::iterator itr = m_members.begin(); itr != m_members.end(); ++itr)
    {
        if (m_leader) // avoid crash if leader was killed and reset.
            TC_LOG_DEBUG("entities.unit", "GROUP ATTACK: group instance id %u calls member instid %u", m_leader->GetInstanceId(), member->GetInstanceId());

        Creature* other = itr->first;

        // Skip self
        if (other == member)
            continue;

        if (!other->IsAlive())
            continue;

        if (other->GetVictim())
            continue;

        if (((other != m_leader && (groupAI & FLAG_MEMBERS_ASSIST_LEADER)) || (other == m_leader && (groupAI & FLAG_LEADER_ASSISTS_MEMBER))) && other->IsValidAttackTarget(target))
            other->AI()->AttackStart(target);
    }
}

void CreatureGroup::FormationReset(bool dismiss)
{
    for (CreatureGroupMemberType::iterator itr = m_members.begin(); itr != m_members.end(); ++itr)
    {
        if (itr->first != m_leader && itr->first->IsAlive())
        {
            if (dismiss)
                itr->first->GetMotionMaster()->Initialize();
            else
                itr->first->GetMotionMaster()->MoveIdle();
            TC_LOG_DEBUG("entities.unit", "Set %s movement for member %s", dismiss ? "default" : "idle", itr->first->GetGUID().ToString().c_str());
        }
    }
    m_Formed = !dismiss;
}

void CreatureGroup::LeaderMoveTo(float x, float y, float z)
{
    if (!m_leader)
        return;

    // Destination -> leader direction so the DB convention angle=0 stays
    // "behind the leader". Inverting these args flips the formation 180°.
    float const pathAngle = std::atan2(m_leader->GetPositionY() - y, m_leader->GetPositionX() - x);

    struct Slot
    {
        Creature* member;
        FormationInfo const* info;
        float x, y, z;
        bool taken;
    };
    std::vector<Slot> slots;
    slots.reserve(m_members.size());

    for (auto const& entry : m_members)
    {
        Creature* member = entry.first;
        FormationInfo const* info = entry.second;

        if (member == m_leader || !member->IsAlive() || member->GetVictim())
            continue;
        if (!(info->groupAI & FLAG_IDLE_IN_FORMATION))
            continue;
        if (member->HasUnitState(UNIT_STATE_NOT_MOVE) || member->HasUnitFlag(UNIT_FLAG_PLAYER_CONTROLLED))
            continue;

        // Mirror the angle at scripted turn-around waypoints so the formation
        // pivots cleanly around the leader.
        float angle = info->follow_angle;
        if (info->point_1)
        {
            uint32 const wp = m_leader->GetCurrentWaypointID();
            if (wp == info->point_1 - 1 || wp == info->point_2 - 1)
                angle = float(M_PI) * 2.0f - angle;
        }

        float const dist = info->follow_dist;
        float dx = x + std::cos(angle + pathAngle) * dist;
        float dy = y + std::sin(angle + pathAngle) * dist;
        float dz = z;

        Trinity::NormalizeMapCoord(dx);
        Trinity::NormalizeMapCoord(dy);

        if (!member->IsFlying())
            member->UpdateGroundPositionZ(dx, dy, dz);

        slots.push_back({member, info, dx, dy, dz, false});
    }

    if (slots.empty())
        return;

    // Greedy closest-slot reassignment. On a 180° turn this picks the
    // mirrored slot, so trajectories don't cross.
    std::sort(slots.begin(), slots.end(),
              [](Slot const& a, Slot const& b) { return a.member->GetGUID() < b.member->GetGUID(); });

    constexpr float FORMATION_CATCHUP_CAP = 3.0f;
    constexpr float FORMATION_CATCHUP_TRIGGER = 15.0f;
    bool const leaderWalking = m_leader->IsWalking();
    float const leaderRunSpeed = m_leader->GetSpeed(MOVE_RUN);
    float const leaderWalkSpeed = m_leader->GetSpeed(MOVE_WALK);
    float const leaderSpeed = leaderWalking ? leaderWalkSpeed : leaderRunSpeed;

    // In normal flow each slot is ~segmentLength ahead of the member;
    // anything beyond that + trigger is a real straggler.
    float const segmentLength = std::sqrt(
        (x - m_leader->GetPositionX()) * (x - m_leader->GetPositionX()) +
        (y - m_leader->GetPositionY()) * (y - m_leader->GetPositionY()));

    for (auto const& cur : slots)
    {
        Slot* best = nullptr;
        float bestDist = std::numeric_limits<float>::max();
        for (auto& cand : slots)
        {
            if (cand.taken)
                continue;
            float const d = cur.member->GetExactDist2d(cand.x, cand.y);
            if (d < bestDist)
            {
                bestDist = d;
                best = &cand;
            }
        }
        if (!best)
            continue;

        bool const isStraggler = bestDist > segmentLength + FORMATION_CATCHUP_TRIGGER;
        bool const walk = !isStraggler && leaderWalking;
        float velocity;
        if (isStraggler)
        {
            // Scale catch-up velocity with how far behind, capped at CAP×run.
            float const referenceDist = std::max(leaderRunSpeed, 1.0f);
            float const velocityMod = std::clamp(bestDist / referenceDist, 1.0f, FORMATION_CATCHUP_CAP);
            velocity = leaderRunSpeed * velocityMod;
        }
        else
        {
            velocity = leaderSpeed;
        }

        Position const slotPos(best->x, best->y, best->z);

        MotionMaster* mm = cur.member->GetMotionMaster();
        FormationMovementGenerator* fmg = dynamic_cast<FormationMovementGenerator*>(mm->top());
        if (!fmg)
        {
            // Just woke up or returned from combat - start a generator.
            mm->MoveFormation(m_leader, cur.info->follow_dist, cur.info->follow_angle,
                              cur.info->point_1, cur.info->point_2);
            fmg = dynamic_cast<FormationMovementGenerator*>(mm->top());
        }
        if (fmg)
            fmg->SetSlot(slotPos, velocity, walk);

        best->taken = true;
    }
}
