#include "WaypointGenerator.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "CreatureGroups.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"
#include "SplineExecutor.h"
#include "Transport.h"
#include "Unit.h"
#include "WaypointDefines.h"
#include "WaypointManager.h"
#include "World.h"

namespace
{
    uint32 ExternalStopDelay(bool loadedFromDB)
    {
        return loadedFromDB
            ? sWorld->getIntConfig(CONFIG_CREATURE_STOP_FOR_PLAYER)
            : 2 * HOUR * IN_MILLISECONDS;
    }
}

WaypointGenerator::WaypointGenerator(uint32 pathId, bool repeating)
    : _pathId(pathId),
      _path(nullptr),
      _currentNode(0),
      _repeating(repeating),
      _loadedFromDB(true),
      _isArrivalDone(false),
      _recalculateSpeed(false),
      _pauseTimer(0)
{
}

WaypointGenerator::WaypointGenerator(WaypointPath& path, bool repeating)
    : _pathId(0),
      _path(&path),
      _currentNode(0),
      _repeating(repeating),
      _loadedFromDB(false),
      _isArrivalDone(false),
      _recalculateSpeed(false),
      _pauseTimer(0)
{
}

WaypointGenerator::~WaypointGenerator() = default;

void WaypointGenerator::Initialize(Unit* owner)
{
    if (!owner)
        return;
    _splineExecutor = std::make_unique<Movement::SplineExecutor>(owner);
    LoadPath(owner);
}

void WaypointGenerator::Finalize(Unit* owner)
{
    if (!owner)
        return;
    owner->ClearUnitState(UNIT_STATE_ROAMING | UNIT_STATE_ROAMING_MOVE);
    owner->SetWalk(false);
}

void WaypointGenerator::Reset(Unit* owner)
{
    if (!IsPaused())
        StartMoveNow(owner);
}

void WaypointGenerator::Pause(uint32 ms)
{
    _pauseTimer.Reset(ms);
}

void WaypointGenerator::Resume()
{
    // 1ms (not 0) so the next CanMove-style check still consumes the tick.
    _pauseTimer.Reset(1);
}

void WaypointGenerator::LoadPath(Unit* owner)
{
    Creature* creature = owner ? owner->ToCreature() : nullptr;
    if (!creature)
        return;

    if (_loadedFromDB)
    {
        if (!_pathId)
            _pathId = creature->GetWaypointPath();

        _path = sWaypointMgr->GetPath(_pathId);
    }

    if (!_path)
    {
        TC_LOG_ERROR("sql.sql", "WaypointGenerator: creature %s (%s DB GUID: " UI64FMTD ") missing waypoint path id %u",
                     creature->GetName().c_str(), creature->GetGUID().ToString().c_str(),
                     creature->GetSpawnId(), _pathId);
        return;
    }

    if (!IsPaused())
        StartMoveNow(owner);
}

void WaypointGenerator::OnArrived(Unit* owner)
{
    Creature* creature = owner ? owner->ToCreature() : nullptr;
    if (!creature || !_path || _path->nodes.empty())
        return;

    WaypointNode const& waypoint = _path->nodes.at(_currentNode);
    if (waypoint.delay)
    {
        creature->ClearUnitState(UNIT_STATE_ROAMING_MOVE);
        Pause(waypoint.delay);
    }

    if (waypoint.eventId && urand(0, 99) < waypoint.eventChance)
    {
        creature->ClearUnitState(UNIT_STATE_ROAMING_MOVE);
        creature->GetMap()->ScriptsStart(sWaypointScripts, waypoint.eventId, creature, nullptr);
    }

    EmitMovementInform(creature);
    creature->UpdateWaypointID(_currentNode);
    creature->SetWalk(waypoint.moveType != WAYPOINT_MOVE_TYPE_RUN);
}

bool WaypointGenerator::StartMove(Unit* owner)
{
    Creature* creature = owner ? owner->ToCreature() : nullptr;
    if (!creature || !creature->IsAlive())
        return false;
    if (!_path || _path->nodes.empty())
        return false;
    if (IsPaused())
        return true;

    bool const transportPath = creature->GetTransport() != nullptr;

    if (_isArrivalDone)
    {
        if ((_currentNode == _path->nodes.size() - 1) && !_repeating)
        {
            WaypointNode const& waypoint = _path->nodes.at(_currentNode);

            float x = waypoint.x;
            float y = waypoint.y;
            float z = waypoint.z;
            float o = creature->GetOrientation();

            if (!transportPath)
            {
                creature->SetHomePosition(x, y, z, o);
            }
            else
            {
                if (Transport* trans = creature->GetTransport())
                {
                    o -= trans->GetOrientation();
                    creature->SetTransportHomePosition(x, y, z, o);
                    trans->CalculatePassengerPosition(x, y, z, &o);
                    creature->SetHomePosition(x, y, z, o);
                }
            }
            return false;
        }

        _currentNode = (_currentNode + 1) % _path->nodes.size();
    }

    // Bundle waypoints up to the first node with a delay, so the spline plays
    // straight through delay-less waypoints.
    float finalOrient = 0.0f;
    uint8 finalMove = WAYPOINT_MOVE_TYPE_WALK;

    Movement::PointsArray pathing;
    pathing.reserve((_path->nodes.size() - _currentNode) + 1);
    pathing.emplace_back(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ());

    for (uint32 i = _currentNode; i < _path->nodes.size(); ++i)
    {
        WaypointNode const& waypoint = _path->nodes.at(i);
        pathing.emplace_back(waypoint.x, waypoint.y, waypoint.z);

        finalOrient = waypoint.orientation;
        finalMove = waypoint.moveType;

        if (waypoint.delay)
            break;
    }

    if (pathing.size() < 2)
        return false;

    _isArrivalDone = false;
    _recalculateSpeed = false;

    creature->AddUnitState(UNIT_STATE_ROAMING_MOVE);

    Movement::SplineLaunchOptions opts;
    // Catmull-Rom < 3 points fabricates control points and overshoots at the
    // ends (NPC spins on arrival). Formation leaders also stay linear so
    // members chasing the leader's instantaneous position don't drift.
    bool const isFormationLeader = creature->GetFormation()
        && creature->GetFormation()->getLeader() == creature
        && !creature->GetFormation()->isEmpty();
    opts.smooth = !isFormationLeader && (pathing.size() >= 3);
    opts.firstPointId = _currentNode;

    switch (finalMove)
    {
        case WAYPOINT_MOVE_TYPE_LAND:
            opts.animation = Movement::ToGround;
            break;
        case WAYPOINT_MOVE_TYPE_TAKEOFF:
            opts.animation = Movement::ToFly;
            break;
        case WAYPOINT_MOVE_TYPE_RUN:
            opts.walk = false;
            break;
        case WAYPOINT_MOVE_TYPE_WALK:
            opts.walk = true;
            break;
    }

    // 0 in the DB means "no preference"; any other value pins the orientation
    // at the last waypoint of the bundle.
    if (finalOrient != 0.0f)
        opts.fixedOrientation = finalOrient;

    if (transportPath)
    {
        // Transport-bound waypoints are in transport-local space and need
        // DisableTransportPathTransformations, which SplineExecutor doesn't
        // expose yet - go directly through MoveSplineInit.
        Movement::MoveSplineInit init(creature);
        init.MovebyPath(pathing, _currentNode);
        init.DisableTransportPathTransformations();
        if (opts.smooth)
            init.SetSmooth();
        init.SetWalk(opts.walk);
        creature->SetWalk(opts.walk);
        if (opts.animation)
            init.SetAnimation(*opts.animation);
        if (opts.fixedOrientation)
            init.SetFacing(*opts.fixedOrientation);
        init.Launch();
    }
    else
    {
        _splineExecutor->LaunchAlongPath(pathing, opts);
    }

    NotifyFormationLeaderMoved(creature);
    return true;
}

bool WaypointGenerator::StartMoveNow(Unit* owner)
{
    Pause(0);
    return StartMove(owner);
}

void WaypointGenerator::NotifyFormationLeaderMoved(Unit* owner) const
{
    Creature* creature = owner ? owner->ToCreature() : nullptr;
    if (!creature || !_path || _path->nodes.empty())
        return;
    CreatureGroup* formation = creature->GetFormation();
    if (!formation || formation->getLeader() != creature)
        return;

    // Push the next scripted waypoint as the anchor; LeaderMoveTo derives all
    // member slots and pushes them via SetSlot.
    WaypointNode const& target = _path->nodes.at(_currentNode);
    float x = target.x;
    float y = target.y;
    float z = target.z;
    if (Transport* trans = creature->GetTransport())
        trans->CalculatePassengerPosition(x, y, z);
    formation->LeaderMoveTo(x, y, z);
}

void WaypointGenerator::EmitMovementInform(Unit* owner) const
{
    Creature* creature = owner ? owner->ToCreature() : nullptr;
    if (creature && creature->AI())
        creature->AI()->MovementInform(WAYPOINT_MOTION_TYPE, _currentNode);
}

bool WaypointGenerator::Update(Unit* owner, uint32 diff)
{
    Creature* creature = owner ? owner->ToCreature() : nullptr;
    if (!creature || !creature->IsAlive())
        return false;

    // Escort quests, GM stop commands, stuns - clear the move flag and idle.
    if (creature->HasUnitState(UNIT_STATE_NOT_MOVE))
    {
        creature->ClearUnitState(UNIT_STATE_ROAMING_MOVE);
        return true;
    }

    if (!_path || _path->nodes.empty())
        return false;

    // Paused: count down. When the pause expires we drop through to the
    // restart path below so the spline relaunches in the same tick - no
    // extra frame of standing still.
    if (IsPaused())
    {
        _pauseTimer.Update(diff);
        if (IsPaused())
            return true;
        return StartMoveNow(creature);
    }

    // Pin home so an evade reset doesn't snap the creature back mid-patrol.
    if (!creature->GetTransGUID())
        creature->SetHomePosition(creature->GetPosition());

    if (creature->IsStopped())
    {
        Pause(ExternalStopDelay(_loadedFromDB));
        return true;
    }

    if (creature->movespline->Finalized())
    {
        OnArrived(creature);
        _isArrivalDone = true;
        if (!IsPaused() && !creature->IsStopped())
            return StartMove(creature);
        return true;
    }

    if (_recalculateSpeed)
    {
        _recalculateSpeed = false;
        return StartMove(creature);
    }

    uint32 const pointId = uint32(creature->movespline->currentPathIdx());
    if (pointId > _currentNode)
    {
        // Long diff + fast speed can cross multiple waypoints in one tick;
        // fire OnArrived for each so events and MovementInform see them all.
        while (pointId > _currentNode)
        {
            OnArrived(creature);
            _currentNode = (_currentNode + 1) % _path->nodes.size();
            NotifyFormationLeaderMoved(creature);
            if (_currentNode == 0 && !_repeating)
                break;
        }
    }
    return true;
}

bool WaypointGenerator::GetResetPos(Unit*, float& x, float& y, float& z)
{
    if (!_path || _path->nodes.empty())
        return false;
    WaypointNode const& waypoint = _path->nodes.at(_currentNode);
    x = waypoint.x;
    y = waypoint.y;
    z = waypoint.z;
    return true;
}
