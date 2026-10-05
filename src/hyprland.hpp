// Queries against the running Hyprland compositor, plus interactive region
// selection through slurp.
#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace shot {

// Ask slurp for a region. nullopt when the selection was cancelled or slurp
// failed.
std::optional<std::string> select_region();

// Offer the mapped windows of the active workspace to slurp and return the one
// the user clicked. nullopt when cancelled or when no window is selectable.
std::optional<std::string> select_window();

// Whether a layer surface with this namespace is currently mapped. nullopt when
// the compositor could not be queried, so that "absent" and "unknown" differ.
std::optional<bool> layer_surface_present(const std::string& namespace_name);

// Block until that layer surface is gone, or `timeout` elapses. Destroying a
// layer surface is asynchronous, so a capture started immediately afterwards
// can still contain it.
void wait_for_layer_surface_gone(const std::string&       namespace_name,
                                 std::chrono::milliseconds timeout);

}  // namespace shot
