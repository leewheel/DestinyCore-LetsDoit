#include "MovementPredictor.h"
#include <algorithm>
#include <cmath>

namespace Movement
{
    // sigma_a^2 = 25 matches the ~5 y/s^2 peak of a chase target turning.
    static constexpr float PROCESS_ACCEL_VARIANCE = 25.0f;

    // 0.2y std absorbs tick jitter without slowing convergence on real motion.
    static constexpr float OBSERVATION_NOISE = 0.04f;

    // Constant-velocity assumption breaks down past this; PredictAt caps here.
    static constexpr uint32 PREDICTION_HORIZON_CAP_MS = 750;

    MovementPredictor::MovementPredictor()
        : _z(0.0f), _o(0.0f), _lastObservationMs(0), _observationCount(0)
    {
    }

    void MovementPredictor::Reset()
    {
        _x = DimensionState{};
        _y = DimensionState{};
        _lastObservationMs = 0;
        _observationCount = 0;
    }

    void MovementPredictor::Observe(Position const& position, uint32 timestampMs)
    {
        _z = position.GetPositionZ();
        _o = position.GetOrientation();

        if (_observationCount == 0)
        {
            _x.pos = position.GetPositionX();
            _y.pos = position.GetPositionY();
            _lastObservationMs = timestampMs;
            _observationCount = 1;
            return;
        }

        if (timestampMs <= _lastObservationMs)
            return;

        float const dt = float(timestampMs - _lastObservationMs) * 0.001f;
        Predict(_x, dt);
        Predict(_y, dt);
        Update(_x, position.GetPositionX());
        Update(_y, position.GetPositionY());

        _lastObservationMs = timestampMs;
        if (_observationCount < 0xFFFFFFFF)
            ++_observationCount;
    }

    Position MovementPredictor::PredictAt(uint32 horizonMs) const
    {
        if (_observationCount < 2 || horizonMs == 0)
            return Position(_x.pos, _y.pos, _z, _o);

        float const dt = float(std::min(horizonMs, PREDICTION_HORIZON_CAP_MS)) * 0.001f;
        float const px = _x.pos + _x.vel * dt;
        float const py = _y.pos + _y.vel * dt;
        return Position(px, py, _z, _o);
    }

    float MovementPredictor::GetEstimatedSpeed() const
    {
        return std::sqrt(_x.vel * _x.vel + _y.vel * _y.vel);
    }

    // F = [[1, dt], [0, 1]]; Q = sigma_a^2 * [[dt^4/4, dt^3/2], [dt^3/2, dt^2]].
    void MovementPredictor::Predict(DimensionState& d, float dt)
    {
        float const dt2 = dt * dt;
        float const dt3 = dt2 * dt;
        float const dt4 = dt2 * dt2;

        d.pos += d.vel * dt;

        float const newVarPos = d.varPos + 2.0f * dt * d.covPV + dt2 * d.varVel + (dt4 * 0.25f) * PROCESS_ACCEL_VARIANCE;
        float const newCovPV  = d.covPV + dt * d.varVel + (dt3 * 0.5f) * PROCESS_ACCEL_VARIANCE;
        float const newVarVel = d.varVel + dt2 * PROCESS_ACCEL_VARIANCE;

        d.varPos = newVarPos;
        d.covPV  = newCovPV;
        d.varVel = newVarVel;
    }

    // H = [1, 0]; S = varPos + R; K = [varPos, covPV] / S.
    void MovementPredictor::Update(DimensionState& d, float observation)
    {
        float const innovation = observation - d.pos;
        float const s = d.varPos + OBSERVATION_NOISE;
        float const kPos = d.varPos / s;
        float const kVel = d.covPV / s;

        d.pos += kPos * innovation;
        d.vel += kVel * innovation;

        float const newVarPos = (1.0f - kPos) * d.varPos;
        float const newCovPV  = (1.0f - kPos) * d.covPV;
        float const newVarVel = d.varVel - kVel * d.covPV;

        d.varPos = newVarPos;
        d.covPV  = newCovPV;
        d.varVel = newVarVel;
    }
}
