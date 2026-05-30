/*
 * Copyright (C) DestinyCore <https://www.destinycore.org/>
 *
 * WanderProfileMgr - SQL-backed cache of SmartWander::Profile.
 *
 * Profiles are loaded once at world startup from the `wander_profile` table
 * and never mutated afterwards (callers borrow stable pointers). Reloading
 * is supported via .reload wander_profile (TODO: 2.2.6 follow-up command).
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef _WANDER_PROFILE_MGR_H
#define _WANDER_PROFILE_MGR_H

#include "SmartWanderGenerator.h"
#include <string>
#include <unordered_map>

class TC_GAME_API WanderProfileMgr
{
public:
    static WanderProfileMgr* instance();

    // Pulls every row from `wander_profile`. Safe to call multiple times
    // (clears the cache first). Pointers returned by GetProfile() are
    // invalidated by a reload - callers must not retain them across a reload.
    void LoadAll();

    // Returns a borrowed pointer to the profile with the given id, or
    // nullptr if no such id exists. id == 0 is treated as "no override",
    // so it always returns nullptr (callers should fall back to
    // SmartWander::Profile::Default()).
    SmartWander::Profile const* GetProfile(uint32 id) const;

private:
    WanderProfileMgr() = default;
    ~WanderProfileMgr() = default;
    WanderProfileMgr(WanderProfileMgr const&) = delete;
    WanderProfileMgr& operator=(WanderProfileMgr const&) = delete;

    struct ProfileEntry
    {
        std::string name;
        SmartWander::Profile profile;
    };

    std::unordered_map<uint32, ProfileEntry> _profiles;
};

#define sWanderProfileMgr WanderProfileMgr::instance()

#endif
