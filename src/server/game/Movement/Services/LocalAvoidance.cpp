#include "LocalAvoidance.h"
#include "ObjectGuid.h"
#include "RVO.h"
#include "Unit.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace Movement
{
    namespace
    {
        // Park coord for inactive slots: out-of-range so they add no constraints.
        constexpr float PARK_COORD = 1.0e6f;

        // 10y neighbour distance roughly matches melee/short-range AoE so
        // distant mobs don't pollute the local solve.
        constexpr float DEFAULT_NEIGHBOR_DIST    = 10.0f;
        constexpr std::size_t DEFAULT_MAX_NEIGHBORS = 8;
        constexpr float DEFAULT_TIME_HORIZON     = 1.5f;
        constexpr float DEFAULT_TIME_HORIZON_OBST= 2.0f;
        constexpr float DEFAULT_AGENT_RADIUS     = 1.0f;
        constexpr float DEFAULT_AGENT_MAX_SPEED  = 7.5f;

        constexpr float SIM_TIME_STEP = 0.05f;
    }

    class LocalAvoidance::AgentIndex
    {
    public:
        // Returns size_t(-1) if the guid is not registered.
        std::size_t Find(ObjectGuid guid) const
        {
            auto it = _guidToSlot.find(guid);
            return it != _guidToSlot.end() ? it->second : std::size_t(-1);
        }

        // Empty guid for parked slots awaiting reuse.
        ObjectGuid GuidOf(std::size_t slot) const
        {
            return slot < _slotToGuid.size() ? _slotToGuid[slot] : ObjectGuid::Empty;
        }

        // Returns size_t(-1) when no slot is recyclable; caller must addAgent.
        std::size_t TryAcquire(ObjectGuid guid)
        {
            if (_freeList.empty())
                return std::size_t(-1);
            std::size_t slot = _freeList.back();
            _freeList.pop_back();
            _guidToSlot[guid] = slot;
            _slotToGuid[slot] = guid;
            return slot;
        }

        void RegisterSlot(ObjectGuid guid, std::size_t slot)
        {
            _guidToSlot[guid] = slot;
            if (_slotToGuid.size() <= slot)
                _slotToGuid.resize(slot + 1);
            _slotToGuid[slot] = guid;
        }

        std::size_t Release(ObjectGuid guid)
        {
            auto it = _guidToSlot.find(guid);
            if (it == _guidToSlot.end())
                return std::size_t(-1);
            std::size_t slot = it->second;
            _guidToSlot.erase(it);
            if (slot < _slotToGuid.size())
                _slotToGuid[slot] = ObjectGuid::Empty;
            _freeList.push_back(slot);
            return slot;
        }

        std::unordered_map<ObjectGuid, std::size_t> const& ActiveAgents() const { return _guidToSlot; }

    private:
        std::unordered_map<ObjectGuid, std::size_t> _guidToSlot;
        std::vector<ObjectGuid> _slotToGuid;
        std::vector<std::size_t> _freeList;
    };

    LocalAvoidance::LocalAvoidance(Map* map)
        : _map(map),
          _sim(std::make_unique<RVO::RVOSimulator>()),
          _agents(std::make_unique<AgentIndex>())
    {
        _sim->setTimeStep(SIM_TIME_STEP);
        _sim->setAgentDefaults(DEFAULT_NEIGHBOR_DIST,
                               DEFAULT_MAX_NEIGHBORS,
                               DEFAULT_TIME_HORIZON,
                               DEFAULT_TIME_HORIZON_OBST,
                               DEFAULT_AGENT_RADIUS,
                               DEFAULT_AGENT_MAX_SPEED);
    }

    LocalAvoidance::~LocalAvoidance() = default;

    void LocalAvoidance::RegisterAgent(Unit* unit)
    {
        if (!unit)
            return;
        ObjectGuid guid = unit->GetGUID();
        if (_agents->Find(guid) != std::size_t(-1))
            return;

        RVO::Vector2 pos(unit->GetPositionX(), unit->GetPositionY());
        float radius   = std::max(unit->GetCombatReach(), 0.5f);
        float maxSpeed = std::max(unit->GetSpeed(MOVE_RUN), 1.0f);

        std::size_t slot = _agents->TryAcquire(guid);
        if (slot == std::size_t(-1))
        {
            slot = _sim->addAgent(pos);
            _agents->RegisterSlot(guid, slot);
        }
        else
        {
            _sim->setAgentPosition(slot, pos);
        }
        _sim->setAgentRadius(slot, radius);
        _sim->setAgentMaxSpeed(slot, maxSpeed);
        _sim->setAgentPrefVelocity(slot, RVO::Vector2(0.0f, 0.0f));
        _sim->setAgentVelocity(slot, RVO::Vector2(0.0f, 0.0f));
    }

    void LocalAvoidance::UnregisterAgent(ObjectGuid guid)
    {
        std::size_t slot = _agents->Release(guid);
        if (slot == std::size_t(-1))
            return;
        _sim->setAgentPosition(slot, RVO::Vector2(PARK_COORD, PARK_COORD));
        _sim->setAgentRadius(slot, 0.0f);
        _sim->setAgentPrefVelocity(slot, RVO::Vector2(0.0f, 0.0f));
        _sim->setAgentVelocity(slot, RVO::Vector2(0.0f, 0.0f));
    }

    void LocalAvoidance::SetPreferredVelocity(Unit* owner, Position const& currentPos, Position const& preferredTarget, float maxSpeed)
    {
        if (!owner)
            return;
        std::size_t slot = _agents->Find(owner->GetGUID());
        if (slot == std::size_t(-1))
            return;

        float dx = preferredTarget.GetPositionX() - currentPos.GetPositionX();
        float dy = preferredTarget.GetPositionY() - currentPos.GetPositionY();
        float lenSq = dx * dx + dy * dy;

        RVO::Vector2 prefVel(0.0f, 0.0f);
        if (lenSq > 1.0e-6f)
        {
            float len = std::sqrt(lenSq);
            float scale = maxSpeed / len;
            prefVel = RVO::Vector2(dx * scale, dy * scale);
        }

        _sim->setAgentPosition(slot, RVO::Vector2(currentPos.GetPositionX(), currentPos.GetPositionY()));
        _sim->setAgentMaxSpeed(slot, std::max(maxSpeed, 1.0f));
        _sim->setAgentPrefVelocity(slot, prefVel);
    }

    void LocalAvoidance::Update(uint32 /*diff*/)
    {
        if (_sim->getNumAgents() == 0)
            return;
        _sim->doStep();
    }

    Position LocalAvoidance::QueryNudgedVelocity(ObjectGuid ownerGuid) const
    {
        std::size_t slot = _agents->Find(ownerGuid);
        if (slot == std::size_t(-1))
            return Position();
        RVO::Vector2 const& v = _sim->getAgentVelocity(slot);
        return Position(v.x(), v.y(), 0.0f, 0.0f);
    }
}
