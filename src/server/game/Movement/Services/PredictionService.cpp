#include "PredictionService.h"
#include "MovementPredictor.h"
#include "Timer.h"
#include "Unit.h"

namespace Movement
{
    // Resetting after a long gap avoids the dt^2 variance inflation that
    // would otherwise land the next observation as a giant innovation spike.
    static constexpr uint32 PREDICTOR_STALE_MS = 5000;

    static constexpr uint32 PREDICTOR_IDLE_TTL_MS = 30000;
    static constexpr uint32 PREDICTOR_SWEEP_PERIOD_MS = 5000;

    struct PredictionService::Entry
    {
        std::unique_ptr<MovementPredictor> predictor = std::make_unique<MovementPredictor>();
        uint32 lastTouchMs = 0;
    };

    PredictionService::PredictionService(Map* map) : _map(map) {}
    PredictionService::~PredictionService() = default;

    MovementPredictor const* PredictionService::TrackTarget(Unit const* target)
    {
        if (!target)
            return nullptr;

        ObjectGuid const guid = target->GetGUID();
        auto& slot = _entries[guid];
        if (!slot)
            slot = std::make_unique<Entry>();

        uint32 const nowMs = getMSTime();

        if (slot->lastTouchMs != 0 && getMSTimeDiff(slot->lastTouchMs, nowMs) > PREDICTOR_STALE_MS)
            slot->predictor->Reset();

        slot->predictor->Observe(target->GetPosition(), nowMs);
        slot->lastTouchMs = nowMs;
        return slot->predictor.get();
    }

    void PredictionService::OnTargetRemoved(ObjectGuid guid)
    {
        _entries.erase(guid);
    }

    void PredictionService::Update(uint32 /*diff*/)
    {
        uint32 const nowMs = getMSTime();
        if (_nextSweepMs != 0 && nowMs < _nextSweepMs)
            return;
        _nextSweepMs = nowMs + PREDICTOR_SWEEP_PERIOD_MS;

        for (auto it = _entries.begin(); it != _entries.end(); )
        {
            if (getMSTimeDiff(it->second->lastTouchMs, nowMs) > PREDICTOR_IDLE_TTL_MS)
                it = _entries.erase(it);
            else
                ++it;
        }
    }
}
