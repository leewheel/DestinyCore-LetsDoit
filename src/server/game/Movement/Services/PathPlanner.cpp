#include "PathPlanner.h"
#include "Creature.h"
#include "DisableMgr.h"
#include "Map.h"
#include "MapDefines.h"
#include "MMapFactory.h"
#include "MMapManager.h"
#include "PathCache.h"
#include "PathComputeKernel.h"
#include "PhaseShift.h"
#include "PhasingHandler.h"
#include "SpellAuraDefines.h"
#include "Timer.h"
#include "Unit.h"

namespace Movement
{
    struct PathPlanner::QueuedRequest
    {
        // Snapshot at queue time so the worker never touches the owner Unit
        // (whose lifetime it cannot reason about).
        PathKernelInput kernelInput;
        Map* map = nullptr;
        PhaseShift phaseShift;
        ObjectGuid ownerGuid;
        bool ownerHasTransport = false;
        bool isCreature = false;
        bool isPlayer = false;
        bool canFly = false;
        bool canSwim = false;
        bool hasWaterWalk = false;
        bool acceptIncomplete = false;

        PathKernelOutput kernelOutput;
        std::shared_ptr<PathHandle> handle;
    };

    struct PathPlanner::WorkerSlot
    {
        std::thread thread;
        std::unique_ptr<PathComputeKernel> kernel;
    };

    PathPlanner::PathPlanner(Map* map, uint32 /*workerCount*/)
        : _map(map),
          _cache(std::make_unique<PathCache>())
    {
        auto slot = std::make_unique<WorkerSlot>();
        slot->kernel = std::make_unique<PathComputeKernel>();
        WorkerSlot* raw = slot.get();
        slot->thread = std::thread(&PathPlanner::WorkerLoop, this, raw);
        _workers.push_back(std::move(slot));
    }

    PathPlanner::~PathPlanner()
    {
        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            _shutdown.store(true, std::memory_order_release);
        }
        _queueCondition.notify_all();

        for (auto& slot : _workers)
            if (slot && slot->thread.joinable())
                slot->thread.join();

        auto signalFailed = [](std::unique_ptr<QueuedRequest>& req)
        {
            if (!req || !req->handle)
                return;
            PathResult result;
            result.success = false;
            result.type = PATHFIND_NOPATH;
            req->handle->_result = std::move(result);
            req->handle->_ready.store(true, std::memory_order_release);
        };
        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            for (auto& req : _queue)
                signalFailed(req);
            _queue.clear();
        }
        {
            std::lock_guard<std::mutex> lock(_completedMutex);
            for (auto& req : _completed)
                signalFailed(req);
            _completed.clear();
        }
    }

    std::unique_ptr<PathPlanner::QueuedRequest> PathPlanner::BuildRequest(Unit const* owner, PathRequest const& request) const
    {
        if (!owner || !_map)
            return nullptr;

        auto req = std::make_unique<QueuedRequest>();
        req->map = _map;
        req->phaseShift = owner->GetPhaseShift();
        req->ownerGuid = owner->GetGUID();
        req->ownerHasTransport = owner->GetTransport() != nullptr;
        req->isCreature = owner->GetTypeId() == TYPEID_UNIT;
        req->isPlayer = owner->GetTypeId() == TYPEID_PLAYER;
        req->acceptIncomplete = request.forceDestination;

        if (Creature const* creature = owner->ToCreature())
        {
            req->canFly = creature->CanFly();
            req->canSwim = creature->CanSwim();
        }
        else
        {
            req->canFly = false;
            req->canSwim = true;
        }
        req->hasWaterWalk = owner->HasAuraType(SPELL_AURA_WATER_WALK);

        req->kernelInput.start = G3D::Vector3(request.from.GetPositionX(), request.from.GetPositionY(), request.from.GetPositionZ());
        req->kernelInput.end   = G3D::Vector3(request.to.GetPositionX(),   request.to.GetPositionY(),   request.to.GetPositionZ());
        req->kernelInput.forceDestination = request.forceDestination;
        req->kernelInput.straightLine = HasFlag(request.flags, PathPlannerFlags::StraightLine);
        req->kernelInput.pointPathLimit = request.pointLimit ? request.pointLimit : MAX_POINT_PATH_LENGTH;

        uint16 includeFlags = 0;
        if (req->isCreature)
        {
            Creature const* creature = owner->ToCreature();
            if (creature->CanWalk())
                includeFlags |= NAV_GROUND;
            if (req->canSwim)
                includeFlags |= (NAV_WATER | NAV_MAGMA_SLIME);
        }
        else
        {
            includeFlags |= (NAV_GROUND | NAV_WATER | NAV_MAGMA_SLIME);
        }
        req->kernelInput.filter.setIncludeFlags(includeFlags);
        req->kernelInput.filter.setExcludeFlags(0);

        if (owner->HasUnitState(UNIT_STATE_IGNORE_PATHFINDING) || !DisableMgr::IsPathfindingEnabled(owner->GetMapId()))
        {
            req->kernelInput.navMesh = nullptr;
            req->kernelInput.navMeshQuery = nullptr;
        }
        else
        {
            uint32 terrainMapId = PhasingHandler::GetTerrainMapId(req->phaseShift, _map, request.from.GetPositionX(), request.from.GetPositionY());
            MMAP::MMapManager* mmap = MMAP::MMapFactory::createOrGetMMapManager();
            req->kernelInput.navMesh = mmap->GetNavMesh(owner->GetMapId());
            req->kernelInput.navMeshQuery = mmap->GetNavMeshQuery(terrainMapId, owner->GetInstanceId());
        }

        return req;
    }

    void PathPlanner::ApplyTerrainZ(QueuedRequest& req) const
    {
        if (!req.map || req.ownerHasTransport)
            return;
        if (req.kernelOutput.type & PATHFIND_NOT_USING_PATH)
            return;
        if (req.kernelOutput.points.empty())
            return;

        for (G3D::Vector3& point : req.kernelOutput.points)
        {
            float z = point.z;
            if (req.canFly)
            {
                float ground = req.map->GetHeight(req.phaseShift, point.x, point.y, z, true);
                if (z < ground && ground > INVALID_HEIGHT)
                    z = ground;
            }
            else if (req.canSwim)
            {
                float ground = z;
                float max_z = req.map->GetWaterOrGroundLevel(req.phaseShift, point.x, point.y, z, &ground, !req.hasWaterWalk);
                if (max_z > INVALID_HEIGHT)
                {
                    if (z > max_z) z = max_z;
                    else if (z < ground) z = ground;
                }
            }
            else
            {
                float ground = req.map->GetHeight(req.phaseShift, point.x, point.y, z, true);
                if (ground > INVALID_HEIGHT)
                    z = ground;
            }
            point.z = z;
        }
    }

    bool PathPlanner::TryServeFromCache(QueuedRequest& req) const
    {
        if (!_cache || !req.kernelInput.navMeshQuery)
            return false;

        PathCacheKey key;
        if (!PathCache::BuildKey(req.kernelInput.start, req.kernelInput.end,
                                 req.map->GetId(), req.map->GetInstanceId(),
                                 req.kernelInput.filter.getIncludeFlags(),
                                 req.kernelInput.filter.getExcludeFlags(),
                                 req.kernelInput.straightLine,
                                 key))
            return false;

        CachedPath hit;
        if (!_cache->TryGet(key, getMSTime(), hit))
            return false;

        req.kernelOutput.type = hit.type;
        req.kernelOutput.points = std::move(hit.points);
        req.kernelOutput.endPosition = req.kernelOutput.points.empty()
            ? req.kernelInput.end
            : req.kernelOutput.points.back();
        return true;
    }

    void PathPlanner::StoreInCache(QueuedRequest const& req) const
    {
        if (!_cache || !req.kernelInput.navMeshQuery)
            return;
        if (req.kernelOutput.points.empty())
            return;
        // Only cache fully valid paths; nopath/incomplete results are tied
        // to the source position and would mislead later callers.
        if (req.kernelOutput.type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE | PATHFIND_NOT_USING_PATH | PATHFIND_SHORT))
            return;

        PathCacheKey key;
        if (!PathCache::BuildKey(req.kernelInput.start, req.kernelInput.end,
                                 req.map->GetId(), req.map->GetInstanceId(),
                                 req.kernelInput.filter.getIncludeFlags(),
                                 req.kernelInput.filter.getExcludeFlags(),
                                 req.kernelInput.straightLine,
                                 key))
            return;

        _cache->Insert(key, req.kernelOutput.type, req.kernelOutput.points, getMSTime());
    }

    void PathPlanner::FinalizeRequest(QueuedRequest& req) const
    {
        ApplyTerrainZ(req);

        PathResult result;
        result.type = req.kernelOutput.type;
        result.success = !(req.kernelOutput.type & PATHFIND_NOPATH);
        result.points = std::move(req.kernelOutput.points);

        if (req.handle)
        {
            req.handle->_result = std::move(result);
            req.handle->_ready.store(true, std::memory_order_release);
        }
    }

    PathResult PathPlanner::RequestPathSync(Unit const* owner, PathRequest const& request)
    {
        auto req = BuildRequest(owner, request);
        if (!req)
        {
            PathResult result;
            result.success = false;
            result.type = PATHFIND_NOPATH;
            return result;
        }

        if (!TryServeFromCache(*req))
        {
            {
                std::lock_guard<std::mutex> lock(_detourMutex);
                _workers.front()->kernel->Compute(req->kernelInput, req->kernelOutput);
            }
            StoreInCache(*req);
        }

        ApplyTerrainZ(*req);

        PathResult result;
        result.type = req->kernelOutput.type;
        result.success = !(req->kernelOutput.type & PATHFIND_NOPATH);
        result.points = std::move(req->kernelOutput.points);
        return result;
    }

    std::shared_ptr<PathHandle> PathPlanner::RequestPathAsync(Unit const* owner, PathRequest const& request)
    {
        auto handle = std::make_shared<PathHandle>();
        auto req = BuildRequest(owner, request);
        if (!req)
        {
            PathResult fail;
            fail.success = false;
            fail.type = PATHFIND_NOPATH;
            handle->_result = std::move(fail);
            handle->_ready.store(true, std::memory_order_release);
            return handle;
        }
        req->handle = handle;

        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            _queue.push_back(std::move(req));
        }
        _queueCondition.notify_one();
        return handle;
    }

    void PathPlanner::Update(uint32 /*diff*/)
    {
        std::deque<std::unique_ptr<QueuedRequest>> drained;
        {
            std::lock_guard<std::mutex> lock(_completedMutex);
            std::swap(drained, _completed);
        }
        for (auto& req : drained)
            FinalizeRequest(*req);

        uint32 const now = getMSTime();
        if (_cache && now >= _nextCacheExpireMs)
        {
            _cache->ExpireOlderThan(now);
            _nextCacheExpireMs = now + 5000;
        }
    }

    void PathPlanner::OnOwnerRemoved(ObjectGuid guid)
    {
        auto markFailed = [](std::unique_ptr<QueuedRequest>& req)
        {
            PathResult result;
            result.success = false;
            result.type = PATHFIND_NOPATH;
            if (req->handle)
            {
                req->handle->_result = std::move(result);
                req->handle->_ready.store(true, std::memory_order_release);
            }
        };

        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            for (auto it = _queue.begin(); it != _queue.end(); )
            {
                if ((*it)->ownerGuid == guid)
                {
                    markFailed(*it);
                    it = _queue.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(_completedMutex);
            for (auto it = _completed.begin(); it != _completed.end(); )
            {
                if ((*it)->ownerGuid == guid)
                {
                    markFailed(*it);
                    it = _completed.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
    }

    void PathPlanner::WorkerLoop(WorkerSlot* slot)
    {
        while (true)
        {
            std::unique_ptr<QueuedRequest> req;
            {
                std::unique_lock<std::mutex> lock(_queueMutex);
                _queueCondition.wait(lock, [this] {
                    return _shutdown.load(std::memory_order_acquire) || !_queue.empty();
                });
                if (_shutdown.load(std::memory_order_acquire))
                    return;
                req = std::move(_queue.front());
                _queue.pop_front();
            }

            if (!TryServeFromCache(*req))
            {
                {
                    std::lock_guard<std::mutex> detourLock(_detourMutex);
                    slot->kernel->Compute(req->kernelInput, req->kernelOutput);
                }
                StoreInCache(*req);
            }

            {
                std::lock_guard<std::mutex> lock(_completedMutex);
                _completed.push_back(std::move(req));
            }
        }
    }
}
