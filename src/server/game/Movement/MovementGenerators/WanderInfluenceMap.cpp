/*
 * Copyright (C) DestinyCore <https://www.destinycore.org/>
 *
 * See WanderInfluenceMap.h for the design rationale.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "WanderInfluenceMap.h"
#include <cmath>

uint64 WanderInfluenceMap::KeyFor(float x, float y)
{
    // floor() rounds toward -inf, which is what we want for negative coords
    // (otherwise floor(-3.5)==-3, mixing cells (-1) and (0) around the origin).
    int32 cx = static_cast<int32>(std::floor(x / CELL_SIZE)) + CELL_BIAS;
    int32 cy = static_cast<int32>(std::floor(y / CELL_SIZE)) + CELL_BIAS;
    return (static_cast<uint64>(static_cast<uint32>(cx)) << 32)
         |  static_cast<uint64>(static_cast<uint32>(cy));
}

void WanderInfluenceMap::Add(float x, float y)
{
    ++_cells[KeyFor(x, y)];
}

void WanderInfluenceMap::Remove(float x, float y)
{
    auto it = _cells.find(KeyFor(x, y));
    if (it == _cells.end())
        return;
    if (it->second <= 1)
        _cells.erase(it);
    else
        --it->second;
}

void WanderInfluenceMap::Move(float oldX, float oldY, float newX, float newY)
{
    uint64 oldKey = KeyFor(oldX, oldY);
    uint64 newKey = KeyFor(newX, newY);
    if (oldKey == newKey)
        return; // intra-cell move, nothing to update

    if (auto it = _cells.find(oldKey); it != _cells.end())
    {
        if (it->second <= 1)
            _cells.erase(it);
        else
            --it->second;
    }
    ++_cells[newKey];
}

uint16 WanderInfluenceMap::GetDensity(float x, float y) const
{
    auto it = _cells.find(KeyFor(x, y));
    return (it != _cells.end()) ? it->second : uint16(0);
}
