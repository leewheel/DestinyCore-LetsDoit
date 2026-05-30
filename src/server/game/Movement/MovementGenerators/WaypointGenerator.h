#ifndef TRINITY_WAYPOINT_GENERATOR_H
#define TRINITY_WAYPOINT_GENERATOR_H

#include "MovementGenerator.h"
#include "Timer.h"
#include <memory>

class Unit;
struct WaypointPath;

namespace Movement
{
    class SplineExecutor;
}

// Replaces WaypointMovementGenerator<Creature> with a non-templated class
// using the shared movement services. DB schema and SmartAI surface (Pause/
// Resume/StopPath through MotionMaster) are preserved.
class TC_GAME_API WaypointGenerator : public MovementGenerator
{
public:
    explicit WaypointGenerator(uint32 pathId = 0, bool repeating = true);
    explicit WaypointGenerator(WaypointPath& path, bool repeating = true);
    ~WaypointGenerator() override;

    WaypointGenerator(WaypointGenerator const&) = delete;
    WaypointGenerator& operator=(WaypointGenerator const&) = delete;

    void Initialize(Unit* owner) override;
    void Finalize(Unit* owner) override;
    void Reset(Unit* owner) override;
    bool Update(Unit* owner, uint32 diff) override;

    MovementGeneratorType GetMovementGeneratorType() const override { return WAYPOINT_MOTION_TYPE; }

    void unitSpeedChanged() override { _recalculateSpeed = true; }

    bool GetResetPos(Unit*, float& x, float& y, float& z);

    void Pause(uint32 ms);
    void Resume();
    [[nodiscard]] bool IsPaused() const { return !_pauseTimer.Passed(); }
    [[nodiscard]] uint32 GetCurrentNode() const { return _currentNode; }
    [[nodiscard]] int32 GetRemainingPauseMs() const { return int32(_pauseTimer.GetExpiry()); }

private:
    uint32 _pathId;
    WaypointPath const* _path;
    uint32 _currentNode;
    bool _repeating;
    bool _loadedFromDB;
    bool _isArrivalDone;
    bool _recalculateSpeed;

    TimeTrackerSmall _pauseTimer;

    std::unique_ptr<Movement::SplineExecutor> _splineExecutor;

    void LoadPath(Unit* owner);
    void OnArrived(Unit* owner);
    bool StartMove(Unit* owner);
    bool StartMoveNow(Unit* owner);
    void NotifyFormationLeaderMoved(Unit* owner) const;
    void EmitMovementInform(Unit* owner) const;
};

#endif
