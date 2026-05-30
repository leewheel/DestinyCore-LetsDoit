/*
 * Copyright (C) DestinyCore <https://www.destinycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "FormationMovementGenerator.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Map.h"
#include "MoveSpline.h"
#include "MovementServices.h"
#include "ObjectAccessor.h"
#include "PathPlanner.h"
#include "SplineExecutor.h"

namespace
{
    Movement::PathPlanner* GetPlanner(Unit* owner)
    {
        if (!owner)
            return nullptr;
        Map* map = owner->GetMap();
        Movement::MovementServices* services = map ? map->GetMovementServices() : nullptr;
        return services ? services->GetPathPlanner() : nullptr;
    }
}

FormationMovementGenerator::FormationMovementGenerator(Unit* leader, float range, float angle, uint32 point1, uint32 point2)
    : _leaderGuid(leader ? leader->GetGUID() : ObjectGuid::Empty),
      _range(range), _angle(angle), _point1(point1), _point2(point2),
      _slotVelocity(0.0f), _slotWalk(false), _slotPending(false), _isMoving(false)
{
}

void FormationMovementGenerator::DoInitialize(Creature* owner)
{
    if (!_splineExecutor)
        _splineExecutor = std::make_unique<Movement::SplineExecutor>(owner);

    if (owner->HasUnitState(UNIT_STATE_NOT_MOVE) || owner->IsMovementPreventedByCasting())
    {
        owner->StopMoving();
        return;
    }

    // Wait for the leader's first SetSlot push.
    _slotPending = false;
    _isMoving = false;
}

void FormationMovementGenerator::DoFinalize(Creature* owner)
{
    owner->ClearUnitState(UNIT_STATE_FOLLOW_MOVE);
}

void FormationMovementGenerator::DoReset(Creature* owner)
{
    DoInitialize(owner);
}

void FormationMovementGenerator::SetSlot(Position const& slot, float velocity, bool walk)
{
    _targetSlot = slot;
    _slotVelocity = velocity;
    _slotWalk = walk;
    _slotPending = true;
}

bool FormationMovementGenerator::DoUpdate(Creature* owner, uint32 /*diff*/)
{
    if (!owner)
        return false;

    if (owner->HasUnitState(UNIT_STATE_NOT_MOVE) || owner->IsMovementPreventedByCasting())
    {
        owner->StopMoving();
        _isMoving = false;
        return true;
    }

    // Pin home position so an evade reset doesn't snap us back mid-patrol.
    if (!owner->movespline->Finalized())
        owner->SetHomePosition(owner->GetPosition());

    if (_slotPending)
    {
        _slotPending = false;
        LaunchToSlot(owner);
    }

    if (_isMoving && owner->movespline->Finalized())
    {
        _isMoving = false;
        if (Unit* leader = ObjectAccessor::GetUnit(*owner, _leaderGuid))
            owner->SetFacingTo(leader->GetOrientation());
        MovementInform(owner);
    }

    return true;
}

void FormationMovementGenerator::LaunchToSlot(Creature* owner)
{
    // Sync planner request: must launch this tick to keep up with the leader.
    Movement::PointsArray path;
    if (Movement::PathPlanner* planner = GetPlanner(owner))
    {
        Movement::PathRequest req;
        req.ownerGuid = owner->GetGUID();
        req.from = owner->GetPosition();
        req.to = _targetSlot;
        Movement::PathResult const result = planner->RequestPathSync(owner, req);
        if (result.success && !result.points.empty())
            path = result.points;
    }
    if (path.empty())
    {
        path.emplace_back(owner->GetPositionX(), owner->GetPositionY(), owner->GetPositionZ());
        path.emplace_back(_targetSlot.GetPositionX(), _targetSlot.GetPositionY(), _targetSlot.GetPositionZ());
    }

    Movement::SplineLaunchOptions opts;
    opts.walk = _slotWalk;
    opts.velocityOverride = (_slotVelocity > 0.0f) ? _slotVelocity : owner->GetSpeed(MOVE_WALK);
    _splineExecutor->LaunchAlongPath(path, opts);

    _isMoving = true;
    owner->AddUnitState(UNIT_STATE_FOLLOW_MOVE);
}

void FormationMovementGenerator::MovementInform(Creature* owner)
{
    if (owner->AI())
        owner->AI()->MovementInform(FORMATION_MOTION_TYPE, 0);
}
