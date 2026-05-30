#include "ChaseGenerator.h"
#include "Creature.h"
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
    // Below this delta the target hasn't moved beyond tick-quantised jitter.
    constexpr float CHASE_REPLAN_THRESHOLD_Y = 0.2f;

    // Asymmetric hysteresis: outer wide so micro-drift doesn't re-trigger the
    // chase, inner narrow so a player pushing in is answered by a step back.
    constexpr float CHASE_BAND_OUTER_MARGIN_Y = 1.5f;
    constexpr float CHASE_BAND_INNER_MARGIN_Y = 0.5f;

    // 200ms keeps the lead point useful without overshooting on direction
    // changes; the constant-velocity Kalman drifts past ~500ms.
    constexpr uint32 CHASE_LEAD_HORIZON_MS = 200;
    constexpr float CHASE_LEAD_MIN_SPEED = 1.0f;

    constexpr float CHASE_AVOIDANCE_HORIZON_SEC = 0.1f;
    constexpr float CHASE_AVOIDANCE_MIN_SPEED = 0.5f;

    Position ComputeDesiredPosition(Unit* owner, Unit* target, Movement::MovementPredictor const* predictor, Optional<ChaseRange> const& range, Optional<ChaseAngle> const& angle)
    {
        float const dxOwner = owner->GetPositionX() - target->GetPositionX();
        float const dyOwner = owner->GetPositionY() - target->GetPositionY();
        float const distOwnerToTarget = std::sqrt(dxOwner * dxOwner + dyOwner * dyOwner);
        float const reach = owner->GetCombatReach() + target->GetCombatReach();
        float const innerBand = std::max(0.5f, reach - CHASE_BAND_INNER_MARGIN_Y);
        bool const backwardCase = !range && !angle && distOwnerToTarget < innerBand;

        // Target the CENTER of the stable band so a single step lands inside
        // the hysteresis window instead of nibbling the inner edge.
        //
        // MovePositionToFirstCollision is required because navmeshes don't
        // cover M2 doodads (trees, props); A* would happily route through
        // them. VMAP raycast clips the backward target at the first collision.
        if (backwardCase)
        {
            float const outerBand = reach + CHASE_BAND_OUTER_MARGIN_Y;
            float const stepDist = (innerBand + outerBand) * 0.5f;
            float const len = distOwnerToTarget;
            float const nx = (len > 1e-3f) ? dxOwner / len : 1.0f;
            float const ny = (len > 1e-3f) ? dyOwner / len : 0.0f;

            float const stepBack = stepDist - len;
            if (stepBack <= 0.0f)
                return owner->GetPosition();

            Position backPos = owner->GetPosition();
            float const worldAngle = std::atan2(ny, nx);
            float const relAngle = worldAngle - owner->GetOrientation();
            owner->MovePositionToFirstCollision(backPos, stepBack, relAngle);
            return backPos;
        }

        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!range && !angle)
        {
            target->GetContactPoint(owner, x, y, z);
        }
        else
        {
            float const nearDist = range
                ? range->MinTolerance + (range->MaxTolerance - range->MinTolerance) * 0.5f
                : 0.0f;
            float const absAngle = angle
                ? Position::NormalizeOrientation(target->GetOrientation() + angle->RelativeAngle)
                : target->GetAngle(owner);
            target->GetNearPoint(owner, x, y, z, owner->GetObjectSize(), nearDist, absAngle);
        }

        // Valid only while the target's orientation doesn't flip during the
        // horizon (true at CHASE_LEAD_HORIZON_MS, unsafe past ~500ms).
        if (predictor && predictor->IsConverged() && predictor->GetEstimatedSpeed() >= CHASE_LEAD_MIN_SPEED)
        {
            Position const predicted = predictor->PredictAt(CHASE_LEAD_HORIZON_MS);
            x += predicted.GetPositionX() - target->GetPositionX();
            y += predicted.GetPositionY() - target->GetPositionY();
        }

        return Position(x, y, z);
    }

    float Dist2DSq(Position const& a, Position const& b)
    {
        float const dx = a.GetPositionX() - b.GetPositionX();
        float const dy = a.GetPositionY() - b.GetPositionY();
        return dx * dx + dy * dy;
    }

    bool TargetMovedEnough(Position const& current, Position const& last)
    {
        return Dist2DSq(current, last) > CHASE_REPLAN_THRESHOLD_Y * CHASE_REPLAN_THRESHOLD_Y;
    }

    Movement::MovementServices* GetServices(Unit* owner)
    {
        if (!owner)
            return nullptr;
        Map* map = owner->GetMap();
        return map ? map->GetMovementServices() : nullptr;
    }
}

ChaseGenerator::ChaseGenerator(Unit* target, Optional<ChaseRange> range, Optional<ChaseAngle> angle)
    : _targetGuid(target ? target->GetGUID() : ObjectGuid::Empty),
      _range(range),
      _angle(angle)
{
}

ChaseGenerator::~ChaseGenerator() = default;

void ChaseGenerator::Initialize(Unit* owner)
{
    if (!owner)
        return;
    owner->AddUnitState(UNIT_STATE_CHASE);
    if (Creature* creature = owner->ToCreature())
    {
        creature->SetCannotReachTarget(false);
        creature->SetWalk(false);
    }
    _splineExecutor = std::make_unique<Movement::SplineExecutor>(owner);
    _state = ChaseGeneratorState::Planning;
    _lastTargetPosition.reset();

    if (Movement::MovementServices* services = GetServices(owner))
        if (Movement::LocalAvoidance* avoid = services->GetLocalAvoidance())
            avoid->RegisterAgent(owner);
}

void ChaseGenerator::Finalize(Unit* owner)
{
    if (!owner)
        return;
    owner->ClearUnitState(UNIT_STATE_CHASE | UNIT_STATE_CHASE_MOVE);
    if (Creature* creature = owner->ToCreature())
        creature->SetCannotReachTarget(false);

    if (Movement::MovementServices* services = GetServices(owner))
        if (Movement::LocalAvoidance* avoid = services->GetLocalAvoidance())
            avoid->UnregisterAgent(owner->GetGUID());
}

void ChaseGenerator::Reset(Unit* owner)
{
    Initialize(owner);
}

void ChaseGenerator::unitSpeedChanged()
{
    _lastTargetPosition.reset();
    if (_state == ChaseGeneratorState::Executing || _state == ChaseGeneratorState::IdleInBand)
        _state = ChaseGeneratorState::Planning;
}

void ChaseGenerator::SetRangeAndAngle(Optional<ChaseRange> range, Optional<ChaseAngle> angle)
{
    _range = range;
    _angle = angle;
    _lastTargetPosition.reset();
    _state = ChaseGeneratorState::Planning;
}

void ChaseGenerator::DistanceYourself(Unit* /*owner*/, float /*distance*/)
{
}

Unit* ChaseGenerator::ResolveTarget(Unit* owner)
{
    if (!owner || _targetGuid.IsEmpty())
        return nullptr;
    Unit* target = ObjectAccessor::GetUnit(*owner, _targetGuid);
    if (!target || !target->IsInWorld() || !target->IsInMap(owner))
        return nullptr;
    return target;
}

bool ChaseGenerator::CanChase(Unit* owner, Unit* target) const
{
    if (!owner || !target)
        return false;
    if (!owner->IsAlive() || !target->IsAlive())
        return false;
    if (owner->HasUnitState(UNIT_STATE_NOT_MOVE))
        return false;
    if (owner->IsMovementPreventedByCasting())
        return false;
    return true;
}

bool ChaseGenerator::Update(Unit* owner, uint32 /*diff*/)
{
    Unit* target = ResolveTarget(owner);
    if (!target || !CanChase(owner, target))
        return false;

    Movement::MovementServices* const services = GetServices(owner);
    if (!services)
        return true;
    Movement::PathPlanner* const planner = services->GetPathPlanner();
    if (!planner)
        return true;

    Movement::LocalAvoidance* const avoidance = services->GetLocalAvoidance();
    Movement::PredictionService* const predictionService = services->GetPredictionService();
    Movement::MovementPredictor const* const predictor = predictionService
        ? predictionService->TrackTarget(target)
        : nullptr;

    Position const currentTargetPos = target->GetPosition();
    bool const targetMoved = !_lastTargetPosition
        || TargetMovedEnough(currentTargetPos, *_lastTargetPosition);

    // Unit::m_positionX/Y/Z lags the visual position until the next sync
    // frame; planning from the interpolated spline position avoids a
    // backward-pivot on the chained spline.
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

        // Only the in-band positioning applies the ORCA nudge: the long
        // approach must stay in a straight line so a pack converges instead
        // of zig-zagging. SetPreferredVelocity stays unconditional so other
        // chasers still see this agent as an obstacle during the approach.
        if (avoidance)
        {
            float const maxSpeed = owner->GetSpeed(MOVE_RUN);
            avoidance->SetPreferredVelocity(owner, startPos, desired, maxSpeed);

            float const reach = owner->GetCombatReach() + target->GetCombatReach();
            float const maxBand = reach + CHASE_BAND_OUTER_MARGIN_Y;
            bool const inCombatBand = Dist2DSq(startPos, target->GetPosition()) <= maxBand * maxBand;

            if (inCombatBand)
            {
                Position const nudgedVel = avoidance->QueryNudgedVelocity(owner->GetGUID());
                float const nx = nudgedVel.GetPositionX();
                float const ny = nudgedVel.GetPositionY();
                float const nudgeSpeed = std::sqrt(nx * nx + ny * ny);
                if (nudgeSpeed >= CHASE_AVOIDANCE_MIN_SPEED)
                {
                    effectiveDesired = Position(
                        startPos.GetPositionX() + nx * CHASE_AVOIDANCE_HORIZON_SEC,
                        startPos.GetPositionY() + ny * CHASE_AVOIDANCE_HORIZON_SEC,
                        desired.GetPositionZ());
                }
            }
        }

        Movement::PathRequest request;
        request.ownerGuid = owner->GetGUID();
        request.from = startPos;
        request.to = effectiveDesired;
        Movement::PathResult result = planner->RequestPathSync(owner, request);

        if (!result.success || result.points.empty())
        {
            if (Creature* creature = owner->ToCreature())
                creature->SetCannotReachTarget(true);
            return false;
        }

        // Backward classification uses the raw chase point (not the nudged
        // one) so a tangential ORCA deflection isn't mis-read as retreat.
        Position const& targetPos = target->GetPosition();
        bool const backward = Dist2DSq(desired, targetPos) > Dist2DSq(owner->GetPosition(), targetPos);

        // PATHFIND_INCOMPLETE on a backward step means the destination is
        // off-mesh and the path ends on a cliff-edge sliver. Refusing the
        // step keeps the creature inside the playable area.
        if (backward && (result.type & PATHFIND_INCOMPLETE))
        {
            if (Creature* creature = owner->ToCreature())
                creature->SetCannotReachTarget(true);
            return false;
        }

        if (Creature* creature = owner->ToCreature())
            creature->SetCannotReachTarget(false);
        owner->AddUnitState(UNIT_STATE_CHASE_MOVE);
        Movement::SplineLaunchOptions splineOpts;
        splineOpts.walk = false;
        splineOpts.backward = backward;
        splineOpts.faceTarget = target;
        _splineExecutor->LaunchAlongPath(result.points, splineOpts);
        _lastTargetPosition = currentTargetPos;
        return true;
    };

    switch (_state)
    {
        case ChaseGeneratorState::Planning:
        {
            Position const desired = ComputeDesiredPosition(owner, target, predictor, _range, _angle);
            if (tryComputeAndLaunch(desired))
                _state = ChaseGeneratorState::Executing;
            return true;
        }

        case ChaseGeneratorState::Executing:
        {
            if (_splineExecutor->IsFinalized())
            {
                owner->ClearUnitState(UNIT_STATE_CHASE_MOVE);
                // Compare against the target's current position, not the lead:
                // the lead's job ended when it shaped the previous spline.
                float const reach = owner->GetCombatReach() + target->GetCombatReach() + CHASE_BAND_OUTER_MARGIN_Y;
                if (Dist2DSq(owner->GetPosition(), target->GetPosition()) <= reach * reach)
                {
                    owner->SetInFront(target);
                    _state = ChaseGeneratorState::IdleInBand;
                }
                else
                {
                    _state = ChaseGeneratorState::Planning;
                }
                return true;
            }

            // Without this deadzone the IdleInBand hysteresis would be defeated
            // every tick by micro-jitter that re-classifies the target as moved.
            if (targetMoved)
            {
                float const distSq = Dist2DSq(owner->GetPosition(), target->GetPosition());
                float const reach = owner->GetCombatReach() + target->GetCombatReach();
                float const maxBand = reach + CHASE_BAND_OUTER_MARGIN_Y;
                float const innerBand = std::max(0.5f, reach - CHASE_BAND_INNER_MARGIN_Y);

                bool const outOfBand = distSq > maxBand * maxBand;
                bool const tooClose  = distSq < innerBand * innerBand;

                if (outOfBand || tooClose || _angle)
                {
                    Position const desired = ComputeDesiredPosition(owner, target, predictor, _range, _angle);
                    tryComputeAndLaunch(desired);
                }
            }
            return true;
        }

        case ChaseGeneratorState::IdleInBand:
        {
            if (!owner->HasInArc(0.01f, target))
                owner->SetInFront(target);

            // Idle exits on outer (forward chase), inner (backward step), or
            // any target rotation when angle-constrained.
            float const distSq = Dist2DSq(owner->GetPosition(), target->GetPosition());
            float const reach = owner->GetCombatReach() + target->GetCombatReach();
            float const maxBand = reach + CHASE_BAND_OUTER_MARGIN_Y;
            float const innerBand = std::max(0.5f, reach - CHASE_BAND_INNER_MARGIN_Y);

            bool const outOfBand = distSq > maxBand * maxBand;
            bool const tooClose  = distSq < innerBand * innerBand;

            if (outOfBand || tooClose || (_angle && targetMoved))
                _state = ChaseGeneratorState::Planning;
            return true;
        }
    }
    return true;
}
