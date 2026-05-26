# Copyright (C) DestinyCore <https://www.destinycore.org/>
#
# fkYAML is a header-only YAML 1.2 parser.
# Upstream: https://github.com/fktn-k/fkYAML

add_library(fkYAML INTERFACE)

target_include_directories(fkYAML INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(fkYAML PROPERTIES FOLDER "dep")
