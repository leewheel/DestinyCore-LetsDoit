/*
 * Copyright (C) DestinyCore <https://www.destinycore.org/>
 *
 * WanderProfileMgr implementation - see WanderProfileMgr.h.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "WanderProfileMgr.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Timer.h"
#include <algorithm>

WanderProfileMgr* WanderProfileMgr::instance()
{
    static WanderProfileMgr instance;
    return &instance;
}

void WanderProfileMgr::LoadAll()
{
    uint32 oldMSTime = getMSTime();
    _profiles.clear();

    //                                                  0   1     2            3            4     5            6          7          8           9              10            11             12          13           14          15              16             17
    QueryResult result = WorldDatabase.Query("SELECT Id, Name, SamplerMode, SampleCount, TopK, MemorySize, MinRadius, MaxRadius, TickMsMin, TickMsMax, WeightDistance, WeightSlope, WeightNavmesh, WeightWater, WeightMemory, WeightLosSpawn, WeightDensity, WeightFacing FROM wander_profile");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 wander profiles. DB table `wander_profile` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 id = fields[0].GetUInt32();
        if (id == 0)
        {
            // Id 0 is the implicit "default profile" sentinel - reject so we
            // don't accidentally shadow SmartWander::Profile::Default() and
            // create a hard-to-debug mismatch between code and DB.
            TC_LOG_ERROR("sql.sql", "wander_profile row with Id 0 is forbidden (reserved for built-in default).");
            continue;
        }

        ProfileEntry entry;
        entry.name = fields[1].GetString();

        SmartWander::Profile& p = entry.profile;
        uint8 samplerMode = fields[2].GetUInt8();
        p.samplerMode = (samplerMode == SmartWander::SAMPLER_HALTON)
                            ? SmartWander::SAMPLER_HALTON
                            : SmartWander::SAMPLER_POLAR_RANDOM;
        p.sampleCount = std::clamp<uint8>(fields[3].GetUInt8(), 1, 64);
        p.topK        = std::clamp<uint8>(fields[4].GetUInt8(), 1, p.sampleCount);
        p.memorySize  = std::min<uint8>(fields[5].GetUInt8(), 32);
        p.minRadius   = std::max(0.0f, fields[6].GetFloat());
        p.maxRadius   = std::max(0.0f, fields[7].GetFloat());
        p.tickMsMin   = fields[8].GetUInt32();
        p.tickMsMax   = std::max(p.tickMsMin, fields[9].GetUInt32());
        p.weights.distance = std::max(0.0f, fields[10].GetFloat());
        p.weights.slope    = std::max(0.0f, fields[11].GetFloat());
        p.weights.navmesh  = std::max(0.0f, fields[12].GetFloat());
        p.weights.water    = std::max(0.0f, fields[13].GetFloat());
        p.weights.memory   = std::max(0.0f, fields[14].GetFloat());
        p.weights.losSpawn = std::max(0.0f, fields[15].GetFloat());
        p.weights.density  = std::max(0.0f, fields[16].GetFloat());
        p.weights.facing   = std::max(0.0f, fields[17].GetFloat());

        _profiles.emplace(id, std::move(entry));
        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded %u wander profiles in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}

SmartWander::Profile const* WanderProfileMgr::GetProfile(uint32 id) const
{
    if (id == 0)
        return nullptr;
    auto it = _profiles.find(id);
    return (it != _profiles.end()) ? &it->second.profile : nullptr;
}
