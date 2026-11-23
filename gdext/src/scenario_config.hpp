#pragma once

#include "stocksim/simulation.hpp"

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstddef>
#include <cstdint>

namespace stocksim::gdext {

/// Value of the required top-level "format" key in scenario JSON.
inline constexpr const char* kScenarioFormat = "stocksim-scenario";
/// Scenario format version understood by this build (docs/scenario-format.md).
inline constexpr std::int64_t kScenarioFormatVersion = 1;

/// Event buffer (ring) capacity bounds; the default holds a few seconds of busy output.
inline constexpr std::int64_t kMinEventBufferCapacity = 64;
inline constexpr std::int64_t kMaxEventBufferCapacity = std::int64_t{1} << 22;
inline constexpr std::int64_t kDefaultEventBufferCapacity = std::int64_t{1} << 16;

/// Everything a scenario file configures for the Simulation node.
struct ScenarioSettings {
    godot::String name = "Default";
    SimulationConfig simulation{};
    std::size_t event_buffer_capacity = static_cast<std::size_t>(kDefaultEventBufferCapacity);
};

/// Translates a parsed scenario (JSON object as a Dictionary) into settings. Strict: unknown
/// keys, wrong types, non-integral numbers and out-of-range values are errors, so typos in
/// scenario files are caught instead of silently ignored. On failure returns an error code,
/// sets `message`, and leaves `out` unspecified.
[[nodiscard]] godot::Error parse_scenario(const godot::Dictionary& root, ScenarioSettings& out,
                                          godot::String& message);

} // namespace stocksim::gdext
