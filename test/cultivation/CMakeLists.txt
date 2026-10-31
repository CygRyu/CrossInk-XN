# Path of Immortal Reading: host unit tests.
#
# Standalone on purpose, so test/CMakeLists.txt (upstream) stays untouched:
#   cmake -S test/cultivation -B build/cultivation-tests
#   cmake --build build/cultivation-tests -j
#   ctest --test-dir build/cultivation-tests --output-on-failure
cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(cultivation_tests CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
  endif()
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  enable_testing()
  include(GoogleTest)
endif()

set(CULTIVATION_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src/activities/cultivation)

set(CULTIVATION_CORE_SOURCES
  ${CULTIVATION_SRC}/QiNumber.cpp
  ${CULTIVATION_SRC}/GameState.cpp
  ${CULTIVATION_SRC}/StatsBridge.cpp
  ${CULTIVATION_SRC}/Simulation.cpp
  ${CULTIVATION_SRC}/GrammarPack.cpp
  ${CULTIVATION_SRC}/Grammar.cpp
  ${CULTIVATION_SRC}/GameValues.cpp
  ${CULTIVATION_SRC}/PackCheck.cpp
  ${CULTIVATION_SRC}/BuiltinPack.cpp
  ${CULTIVATION_SRC}/LifeLog.cpp
  ${CULTIVATION_SRC}/Breakthrough.cpp
  ${CULTIVATION_SRC}/Scrolls.cpp
  ${CULTIVATION_SRC}/ScrollNames.cpp
  ${CULTIVATION_SRC}/Trials.cpp
  ${CULTIVATION_SRC}/Inventory.cpp
  ${CULTIVATION_SRC}/Alchemy.cpp
  ${CULTIVATION_SRC}/Exploration.cpp
  ${CULTIVATION_SRC}/ExploreText.cpp
)

add_executable(CultivationCoreTest
  QiNumberTest.cpp
  GameStateTest.cpp
  StatsBridgeTest.cpp
  SimulationTest.cpp
  GrammarTest.cpp
  PackStoreTest.cpp
  LifeLogTest.cpp
  BreakthroughTest.cpp
  ScrollsTest.cpp
  TrialsTest.cpp
  ExplorationTest.cpp
  AlchemyTest.cpp
  ${CULTIVATION_CORE_SOURCES}
  ${CULTIVATION_SRC}/PackStore.cpp
  ${CULTIVATION_SRC}/Chronicle.cpp
  ${CULTIVATION_SRC}/ScrollStore.cpp
)

target_include_directories(CultivationCoreTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CULTIVATION_SRC}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/Memory
)

target_compile_definitions(CultivationCoreTest PRIVATE CROSSINK_CULTIVATION=1)
target_compile_options(CultivationCoreTest PRIVATE -Wall -Wextra -pedantic -Werror)
target_link_libraries(CultivationCoreTest PRIVATE GTest::gtest_main)

gtest_discover_tests(CultivationCoreTest)

# Desktop pack checker: the device's own parser, checker and engine.
#   build/cultivation-tests/cultivation_pack_check my-pack.txt
#   build/cultivation-tests/cultivation_pack_check --expand rumor --count 20
add_executable(cultivation_pack_check PackCheckTool.cpp ${CULTIVATION_CORE_SOURCES})
target_include_directories(cultivation_pack_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${CULTIVATION_SRC})
target_compile_definitions(cultivation_pack_check PRIVATE CROSSINK_CULTIVATION=1)
target_compile_options(cultivation_pack_check PRIVATE -Wall -Wextra -pedantic -Werror)
add_test(NAME BuiltinPackChecksClean COMMAND cultivation_pack_check --expand rumor --count 3)
