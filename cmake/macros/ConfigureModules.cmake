# Copyright (C) 2008-2018 TrinityCore <https://www.trinitycore.org/>
#
# This file is free software; as a special exception the author gives
# unlimited permission to copy and/or distribute it, with or without
# modifications, as long as this notice is preserved.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY, to the extent permitted by law; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# Modules mirror the script-module machinery (see ConfigureScripts.cmake) but
# live in the top-level "modules" directory so third-party content stays out of
# the core source tree.

# Returns the base path to the modules directory in the source directory
function(GetModulesBasePath variable)
  set(${variable} "${CMAKE_SOURCE_DIR}/modules" PARENT_SCOPE)
endfunction()

# Stores the absolute path to the given module's sources in the variable
function(GetPathToModuleSource module variable)
  GetModulesBasePath(MODULE_BASE_PATH)
  set(${variable} "${MODULE_BASE_PATH}/${module}/src" PARENT_SCOPE)
endfunction()

# Stores the absolute path to the given module's config directory in the variable
function(GetPathToModuleConfig module variable)
  GetModulesBasePath(MODULE_BASE_PATH)
  set(${variable} "${MODULE_BASE_PATH}/${module}/conf" PARENT_SCOPE)
endfunction()

# Stores the project name of the given module in the variable
function(GetProjectNameOfModuleName module variable)
  string(TOLOWER "mod_${module}" GENERATED_NAME)
  set(${variable} "${GENERATED_NAME}" PARENT_SCOPE)
endfunction()

# Creates a list of all modules (directories under modules/ that expose a src/
# subdirectory) and stores it in the given variable.
function(GetModuleSourceList variable)
  GetModulesBasePath(BASE_PATH)
  file(GLOB LOCALE_MODULE_LIST RELATIVE
    ${BASE_PATH}
    ${BASE_PATH}/*)

  set(${variable})
  foreach(MODULE ${LOCALE_MODULE_LIST})
    GetPathToModuleSource(${MODULE} MODULE_SOURCE_PATH)
    if(IS_DIRECTORY ${MODULE_SOURCE_PATH})
      list(APPEND ${variable} ${MODULE})
    endif()
  endforeach()
  set(${variable} ${${variable}} PARENT_SCOPE)
endfunction()

# Converts the given module name into the variable name which holds its
# linkage type.
function(ModuleNameToVariable module variable)
  string(TOUPPER ${module} ${variable})
  set(${variable} "MODULE_${${variable}}")
  set(${variable} ${${variable}} PARENT_SCOPE)
endfunction()

# Stores in the given variable whether dynamic linking is required by any module
function(IsDynamicLinkingModulesRequired variable)
  if(MODULES MATCHES "dynamic")
    set(IS_DEFAULT_VALUE_DYNAMIC_MODULE ON)
  endif()

  GetModuleSourceList(MODULE_LIST)
  set(IS_REQUIRED OFF)
  foreach(MODULE ${MODULE_LIST})
    ModuleNameToVariable(${MODULE} MODULE_VARIABLE)
    if((${MODULE_VARIABLE} STREQUAL "dynamic") OR
        (${MODULE_VARIABLE} STREQUAL "default" AND IS_DEFAULT_VALUE_DYNAMIC_MODULE))
      set(IS_REQUIRED ON)
      break()
    endif()
  endforeach()
  set(${variable} ${IS_REQUIRED} PARENT_SCOPE)
endfunction()
