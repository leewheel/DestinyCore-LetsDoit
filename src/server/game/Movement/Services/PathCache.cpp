#include "PathCache.h"
#include <cmath>

namespace Movement
{
    PathCache::PathCache(std::size_t maxEntries, uint32 ttlMs)
        : _maxEntries(maxEntries), _ttlMs(ttlMs)
    {
    }

    bool PathCache::BuildKey(G3D::Vector3 const& from, G3D::Vector3 const& to,
                             uint32 mapId, uint32 instanceId,
                             uint16 includeFlags, uint16 excludeFlags,
                             bool straightLine,
                             PathCacheKey& outKey)
    {
        float const dx = to.x - from.x;
        float const dy = to.y - from.y;
        float const distSq = dx * dx + dy * dy;
        if (distSq < PATH_CACHE_MIN_LENGTH * PATH_CACHE_MIN_LENGTH)
            return false;

        outKey.startCellX = int32(std::floor(from.x / PATH_CACHE_CELL_SIZE));
        outKey.startCellY = int32(std::floor(from.y / PATH_CACHE_CELL_SIZE));
        outKey.endCellX   = int32(std::floor(to.x   / PATH_CACHE_CELL_SIZE));
        outKey.endCellY   = int32(std::floor(to.y   / PATH_CACHE_CELL_SIZE));
        outKey.mapId      = mapId;
        outKey.instanceId = instanceId;
        outKey.filterIncludeFlags = includeFlags;
        outKey.filterExcludeFlags = excludeFlags;
        outKey.straightLine = straightLine;
        return true;
    }

    bool PathCache::TryGet(PathCacheKey const& key, uint32 nowMs, CachedPath& out)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _index.find(key);
        if (it == _index.end())
            return false;

        auto listIt = it->second;
        if (nowMs - listIt->value.insertedAtMs > _ttlMs)
        {
            _lru.erase(listIt);
            _index.erase(it);
            return false;
        }

        out = listIt->value;
        _lru.splice(_lru.begin(), _lru, listIt);
        return true;
    }

    void PathCache::Insert(PathCacheKey const& key, PathType type,
                            PointsArray const& points, uint32 nowMs)
    {
        if (points.empty())
            return;

        std::lock_guard<std::mutex> lock(_mutex);

        auto existing = _index.find(key);
        if (existing != _index.end())
        {
            existing->second->value.type = type;
            existing->second->value.points = points;
            existing->second->value.insertedAtMs = nowMs;
            _lru.splice(_lru.begin(), _lru, existing->second);
            return;
        }

        Entry entry;
        entry.key = key;
        entry.value.type = type;
        entry.value.points = points;
        entry.value.insertedAtMs = nowMs;
        _lru.push_front(std::move(entry));
        _index.emplace(key, _lru.begin());

        while (_lru.size() > _maxEntries)
        {
            auto& victim = _lru.back();
            _index.erase(victim.key);
            _lru.pop_back();
        }
    }

    void PathCache::ExpireOlderThan(uint32 nowMs)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        while (!_lru.empty())
        {
            auto& tail = _lru.back();
            if (nowMs - tail.value.insertedAtMs <= _ttlMs)
                break;
            _index.erase(tail.key);
            _lru.pop_back();
        }
    }

    void PathCache::Clear()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _lru.clear();
        _index.clear();
    }

    std::size_t PathCache::Size() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _lru.size();
    }
}
