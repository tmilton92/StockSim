cmake_minimum_required(VERSION 3.25)

project(
    stocksim
    VERSION 0.1.0
    DESCRIPTION "Educational market-manipulation simulator (simulation core + Godot GDExtension)"
    LANGUAGES CXX
)

option(STOCKSIM_BUILD_TESTS "Build the core unit tests (Catch2)" ON)
option(STOCKSIM_BUILD_GDEXT "Build the Godot GDExtension (requires third_party/godot-cpp)" ON)
option(STOCKSIM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C++20 everywhere, including third-party code built in-tree (Catch2 must be compiled with the
# same standard as the tests so its string_view/span support is enabled). godot-cpp sets its
# own CXX_STANDARD on its target.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Project-wide compiler warnings, applied to our own targets only (never to third-party code).
add_library(stocksim_warnings INTERFACE)
add_library(stocksim::warnings ALIAS stocksim_warnings)
if(MSVC)
    target_compile_options(stocksim_warnings INTERFACE /W4 /permissive- /utf-8)
    if(STOCKSIM_WARNINGS_AS_ERRORS)
        target_compile_options(stocksim_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(stocksim_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(STOCKSIM_WARNINGS_AS_ERRORS)
        target_compile_options(stocksim_warnings INTERFACE -Werror)
    endif()
endif()

add_subdirectory(core)

if(STOCKSIM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(third_party/Catch2 EXCLUDE_FROM_ALL SYSTEM)
    add_subdirectory(core/tests)
endif()

if(STOCKSIM_BUILD_GDEXT)
    add_subdirectory(gdext)
endif()
