/*
 * Copyright (C) DestinyCore <https://www.destinycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef _SMART_WANDER_GENERATOR_H
#define _SMART_WANDER_GENERATOR_H

#include "MovementGenerator.h"
#include "Timer.h"
#include <G3D/Vector3.h>
#include <deque>

class Creature;

namespace SmartWander
{
    enum SamplerMode : uint8
    {
        SAMPLER_POLAR_RANDOM = 0,
        SAMPLER_HALTON       = 1,
    };

    // Setting a weight to 0 short-circuits its criterion evaluation.
    struct CriteriaWeights
    {
        float distance = 1.0f;
        float slope    = 1.0f;
        float navmesh  = 0.7f;
        float water    = 0.0f;  // >0 favours land or water depending on CanSwim
        float memory   = 0.5f;
        float losSpawn = 0.0f;  // >0 prefers points visible from spawn
        float density  = 0.5f;  // >0 penalises clumped destinations
        float facing   = 0.4f;  // soft bias against 180° turns
    };

    struct Profile
    {
        SamplerMode samplerMode = SAMPLER_POLAR_RANDOM;
        uint8 sampleCount       = 8;
        uint8 topK              = 3;
        uint8 memorySize        = 5;
        float minRadius         = 3.0f;
        float maxRadius         = 0.0f; // 0 = Creature::GetRespawnRadius()
        uint32 tickMsMin        = 4000;
        uint32 tickMsMax        = 8000;
        CriteriaWeights weights{};

        static Profile const& Default();
    };
}

class TC_GAME_API SmartWanderGenerator
    : public MovementGeneratorMedium<Creature, SmartWanderGenerator>
{
public:
    // Profile pointer is borrowed. nullptr falls back to Profile::Default().
    // explicitMaxRadius > 0 overrides both the profile and the respawn radius.
    explicit SmartWanderGenerator(SmartWander::Profile const* profile = nullptr,
                                  float explicitMaxRadius = 0.0f);

    MovementGeneratorType GetMovementGeneratorType() const override { return SMART_WANDER_MOTION_TYPE; }

    void DoInitialize(Creature*);
    void DoFinalize(Creature*);
    void DoReset(Creature*);
    bool DoUpdate(Creature*, uint32);

    bool GetResetPosition(Unit*, float& x, float& y, float& z) override;

private:
    bool TryLaunchMove(Creature* owner);
    void RememberPoint(G3D::Vector3 const& p);
    float ResolveMaxRadius(Creature const* owner) const;
    bool IsFarFromPlayers(Creature* owner);

    SmartWander::Profile const* _profile;
    float _explicitMaxRadius = 0.0f;
    G3D::Vector3 _spawnPos = G3D::Vector3::zero();
    // The current cell claimed in the Map's WanderInfluenceMap. Moves with
    // each destination pick; released in DoFinalize.
    G3D::Vector3 _influenceClaimedPos = G3D::Vector3::zero();
    bool _hasInfluenceClaim = false;
    std::deque<G3D::Vector3> _memory;
    TimeTracker _nextTickTimer;
    bool _isMoving = false;
    uint8 _tickSlot = 0;
    bool _slotRegistered = false;
    uint32 _lodLastCheckedMs = 0;
    bool _lodActive = false;
};

#endif
