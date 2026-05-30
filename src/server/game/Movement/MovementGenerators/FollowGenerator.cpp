#include "FollowGenerator.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "LocalAvoidance.h"
#include "Map.h"
#include "MoveSpline.h"
#include "MovementPredictor.h"
#include "MovementServices.h"
#include "ObjectAccessor.h"
#include "PathPlanner.h"
#include "PredictionService.h"
#include "SplineExecutor.h"
#include "Timer.h"
#include "Unit.h"
#include <algorithm>
#include <cmath>

namespace
{
    // Below this the leader is essentially stationary.
    constexpr float FOLLOW_REPLAN_THRESHOLD_Y = 0.2f;

    // Tighter than chase margins because the follow target is a single point,
    // not a melee circle - half a yard of drift is visibly wrong on a pet.
    constexpr float FOLLOW_BAND_MARGIN_Y = 0.5f;

    constexpr uint32 FOLLOW_LEAD_HORIZON_MS = 200;
    constexpr float FOLLOW_LEAD_MIN_SPEED = 1.0f;

    constexpr float FOLLOW_AVOIDANCE_HORIZON_SEC = 0.1f;
    constexpr float FOLLOW_AVOIDANCE_MIN_SPEED = 0.5f;

    float Dist2DSq(Position const& a, Position const& b)
    {
        float const dx = a.GetPositionX() - b.GetPositionX();
        float const dy = a.GetPositionY() - b.GetPositionY();
        return dx * dx + dy * dy;
    }

    bool LeaderMovedEnough(Position const& current, Position const& last)
    {
        return Dist2DSq(current, last) > FOLLOW_REPLAN_THRESHOLD_Y * FOLLOW_REPLAN_THRESHOLD_Y;
    }

    Movement::MovementServices* GetServices(Unit* owner)
    {
        if (!owner)
            return nullptr;
        Map* map = owner->GetMap();
        return map ? map->GetMovementServices() : nullptr;
    }

    // Pets use a fixed 1.0y so the visual gap stays constant regardless of
    // the master's hitbox (gnome vs worgen).
    Position ComputeFollowAnchor(Unit* owner, Unit* leader, float offset, float angle)
    {
        float const size = (owner->IsPet() && leader->GetTypeId() == TYPEID_PLAYER)
            ? 1.0f
            : owner->GetObjectSize();

        float const absAngle = Position::NormalizeOrientation(leader->GetOrientation() + angle);

        float x = 0.0f, y = 0.0f, z = 0.0f;
        leader->GetNearPoint(owner, x, y, z, size, offset, absAngle);
        return Position(x, y, z);
    }

    Position ApplyKalmanLead(Position const& anchor, Unit* leader, Movement::MovementPredictor const* predictor)
    {
        if (!predictor || !predictor->IsConverged() || predictor->GetEstimatedSpeed() < FOLLOW_LEAD_MIN_SPEED)
            return anchor;

        Position const predicted = predictor->PredictAt(FOLLOW_LEAD_HORIZON_MS);
        return Position(anchor.GetPositionX() + (predicted.GetPositionX() - leader->GetPositionX()),
                        anchor.GetPositionY() + (predicted.GetPositionY() - leader->GetPositionY()),
                        anchor.GetPositionZ());
    }
}

FollowGenerator::FollowGenerator(Unit* leader, float offset, float angle)
    : _leaderGuid(leader ? leader->GetGUID() : ObjectGuid::Empty),
      _offset(offset),
      _angle(angle)
{
}

FollowGenerator::~FollowGenerator() = default;

void FollowGenerator::Initialize(Unit* owner)
{
    if (!owner)
        return;
    owner->AddUnitState(UNIT_STATE_FOLLOW | UNIT_STATE_FOLLOW_MOVE);

    _splineExecutor = std::make_unique<Movement::SplineExecutor>(owner);
    _state = FollowGeneratorState::Planning;
    _lastLeaderPosition.reset();
    _movementInformPending = false;

    Unit* const leader = ObjectAccessor::GetUnit(*owner, _leaderGuid);
    _petMirrorMode = owner->IsPet() && leader && leader->GetGUID() == owner->GetOwnerGUID();

    if (leader)
        SyncPetSpeed(owner, leader);

    // Pets stay registered in ORCA so other agents still route around them;
    // the mirror mode only suppresses ORCA's nudge on the pet itself.
    if (Movement::MovementServices* services = GetServices(owner))
        if (Movement::LocalAvoidance* avoid = services->GetLocalAvoidance())
            avoid->RegisterAgent(owner);
}

void FollowGenerator::Finalize(Unit* owner)
{
    if (!owner)
        return;
    owner->ClearUnitState(UNIT_STATE_FOLLOW | UNIT_STATE_FOLLOW_MOVE);

    if (Unit* leader = ObjectAccessor::GetUnit(*owner, _leaderGuid))
        SyncPetSpeed(owner, leader);

    if (Movement::MovementServices* services = GetServices(owner))
        if (Movement::LocalAvoidance* avoid = services->GetLocalAvoidance())
            avoid->UnregisterAgent(owner->GetGUID());
}

void FollowGenerator::Reset(Unit* owner)
{
    Initialize(owner);
}

void FollowGenerator::unitSpeedChanged()
{
    _lastLeaderPosition.reset();
    if (_state != FollowGeneratorState::Planning)
        _state = FollowGeneratorState::Planning;
}

Unit* FollowGenerator::ResolveLeader(Unit* owner)
{
    if (!owner || _leaderGuid.IsEmpty())
        return nullptr;
    Unit* leader = ObjectAccessor::GetUnit(*owner, _leaderGuid);
    if (!leader || !leader->IsInWorld() || !leader->IsInMap(owner))
        return nullptr;
    return leader;
}

bool FollowGenerator::CanFollow(Unit* owner, Unit* leader) const
{
    if (!owner || !leader)
        return false;
    if (!owner->IsAlive())
        return false;
    if (owner->HasUnitState(UNIT_STATE_NOT_MOVE))
        return false;
    if (owner->IsMovementPreventedByCasting())
        return false;
    return true;
}

void FollowGenerator::SyncPetSpeed(Unit* owner, Unit* leader) const
{
    // Only pets share their master's movement rate.
    if (!owner || !owner->IsPet() || !owner->IsInWorld() || !leader)
        return;
    if (leader->GetGUID() != owner->GetOwnerGUID())
        return;
    owner->UpdateSpeed(MOVE_RUN);
    owner->UpdateSpeed(MOVE_WALK);
    owner->UpdateSpeed(MOVE_SWIM);
}

bool FollowGenerator::Update(Unit* owner, uint32 /*diff*/)
{
    Unit* leader = ResolveLeader(owner);
    if (!leader || !CanFollow(owner, leader))
        return false;

    Movement::MovementServices* const services = GetServices(owner);
    if (!services)
        return true;
    Movement::PathPlanner* const planner = services->GetPathPlanner();
    if (!planner)
        return true;

    Movement::LocalAvoidance* const avoidance = services->GetLocalAvoidance();

    // Pets mirror 1:1; generic followers use sub-second Kalman extrapolation.
    Movement::MovementPredictor const* const predictor = (!_petMirrorMode && services->GetPredictionService())
        ? services->GetPredictionService()->TrackTarget(leader)
        : nullptr;

    Position const currentLeaderPos = leader->GetPosition();
    bool const leaderMoved = !_lastLeaderPosition
        || LeaderMovedEnough(currentLeaderPos, *_lastLeaderPosition);

    auto resolveStartPosition = [&]() -> Position
    {
        if (owner->movespline && owner->movespline->Initialized() && !owner->movespline->Finalized())
        {
            Movement::Location loc = owner->movespline->ComputePosition();
            return Position(loc.x, loc.y, loc.z, loc.orientation);
        }
        return owner->GetPosition();
    };

    auto tryComputeAndLaunch = [&](Position const& desired) -> bool
    {
        Position const startPos = resolveStartPosition();
        Position effectiveDesired = desired;

        if (avoidance)
        {
            float const maxSpeed = owner->GetSpeed(MOVE_RUN);
            avoidance->SetPreferredVelocity(owner, startPos, desired, maxSpeed);

            // Pet mirror suppresses self-nudge so the pet stays glued to its
            // anchor; other agents still see and route around it.
            if (!_petMirrorMode)
            {
                float const maxBand = FOLLOW_BAND_MARGIN_Y * 2.0f;
                bool const inBand = Dist2DSq(startPos, desired) <= maxBand * maxBand;
                if (inBand)
                {
                    Position const nudgedVel = avoidance->QueryNudgedVelocity(owner->GetGUID());
                    float const nx = nudgedVel.GetPositionX();
                    float const ny = nudgedVel.GetPositionY();
                    float const nudgeSpeed = std::sqrt(nx * nx + ny * ny);
                    if (nudgeSpeed >= FOLLOW_AVOIDANCE_MIN_SPEED)
                    {
                        effectiveDesired = Position(
                            startPos.GetPositionX() + nx * FOLLOW_AVOIDANCE_HORIZON_SEC,
                            startPos.GetPositionY() + ny * FOLLOW_AVOIDANCE_HORIZON_SEC,
                            desired.GetPositionZ());
                    }
                }
            }
        }

        Movement::PathRequest request;
        request.ownerGuid = owner->GetGUID();
        request.from = startPos;
        request.to = effectiveDesired;
        Movement::PathResult result = planner->RequestPathSync(owner, request);

        if (!result.success || result.points.empty())
            return false;

        // MOVEMENTFLAG_BACKWARD is unambiguous; geometry heuristics misread
        // post-180° rotations and long return trips as backward steps.
        bool const backward = _petMirrorMode && leader->HasUnitMovementFlag(MOVEMENTFLAG_BACKWARD);

        bool const walk = leader->IsWalking();
        owner->AddUnitState(UNIT_STATE_FOLLOW_MOVE);

        Movement::SplineLaunchOptions splineOpts;
        splineOpts.walk = walk;
        splineOpts.backward = backward;
        // Lock orientation so the pet keeps facing where the master faces,
        // not where the spline tangent points (otherwise sideways = side-facing).
        if (_petMirrorMode)
            splineOpts.fixedOrientation = leader->GetOrientation();
        _splineExecutor->LaunchAlongPath(result.points, splineOpts);
        _lastLeaderPosition = currentLeaderPos;
        _movementInformPending = true;
        return true;
    };

    switch (_state)
    {
        case FollowGeneratorState::Planning:
        {
            Position const anchor = ComputeFollowAnchor(owner, leader, _offset, _angle);
            Position const desired = _petMirrorMode ? anchor : ApplyKalmanLead(anchor, leader, predictor);
            if (tryComputeAndLaunch(desired))
                _state = FollowGeneratorState::Executing;
            return true;
        }

        case FollowGeneratorState::Executing:
        {
            if (_splineExecutor->IsFinalized())
            {
                owner->ClearUnitState(UNIT_STATE_FOLLOW_MOVE);

                Position const anchor = ComputeFollowAnchor(owner, leader, _offset, _angle);
                bool const inBand = Dist2DSq(owner->GetPosition(), anchor) <= FOLLOW_BAND_MARGIN_Y * FOLLOW_BAND_MARGIN_Y;
                if (inBand)
                {
                    if (_movementInformPending)
                    {
                        if (Creature* creature = owner->ToCreature())
                            if (creature->AI())
                                creature->AI()->MovementInform(FOLLOW_MOTION_TYPE, leader->GetGUID().GetCounter());
                        _movementInformPending = false;
                    }
                    _state = FollowGeneratorState::IdleNearLeader;
                }
                else
                {
                    _state = FollowGeneratorState::Planning;
                }
                return true;
            }

            if (leaderMoved)
            {
                Position const anchor = ComputeFollowAnchor(owner, leader, _offset, _angle);
                // Pet: no deadzone, replan on every motion. Generic: band drift OK.
                bool const shouldReplan = _petMirrorMode
                    || Dist2DSq(owner->GetPosition(), anchor) > FOLLOW_BAND_MARGIN_Y * FOLLOW_BAND_MARGIN_Y;
                if (shouldReplan)
                {
                    Position const desired = _petMirrorMode ? anchor : ApplyKalmanLead(anchor, leader, predictor);
                    tryComputeAndLaunch(desired);
                }
            }
            return true;
        }

        case FollowGeneratorState::IdleNearLeader:
        {
            // 0.1rad filter keeps SetFacing packets from spamming on jitter.
            if (_petMirrorMode)
            {
                float const delta = Position::NormalizeOrientation(leader->GetOrientation() - owner->GetOrientation());
                if (std::fabs(delta) > 0.1f)
                    owner->SetFacingTo(leader->GetOrientation());
            }

            // leader->isMoving() fires at key-press, ~one tick before any
            // position delta accumulates - cuts ~50ms off the pet's react lag.
            // leaderMoved still catches teleports / knockbacks that bypass flags.
            bool const shouldReplan = _petMirrorMode
                ? (leader->isMoving() || leaderMoved)
                : Dist2DSq(owner->GetPosition(), ComputeFollowAnchor(owner, leader, _offset, _angle))
                      > FOLLOW_BAND_MARGIN_Y * FOLLOW_BAND_MARGIN_Y;
            if (shouldReplan)
                _state = FollowGeneratorState::Planning;
            return true;
        }
    }
    return true;
}
