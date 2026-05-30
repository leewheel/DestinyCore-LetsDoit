/*
 * Copyright (C) 2008-2018 TrinityCore <https://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef TRINITY_FLIGHT_PATH_MOVEMENT_GENERATOR_H
#define TRINITY_FLIGHT_PATH_MOVEMENT_GENERATOR_H

#include "MovementGenerator.h"
#include "DB2Stores.h"
#include "Player.h"
#include <deque>

#define FLIGHT_TRAVEL_UPDATE  100
#define TIMEDIFF_NEXT_WP      250

// Player taxi flight generator. Kept structurally identical to the legacy
// implementation: the cross-continent stitching and event-firing logic are
// closely tied to the DB2 TaxiPathNode tables and have no overlap with the
// modernised creature waypoint flow. Untouched by Phase 4.
class FlightPathMovementGenerator : public MovementGeneratorMedium< Player, FlightPathMovementGenerator >
{
public:
    explicit FlightPathMovementGenerator()
        : _currentNode(0), _endGridX(0.0f), _endGridY(0.0f), _endMapId(0), _preloadTargetNode(0)
    {
    }

    void LoadPath(Player* player, uint32 startNode = 0);
    void DoInitialize(Player*);
    void DoReset(Player*);
    void DoFinalize(Player*);
    bool DoUpdate(Player*, uint32);
    MovementGeneratorType GetMovementGeneratorType() const override { return FLIGHT_MOTION_TYPE; }

    TaxiPathNodeList const& GetPath() const { return _path; }
    uint32 GetPathAtMapEnd() const;
    bool HasArrived() const { return (_currentNode >= _path.size()); }
    void SetCurrentNodeAfterTeleport();
    void SkipCurrentNode() { ++_currentNode; }
    void DoEventIfAny(Player* player, TaxiPathNodeEntry const* node, bool departure);

    bool GetResetPos(Player*, float& x, float& y, float& z);

    void InitEndGridInfo();
    void PreloadEndGrid();

    uint32 GetCurrentNode() const { return _currentNode; }

private:
    TaxiPathNodeList _path;
    uint32 _currentNode;

    float _endGridX;
    float _endGridY;
    uint32 _endMapId;
    uint32 _preloadTargetNode;

    struct TaxiNodeChangeInfo
    {
        uint32 PathIndex;
        int64 Cost;
    };

    std::deque<TaxiNodeChangeInfo> _pointsForPathSwitch;
};

#endif
