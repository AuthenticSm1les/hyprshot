// Desktop notifications for saved screenshots.
#pragma once

#include <filesystem>

namespace shot {

// Show "Screenshot saved" with an "Open screenshot" action. The work happens
// in a detached child so the caller is never delayed by the notification
// daemon. Silently does nothing when notify-send or xdg-open is unavailable.
void notify_saved_screenshot(const std::filesystem::path& path);

// Entry point for that detached child: show the notification, and open the
// file when the action is activated.
void handle_notification_action(const std::filesystem::path& path);

}  // namespace shot
