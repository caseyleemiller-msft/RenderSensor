# This file copied from:
# https://google.github.io/googletest/quickstart-cmake.html
#
# To build and run:
# 1. setup source and build dirs: cmake -S . -B build
# 2. build                      : cmake --build build --config Release (or Debug)
# 3. to run unit tests          : ctest -V --test-dir build
# 4. to run                     : .\build\Release\CubeTest.exe

cmake_minimum_required(VERSION 3.14)
project(RenderSensor)

# GoogleTest requires at least C++14
set(CMAKE_CXX_STANDARD 14)

# Note: keep the following URL updated with latest release
# This is latest commit on 11/11/2022
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/912db742531bf82efb01194bc08140416e3b3467.zip
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_compile_definitions(
    WIN32_LEAN_AND_MEAN=1 # Exclude rarely used APIs from windows.h
    NOMINMAX=1            # Exclude min and max macros from windows.h
)

include_directories(
  ./inc
)

#------------------------------------------------------------------------------
# Unit tests
add_executable(
  RenderFXPTests
  test/RenderFXPTests.cpp
  src/RenderFXP.cpp
  src/random.cpp
)

target_link_libraries(
  RenderFXPTests
  GTest::gtest_main # google test library
)

include(GoogleTest)
gtest_discover_tests(RenderFXPTests)

#------------------------------------------------------------------------------
# Rotating Cubes example
add_executable(
    CubeTest WIN32
    src/CubeTest.cpp
    src/RenderFXP.cpp
    src/random.cpp
)
