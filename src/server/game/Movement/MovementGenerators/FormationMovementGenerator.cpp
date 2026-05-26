/*
 * Copyright (C) DestinyCore <https://www.destinycore.org/>
 *
 * Ported from AzerothCore (FormationMovementGenerator).
 * Adapted for Legion 7.x — see FormationMovementGenerator.h for notes.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "FormationMovementGenerator.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "CreatureGroups.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"

FormationMovementGenerator::FormationMovementGenerator(Unit* leader, float range, float angle, uint32 point1, uint32 point2)
    : TargetedMovementGeneratorBase(leader),
      _range(range), _angle(angle), _point1(point1), _point2(point2),
      _lastLeaderSplineID(0), _hasPredictedDestination(false), _isMoving(false),
      _nextMoveTimer(0)
{
}

void FormationMovementGenerator::DoInitialize(Creature* owner)
{
    if (owner->HasUnitState(UNIT_STATE_NOT_MOVE) || owner->IsMovementPreventedByCasting())
    {
        owner->StopMoving();
        return;
    }

    _nextMoveTimer.Reset(0);
}

void FormationMovementGenerator::DoFinalize(Creature* owner)
{
    owner->ClearUnitState(UNIT_STATE_FOLLOW_MOVE);
}

void FormationMovementGenerator::DoReset(Creature* owner)
{
    DoInitialize(owner);
}

bool FormationMovementGenerator::DoUpdate(Creature* owner, uint32 diff)
{
    Unit* target = GetTarget();
    if (!owner || !target)
        return false;

    if (owner->HasUnitState(UNIT_STATE_NOT_MOVE) || owner->IsMovementPreventedByCasting())
    {
        owner->StopMoving();
        _nextMoveTimer.Reset(0);
        _hasPredictedDestination = false;
        _isMoving = false;
        return true;
    }

    // Leader has reached the destination we predicted for him — settle.
    if (target->movespline->Finalized() && target->movespline->GetId() == _lastLeaderSplineID && _hasPredictedDestination)
    {
        owner->StopMoving();
        _nextMoveTimer.Reset(0);
        _hasPredictedDestination = false;
        _isMoving = false;
        return true;
    }

    // Mid-movement: lock down our home position so an evade reset doesn't snap us.
    if (!owner->movespline->Finalized())
        owner->SetHomePosition(owner->GetPosition());

    // Leader just started a new spline — react.
    if (!target->movespline->Finalized() && target->movespline->GetId() != _lastLeaderSplineID)
    {
        // Waypoint flip support: at the configured waypoint indices we mirror
        // our follow angle so the formation pivots cleanly around the leader.
        if (_point1 && target->IsCreature())
        {
            if (CreatureGroup* formation = target->ToCreature()->GetFormation())
            {
                if (Creature* leader = formation->getLeader())
                {
                    uint32 currentWaypoint = leader->GetCurrentWaypointID() + 1;
                    if (currentWaypoint == _point1 || currentWaypoint == _point2)
                        _angle = Position::NormalizeOrientation(float(2 * M_PI) - _angle);
                }
            }
        }

        LaunchMovement(owner, target);
        _lastLeaderSplineID = target->movespline->GetId();
        return true;
    }

    // Leader is idle — periodically check that we haven't drifted (or the
    // leader teleported) and re-anchor on him.
    _nextMoveTimer.Update(diff);
    if (_nextMoveTimer.Passed())
    {
        _nextMoveTimer.Reset(FORMATION_MOVEMENT_INTERVAL);

        if (_lastLeaderPosition != target->GetPosition())
        {
            LaunchMovement(owner, target);
            return true;
        }
    }

    if (_isMoving && owner->movespline->Finalized())
    {
        _isMoving = false;
        owner->SetFacingTo(target->GetOrientation());
        MovementInform(owner);
    }

    return true;
}

void FormationMovementGenerator::LaunchMovement(Creature* owner, Unit* target)
{
    float relativeAngle = 0.0f;

    if (!target->movespline->Finalized())
    {
        G3D::Vector3 const leaderDestination = target->movespline->CurrentDestination();
        relativeAngle = target->GetRelativeAngle(leaderDestination.x, leaderDestination.y);
    }

    Position dest = target->GetPosition();
    float velocity = 0.0f;

    if (!target->movespline->Finalized())
    {
        // Legion's MoveSpline does not expose Velocity() like AC does. Fall
        // back to the leader's run/walk speed — close enough for the predictive
        // dead-reckoning below, and we further damp it via velocityMod.
        velocity = target->IsWalking() ? target->GetSpeed(MOVE_WALK) : target->GetSpeed(MOVE_RUN);

        // Predict 1.65 seconds of leader travel and aim for that.
        float travelDist = velocity * 1.65f;
        target->MovePositionToFirstCollision(dest, travelDist, relativeAngle);
        target->MovePositionToFirstCollision(dest, _range, _angle + relativeAngle);

        float distance = owner->GetExactDist(dest);
        float velocityMod = std::min<float>(distance / travelDist, 1.5f);

        velocity *= velocityMod;
        _hasPredictedDestination = true;
    }
    else
    {
        target->MovePositionToFirstCollision(dest, _range, _angle + relativeAngle);
        _hasPredictedDestination = false;
    }

    if (velocity == 0.0f)
        velocity = target->GetSpeed(MOVE_WALK);

    Movement::MoveSplineInit init(owner);
    init.MoveTo(dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ());
    init.SetVelocity(velocity);
    init.Launch();

    _lastLeaderPosition.Relocate(target->GetPosition());
    _isMoving = true;
    owner->AddUnitState(UNIT_STATE_FOLLOW_MOVE);
}

void FormationMovementGenerator::MovementInform(Creature* owner)
{
    if (owner->AI())
        owner->AI()->MovementInform(FORMATION_MOTION_TYPE, 0);
}
