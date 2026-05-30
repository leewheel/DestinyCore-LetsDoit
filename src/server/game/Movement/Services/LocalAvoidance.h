#ifndef TRINITY_LOCAL_AVOIDANCE_H
#define TRINITY_LOCAL_AVOIDANCE_H

#include "Define.h"
#include "ObjectGuid.h"
#include "Position.h"
#include <memory>

class Map;
class Unit;

namespace RVO
{
    class RVOSimulator;
}

namespace Movement
{
    // ORCA inter-agent steering only. Static obstacles belong to the navmesh.
    // Query/answer: never moves units, only reports nudged velocities.
    class TC_GAME_API LocalAvoidance
    {
    public:
        explicit LocalAvoidance(Map* map);
        ~LocalAvoidance();

        LocalAvoidance(LocalAvoidance const&) = delete;
        LocalAvoidance& operator=(LocalAvoidance const&) = delete;

        // Idempotent. Radius and max speed are refreshed each Update().
        void RegisterAgent(Unit* unit);
        void UnregisterAgent(ObjectGuid guid);

        // Must run BEFORE Update() in the same tick.
        void SetPreferredVelocity(Unit* owner, Position const& currentPos, Position const& preferredTarget, float maxSpeed);

        void Update(uint32 diff);

        // Velocity packed as (vx, vy, 0, 0). Zero if no slot or no step yet.
        [[nodiscard]] Position QueryNudgedVelocity(ObjectGuid ownerGuid) const;

    private:
        Map* _map;
        std::unique_ptr<RVO::RVOSimulator> _sim;

        class AgentIndex;
        std::unique_ptr<AgentIndex> _agents;
    };
}

#endif
