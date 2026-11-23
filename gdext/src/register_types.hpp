#pragma once

#include <godot_cpp/core/class_db.hpp>

namespace stocksim::gdext {

/// Registers all stocksim classes with Godot's ClassDB at the given initialization level.
void initialize_module(godot::ModuleInitializationLevel level);

/// Unregisters module state at the given initialization level.
void uninitialize_module(godot::ModuleInitializationLevel level);

} // namespace stocksim::gdext
