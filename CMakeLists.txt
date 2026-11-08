cmake_minimum_required(VERSION 3.28)
project(lob LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# -----------------------------------------------------------------------------
# measure/ -- the harness.  Deliberately the first thing built: the methodology
# has to be settled before there is a latency number anyone is attached to.
# -----------------------------------------------------------------------------
add_library(lob_measure
  src/measure/stats.cpp
  src/measure/environment.cpp
  src/measure/clocks.cpp
)
target_include_directories(lob_measure PUBLIC include)

add_executable(env_report bench/env_report.cpp)
target_link_libraries(env_report PRIVATE lob_measure)

# -----------------------------------------------------------------------------
# tests
# -----------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.2
)
FetchContent_MakeAvailable(Catch2)

enable_testing()
add_executable(unit_tests
  tests/test_stats.cpp
  tests/test_clocks.cpp
)
target_link_libraries(unit_tests PRIVATE lob_measure Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(unit_tests)
