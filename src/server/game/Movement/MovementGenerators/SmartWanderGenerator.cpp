/*
 * Copyright (C) DestinyCore <https://www.destinycore.org/>
 *
 * SmartWanderGenerator implementation — see SmartWanderGenerator.h for the
 * design rationale.
 *
 * Implementation notes:
 *   - All scoring helpers, the sampler, and the picker live in an anonymous
 *     namespace at the top of this file. Once Phase 2.2.6 (SQL profiles) and
 *     2.2.4 (influence map) land we can split them out cleanly.
 *   - The navmesh score builds a real PathGenerator per candidate. This is
 *     expensive, so we gate it on the other (cheap) criteria first — only
 *     candidates that already look good get the A* check.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "SmartWanderGenerator.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Map.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"
#include "PathGenerator.h"
#include "PhasingHandler.h"
#include "Random.h"
#include "Util.h"
#include "VMapManager2.h"
#include "WanderInfluenceMap.h"
#include "WanderTickScheduler.h"
#include "ObjectDefines.h"
#include "Player.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>
#include <optional>

// --- LOD tuning -----------------------------------------------------------
// Range beyond which we consider "nobody can see this creature": 1.5x the
// player visibility distance, so creatures right at the edge of someone's
// screen still get full quality (no popping when they cross threshold).
static constexpr float LOD_NEAR_PLAYER_RANGE = DEFAULT_VISIBILITY_DISTANCE * 1.5f;
// TTL for the "is a player near?" cache. SmartWander decisions fire every
// 4-8 seconds, so 5s means at most one player search per tick. Stale entries
// for ~5s are acceptable — a player who teleports next to the creature will
// see degraded behaviour for at most 5s, then full-quality kicks back in.
static constexpr uint32 LOD_REFRESH_MS = 5000;

namespace SmartWander
{
    Profile const& Profile::Default()
    {
        // Default-initialized POD: polar sampler, 8 candidates, top-K 3,
        // weights distance/slope/navmesh/memory enabled, water/LOS off.
        static const Profile defaultProfile{};
        return defaultProfile;
    }
}

namespace
{
    using Vec3 = G3D::Vector3;

    // ---- Sampler --------------------------------------------------------

    Vec3 SamplePolar(Vec3 const& origin, float minR, float maxR)
    {
        float angle = float(rand_norm() * 2.0 * M_PI);
        float r = minR + float(rand_norm()) * std::max(0.0f, maxR - minR);
        return Vec3(origin.x + std::cos(angle) * r,
                    origin.y + std::sin(angle) * r,
                    origin.z);
    }

    // Single-dimension Halton (van der Corput) sequence value.
    float HaltonComponent(uint32 index, uint32 base)
    {
        float f = 1.0f;
        float r = 0.0f;
        while (index > 0)
        {
            f /= float(base);
            r += f * float(index % base);
            index /= base;
        }
        return r;
    }

    Vec3 SampleHalton(uint32 seq, Vec3 const& origin, float minR, float maxR)
    {
        // Classic 2,3 Halton pair — angle from base-2, radius from base-3.
        float angle = HaltonComponent(seq + 1, 2) * 2.0f * float(M_PI);
        float r = minR + HaltonComponent(seq + 1, 3) * std::max(0.0f, maxR - minR);
        return Vec3(origin.x + std::cos(angle) * r,
                    origin.y + std::sin(angle) * r,
                    origin.z);
    }

    std::vector<Vec3> GenerateCandidates(SmartWander::Profile const& profile,
                                         Vec3 const& origin, float effectiveMaxR,
                                         uint8 sampleCount)
    {
        std::vector<Vec3> out;
        out.reserve(sampleCount);

        if (profile.samplerMode == SmartWander::SAMPLER_HALTON)
        {
            // Per-call random offset to avoid every NPC sampling the exact same
            // points at the exact same time — preserves the low-discrepancy
            // property within a tick but decorrelates across NPCs.
            uint32 seed = rand32() % 4096;
            for (uint8 i = 0; i < sampleCount; ++i)
                out.push_back(SampleHalton(seed + i, origin, profile.minRadius, effectiveMaxR));
        }
        else
        {
            for (uint8 i = 0; i < sampleCount; ++i)
                out.push_back(SamplePolar(origin, profile.minRadius, effectiveMaxR));
        }
        return out;
    }

    // ---- Scoring helpers ------------------------------------------------

    struct ScoredCandidate
    {
        Vec3 pos;
        float score;
    };

    // Trapezoidal response curve: 0 outside [lo,hi], 1 in [plateauLo,plateauHi],
    // linear ramp on the shoulders. Used for distance scoring (we want neither
    // too close nor too far from spawn).
    float TrapezoidCurve(float v, float lo, float plateauLo, float plateauHi, float hi)
    {
        if (v <= lo || v >= hi)
            return 0.0f;
        if (v <= plateauLo)
            return (plateauLo > lo) ? (v - lo) / (plateauLo - lo) : 1.0f;
        if (v >= plateauHi)
            return (hi > plateauHi) ? (hi - v) / (hi - plateauHi) : 1.0f;
        return 1.0f;
    }

    float ScoreDistance(Vec3 const& candidate, Vec3 const& spawn,
                        float minR, float maxR)
    {
        float dx = candidate.x - spawn.x;
        float dy = candidate.y - spawn.y;
        float d = std::sqrt(dx * dx + dy * dy);
        float plateauLo = minR + (maxR - minR) * 0.3f;
        float plateauHi = minR + (maxR - minR) * 0.7f;
        return TrapezoidCurve(d, 0.0f, plateauLo, plateauHi, maxR * 1.2f);
    }

    float ScoreSlope(Creature const* creature, Vec3 const& candidate)
    {
        float x = creature->GetPositionX();
        float y = creature->GetPositionY();
        float z = creature->GetPositionZ();
        // Use a conservative "human-sized" 2.0f height — the actual collision
        // height isn't worth computing here, and the static formula already
        // tolerates a wide range.
        float reqH = PathGenerator::GetRequiredHeightToClimb(x, y, z,
                                                             candidate.x, candidate.y, candidate.z,
                                                             2.0f);
        float diffZ = std::abs(candidate.z - z);
        if (reqH <= 0.0f)
            return 0.0f;
        if (diffZ >= reqH)
            return 0.0f;
        // Linear taper: more headroom → higher score
        return std::clamp(1.0f - diffZ / reqH, 0.0f, 1.0f);
    }

    float ScoreNavmesh(Creature* creature, Vec3 const& candidate)
    {
        // PathGenerator construction is cheap but the path search itself isn't.
        // Caller should already have gated this on cheaper criteria.
        PathGenerator path(creature);
        if (!path.CalculatePath(candidate.x, candidate.y, candidate.z, false))
            return 0.0f;

        PathType type = path.GetPathType();
        if (type & PATHFIND_NOPATH)
            return 0.0f;
        if (!(type & PATHFIND_NORMAL))
            return 0.3f; // partial / shortcut paths — usable but penalised

        auto const& pts = path.GetPath();
        if (pts.size() < 2)
            return 1.0f;

        float pathLen = 0.0f;
        for (size_t i = 1; i < pts.size(); ++i)
            pathLen += (pts[i] - pts[i - 1]).length();

        float dx = candidate.x - creature->GetPositionX();
        float dy = candidate.y - creature->GetPositionY();
        float euclidean = std::sqrt(dx * dx + dy * dy);
        if (euclidean <= 0.01f)
            return 0.0f;

        // AC-style heuristic: a "good" path is at most 1.85× the straight-line
        // distance; beyond 3× we treat it as garbage (detour around the world).
        float ratio = pathLen / euclidean;
        if (ratio <= 1.85f)
            return 1.0f;
        if (ratio >= 3.0f)
            return 0.0f;
        return 1.0f - (ratio - 1.85f) / (3.0f - 1.85f);
    }

    float ScoreWater(Creature* creature, Vec3 const& candidate)
    {
        Map const* map = creature->GetMap();
        bool inWater = map->IsInWater(creature->GetPhaseShift(),
                                      candidate.x, candidate.y, candidate.z);
        if (creature->CanSwim())
            return inWater ? 1.0f : 0.5f;
        // Landlubber: reject water hard
        return inWater ? 0.0f : 1.0f;
    }

    float ScoreMemory(Vec3 const& candidate, std::deque<Vec3> const& memory)
    {
        if (memory.empty())
            return 1.0f;
        float minD = std::numeric_limits<float>::max();
        for (Vec3 const& m : memory)
        {
            float dx = candidate.x - m.x;
            float dy = candidate.y - m.y;
            float d = std::sqrt(dx * dx + dy * dy);
            if (d < minD)
                minD = d;
        }
        // <1y from a recent spot: same place; >10y: unrelated. Linear in-between.
        if (minD < 1.0f)
            return 0.0f;
        if (minD > 10.0f)
            return 1.0f;
        return (minD - 1.0f) / 9.0f;
    }

    // Facing score: rewards candidates lying in roughly the same direction
    // the creature is currently facing. 0 rad (dead ahead) = 1.0, π rad
    // (behind) = 0.0, linear in between. Killing the 180°-snap-turn jank
    // between two consecutive splines without making the wander predictable
    // — the weight is intentionally low (default 0.4) so this only "breaks
    // ties" between geometrically equivalent candidates.
    float ScoreFacing(Creature const* creature, Vec3 const& candidate)
    {
        float dx = candidate.x - creature->GetPositionX();
        float dy = candidate.y - creature->GetPositionY();
        if (dx * dx + dy * dy < 0.01f)
            return 0.0f;

        float candidateBearing = std::atan2(dy, dx);
        float diff = std::fabs(Position::NormalizeOrientation(candidateBearing - creature->GetOrientation()));
        if (diff > float(M_PI))
            diff = 2.0f * float(M_PI) - diff;
        return 1.0f - diff / float(M_PI);
    }

    // Density score: penalise candidates whose cell is already crowded with
    // other smart-wanderers. We bias the saturation point at 5 wanderers per
    // 8m cell — past that, the cell is "full" and the candidate scores 0.
    // We do NOT subtract this creature's own claim: candidates rarely land in
    // exactly the same cell as the previous claim, and when they do, a small
    // density penalty actually helps push the creature out of its current spot.
    float ScoreDensity(Creature* creature, Vec3 const& candidate)
    {
        uint16 d = creature->GetMap()->GetWanderInfluence().GetDensity(candidate.x, candidate.y);
        if (d == 0)
            return 1.0f;
        if (d >= 5)
            return 0.0f;
        return 1.0f - float(d) / 5.0f;
    }

    float ScoreLOSToSpawn(Creature const* creature, Vec3 const& candidate, Vec3 const& spawn)
    {
        Map const* map = creature->GetMap();
        // +2.0z to "stand up" the LOS rays so we test creature-eye to creature-eye
        // rather than feet-to-feet (which would always hit the ground triangle).
        bool los = map->isInLineOfSight(creature->GetPhaseShift(),
                                        spawn.x, spawn.y, spawn.z + 2.0f,
                                        candidate.x, candidate.y, candidate.z + 2.0f,
                                        VMAP::ModelIgnoreFlags::Nothing);
        return los ? 1.0f : 0.0f;
    }

    // ---- Reservoir top-K picker -----------------------------------------

    // Sorts the top-K by score (descending), then performs a weighted random
    // pick across the top-K. Returns nullopt if no positive-scored candidate
    // exists. Weighted pick is a single-pass cumulative-sum scan (so
    // technically "weighted roulette wheel", not reservoir sampling — but the
    // semantics callers care about are "pick proportional to score from top-K"
    // and that's what this does in one pass over the bucket).
    std::optional<Vec3> PickTopK(std::vector<ScoredCandidate>& candidates, uint8 topK)
    {
        if (candidates.empty())
            return std::nullopt;

        size_t k = std::min<size_t>(topK, candidates.size());
        std::partial_sort(candidates.begin(),
                          candidates.begin() + k,
                          candidates.end(),
                          [](ScoredCandidate const& a, ScoredCandidate const& b)
                          { return a.score > b.score; });

        // Drop trailing zero-scored entries from the pool
        while (k > 0 && candidates[k - 1].score <= 0.0f)
            --k;
        if (k == 0)
            return std::nullopt;

        float total = 0.0f;
        for (size_t i = 0; i < k; ++i)
            total += candidates[i].score;

        if (total <= 0.0f)
            return std::nullopt;

        float r = float(rand_norm()) * total;
        float acc = 0.0f;
        for (size_t i = 0; i < k; ++i)
        {
            acc += candidates[i].score;
            if (r <= acc)
                return candidates[i].pos;
        }
        return candidates[0].pos;
    }
}

// ---- SmartWanderGenerator -----------------------------------------------

SmartWanderGenerator::SmartWanderGenerator(SmartWander::Profile const* profile, float explicitMaxRadius)
    : _profile(profile ? profile : &SmartWander::Profile::Default()),
      _explicitMaxRadius(explicitMaxRadius),
      _nextTickTimer(0)
{
}

float SmartWanderGenerator::ResolveMaxRadius(Creature const* owner) const
{
    // Cascade: explicit ctor arg > profile > creature respawn radius > 10y.
    if (_explicitMaxRadius > 0.0f)
        return _explicitMaxRadius;
    if (_profile->maxRadius > 0.0f)
        return _profile->maxRadius;
    float r = owner->GetRespawnRadius();
    return r > 0.0f ? r : 10.0f;
}

void SmartWanderGenerator::DoInitialize(Creature* owner)
{
    if (!owner->IsAlive())
        return;

    if (owner->HasUnitState(UNIT_STATE_NOT_MOVE))
    {
        owner->StopMoving();
        return;
    }

    float ox, oy, oz, oo;
    owner->GetHomePosition(ox, oy, oz, oo);
    _spawnPos = Vec3(ox, oy, oz);
    _memory.clear();
    _isMoving = false;

    // Stake our claim in the WanderInfluenceMap at the spawn cell. We'll
    // migrate this claim each time we pick a new destination.
    _influenceClaimedPos = _spawnPos;
    owner->GetMap()->GetWanderInfluence().Add(_spawnPos.x, _spawnPos.y);
    _hasInfluenceClaim = true;

    // Get a stable scheduler slot once. DoReset re-enters DoInitialize but we
    // keep the existing slot to avoid bunching all reset generators into the
    // same slot bucket (e.g. after a mass evade event).
    if (!_slotRegistered)
    {
        _tickSlot = owner->GetMap()->GetWanderScheduler().RegisterSlot();
        _slotRegistered = true;
    }

    owner->AddUnitState(UNIT_STATE_ROAMING);
    _nextTickTimer.Reset(0);
}

void SmartWanderGenerator::DoFinalize(Creature* owner)
{
    owner->ClearUnitState(UNIT_STATE_ROAMING | UNIT_STATE_ROAMING_MOVE);
    if (owner->IsAlive() && _isMoving)
        owner->StopMoving();

    // Release the cell we were claiming so we don't leak density forever
    // when the generator is replaced (combat reset, despawn, possession...).
    if (_hasInfluenceClaim && owner->FindMap())
    {
        owner->GetMap()->GetWanderInfluence().Remove(_influenceClaimedPos.x, _influenceClaimedPos.y);
        _hasInfluenceClaim = false;
    }
}

void SmartWanderGenerator::DoReset(Creature* owner)
{
    DoInitialize(owner);
}

bool SmartWanderGenerator::DoUpdate(Creature* owner, uint32 diff)
{
    if (!owner || !owner->IsAlive())
        return false;

    if (owner->HasUnitState(UNIT_STATE_NOT_MOVE | UNIT_STATE_DISTRACTED))
    {
        owner->StopMoving();
        owner->ClearUnitState(UNIT_STATE_ROAMING_MOVE);
        _nextTickTimer.Reset(0);
        _isMoving = false;
        return true;
    }

    // Mid-spline: let it finish.
    if (_isMoving)
    {
        if (owner->movespline->Finalized())
        {
            _isMoving = false;
            owner->ClearUnitState(UNIT_STATE_ROAMING_MOVE);
            // Inter-move pause is sampled fresh each time — variable rhythm
            // hides the underlying tick cadence from the player's eye.
            uint32 pause = urand(_profile->tickMsMin, _profile->tickMsMax);
            _nextTickTimer.Reset(pause);
        }
        return true;
    }

    _nextTickTimer.Update(diff);
    if (!_nextTickTimer.Passed())
        return true;

    // Time-budgeted dispatch: even if our timer is up, we only run the
    // heavy decision pipeline when our slot is active this frame. With 8
    // slots and ~16ms per Map update the wait is bounded to ~128ms — well
    // below any player-perceivable threshold for idle wander cadence.
    if (!owner->GetMap()->GetWanderScheduler().IsActiveSlot(_tickSlot))
        return true;

    if (!TryLaunchMove(owner))
    {
        // No usable candidate this tick — back off briefly rather than spin.
        _nextTickTimer.Reset(1000);
    }
    return true;
}

bool SmartWanderGenerator::IsFarFromPlayers(Creature* owner)
{
    uint32 now = getMSTime();
    if (now - _lodLastCheckedMs < LOD_REFRESH_MS)
        return _lodActive;

    _lodLastCheckedMs = now;
    // FindNearestPlayer iterates surrounding grid cells. Cheap if grids are
    // sparse (typical case for distant zones) but it's still the most
    // expensive thing in the LOD path — hence the TTL above.
    _lodActive = owner->FindNearestPlayer(LOD_NEAR_PLAYER_RANGE) == nullptr;
    return _lodActive;
}

bool SmartWanderGenerator::TryLaunchMove(Creature* owner)
{
    bool lod = IsFarFromPlayers(owner);
    float maxR = ResolveMaxRadius(owner);

    // LOD mode: halve the candidate count, skip expensive criteria entirely.
    // The trapezoidal distance + slope + memory checks remain since they're
    // cheap and keep behaviour sane even off-camera.
    uint8 effectiveSampleCount = lod
        ? std::max<uint8>(2, _profile->sampleCount / 2)
        : _profile->sampleCount;
    uint8 effectiveTopK = lod
        ? std::min<uint8>(_profile->topK, 2)
        : _profile->topK;

    std::vector<Vec3> candidates = GenerateCandidates(*_profile, _spawnPos, maxR, effectiveSampleCount);

    std::vector<ScoredCandidate> scored;
    scored.reserve(candidates.size());

    SmartWander::CriteriaWeights const& w = _profile->weights;
    Map const* map = owner->GetMap();

    bool const canFly = owner->CanFly();
    // For flyers, vary Z around spawn altitude with a fraction of the wander
    // range — keeps them aloft without yo-yoing in 3D. Conservatively biased
    // upward (more headroom) than downward (don't drop too close to the floor).
    float const flyDzUp    = canFly ? std::max(2.0f, maxR * 0.25f) : 0.0f;
    float const flyDzDown  = canFly ? std::max(1.0f, maxR * 0.15f) : 0.0f;

    for (Vec3& c : candidates)
    {
        if (canFly)
        {
            // Sample altitude around the spawn Z, ensure we end up above the
            // ground/water level at the candidate's XY. Mirrors the legacy
            // RandomMovementGenerator's "is_air_ok" path.
            c.z = _spawnPos.z + frand(-flyDzDown, flyDzUp);
            float floorZ = map->GetWaterOrGroundLevel(owner->GetPhaseShift(), c.x, c.y, c.z);
            if (floorZ >= c.z)
                continue; // would clip into terrain/water — try next candidate
        }
        else
        {
            // Ground-clamp non-flyers. Skip invalid samples (off-grid).
            float groundZ = map->GetHeight(owner->GetPhaseShift(), c.x, c.y, c.z + 2.0f, true);
            if (groundZ <= INVALID_HEIGHT)
                continue;
            c.z = groundZ;
        }

        float weightedSum = 0.0f;
        float weightTotal = 0.0f;

        if (w.distance > 0.0f)
        {
            weightedSum += w.distance * ScoreDistance(c, _spawnPos, _profile->minRadius, maxR);
            weightTotal += w.distance;
        }
        if (w.slope > 0.0f)
        {
            weightedSum += w.slope * ScoreSlope(owner, c);
            weightTotal += w.slope;
        }
        if (w.water > 0.0f)
        {
            weightedSum += w.water * ScoreWater(owner, c);
            weightTotal += w.water;
        }
        if (w.memory > 0.0f)
        {
            weightedSum += w.memory * ScoreMemory(c, _memory);
            weightTotal += w.memory;
        }
        // Facing bias is cheap (atan2 + 1 subtract) and crucial visually —
        // keep it on even under LOD.
        if (w.facing > 0.0f)
        {
            weightedSum += w.facing * ScoreFacing(owner, c);
            weightTotal += w.facing;
        }
        // LOS, density and navmesh A* are skipped entirely under LOD — none of
        // them produce a perceptible difference for a creature no player is
        // looking at, and they're by far the most expensive criteria.
        if (!lod && w.losSpawn > 0.0f)
        {
            weightedSum += w.losSpawn * ScoreLOSToSpawn(owner, c, _spawnPos);
            weightTotal += w.losSpawn;
        }
        if (!lod && w.density > 0.0f)
        {
            weightedSum += w.density * ScoreDensity(owner, c);
            weightTotal += w.density;
        }

        // Cheap-first gate: only spend an A* on candidates that already look
        // viable on the basis of geometry + memory + density. Threshold 0.3
        // is empirical — low enough to allow recovery from a bad memory hit
        // or a moderately crowded cell, high enough to skip broken candidates.
        if (!lod && w.navmesh > 0.0f && weightTotal > 0.0f && (weightedSum / weightTotal) > 0.3f)
        {
            weightedSum += w.navmesh * ScoreNavmesh(owner, c);
            weightTotal += w.navmesh;
        }

        if (weightTotal > 0.0f)
            scored.push_back({c, weightedSum / weightTotal});
    }

    std::optional<Vec3> picked = PickTopK(scored, effectiveTopK);
    if (!picked)
        return false;

    Movement::MoveSplineInit init(owner);

    // Smooth-path the chosen destination: re-run PathGenerator on the winner
    // and feed the full navmesh path through MovebyPath + catmullrom. This is
    // an extra A* (we already ran one in scoring on the same candidate, but
    // discarded the points), but it costs ~1/N of total scoring work — fine
    // tradeoff for visibly smoother movement.
    //
    // LOD: don't bother smoothing for creatures no player is looking at.
    // The catmullrom curve is purely visual; out of sight it's wasted A*.
    //
    // - Path size <= 1 OR NOPATH/NOT_USING_PATH: fall back to direct MoveTo
    //   (catmullrom needs 2+ points; otherwise it would degenerate).
    // - Path size == 2: catmullrom degrades to linear, same as MoveTo.
    // - Path size >= 3 (obstacle avoidance): visible curve through waypoints.
    bool smoothLaunched = false;
    if (!lod)
    {
        PathGenerator pathfinder(owner);
        if (pathfinder.CalculatePath(picked->x, picked->y, picked->z, false))
        {
            PathType type = pathfinder.GetPathType();
            if (!(type & PATHFIND_NOPATH) && !(type & PATHFIND_NOT_USING_PATH))
            {
                Movement::PointsArray const& points = pathfinder.GetPath();
                // >= 3 points = real obstacle-avoiding detour. 2-point paths
                // are straight lines and catmullrom can overshoot near tight
                // corners — see TargetedMovementGenerator.cpp for the same
                // reasoning. We accept linear motion for trivial paths.
                if (points.size() >= 3)
                {
                    init.MovebyPath(points);
                    init.SetSmooth();
                    smoothLaunched = true;
                }
                else if (points.size() == 2)
                {
                    init.MovebyPath(points);
                    smoothLaunched = true;
                }
            }
        }
    }
    if (!smoothLaunched)
        init.MoveTo(picked->x, picked->y, picked->z);

    init.SetWalk(owner->GetMovementMode() != 1);
    init.Launch();

    // Migrate our influence claim to the new target cell. Map::GetWanderInfluence
    // is a no-op if old and new map to the same cell.
    owner->GetMap()->GetWanderInfluence().Move(_influenceClaimedPos.x, _influenceClaimedPos.y,
                                               picked->x, picked->y);
    _influenceClaimedPos = *picked;
    _hasInfluenceClaim = true;

    RememberPoint(*picked);
    owner->AddUnitState(UNIT_STATE_ROAMING_MOVE);
    _isMoving = true;
    return true;
}

void SmartWanderGenerator::RememberPoint(Vec3 const& p)
{
    _memory.push_back(p);
    while (_memory.size() > _profile->memorySize)
        _memory.pop_front();
}

bool SmartWanderGenerator::GetResetPosition(Unit*, float& x, float& y, float& z)
{
    x = _spawnPos.x;
    y = _spawnPos.y;
    z = _spawnPos.z;
    return true;
}
