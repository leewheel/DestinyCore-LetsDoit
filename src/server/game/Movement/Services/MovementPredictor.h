#ifndef TRINITY_MOVEMENT_PREDICTOR_H
#define TRINITY_MOVEMENT_PREDICTOR_H

#include "Define.h"
#include "Position.h"

namespace Movement
{
    // 2D constant-velocity Kalman per axis. Z and orientation passthrough.
    // Predictions diverge past ~500ms on accelerating targets; replan instead.
    class TC_GAME_API MovementPredictor
    {
    public:
        MovementPredictor();
        ~MovementPredictor() = default;

        // Out-of-order observations are dropped silently.
        void Observe(Position const& position, uint32 timestampMs);

        // Reset on tracked target switch or teleport.
        void Reset();

        // Returns the last observation when fewer than 2 samples have been fed.
        [[nodiscard]] Position PredictAt(uint32 horizonMs) const;
        [[nodiscard]] float GetEstimatedSpeed() const;
        [[nodiscard]] bool IsConverged() const { return _observationCount >= 2; }

    private:
        struct DimensionState
        {
            float pos = 0.0f;
            float vel = 0.0f;
            float varPos = 100.0f;
            float varVel = 100.0f;
            float covPV  = 0.0f;
        };

        DimensionState _x;
        DimensionState _y;

        float _z;
        float _o;

        uint32 _lastObservationMs;
        uint32 _observationCount;

        static void Predict(DimensionState& d, float dt);
        static void Update(DimensionState& d, float observation);
    };
}

#endif
