#ifndef TRINITY_MOVEMENT_SERVICES_H
#define TRINITY_MOVEMENT_SERVICES_H

#include "Define.h"
#include <memory>

class Map;
class Unit;

namespace Movement
{
    class PathPlanner;
    class LocalAvoidance;
    class PredictionService;

    // Lifetime matches the owning Map. Services are shared at map scope so
    // multiple generators chasing the same target collaborate rather than
    // duplicate work.
    class TC_GAME_API MovementServices
    {
    public:
        explicit MovementServices(Map* map);
        ~MovementServices();

        MovementServices(MovementServices const&) = delete;
        MovementServices& operator=(MovementServices const&) = delete;

        [[nodiscard]] PathPlanner* GetPathPlanner() const { return _pathPlanner.get(); }
        [[nodiscard]] LocalAvoidance* GetLocalAvoidance() const { return _localAvoidance.get(); }
        [[nodiscard]] PredictionService* GetPredictionService() const { return _predictionService.get(); }

        void Update(uint32 diff);
        void OnUnitRemoved(Unit* unit);

    private:
        Map* _map;
        std::unique_ptr<PathPlanner> _pathPlanner;
        std::unique_ptr<LocalAvoidance> _localAvoidance;
        std::unique_ptr<PredictionService> _predictionService;
    };
}

#endif
