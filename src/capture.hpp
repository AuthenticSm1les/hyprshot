// Turning a resolved capture plan into an image: on disk, or straight into the
// clipboard.
#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace shot {

struct CapturePlan {
    std::string                monitor;  // grim -o; empty means every output
    std::optional<std::string> region;   // grim -g; unset means the whole screen
};

// ~/Pictures/Screenshots, or a relative fallback when HOME is unset.
std::filesystem::path default_screenshot_dir();

// Work out where a capture should be written, creating directories as needed.
// An explicit `file` is used as given (overwriting any existing file); a
// timestamped name is made unique instead. Returns nullopt and fills `error`
// on failure.
std::optional<std::filesystem::path> resolve_destination(const std::string& dir,
                                                         const std::string& file,
                                                         std::string&       error);

// Each returns nullopt on success, or a message describing the failure.
std::optional<std::string> capture_to_file(const CapturePlan&           plan,
                                           const std::filesystem::path& destination);
std::optional<std::string> capture_to_clipboard(const CapturePlan& plan);
std::optional<std::string> copy_file_to_clipboard(const std::filesystem::path& source);

}  // namespace shot
