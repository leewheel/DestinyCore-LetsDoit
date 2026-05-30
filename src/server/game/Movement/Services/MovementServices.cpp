#include "MovementServices.h"
#include "LocalAvoidance.h"
#include "PathPlanner.h"
#include "PredictionService.h"
#include "Unit.h"

namespace Movement
{
    MovementServices::MovementServices(Map* map)
        : _map(map),
          _pathPlanner(std::make_unique<PathPlanner>(map)),
          _localAvoidance(std::make_unique<LocalAvoidance>(map)),
          _predictionService(std::make_unique<PredictionService>(map))
    {
    }

    MovementServices::~MovementServices() = default;

    void MovementServices::Update(uint32 diff)
    {
        _pathPlanner->Update(diff);
        _localAvoidance->Update(diff);
        _predictionService->Update(diff);
    }

    void MovementServices::OnUnitRemoved(Unit* unit)
    {
        if (!unit)
            return;
        ObjectGuid const guid = unit->GetGUID();
        _pathPlanner->OnOwnerRemoved(guid);
        _localAvoidance->UnregisterAgent(guid);
        _predictionService->OnTargetRemoved(guid);
    }
}
