# Copyright (C) 2008-2018 TrinityCore <https://www.trinitycore.org/>
#
# This file is free software; as a special exception the author gives
# unlimited permission to copy and/or distribute it, with or without
# modifications, as long as this notice is preserved.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY, to the extent permitted by law; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

message("")

# Make the module list available in the current scope
GetModuleSourceList(MODULE_LIST)

# Make the native install offset available in this scope
GetInstallOffset(INSTALL_OFFSET)

# Sets the MODULE_${MODULE} variables when using predefined templates for
# module building like dynamic, static.
# Sets MODULES_DEFAULT_LINKAGE
if (MODULES MATCHES "dynamic")
  set(MODULES_DEFAULT_LINKAGE "dynamic")
elseif(MODULES MATCHES "static")
  set(MODULES_DEFAULT_LINKAGE "static")
else()
  set(MODULES_DEFAULT_LINKAGE "disabled")
endif()

# Set the MODULE_${MODULE} variables from the variables set above
foreach(MODULE ${MODULE_LIST})
  ModuleNameToVariable(${MODULE} MODULE_VARIABLE)
  if (${MODULE_VARIABLE} STREQUAL "default")
    set(${MODULE_VARIABLE} ${MODULES_DEFAULT_LINKAGE})
  endif()
  # Build the Graph values
  if (${MODULE_VARIABLE} MATCHES "dynamic")
    GetProjectNameOfModuleName(${MODULE} MODULE_PROJECT_NAME)
    GetNativeSharedLibraryName(${MODULE_PROJECT_NAME} MODULE_PROJECT_LIBRARY)
    list(APPEND GRAPH_KEYS ${MODULE_PROJECT_NAME})
    set(GRAPH_VALUE_DISPLAY_${MODULE_PROJECT_NAME} ${MODULE_PROJECT_LIBRARY})
    list(APPEND GRAPH_VALUE_CONTAINS_MODULES_${MODULE_PROJECT_NAME} ${MODULE})
  elseif(${MODULE_VARIABLE} MATCHES "static")
    list(APPEND GRAPH_KEYS worldserver)
    set(GRAPH_VALUE_DISPLAY_worldserver worldserver)
    list(APPEND GRAPH_VALUE_CONTAINS_MODULES_worldserver ${MODULE})
  else()
    list(APPEND GRAPH_KEYS disabled)
    set(GRAPH_VALUE_DISPLAY_disabled disabled)
    list(APPEND GRAPH_VALUE_CONTAINS_MODULES_disabled ${MODULE})
  endif()
endforeach()

list(SORT GRAPH_KEYS)
list(REMOVE_DUPLICATES GRAPH_KEYS)

# Display the module graph
message("* Module configuration (${MODULES}):
    |")

foreach(GRAPH_KEY ${GRAPH_KEYS})
  if (NOT GRAPH_KEY STREQUAL "disabled")
    message("    +- ${GRAPH_VALUE_DISPLAY_${GRAPH_KEY}}")
  else()
    message("    |  ${GRAPH_VALUE_DISPLAY_${GRAPH_KEY}}")
  endif()
  foreach(GRAPH_PROJECT_ENTRY ${GRAPH_VALUE_CONTAINS_MODULES_${GRAPH_KEY}})
    message("    |   +- ${GRAPH_PROJECT_ENTRY}")
  endforeach()
  message("    |")
endforeach()

# Base sources which are used by every module project
if (USE_SCRIPTPCH)
  set(PRIVATE_PCH_HEADER ModulesPCH.h)
endif ()

GroupSources(${CMAKE_CURRENT_SOURCE_DIR})

# Copies a module config (*.conf.dist) next to the worldserver config so it is
# auto-loaded at runtime (see ConfigMgr::LoadModulesConfigs). The admin still
# renames the *.conf.dist to *.conf to activate it.
function(CopyModuleConfig configFile)
  if (WIN32)
    if ("${CMAKE_MAKE_PROGRAM}" MATCHES "MSBuild")
      add_custom_command(TARGET modules POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin/$(ConfigurationName)/configs/modules")
      add_custom_command(TARGET modules POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${configFile}" "${CMAKE_BINARY_DIR}/bin/$(ConfigurationName)/configs/modules")
    elseif (MINGW)
      add_custom_command(TARGET modules POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin/configs/modules")
      add_custom_command(TARGET modules POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${configFile}" "${CMAKE_BINARY_DIR}/bin/configs/modules")
    endif()
  endif()

  if (UNIX)
    install(FILES "${configFile}" DESTINATION "${CONF_DIR}/modules")
  elseif (WIN32)
    install(FILES "${configFile}" DESTINATION "${CMAKE_INSTALL_PREFIX}/configs/modules")
  endif()
endfunction()

# Configures the module loader with the given name and stores the output in the
# LOADER_OUT variable. Mirrors ConfigureScriptLoader() but generates the module
# entry points (AddModulesScripts / AddScripts) from ModulesLoader.cpp.in.cmake.
function(ConfigureModuleLoader SCRIPTLOADER_NAME LOADER_OUT IS_DYNAMIC_SCRIPTLOADER)
  # Deduces the variables referenced by the template:
  # TRINITY_IS_DYNAMIC_SCRIPTLOADER
  # TRINITY_SCRIPTS_FORWARD_DECL
  # TRINITY_SCRIPTS_INVOKE
  # TRINITY_CURRENT_SCRIPT_PROJECT
  set(TRINITY_IS_DYNAMIC_SCRIPTLOADER ${IS_DYNAMIC_SCRIPTLOADER})
  unset(TRINITY_SCRIPTS_FORWARD_DECL)
  unset(TRINITY_SCRIPTS_INVOKE)
  set(TRINITY_CURRENT_SCRIPT_PROJECT ${SCRIPTLOADER_NAME})
  foreach(LOCALE_MODULE ${ARGN})
    # The loader function is derived from the directory name, so the '-' that is
    # common in module names has to be sanitized into a valid identifier.
    string(REGEX REPLACE "-" "_" LOCALE_MODULE ${LOCALE_MODULE})
    set(LOADER_FUNCTION "Add${LOCALE_MODULE}Scripts()")
    set(TRINITY_SCRIPTS_FORWARD_DECL
      "${TRINITY_SCRIPTS_FORWARD_DECL}void ${LOADER_FUNCTION};\n")
    set(TRINITY_SCRIPTS_INVOKE
      "${TRINITY_SCRIPTS_INVOKE}    ${LOADER_FUNCTION};\n")
  endforeach()
  set(GENERATED_LOADER ${CMAKE_CURRENT_BINARY_DIR}/gen_modulesloader/${SCRIPTLOADER_NAME}/ModulesLoader.cpp)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/ModulesLoader.cpp.in.cmake ${GENERATED_LOADER})
  set(${LOADER_OUT} ${GENERATED_LOADER} PARENT_SCOPE)
endfunction()

# Generates the actual module projects.
# Fills the STATIC_MODULES and DYNAMIC_MODULE_PROJECTS variables which contain
# the names which modules are linked statically/dynamically and adds the sources
# of the static modules to the PRIVATE_SOURCES variable.
foreach(MODULE ${MODULE_LIST})
  GetPathToModuleSource(${MODULE} MODULE_SOURCE_PATH)
  ModuleNameToVariable(${MODULE} MODULE_VARIABLE)

  if ((${MODULE_VARIABLE} STREQUAL "disabled") OR
      (${MODULE_VARIABLE} STREQUAL "static"))
    # Uninstall disabled modules
    GetProjectNameOfModuleName(${MODULE} MODULE_PROJECT_NAME)
    GetNativeSharedLibraryName(${MODULE_PROJECT_NAME} MODULE_OUTPUT_NAME)
    list(APPEND DISABLED_MODULE_PROJECTS ${INSTALL_OFFSET}/${MODULE_OUTPUT_NAME})
    if (${MODULE_VARIABLE} STREQUAL "static")
      # Add the module name to STATIC_MODULES
      list(APPEND STATIC_MODULES ${MODULE})
      # Add the module content to the whole static module
      CollectSourceFiles(${MODULE_SOURCE_PATH} PRIVATE_SOURCES)
      CollectIncludeDirectories(${MODULE_SOURCE_PATH} PUBLIC_INCLUDES)
    endif()
  elseif(${MODULE_VARIABLE} STREQUAL "dynamic")
    # Generate an own dynamic module which is loadable on runtime
    unset(MODULE_PRIVATE_SOURCES)
    CollectSourceFiles(${MODULE_SOURCE_PATH} MODULE_PRIVATE_SOURCES)
    CollectIncludeDirectories(${MODULE_SOURCE_PATH} PUBLIC_INCLUDES)
    # Configure the module loader
    ConfigureModuleLoader(${MODULE} MODULE_PRIVATE_SCRIPTLOADER ON ${MODULE})
    GetProjectNameOfModuleName(${MODULE} MODULE_PROJECT_NAME)
    # Add the module name to DYNAMIC_MODULE_PROJECTS
    list(APPEND DYNAMIC_MODULE_PROJECTS ${MODULE_PROJECT_NAME})
    # Create the module project
    add_library(${MODULE_PROJECT_NAME} SHARED
      ${MODULE_PRIVATE_SOURCES}
      ${MODULE_PRIVATE_SCRIPTLOADER})

    target_link_libraries(${MODULE_PROJECT_NAME}
      PRIVATE
        trinity-core-interface
      PUBLIC
        game)

    target_include_directories(${MODULE_PROJECT_NAME}
      PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR})

    set_target_properties(${MODULE_PROJECT_NAME}
      PROPERTIES
        FOLDER
          "modules")

    if (UNIX)
      install(TARGETS ${MODULE_PROJECT_NAME}
        DESTINATION ${INSTALL_OFFSET}
        COMPONENT ${MODULE_PROJECT_NAME})
    elseif (WIN32)
      install(TARGETS ${MODULE_PROJECT_NAME}
        RUNTIME DESTINATION ${INSTALL_OFFSET}
        COMPONENT ${MODULE_PROJECT_NAME})
      if (MSVC)
        # Place the modules in the script subdirectory
        set_target_properties(${MODULE_PROJECT_NAME} PROPERTIES
          RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin/Debug/scripts
          RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/Release/scripts
          RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/bin/RelWithDebInfo/scripts
          RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/bin/MinSizeRel/scripts)
      endif()
    endif()
  else()
    message(FATAL_ERROR "Unknown value \"${${MODULE_VARIABLE}}\" for module (${MODULE})!")
  endif()
endforeach()

# Add the dynamic module projects to the worldserver as dependency
set(WORLDSERVER_DYNAMIC_MODULE_PROJECTS_DEPENDENCIES ${DYNAMIC_MODULE_PROJECTS} PARENT_SCOPE)

ConfigureModuleLoader("static" MODULE_PRIVATE_SCRIPTLOADER OFF ${STATIC_MODULES})

add_library(modules STATIC
  ModulesScriptLoader.h
  ${MODULE_PRIVATE_SCRIPTLOADER}
  ${PRIVATE_SOURCES})

target_link_libraries(modules
  PRIVATE
    trinity-core-interface
  PUBLIC
    game-interface)

target_include_directories(modules
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PUBLIC_INCLUDES})

set_target_properties(modules
  PROPERTIES
    FOLDER
      "modules")

# Allows a module to include its own cmake file that is run inline with the config.
foreach(MODULE ${MODULE_LIST})
  include("${CMAKE_CURRENT_SOURCE_DIR}/${MODULE}/${MODULE}.cmake" OPTIONAL)
endforeach()

# Generate precompiled header
if (USE_SCRIPTPCH)
  list(APPEND ALL_MODULE_PROJECTS modules ${DYNAMIC_MODULE_PROJECTS})
  add_cxx_pch("${ALL_MODULE_PROJECTS}" ${PRIVATE_PCH_HEADER})
endif()

# Remove all shared libraries in the install directory which are
# contained in the static library already.
if (DISABLED_MODULE_PROJECTS)
  install(CODE "
    foreach(MODULE_TO_UNINSTALL ${DISABLED_MODULE_PROJECTS})
      if (EXISTS \"\${MODULE_TO_UNINSTALL}\")
        message(STATUS \"Uninstalling: \${MODULE_TO_UNINSTALL}\")
        file(REMOVE \"\${MODULE_TO_UNINSTALL}\")
      endif()
    endforeach()
  ")
endif()

# Distribute every enabled module's config files
message(STATUS "* Module config list:
  |")

foreach(MODULE ${MODULE_LIST})
  ModuleNameToVariable(${MODULE} MODULE_VARIABLE)
  if (${MODULE_VARIABLE} STREQUAL "disabled")
    continue()
  endif()

  GetPathToModuleConfig(${MODULE} MODULE_CONFIG_PATH)
  if (NOT IS_DIRECTORY ${MODULE_CONFIG_PATH})
    continue()
  endif()

  file(GLOB MODULE_CONFIG_FILES ${MODULE_CONFIG_PATH}/*.conf.dist)
  if (MODULE_CONFIG_FILES)
    message(STATUS "  +- ${MODULE}")
    foreach(MODULE_CONFIG_FILE ${MODULE_CONFIG_FILES})
      CopyModuleConfig("${MODULE_CONFIG_FILE}")
      get_filename_component(MODULE_CONFIG_NAME ${MODULE_CONFIG_FILE} NAME)
      message(STATUS "  |  * ${MODULE_CONFIG_NAME}")
    endforeach()
  endif()
endforeach()

message("")
