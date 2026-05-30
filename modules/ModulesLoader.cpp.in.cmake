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

// This file was created automatically from your module configuration!
// Use CMake to reconfigure this file, never change it on your own!

#cmakedefine TRINITY_IS_DYNAMIC_SCRIPTLOADER

#include "Define.h"
#include <vector>
#include <string>

@TRINITY_SCRIPTS_FORWARD_DECL@
#ifdef TRINITY_IS_DYNAMIC_SCRIPTLOADER
#  include "revision_data.h"
#  define TC_SCRIPT_API TC_API_EXPORT
extern "C" {

/// Exposed in module libraries to return the module revision hash.
TC_SCRIPT_API char const* GetScriptModuleRevisionHash()
{
    return _HASH;
}

/// Exposed in module libraries to return the name of the module
/// contained in this shared library.
TC_SCRIPT_API char const* GetScriptModule()
{
    return "@TRINITY_CURRENT_SCRIPT_PROJECT@";
}

/// A dynamically linked module exposes the same entry point as a dynamic
/// script module, so the hotswap loader (ScriptReloadMgr) picks it up unchanged.
TC_SCRIPT_API void AddScripts()
{
@TRINITY_SCRIPTS_INVOKE@}

/// Exposed in module libraries to get the build directive of the module.
TC_SCRIPT_API char const* GetBuildDirective()
{
    return _BUILD_DIRECTIVE;
}

} // extern "C"
#else
#  include "ModulesScriptLoader.h"

/// Statically linked modules register through a dedicated entry point so the
/// symbol never collides with the core's own AddScripts().
void AddModulesScripts()
{
@TRINITY_SCRIPTS_INVOKE@}
#endif
