# stocksim_core: the simulation library. Standard C++ only; no Godot dependency.
#
# The core is header-only until the first compiled sources land (TODO.md section 1); at that
# point this becomes a STATIC library linking stocksim::warnings privately.

set(STOCKSIM_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(
    include/stocksim/version.hpp.in
    "${STOCKSIM_GENERATED_INCLUDE_DIR}/stocksim/version.hpp"
    @ONLY
)

add_library(stocksim_core INTERFACE)
add_library(stocksim::core ALIAS stocksim_core)

target_sources(
    stocksim_core
    INTERFACE
        FILE_SET HEADERS
        BASE_DIRS "${STOCKSIM_GENERATED_INCLUDE_DIR}"
        FILES "${STOCKSIM_GENERATED_INCLUDE_DIR}/stocksim/version.hpp"
)

target_compile_features(stocksim_core INTERFACE cxx_std_20)
