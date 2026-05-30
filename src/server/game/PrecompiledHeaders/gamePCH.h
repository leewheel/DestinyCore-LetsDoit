// Precompiled header for the game library.
//
// Selection rule: headers included by a large share of the ~400 game
// translation units AND heavy and rarely edited. A header listed here is a
// dependency of every TU, so editing it forces a full library rebuild — only
// add stable, ubiquitous headers.

// STL aggregator (pulls fmt, regex, the common containers) + utilities
#include "Common.h"
#include "Containers.h"
#include "Util.h"

// Core object model and managers (the hot path of nearly every TU)
#include "Creature.h"
#include "GameObject.h"
#include "Group.h"
#include "Item.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Unit.h"
#include "World.h"

// Spells
#include "SpellInfo.h"
#include "SpellMgr.h"

// Networking / sessions / data stores
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "Opcodes.h"
#include "Packet.h"
#include "PacketUtilities.h"
#include "WorldPacket.h"
#include "WorldSession.h"

// Scripting entry point and diagnostics
#include "Errors.h"
#include "Log.h"
#include "ScriptMgr.h"

// STL not already pulled in transitively through Common.h
#include <algorithm>
#include <functional>
#include <set>
#include <sstream>
#include <vector>
