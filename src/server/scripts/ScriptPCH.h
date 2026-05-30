// Precompiled header for the scripts library.
//
// Selection rule: headers included by a large share of the ~1200 script
// translation units. Scripts almost always edit their own .cpp files and only
// consume the (stable) gameplay API below, which makes these near-ideal PCH
// candidates: high reuse, very low churn.
//
// Order matters: a PCH is force-included at the top of every TU, so headers
// are listed low-level first. Several inline accessors (e.g. ScriptedAI) need a
// complete Map/Creature, so the object model must precede the scripting layer.

// STL aggregator
#include "Common.h"

// Object model (low level first so inline accessors see complete types)
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "Player.h"
#include "Vehicle.h"
#include "TemporarySummon.h"
#include "MotionMaster.h"
#include "PhasingHandler.h"

// Spells
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"

// Scripting framework
#include "ScriptMgr.h"
#include "CreatureAIImpl.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"

// Common helpers
#include "Chat.h"
#include "Log.h"
