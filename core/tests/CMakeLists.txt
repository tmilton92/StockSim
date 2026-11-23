add_executable(stocksim_core_tests)
target_sources(stocksim_core_tests PRIVATE version_test.cpp)
target_compile_features(stocksim_core_tests PRIVATE cxx_std_20)
set_target_properties(stocksim_core_tests PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    stocksim_core_tests
    PRIVATE stocksim::core stocksim::warnings Catch2::Catch2WithMain
)

include(${PROJECT_SOURCE_DIR}/third_party/Catch2/extras/Catch.cmake)
catch_discover_tests(stocksim_core_tests)
