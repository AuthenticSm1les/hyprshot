#include "capture.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include "process.hpp"

namespace fs = std::filesystem;

namespace shot {
namespace {

// The grim invocation, without the output operand.
std::vector<std::string> grim_command(const CapturePlan& plan) {
    std::vector<std::string> command{"grim"};

    if (!plan.monitor.empty()) {
        command.emplace_back("-o");
        command.push_back(plan.monitor);
    }
    if (plan.region) {
        command.emplace_back("-g");
        command.push_back(*plan.region);
    }
    return command;
}

fs::path expand_home(const std::string& path) {
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        if (path == "~")
            return fs::path(home);
        if (path.rfind("~/", 0) == 0)
            return fs::path(home) / path.substr(2);
    }
    return fs::path(path);
}

std::string timestamp_name() {
    const auto        now     = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);

    std::tm local {};
    localtime_r(&seconds, &local);

    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H%M%S", &local);
    return buffer;
}

// Timestamps have one-second resolution, so back-to-back captures would
// otherwise overwrite each other.
fs::path make_unique(fs::path desired) {
    std::error_code ec;
    if (!fs::exists(desired, ec))
        return desired;

    const fs::path    parent = desired.parent_path();
    const std::string stem   = desired.stem().string();
    const std::string ext    = desired.extension().string();

    for (int suffix = 1; suffix < 1000; ++suffix) {
        fs::path candidate = parent / (stem + "_" + std::to_string(suffix) + ext);
        if (!fs::exists(candidate, ec))
            return candidate;
    }
    return desired;
}

}  // namespace

fs::path default_screenshot_dir() {
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0')
        return fs::path("Screenshots");
    return fs::path(home) / "Pictures" / "Screenshots";
}

std::optional<fs::path> resolve_destination(const std::string& dir, const std::string& file,
                                            std::string& error) {
    const fs::path save_dir = dir.empty() ? default_screenshot_dir() : expand_home(dir);

    std::error_code ec;
    fs::create_directories(save_dir, ec);
    if (ec) {
        error = "cannot create save directory " + save_dir.string() + ": " + ec.message();
        return std::nullopt;
    }

    if (file.empty())
        return make_unique(save_dir / (timestamp_name() + ".png"));

    fs::path destination = expand_home(file);
    if (destination.is_relative())
        destination = save_dir / destination;
    if (destination.extension().empty())
        destination += ".png";

    // An explicit filename may name directories that do not exist yet.
    const fs::path parent = destination.parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent, ec);
        if (ec) {
            error = "cannot create " + parent.string() + ": " + ec.message();
            return std::nullopt;
        }
    }
    return destination;
}

std::optional<std::string> capture_to_file(const CapturePlan& plan, const fs::path& destination) {
    std::vector<std::string> command = grim_command(plan);
    command.push_back(destination.string());

    const CommandResult result = run(command);
    if (result.ok())
        return std::nullopt;

    // Never leave a half-written image behind.
    std::error_code ec;
    fs::remove(destination, ec);

    const std::string detail = result.error_text();
    return detail.empty() ? "capture failed" : detail;
}

std::optional<std::string> capture_to_clipboard(const CapturePlan& plan) {
    std::vector<std::string> producer = grim_command(plan);
    producer.emplace_back("-");  // grim writes the PNG to stdout

    const PipelineResult result = run_pipeline(producer, {"wl-copy", "--type", "image/png"});
    if (result.ok())
        return std::nullopt;

    const std::string detail = result.error_text();
    return detail.empty() ? "clipboard copy failed" : detail;
}

std::optional<std::string> copy_file_to_clipboard(const fs::path& source) {
    std::ifstream file(source, std::ios::binary);
    if (!file)
        return "cannot read " + source.string();

    const std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    const CommandResult result = run({"wl-copy", "--type", "image/png"}, data);
    if (result.ok())
        return std::nullopt;

    const std::string detail = result.error_text();
    return detail.empty() ? "clipboard copy failed" : detail;
}

}  // namespace shot
