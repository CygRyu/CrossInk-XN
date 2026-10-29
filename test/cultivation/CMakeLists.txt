add_executable(CultivationEngineTest
  CultivationEngineTest.cpp
  ${REPO_ROOT}/lib/Cultivation/CultivationEngine.cpp
)

target_link_libraries(CultivationEngineTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CultivationEngineTest)

target_include_directories(CultivationEngineTest PRIVATE
  ${REPO_ROOT}/lib/Cultivation
)
