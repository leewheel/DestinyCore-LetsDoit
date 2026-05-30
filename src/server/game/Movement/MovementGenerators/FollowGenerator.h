#ifndef TRINITY_FOLLOW_GENERATOR_H
#define TRINITY_FOLLOW_GENERATOR_H

#include "MovementGenerator.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include "Position.h"
#include <memory>

class Unit;

namespace Movement
{
    class SplineExecutor;
}

enum class FollowGeneratorState : uint8
{
    Planning,
    Executing,
    IdleNearLeader,
};

// Sync follow on the same service stack as ChaseGenerator. Leader resolved by
// GUID per tick. One code path for Player and Creature owners.
class TC_GAME_API FollowGenerator : public MovementGenerator
{
public:
    explicit FollowGenerator(Unit* leader, float offset, float angle);
    ~FollowGenerator() override;

    FollowGenerator(FollowGenerator const&) = delete;
    FollowGenerator& operator=(FollowGenerator const&) = delete;

    void Initialize(Unit* owner) override;
    void Finalize(Unit* owner) override;
    void Reset(Unit* owner) override;
    bool Update(Unit* owner, uint32 diff) override;

    MovementGeneratorType GetMovementGeneratorType() const override { return FOLLOW_MOTION_TYPE; }

    void unitSpeedChanged() override;

    [[nodiscard]] ObjectGuid GetTargetGuid() const { return _leaderGuid; }

private:
    ObjectGuid _leaderGuid;
    float _offset;
    float _angle;

    FollowGeneratorState _state = FollowGeneratorState::Planning;
    Optional<Position> _lastLeaderPosition;
    bool _movementInformPending = false;

    // 1:1 mirror, no Kalman lead, no deadzone, no ORCA on self (pets are
    // still registered so other agents route around them). Locked at init.
    bool _petMirrorMode = false;

    std::unique_ptr<Movement::SplineExecutor> _splineExecutor;

    Unit* ResolveLeader(Unit* owner);
    bool CanFollow(Unit* owner, Unit* leader) const;
    void SyncPetSpeed(Unit* owner, Unit* leader) const;
};

#endif
