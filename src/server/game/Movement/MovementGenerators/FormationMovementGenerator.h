/*
 * Copyright (C) DestinyCore <https://www.destinycore.org/>
 *
 * Ported from AzerothCore (FormationMovementGenerator).
 * Adapted for Legion 7.x (FollowerReference instead of AbstractFollower;
 * leader velocity sourced from Unit::GetSpeed since Legion's MoveSpline
 * does not expose Velocity()).
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef _FORMATION_MOVEMENT_GENERATOR_H
#define _FORMATION_MOVEMENT_GENERATOR_H

#include "MovementGenerator.h"
#include "Position.h"
#include "TargetedMovementGenerator.h"
#include "Timer.h"

class Creature;
class Unit;

class TC_GAME_API FormationMovementGenerator
    : public MovementGeneratorMedium<Creature, FormationMovementGenerator>,
      public TargetedMovementGeneratorBase
{
public:
    explicit FormationMovementGenerator(Unit* leader, float range, float angle, uint32 point1 = 0, uint32 point2 = 0);

    MovementGeneratorType GetMovementGeneratorType() const override { return FORMATION_MOTION_TYPE; }

    void DoInitialize(Creature*);
    void DoFinalize(Creature*);
    void DoReset(Creature*);
    bool DoUpdate(Creature*, uint32);

    Unit* GetTarget() const { return i_target.getTarget(); }

private:
    void MovementInform(Creature* owner);
    void LaunchMovement(Creature* owner, Unit* target);

    static constexpr uint32 FORMATION_MOVEMENT_INTERVAL = 1200;

    float const _range;
    float _angle;
    uint32 const _point1;
    uint32 const _point2;
    uint32 _lastLeaderSplineID;
    bool _hasPredictedDestination;
    bool _isMoving;

    Position _lastLeaderPosition;
    TimeTracker _nextMoveTimer;
};

#endif
