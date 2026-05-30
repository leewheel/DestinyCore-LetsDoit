/*
 * Copyright (C) DestinyCore <https://www.destinycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef _FORMATION_MOVEMENT_GENERATOR_H
#define _FORMATION_MOVEMENT_GENERATOR_H

#include "MovementGenerator.h"
#include "ObjectGuid.h"
#include "Position.h"
#include "Timer.h"
#include <memory>

class Creature;
class Unit;

namespace Movement
{
    class SplineExecutor;
}

// Passive: never resolves the leader or computes its slot. The leader's
// CreatureGroup pushes fresh slot positions via SetSlot() at each spline
// launch, keeping the whole formation coherent from a single computation.
class TC_GAME_API FormationMovementGenerator
    : public MovementGeneratorMedium<Creature, FormationMovementGenerator>
{
public:
    explicit FormationMovementGenerator(Unit* leader, float range, float angle, uint32 point1 = 0, uint32 point2 = 0);

    MovementGeneratorType GetMovementGeneratorType() const override { return FORMATION_MOTION_TYPE; }

    void DoInitialize(Creature*);
    void DoFinalize(Creature*);
    void DoReset(Creature*);
    bool DoUpdate(Creature*, uint32);

    ObjectGuid GetLeaderGuid() const { return _leaderGuid; }

    // Called by CreatureGroup::LeaderMoveTo on each leader spline launch.
    void SetSlot(Position const& slot, float velocity, bool walk);

private:
    void LaunchToSlot(Creature* owner);
    void MovementInform(Creature* owner);

    ObjectGuid _leaderGuid;
    // Kept for MotionMaster::MoveFormation API compatibility; the actual
    // slot is computed by CreatureGroup and pushed via SetSlot.
    float const _range;
    float const _angle;
    uint32 const _point1;
    uint32 const _point2;

    Position _targetSlot;
    float _slotVelocity;
    bool _slotWalk;
    bool _slotPending;
    bool _isMoving;

    std::unique_ptr<Movement::SplineExecutor> _splineExecutor;
};

#endif
