#ifndef TRINITY_CHASE_GENERATOR_H
#define TRINITY_CHASE_GENERATOR_H

#include "MotionMaster.h"
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

enum class ChaseGeneratorState : uint8
{
    Planning,
    Executing,
    IdleInBand,
};

// Synchronous: the next spline must launch the same tick to avoid a visible
// gap. Chase paths are short (~1-3 ms, cached afterwards).
class TC_GAME_API ChaseGenerator : public MovementGenerator
{
public:
    explicit ChaseGenerator(Unit* target, Optional<ChaseRange> range = {}, Optional<ChaseAngle> angle = {});
    ~ChaseGenerator() override;

    ChaseGenerator(ChaseGenerator const&) = delete;
    ChaseGenerator& operator=(ChaseGenerator const&) = delete;

    void Initialize(Unit* owner) override;
    void Finalize(Unit* owner) override;
    void Reset(Unit* owner) override;
    bool Update(Unit* owner, uint32 diff) override;

    MovementGeneratorType GetMovementGeneratorType() const override { return CHASE_MOTION_TYPE; }

    void unitSpeedChanged() override;

    void SetRangeAndAngle(Optional<ChaseRange> range, Optional<ChaseAngle> angle);

    // Reserved for caster scripts; currently a no-op.
    void DistanceYourself(Unit* owner, float distance);

    [[nodiscard]] ObjectGuid GetTargetGuid() const { return _targetGuid; }

private:
    ObjectGuid _targetGuid;
    Optional<ChaseRange> _range;
    Optional<ChaseAngle> _angle;

    ChaseGeneratorState _state = ChaseGeneratorState::Planning;
    Optional<Position> _lastTargetPosition;

    std::unique_ptr<Movement::SplineExecutor> _splineExecutor;

    Unit* ResolveTarget(Unit* owner);
    bool CanChase(Unit* owner, Unit* target) const;
};

#endif
