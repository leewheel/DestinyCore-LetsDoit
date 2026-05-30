/*
 * Copyright (C) 2008-2018 TrinityCore <https://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _MODULES_PRECOMPILED_H_
#define _MODULES_PRECOMPILED_H_

// Modules are scripts living outside the core tree, so the prefix mirrors the
// stable gameplay API a module commonly consumes (see ScriptPCH.h).
//
// Order matters: the object model is listed before the scripting layer because
// inline accessors (e.g. ScriptedAI) need complete Map/Creature types.

#include "Common.h"

// Object model
#include "Map.h"
#include "ObjectAccessor.h"
#include "Creature.h"
#include "GameObject.h"
#include "Player.h"

// Scripting framework
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"

// Common helpers
#include "Chat.h"
#include "Config.h"
#include "Log.h"
#include "WorldSession.h"

#endif

