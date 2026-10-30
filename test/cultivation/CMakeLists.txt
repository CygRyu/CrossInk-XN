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

add_executable(CultivationCoreTest
  QiNumberTest.cpp
  GameStateTest.cpp
  StatsBridgeTest.cpp
  ${CULTIVATION_SRC}/QiNumber.cpp
  ${CULTIVATION_SRC}/GameState.cpp
  ${CULTIVATION_SRC}/StatsBridge.cpp
)

target_include_directories(CultivationCoreTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CULTIVATION_SRC}
)

target_compile_definitions(CultivationCoreTest PRIVATE CROSSINK_CULTIVATION=1)
target_compile_options(CultivationCoreTest PRIVATE -Wall -Wextra -pedantic -Werror)
target_link_libraries(CultivationCoreTest PRIVATE GTest::gtest_main)

gtest_discover_tests(CultivationCoreTest)
