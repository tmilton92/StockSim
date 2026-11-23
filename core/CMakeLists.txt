# stocksim_core: the simulation library. Standard C++ only; no Godot dependency.

set(STOCKSIM_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(
    include/stocksim/version.hpp.in
    "${STOCKSIM_GENERATED_INCLUDE_DIR}/stocksim/version.hpp"
    @ONLY
)

add_library(stocksim_core STATIC)
add_library(stocksim::core ALIAS stocksim_core)

target_sources(
    stocksim_core
    PRIVATE
        src/matching_engine.cpp
        src/names.cpp
        src/order_book.cpp
        src/rng.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include "${STOCKSIM_GENERATED_INCLUDE_DIR}"
        FILES
            include/stocksim/event.hpp
            include/stocksim/matching_engine.hpp
            include/stocksim/order.hpp
            include/stocksim/order_book.hpp
            include/stocksim/rng.hpp
            include/stocksim/strong_type.hpp
            include/stocksim/types.hpp
            "${STOCKSIM_GENERATED_INCLUDE_DIR}/stocksim/version.hpp"
)

target_compile_features(stocksim_core PUBLIC cxx_std_20)
set_target_properties(
    stocksim_core
    PROPERTIES
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
)
target_link_libraries(stocksim_core PRIVATE stocksim::warnings)
