#include "notify.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>

#include "process.hpp"

namespace shot {
namespace {

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// Best-effort path of this executable, so the notification can re-enter it.
std::string self_exe() {
    std::array<char, 4096> buffer {};
    const ssize_t          n = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (n <= 0)
        return {};
    return std::string(buffer.data(), static_cast<std::size_t>(n));
}

// Notification bodies are rendered as a markup subset by common daemons
// (dunst, mako), so a path containing '&' or '<' must be escaped to display
// correctly.
std::string escape_markup(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            case '\'': escaped += "&#39;"; break;
            default: escaped.push_back(c); break;
        }
    }
    return escaped;
}

}  // namespace

void handle_notification_action(const std::filesystem::path& path) {
    // Offer the "Open" action only when it can actually do something;
    // otherwise still announce the saved file.
    const bool can_open = command_exists("xdg-open");

    std::vector<std::string> command{"notify-send", "--app-name=shot", "--urgency=low"};
    if (can_open)
        command.emplace_back("--action=open=Open screenshot");
    command.emplace_back("Screenshot saved");
    command.push_back(escape_markup(path.string()));

    const CommandResult result = run(command);
    if (!result.ok() || !can_open)
        return;

    if (trim(result.out) != "open")
        return;

    spawn_detached({"xdg-open", path.string()});
}

void notify_saved_screenshot(const std::filesystem::path& path) {
    // Scripts (and the test suite) can opt out of desktop notifications.
    if (const char* opt_out = std::getenv("SHOT_NO_NOTIFY"); opt_out != nullptr && *opt_out != '\0')
        return;

    if (!command_exists("notify-send"))
        return;

    const std::string executable = self_exe();
    if (executable.empty())
        return;

    spawn_detached({executable, "--notify-saved", path.string()});
}

}  // namespace shot
