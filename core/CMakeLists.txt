# stocksim_core: the simulation library. Standard C++ only; no Godot dependency.

set(STOCKSIM_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(
    include/stocksim/version.hpp.in
    "${STOCKSIM_GENERATED_INCLUDE_DIR}/stocksim/version.hpp"
    @ONLY
)

# Visible to stocksim_add_core_library() wherever it is called from.
set(STOCKSIM_CORE_GENERATED_INCLUDE_DIR
    "${STOCKSIM_GENERATED_INCLUDE_DIR}"
    CACHE INTERNAL "Generated core headers (version.hpp)"
)

# Defines a static library target containing the core. Called once here for the normal build,
# and once more by /gdext for a variant compiled with the GDExtension's exception settings.
function(stocksim_add_core_library target)
    set(STOCKSIM_CORE_DIR "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    add_library(${target} STATIC)
    target_sources(
        ${target}
        PRIVATE
            ${STOCKSIM_CORE_DIR}/src/event_hash.cpp
            ${STOCKSIM_CORE_DIR}/src/event_ring.cpp
            ${STOCKSIM_CORE_DIR}/src/matching_engine.cpp
            ${STOCKSIM_CORE_DIR}/src/matching_engine_state.cpp
            ${STOCKSIM_CORE_DIR}/src/names.cpp
            ${STOCKSIM_CORE_DIR}/src/order_book.cpp
            ${STOCKSIM_CORE_DIR}/src/rng.cpp
            ${STOCKSIM_CORE_DIR}/src/serialization.cpp
            ${STOCKSIM_CORE_DIR}/src/simulation.cpp
        PUBLIC
            FILE_SET HEADERS
            BASE_DIRS ${STOCKSIM_CORE_DIR}/include "${STOCKSIM_CORE_GENERATED_INCLUDE_DIR}"
            FILES
                ${STOCKSIM_CORE_DIR}/include/stocksim/event.hpp
                ${STOCKSIM_CORE_DIR}/include/stocksim/event_hash.hpp
                ${STOCKSIM_CORE_DIR}/include/stocksim/event_ring.hpp
                ${STOCKSIM_CORE_DIR}/include/stocksim/matching_engine.hpp
                ${STOCKSIM_CORE_DIR}/include/stocksim/order.hpp
                ${STOCKSIM_CORE_DIR}/include/stocksim/order_book.hpp
                ${STOCKSIM_CORE_DIR}/include/stocksim/result.hpp
                ${STOCKSIM_CORE_DIR}/include/stocksim/rng.hpp
                ${STOCKSIM_CORE_DIR}/include/stocksim/serialization.hpp
                ${STOCKSIM_CORE_DIR}/include/stocksim/simulation.hpp
                ${STOCKSIM_CORE_DIR}/include/stocksim/strong_type.hpp
                ${STOCKSIM_CORE_DIR}/include/stocksim/types.hpp
                "${STOCKSIM_CORE_GENERATED_INCLUDE_DIR}/stocksim/version.hpp"
    )
    target_compile_features(${target} PUBLIC cxx_std_20)
    set_target_properties(
        ${target}
        PROPERTIES
            CXX_EXTENSIONS OFF
            POSITION_INDEPENDENT_CODE ON
    )
    target_link_libraries(${target} PRIVATE stocksim::warnings)
endfunction()

stocksim_add_core_library(stocksim_core)
add_library(stocksim::core ALIAS stocksim_core)
