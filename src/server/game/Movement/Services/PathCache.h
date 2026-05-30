#ifndef TRINITY_PATH_CACHE_H
#define TRINITY_PATH_CACHE_H

#include "Define.h"
#include "MoveSplineInitArgs.h"
#include "PathGenerator.h"
#include <G3D/Vector3.h>
#include <list>
#include <mutex>
#include <unordered_map>

namespace Movement
{
    static constexpr float PATH_CACHE_CELL_SIZE = 4.0f;
    static constexpr uint32 PATH_CACHE_TTL_MS = 30 * 1000;
    static constexpr std::size_t PATH_CACHE_MAX_ENTRIES = 4096;
    // Short paths skip the cache: the compute is cheaper than the start/end
    // drift the cell quantisation would introduce.
    static constexpr float PATH_CACHE_MIN_LENGTH = 10.0f;

    struct PathCacheKey
    {
        int32 startCellX = 0;
        int32 startCellY = 0;
        int32 endCellX   = 0;
        int32 endCellY   = 0;
        uint32 mapId     = 0;
        uint32 instanceId= 0;
        uint16 filterIncludeFlags = 0;
        uint16 filterExcludeFlags = 0;
        bool straightLine = false;

        bool operator==(PathCacheKey const& o) const
        {
            return startCellX == o.startCellX && startCellY == o.startCellY
                && endCellX == o.endCellX && endCellY == o.endCellY
                && mapId == o.mapId && instanceId == o.instanceId
                && filterIncludeFlags == o.filterIncludeFlags
                && filterExcludeFlags == o.filterExcludeFlags
                && straightLine == o.straightLine;
        }
    };

    struct PathCacheKeyHash
    {
        std::size_t operator()(PathCacheKey const& k) const noexcept
        {
            std::size_t h = std::hash<int64>{}((int64(k.startCellX) << 32) ^ uint32(k.startCellY));
            h ^= std::hash<int64>{}((int64(k.endCellX) << 32) ^ uint32(k.endCellY)) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            h ^= std::hash<uint64>{}((uint64(k.mapId) << 32) ^ k.instanceId) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            h ^= std::hash<uint32>{}((uint32(k.filterIncludeFlags) << 16) ^ k.filterExcludeFlags) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            h ^= k.straightLine ? 0xA5A5A5A5ULL : 0x5A5A5A5AULL;
            return h;
        }
    };

    struct CachedPath
    {
        PathType type = PATHFIND_BLANK;
        PointsArray points;
        uint32 insertedAtMs = 0;
    };

    class TC_GAME_API PathCache
    {
    public:
        explicit PathCache(std::size_t maxEntries = PATH_CACHE_MAX_ENTRIES,
                           uint32 ttlMs = PATH_CACHE_TTL_MS);

        PathCache(PathCache const&) = delete;
        PathCache& operator=(PathCache const&) = delete;

        // Returns false when the segment is too short to be worth caching.
        static bool BuildKey(G3D::Vector3 const& from, G3D::Vector3 const& to,
                             uint32 mapId, uint32 instanceId,
                             uint16 includeFlags, uint16 excludeFlags,
                             bool straightLine,
                             PathCacheKey& outKey);

        bool TryGet(PathCacheKey const& key, uint32 nowMs, CachedPath& out);
        void Insert(PathCacheKey const& key, PathType type,
                    PointsArray const& points, uint32 nowMs);
        void ExpireOlderThan(uint32 nowMs);
        void Clear();

        [[nodiscard]] std::size_t Size() const;

    private:
        struct Entry
        {
            PathCacheKey key;
            CachedPath value;
        };

        mutable std::mutex _mutex;
        std::size_t _maxEntries;
        uint32 _ttlMs;

        std::list<Entry> _lru;
        std::unordered_map<PathCacheKey, std::list<Entry>::iterator, PathCacheKeyHash> _index;
    };
}

#endif
