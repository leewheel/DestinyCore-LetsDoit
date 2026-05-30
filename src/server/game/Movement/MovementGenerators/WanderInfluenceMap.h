/*
 * Copyright (C) DestinyCore <https://www.destinycore.org/>
 *
 * WanderInfluenceMap - sparse density grid keyed by world cell.
 *
 * SmartWanderGenerator queries this to penalise candidate destinations
 * already populated by other wanderers (anti-clumping). Per-Map instance
 * so reads/writes are serialised by Legion's existing per-Map update lock.
 *
 * Cells are 8m squares - big enough to avoid coordinate jitter false-negatives,
 * small enough that "another wanderer is here" is geometrically meaningful at
 * the SmartWander range scale (3..30y).
 *
 * Memory: ~O(N_active_wanderers) entries - sparse. Typical capital with 2000
 * spread wanderers ~= 2000 cells * 16 bytes = 32KB. Negligible.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef _WANDER_INFLUENCE_MAP_H
#define _WANDER_INFLUENCE_MAP_H

#include "Define.h"
#include <unordered_map>

class TC_GAME_API WanderInfluenceMap
{
public:
    // 8.0f is empirical: a creature occupying a cell discourages other
    // creatures from picking destinations in adjacent cells too aggressively
    // (we only check the candidate's own cell, not neighbours).
    static constexpr float CELL_SIZE = 8.0f;

    // Bias used to fold signed cell coordinates into uint32 without losing
    // information. Lets us combine (cellX, cellY) into a single 64-bit key
    // without conditionals or pair-hashing overhead.
    static constexpr int32 CELL_BIAS = 0x40000000;

    // Increment the count in the cell containing (x, y). Pair with Remove
    // when the wanderer leaves the cell.
    void Add(float x, float y);

    // Decrement the count in the cell containing (x, y). No-op if the cell
    // was already at 0 (defensive - late finalize after teleport, etc).
    void Remove(float x, float y);

    // Convenience: Remove(oldX, oldY) + Add(newX, newY) when both coordinates
    // map to different cells. Same-cell moves are a no-op.
    void Move(float oldX, float oldY, float newX, float newY);

    // Read-only density for the cell containing (x, y). 0 if no entry exists
    // (the unordered_map is sparse - absence == 0).
    uint16 GetDensity(float x, float y) const;

    // For testing / debug commands.
    size_t TrackedCells() const { return _cells.size(); }

private:
    static uint64 KeyFor(float x, float y);

    // uint16 = up to 65535 wanderers per 8m cell. Comfortably impossible in
    // practice - even in raid spawns we cap at a few hundred per zone.
    std::unordered_map<uint64, uint16> _cells;
};

#endif
