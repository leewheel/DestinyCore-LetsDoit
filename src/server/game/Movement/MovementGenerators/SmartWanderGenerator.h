/*
 * Copyright (C) DestinyCore <https://www.destinycore.org/>
 *
 * SmartWanderGenerator — utility-AI driven wander behaviour.
 *
 * Each tick, the generator samples N candidate destinations around the
 * creature's spawn point, scores them against weighted criteria
 * (distance curve, slope, navmesh cost, liquid affinity, memory of recent
 * points, optional line-of-sight to spawn), and picks one via weighted
 * reservoir sampling over the top-K. This replaces the unconditional
 * polar-random sampling of RandomMovementGenerator with something far less
 * prone to clipping, cliff-walking, and erratic patrols.
 *
 * Phases delivered:
 *   2.2.1 framework (this file + .cpp)
 *   2.2.2 candidate sampler (polar + Halton low-discrepancy)
 *   2.2.3 scoring engine (multi-criteria, weights from Profile)
 *   2.2.5 reservoir top-K picker
 *
 * Phases still TODO:
 *   2.2.4 WanderInfluenceMap (anti-clumping density score)
 *   2.2.6 SQL-backed profile loader (creature_template_addon.WanderProfileId)
 *   2.2.7 time-budgeted tick scheduler (per-map)
 *   2.2.8 LOD comportemental
 *   2.2.9 migration path from RandomMovementGenerator
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

    // Per-criterion weight. Setting a weight to 0 short-circuits its evaluation,
    // so callers can opt out of expensive checks (e.g. LOS, navmesh) per profile.
    struct CriteriaWeights
    {
        float distance = 1.0f;
        float slope    = 1.0f;
        float navmesh  = 0.7f;
        float water    = 0.0f; // 0 = neutral; >0 land-only or water-only depending on CanSwim
        float memory   = 0.5f;
        float losSpawn = 0.0f; // 0 = ignore LOS; >0 prefer points visible from spawn (guards)
        float density  = 0.5f; // 0 = ignore other wanderers; >0 penalises clumped destinations
        // Penalises candidates that require sharp orientation changes — the
        // creature picks a destination roughly in front of its current facing
        // rather than reversing 180° between two splines. 0.4 is a soft bias,
        // not a hard rule, so creatures still occasionally turn around when
        // forward candidates all score poorly.
        float facing   = 0.4f;
    };

    // A behaviour profile bundles all knobs. WanderProfileMgr (TODO 2.2.6) will
    // load these from SQL; for now, callers can either rely on Default() or
    // pass an ad-hoc Profile pointer to MotionMaster::MoveSmartWander.
    struct Profile
    {
        SamplerMode samplerMode = SAMPLER_POLAR_RANDOM;
        uint8 sampleCount       = 8;
        uint8 topK              = 3;
        uint8 memorySize        = 5;
        float minRadius         = 3.0f;
        float maxRadius         = 0.0f; // 0 means "use Creature::GetRespawnRadius()"
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
    // Profile pointer is borrowed — caller (or WanderProfileMgr) owns it.
    // nullptr falls back to SmartWander::Profile::Default().
    //
    // explicitMaxRadius > 0 overrides the profile's maxRadius and the
    // creature's GetRespawnRadius() — used by legacy callers (script
    // MotionMaster::MoveRandom(dist)) that want a one-shot range override
    // without authoring a Profile row.
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
    // Caches "no player within LOD_RANGE yards" with a TTL. Cheap on hits,
    // ~grid search on misses — but the TTL (5s) makes it amortise nicely
    // since SmartWander only checks at decision time, every 4-8s anyway.
    bool IsFarFromPlayers(Creature* owner);

    SmartWander::Profile const* _profile;
    float _explicitMaxRadius = 0.0f;
    G3D::Vector3 _spawnPos = G3D::Vector3::zero();
    // Where this generator has currently "claimed" a cell in the Map's
    // WanderInfluenceMap. Updated whenever we pick a new destination so the
    // count moves with us; cleared in DoFinalize.
    G3D::Vector3 _influenceClaimedPos = G3D::Vector3::zero();
    bool _hasInfluenceClaim = false;
    std::deque<G3D::Vector3> _memory;
    TimeTracker _nextTickTimer;
    bool _isMoving = false;
    // Scheduler slot assigned at DoInitialize. The generator can only run
    // TryLaunchMove when its slot is active on the Map's WanderTickScheduler.
    uint8 _tickSlot = 0;
    bool _slotRegistered = false;
    // LOD cache: timestamp of last "is a player near?" check + the cached
    // result. Refreshed every LOD_REFRESH_MS, used to skip expensive criteria
    // for creatures nobody can see.
    uint32 _lodLastCheckedMs = 0;
    bool _lodActive = false;
};

#endif
