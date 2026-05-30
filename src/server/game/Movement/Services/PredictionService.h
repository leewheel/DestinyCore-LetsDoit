#ifndef TRINITY_PREDICTION_SERVICE_H
#define TRINITY_PREDICTION_SERVICE_H

#include "Define.h"
#include "ObjectGuid.h"
#include <memory>
#include <unordered_map>

class Map;
class Unit;

namespace Movement
{
    class MovementPredictor;

    // Shares one MovementPredictor across all chasers of the same target,
    // so a mob pack feeds a single Kalman pass per tick and every chaser
    // aims at the same lead point.
    class TC_GAME_API PredictionService
    {
    public:
        explicit PredictionService(Map* map);
        ~PredictionService();

        PredictionService(PredictionService const&) = delete;
        PredictionService& operator=(PredictionService const&) = delete;

        // Returned pointer stays valid until OnTargetRemoved or destruction.
        // The monotonic timestamp guard makes same-tick re-observation a no-op.
        MovementPredictor const* TrackTarget(Unit const* target);

        void OnTargetRemoved(ObjectGuid guid);
        void Update(uint32 diff);

    private:
        struct Entry;
        Map* _map;
        std::unordered_map<ObjectGuid, std::unique_ptr<Entry>> _entries;
        uint32 _nextSweepMs = 0;
    };
}

#endif
