# stocksim GDExtension: thin translation layer between stocksim::core and Godot.
#
# godot-cpp is built against extension_api.json dumped from the pinned engine binary
# (see docs/VERSIONS.md). Regenerate it whenever the engine is upgraded:
#   godot --headless --dump-extension-api   (run inside gdext/)

set(GODOTCPP_CUSTOM_API_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/extension_api.json"
    CACHE FILEPATH "extension_api.json dumped from the pinned Godot engine"
)
set(GODOTCPP_SYSTEM_HEADERS ON CACHE BOOL "Expose godot-cpp headers as SYSTEM")
add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/godot-cpp godot-cpp SYSTEM)

# godot-cpp publicly defines _HAS_EXCEPTIONS=0 on MSVC for everything that links it. Core code
# linked into the extension must be compiled the same way, or the STL's inline functions get
# two different definitions in one binary (an ODR violation). Elsewhere godot-cpp keeps its
# -fno-exceptions private, so the normal core library is already consistent.
if(MSVC AND GODOTCPP_DISABLE_EXCEPTIONS)
    stocksim_add_core_library(stocksim_core_gdext)
    target_compile_definitions(stocksim_core_gdext PUBLIC _HAS_EXCEPTIONS=0)
    set(STOCKSIM_GDEXT_CORE stocksim_core_gdext)
else()
    set(STOCKSIM_GDEXT_CORE stocksim::core)
endif()

add_library(stocksim_gdext SHARED)
target_sources(
    stocksim_gdext
    PRIVATE
        src/register_types.cpp
        src/register_types.hpp
        src/scenario_config.cpp
        src/scenario_config.hpp
        src/simulation_node.cpp
        src/simulation_node.hpp
)
target_link_libraries(stocksim_gdext PRIVATE godot-cpp ${STOCKSIM_GDEXT_CORE} stocksim::warnings)
if(MSVC)
    # C4714 ("__forceinline function not inlined") is reported for godot-cpp's own force-inline
    # templates (memnew/_post_initialize) when instantiated in optimized builds. godot-cpp
    # disables it for its own sources (/wd4714) but privately; this mirrors that, for this
    # target only. It is an optimizer note, not a defect in our code.
    target_compile_options(stocksim_gdext PRIVATE /wd4714)
endif()

if(GODOTCPP_TARGET MATCHES "editor|template_debug")
    # target_doc_sources() runs Python, but godot-cpp only finds it in its own directory scope;
    # without this the generated command silently omits the interpreter.
    find_package(Python3 3.4 REQUIRED COMPONENTS Interpreter)
    file(GLOB_RECURSE STOCKSIM_DOC_XML LIST_DIRECTORIES NO CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/doc_classes/*.xml")
    if(STOCKSIM_DOC_XML)
        target_doc_sources(stocksim_gdext "${STOCKSIM_DOC_XML}")
    endif()
endif()

get_target_property(STOCKSIM_GODOTCPP_SUFFIX godot-cpp GODOTCPP_SUFFIX)
set(STOCKSIM_GDEXT_OUTPUT_DIR "${PROJECT_SOURCE_DIR}/game/bin")

# Output wrapped in $<1:...> so multi-config generators don't append a per-config directory.
set_target_properties(
    stocksim_gdext
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        CXX_VISIBILITY_PRESET ${GODOTCPP_SYMBOL_VISIBILITY}
        POSITION_INDEPENDENT_CODE ON
        BUILD_RPATH_USE_ORIGIN ON
        LINK_SEARCH_START_STATIC ON
        LINK_SEARCH_END_STATIC ON
        LIBRARY_OUTPUT_DIRECTORY "$<1:${STOCKSIM_GDEXT_OUTPUT_DIR}>"
        RUNTIME_OUTPUT_DIRECTORY "$<1:${STOCKSIM_GDEXT_OUTPUT_DIR}>"
        PDB_OUTPUT_DIRECTORY "$<1:${STOCKSIM_GDEXT_OUTPUT_DIR}>"
        PREFIX "lib"
        OUTPUT_NAME "stocksim${STOCKSIM_GODOTCPP_SUFFIX}"
)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set_target_properties(
        stocksim_gdext
        PROPERTIES
            SUFFIX ""
            LIBRARY_OUTPUT_DIRECTORY
                "$<1:${STOCKSIM_GDEXT_OUTPUT_DIR}/libstocksim.macos.${GODOTCPP_TARGET}.framework>"
    )
endif()
