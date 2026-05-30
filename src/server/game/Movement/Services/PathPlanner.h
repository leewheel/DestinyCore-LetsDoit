#ifndef TRINITY_PATH_PLANNER_H
#define TRINITY_PATH_PLANNER_H

#include "Define.h"
#include "MoveSplineInitArgs.h"
#include "ObjectGuid.h"
#include "PathGenerator.h"
#include "Position.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class Map;
class Unit;

namespace Movement
{
    enum class PathPlannerFlags : uint32
    {
        None         = 0x0,
        AllowJump    = 0x1,   // Detour off-mesh links carrying jump action flag
        AllowSwim    = 0x2,
        AllowFly     = 0x4,
        StraightLine = 0x8,
    };

    inline PathPlannerFlags operator|(PathPlannerFlags a, PathPlannerFlags b)
    {
        return PathPlannerFlags(uint32(a) | uint32(b));
    }
    inline bool HasFlag(PathPlannerFlags flags, PathPlannerFlags f)
    {
        return (uint32(flags) & uint32(f)) != 0;
    }

    struct PathRequest
    {
        ObjectGuid ownerGuid;
        Position from;
        Position to;
        PathPlannerFlags flags = PathPlannerFlags::None;
        uint32 pointLimit = 0;   // 0 = MAX_POINT_PATH_LENGTH
        bool forceDestination = false;
    };

    struct PathResult
    {
        bool success = false;
        PathType type = PATHFIND_BLANK;
        PointsArray points;
    };

    // Signalled exactly once. Caller polls IsReady() each tick.
    class TC_GAME_API PathHandle
    {
    public:
        [[nodiscard]] bool IsReady() const { return _ready.load(std::memory_order_acquire); }
        [[nodiscard]] PathResult const& GetResult() const { return _result; }

    private:
        friend class PathPlanner;
        std::atomic<bool> _ready{false};
        PathResult _result;
    };

    // Detour queries are serialised on _detourMutex because dtNavMeshQuery is
    // not thread-safe. Kernel Z comes from poly samples; ApplyTerrainZ runs on
    // the main thread to land paths on terrain-accurate elevations.
    class TC_GAME_API PathPlanner
    {
    public:
        // workerCount > 1 is currently capped to 1 internally until per-thread
        // dtNavMeshQuery instances land.
        explicit PathPlanner(Map* map, uint32 workerCount = 1);
        ~PathPlanner();

        PathPlanner(PathPlanner const&) = delete;
        PathPlanner& operator=(PathPlanner const&) = delete;

        [[nodiscard]] PathResult RequestPathSync(Unit const* owner, PathRequest const& request);
        [[nodiscard]] std::shared_ptr<PathHandle> RequestPathAsync(Unit const* owner, PathRequest const& request);

        void Update(uint32 diff);
        void OnOwnerRemoved(ObjectGuid guid);

    private:
        struct WorkerSlot;
        struct QueuedRequest;

        Map* _map;
        std::vector<std::unique_ptr<WorkerSlot>> _workers;
        std::unique_ptr<class PathCache> _cache;

        std::mutex _queueMutex;
        std::condition_variable _queueCondition;
        std::deque<std::unique_ptr<QueuedRequest>> _queue;

        std::mutex _completedMutex;
        std::deque<std::unique_ptr<QueuedRequest>> _completed;

        std::mutex _detourMutex;
        std::atomic<bool> _shutdown{false};
        uint32 _nextCacheExpireMs = 0;

        void WorkerLoop(WorkerSlot* slot);
        std::unique_ptr<QueuedRequest> BuildRequest(Unit const* owner, PathRequest const& request) const;
        void ApplyTerrainZ(QueuedRequest& req) const;
        void FinalizeRequest(QueuedRequest& req) const;

        bool TryServeFromCache(QueuedRequest& req) const;
        void StoreInCache(QueuedRequest const& req) const;
    };
}

#endif
