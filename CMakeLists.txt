cmake_minimum_required(VERSION 3.25)
project(ShadowWormFarm VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Deterministic floating point: no fast-math, no FMA contraction differences between call sites.
set(WORMFARM_FP_FLAGS -fno-fast-math -ffp-contract=off)

option(WORMFARM_BUILD_EXTENSION "Build the Godot GDExtension (needs third_party/godot-cpp)" ON)

add_library(wormfarm_core STATIC
  core/src/config.cpp
  core/src/sim.cpp)
target_include_directories(wormfarm_core PUBLIC core/include)
target_compile_options(wormfarm_core PRIVATE -Wall -Wextra ${WORMFARM_FP_FLAGS})

add_executable(wormfarm_tests tests/core_tests.cpp)
target_link_libraries(wormfarm_tests PRIVATE wormfarm_core)
target_compile_options(wormfarm_tests PRIVATE ${WORMFARM_FP_FLAGS})

add_executable(wormfarm_sim tools/wormfarm_sim.cpp)
target_link_libraries(wormfarm_sim PRIVATE wormfarm_core)
target_compile_options(wormfarm_sim PRIVATE ${WORMFARM_FP_FLAGS})

enable_testing()
add_test(NAME core COMMAND wormfarm_tests)

if(WORMFARM_BUILD_EXTENSION)
  set(GODOTCPP_API_VERSION "4.7" CACHE STRING "" FORCE)
  set(GODOTCPP_TARGET "template_release" CACHE STRING "" FORCE)
  add_subdirectory(third_party/godot-cpp EXCLUDE_FROM_ALL)
  add_library(wormfarm_gdext SHARED
    extension/src/register_types.cpp
    extension/src/worm_farm_sim.cpp
    extension/src/worm_audio.cpp
    extension/src/farm_live_stream.cpp)
  target_link_libraries(wormfarm_gdext PRIVATE wormfarm_core godot-cpp)
  target_compile_options(wormfarm_gdext PRIVATE ${WORMFARM_FP_FLAGS})
  set_target_properties(wormfarm_gdext PROPERTIES
    OUTPUT_NAME "wormfarm"
    PREFIX "lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/game/bin")
endif()
