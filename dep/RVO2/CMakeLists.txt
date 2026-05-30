set(RVO2_SOURCES
  src/Agent.cc
  src/KdTree.cc
  src/Line.cc
  src/Obstacle.cc
  src/RVOSimulator.cc
  src/Vector2.cc)

set(RVO2_HEADERS
  src/Agent.h
  src/Export.h
  src/KdTree.h
  src/Line.h
  src/Obstacle.h
  src/RVO.h
  src/RVOSimulator.h
  src/Vector2.h)

add_library(RVO2 STATIC ${RVO2_SOURCES} ${RVO2_HEADERS})

target_compile_definitions(RVO2
  PUBLIC
    RVO_STATIC_DEFINE)

target_include_directories(RVO2
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(RVO2
  PRIVATE
    trinity-dependency-interface)

set_target_properties(RVO2
  PROPERTIES
    FOLDER
      "dep")
