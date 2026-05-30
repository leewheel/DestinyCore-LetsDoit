#ifndef TRINITY_SPLINE_EXECUTOR_H
#define TRINITY_SPLINE_EXECUTOR_H

#include "Define.h"
#include "MoveSplineInit.h"
#include "Optional.h"

class Unit;

namespace Movement
{
    struct SplineLaunchOptions
    {
        bool walk = false;
        bool backward = false;                      // plays the spline reversed; owner keeps its forward orient
        bool smooth = false;                        // Catmull-Rom rounds waypoint corners
        Unit const* faceTarget = nullptr;
        Optional<float> fixedOrientation = {};      // overrides faceTarget
        uint32 firstPointId = 0;
        Optional<AnimType> animation = {};          // ToGround/ToFly transitions, taxi takeoff/landing
        Optional<float> velocityOverride = {};      // yards/sec
    };

    // Sole construction site for MoveSplineInit in the modern movement code.
    class TC_GAME_API SplineExecutor
    {
    public:
        explicit SplineExecutor(Unit* owner);
        ~SplineExecutor() = default;

        SplineExecutor(SplineExecutor const&) = delete;
        SplineExecutor& operator=(SplineExecutor const&) = delete;

        // Returns the spline id (0 on failure).
        uint32 LaunchAlongPath(PointsArray const& path, SplineLaunchOptions const& opts = {});

        [[nodiscard]] bool IsInPreemptWindow(int32 windowMs) const;
        [[nodiscard]] bool IsFinalized() const;
        void Cancel();
        [[nodiscard]] uint32 GetCurrentSplineId() const { return _currentSplineId; }

    private:
        Unit* _owner;
        uint32 _currentSplineId;
    };
}

#endif
