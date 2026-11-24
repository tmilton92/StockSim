add_executable(stocksim_core_tests)
target_sources(
    stocksim_core_tests
    PRIVATE
        account_engine_test.cpp
        agents_test.cpp
        account_ledger_test.cpp
        crt_report_mode.cpp
        determinism_test.cpp
        engine_state_test.cpp
        event_ring_test.cpp
        matching_engine_test.cpp
        matching_property_test.cpp
        order_book_test.cpp
        random_flow.hpp
        rng_test.cpp
        serialization_test.cpp
        simulation_test.cpp
        strong_type_test.cpp
        test_support.hpp
        version_test.cpp
)
target_compile_features(stocksim_core_tests PRIVATE cxx_std_20)
set_target_properties(stocksim_core_tests PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
    stocksim_core_tests
    PRIVATE stocksim::core stocksim::warnings Catch2::Catch2WithMain
)

include(${PROJECT_SOURCE_DIR}/third_party/Catch2/extras/Catch.cmake)
catch_discover_tests(stocksim_core_tests)
