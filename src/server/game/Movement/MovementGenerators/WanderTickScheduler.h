/*
 * Copyright (C) DestinyCore <https://www.destinycore.org/>
 *
 * WanderTickScheduler - time-budgeted dispatcher for SmartWanderGenerator.
 *
 * Without this, every wanderer in a map whose internal pause expires the
 * same frame fires its full decision pipeline (sampler + scoring + up to
 * 5 PathGenerator A* searches) simultaneously. On a capital with thousands
 * of wanderers, that produces nasty per-frame CPU spikes after each global
 * idle window collapses.
 *
 * The scheduler assigns each generator a stable "slot" (0..SLOTS-1) at
 * registration time, round-robin. Each Map update increments an "active
 * slot" counter; only generators whose slot matches the current active
 * slot are allowed to actually decide this frame. Generators whose pause
 * has expired but whose slot is not active simply wait - up to SLOTS-1
 * frames extra latency (~128ms at 60Hz with SLOTS=8), which is well below
 * any player-perceivable threshold for wander cadence.
 *
 * One instance per Map. No locking: Map updates are single-threaded
 * per-instance in Legion's MapUpdater.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef _WANDER_TICK_SCHEDULER_H
#define _WANDER_TICK_SCHEDULER_H

#include "Define.h"

class TC_GAME_API WanderTickScheduler
{
public:
    // 8 slots ≈ ~128ms max scheduling latency at 60Hz Map updates. Pushing
    // this higher buys smoother CPU at the cost of decision latency; below
    // 4 doesn't smooth meaningfully. Power-of-two keeps the modulo cheap.
    static constexpr uint8 SLOTS = 8;

    // Round-robin slot assignment for new generators. Stable across DoUpdate
    // calls - generators store the returned value and use it for their
    // lifetime (unregistered via Map teardown, no explicit free list).
    uint8 RegisterSlot();

    // Called once per Map::Update at the top, before any creature ticks.
    void AdvanceFrame() { _activeSlot = (_activeSlot + 1) & (SLOTS - 1); }

    bool IsActiveSlot(uint8 slot) const { return slot == _activeSlot; }
    uint8 GetActiveSlot() const { return _activeSlot; }

private:
    uint8 _activeSlot = 0;
    uint8 _nextRegisterSlot = 0;
};

#endif
