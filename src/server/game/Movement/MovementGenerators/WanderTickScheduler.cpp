/*
 * Copyright (C) DestinyCore <https://www.destinycore.org/>
 *
 * See WanderTickScheduler.h for the design rationale.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "WanderTickScheduler.h"

uint8 WanderTickScheduler::RegisterSlot()
{
    uint8 slot = _nextRegisterSlot;
    _nextRegisterSlot = (_nextRegisterSlot + 1) & (SLOTS - 1);
    return slot;
}
