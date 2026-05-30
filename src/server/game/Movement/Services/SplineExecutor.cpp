#include "SplineExecutor.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"
#include "Unit.h"

namespace Movement
{
    SplineExecutor::SplineExecutor(Unit* owner)
        : _owner(owner), _currentSplineId(0)
    {
    }

    uint32 SplineExecutor::LaunchAlongPath(PointsArray const& path, SplineLaunchOptions const& opts)
    {
        if (!_owner || path.empty())
            return 0;

        // MoveSplineInit::SetWalk only picks the speed multiplier; the client
        // reads MOVEMENTFLAG_WALKING for the walk/run animation, so we must
        // also update the unit state explicitly.
        _owner->SetWalk(opts.walk);

        MoveSplineInit init(_owner);
        init.MovebyPath(path, opts.firstPointId);
        init.SetWalk(opts.walk);
        if (opts.smooth)
            init.SetSmooth();
        if (opts.backward)
            init.SetBackward();
        if (opts.fixedOrientation)
            init.SetFacing(*opts.fixedOrientation);
        else if (opts.faceTarget)
            init.SetFacing(opts.faceTarget);
        if (opts.animation)
            init.SetAnimation(*opts.animation);
        if (opts.velocityOverride)
            init.SetVelocity(*opts.velocityOverride);

        init.Launch();
        _currentSplineId = _owner->movespline->GetId();
        return _currentSplineId;
    }

    bool SplineExecutor::IsInPreemptWindow(int32 windowMs) const
    {
        if (!_owner || !_owner->movespline || !_owner->movespline->Initialized())
            return false;
        if (_owner->movespline->Finalized())
            return false;
        return _owner->movespline->timeRemainingMs() <= windowMs;
    }

    bool SplineExecutor::IsFinalized() const
    {
        return _owner && _owner->movespline && _owner->movespline->Finalized();
    }

    void SplineExecutor::Cancel()
    {
        if (_owner && !_owner->IsStopped())
            _owner->StopMoving();
    }
}
