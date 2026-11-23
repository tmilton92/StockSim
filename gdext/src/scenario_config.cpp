#include "scenario_config.hpp"

#include "stocksim/simulation.hpp"

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>

namespace stocksim::gdext {

namespace {

using godot::Dictionary;
using godot::String;
using godot::Variant;

/// Fails if `dict` has a key not in `allowed`.
bool check_keys(const Dictionary& dict, std::initializer_list<const char*> allowed,
                const String& context, String& message) {
    const godot::Array keys = dict.keys();
    for (const Variant& key : keys) {
        bool known = false;
        if (key.get_type() == Variant::STRING) {
            const String name = key;
            for (const char* candidate : allowed) {
                known = known || name == candidate;
            }
        }
        if (!known) {
            message = context + String(": unknown key \"") + String(key) + String("\".");
            return false;
        }
    }
    return true;
}

/// Reads an optional integer key. JSON numbers arrive as floats, so integral floats are
/// accepted; fractional values, other types, and values outside [min, max] are errors.
bool read_int(const Dictionary& dict, const char* key, int64_t min, int64_t max,
              const String& context, int64_t& value, String& message) {
    if (!dict.has(key)) {
        return true;
    }
    const Variant raw = dict[key];
    std::optional<int64_t> parsed;
    if (raw.get_type() == Variant::INT) {
        parsed = static_cast<int64_t>(raw);
    } else if (raw.get_type() == Variant::FLOAT) {
        const double number = raw;
        // Range check before converting so the cast is always defined.
        const bool integral = std::isfinite(number) && std::trunc(number) == number &&
                              number >= static_cast<double>(min) &&
                              number <= static_cast<double>(max);
        if (integral) {
            parsed = static_cast<int64_t>(number);
        }
    }
    if (!parsed || *parsed < min || *parsed > max) {
        message = context + String(".") + String(key) + String(" must be an integer between ") +
                  String::num_int64(min) + String(" and ") + String::num_int64(max) + String(".");
        return false;
    }
    value = *parsed;
    return true;
}

bool read_section(const Dictionary& root, const char* key, Dictionary& section, String& message) {
    if (!root.has(key)) {
        return true;
    }
    const Variant raw = root[key];
    if (raw.get_type() != Variant::DICTIONARY) {
        message = String("scenario.") + key + " must be an object.";
        return false;
    }
    section = raw;
    return true;
}

godot::Error parse_header(const Dictionary& root, ScenarioSettings& out, String& message) {
    const Variant format = root.get("format", Variant());
    if (format.get_type() != Variant::STRING || String(format) != kScenarioFormat) {
        message = String("scenario.format must be \"") + kScenarioFormat + "\".";
        return godot::ERR_FILE_UNRECOGNIZED;
    }
    int64_t version = -1;
    if (!root.has("format_version") ||
        !read_int(root, "format_version", 0, INT32_MAX, "scenario", version, message)) {
        message = "scenario.format_version is required and must be an integer.";
        return godot::ERR_INVALID_DATA;
    }
    if (version != kScenarioFormatVersion) {
        message = String("Unsupported scenario format_version ") + String::num_int64(version) +
                  String(" (this build reads version ") +
                  String::num_int64(kScenarioFormatVersion) + String(").");
        return godot::ERR_FILE_UNRECOGNIZED;
    }
    for (const char* key : {"name", "description"}) {
        if (root.has(key) && Variant(root[key]).get_type() != Variant::STRING) {
            message = String("scenario.") + key + " must be a string.";
            return godot::ERR_INVALID_DATA;
        }
    }
    if (root.has("name")) {
        out.name = root["name"];
    }
    return godot::OK;
}

} // namespace

godot::Error parse_scenario(const Dictionary& root, ScenarioSettings& out, String& message) {
    out = ScenarioSettings{};
    if (!check_keys(root,
                    {"format", "format_version", "name", "description", "simulation", "runtime"},
                    "scenario", message)) {
        return godot::ERR_INVALID_DATA;
    }
    if (const godot::Error header = parse_header(root, out, message); header != godot::OK) {
        return header;
    }

    Dictionary simulation;
    Dictionary runtime;
    if (!read_section(root, "simulation", simulation, message) ||
        !read_section(root, "runtime", runtime, message)) {
        return godot::ERR_INVALID_DATA;
    }
    if (!check_keys(simulation, {"snapshot_interval_ticks", "expected_orders", "expected_events"},
                    "scenario.simulation", message) ||
        !check_keys(runtime, {"event_buffer_capacity"}, "scenario.runtime", message)) {
        return godot::ERR_INVALID_DATA;
    }

    constexpr auto kMaxHint = static_cast<int64_t>(kMaxCapacityHint);
    auto expected_orders = static_cast<int64_t>(out.simulation.engine.expected_orders);
    auto expected_events = static_cast<int64_t>(out.simulation.engine.expected_events);
    auto buffer_capacity = static_cast<int64_t>(out.event_buffer_capacity);
    const bool read_ok =
        read_int(simulation, "snapshot_interval_ticks", 0, kMaxSnapshotIntervalTicks,
                 "scenario.simulation", out.simulation.snapshot_interval_ticks, message) &&
        read_int(simulation, "expected_orders", 0, kMaxHint, "scenario.simulation", expected_orders,
                 message) &&
        read_int(simulation, "expected_events", 0, kMaxHint, "scenario.simulation", expected_events,
                 message) &&
        read_int(runtime, "event_buffer_capacity", kMinEventBufferCapacity, kMaxEventBufferCapacity,
                 "scenario.runtime", buffer_capacity, message);
    if (!read_ok) {
        return godot::ERR_INVALID_DATA;
    }
    out.simulation.engine.expected_orders = static_cast<std::size_t>(expected_orders);
    out.simulation.engine.expected_events = static_cast<std::size_t>(expected_events);
    out.event_buffer_capacity = static_cast<std::size_t>(buffer_capacity);

    // Core owns the authoritative limits; the per-key ranges above only give better messages.
    if (const std::optional<Error> invalid = validate(out.simulation)) {
        message =
            String::utf8(invalid->message.data(), static_cast<int64_t>(invalid->message.size()));
        return godot::ERR_INVALID_DATA;
    }
    return godot::OK;
}

} // namespace stocksim::gdext
