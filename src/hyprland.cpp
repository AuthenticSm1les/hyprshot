#include "hyprland.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "json.hpp"
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

// Run `hyprctl <args> -j` and parse the result.
std::optional<json::Value> hyprctl_json(const std::vector<std::string>& args) {
    std::vector<std::string> command{"hyprctl"};
    command.insert(command.end(), args.begin(), args.end());
    command.emplace_back("-j");

    const CommandResult result = run(command);
    if (!result.ok())
        return std::nullopt;

    const std::string text = trim(result.out);
    if (text.empty())
        return std::nullopt;

    return json::parse(text);
}

// Read a two-element [x, y] or [width, height] array into a pair of ints.
std::optional<std::pair<int, int>> read_pair(const json::Value& value) {
    if (!value.is_array() || value.size() < 2)
        return std::nullopt;
    if (!value[0].is_number() || !value[1].is_number())
        return std::nullopt;

    const auto round_to_int = [](double number) {
        return static_cast<int>(std::lround(number));
    };
    return std::make_pair(round_to_int(value[0].as_number()), round_to_int(value[1].as_number()));
}

// slurp splits its input on whitespace, so labels must not contain any.
std::string sanitize_label(const std::string& text) {
    std::string label;
    label.reserve(text.size());
    bool previous_was_space = false;
    for (const char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!previous_was_space)
                label.push_back('_');
            previous_was_space = true;
        } else {
            label.push_back(c);
            previous_was_space = false;
        }
    }
    return label;
}

// A window rectangle, only ever used to feed slurp.
struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    [[nodiscard]] bool valid() const { return w > 0 && h > 0; }

    // "x,y wxh", the format slurp reads and grim -g accepts.
    [[nodiscard]] std::string to_geometry() const {
        return std::to_string(x) + "," + std::to_string(y) + " " + std::to_string(w) + "x" +
               std::to_string(h);
    }
};

}  // namespace

std::optional<std::string> select_region() {
    const CommandResult result = run({"slurp"});
    if (!result.ok())
        return std::nullopt;

    const std::string region = trim(result.out);
    return region.empty() ? std::nullopt : std::optional{region};
}

std::optional<std::string> select_window() {
    const auto workspace = hyprctl_json({"activeworkspace"});
    if (!workspace || !workspace->is_object())
        return std::nullopt;

    const json::Value& workspace_id = (*workspace)["id"];
    if (!workspace_id.is_number())
        return std::nullopt;
    const auto active_id = static_cast<int>(std::lround(workspace_id.as_number()));

    const auto clients = hyprctl_json({"clients"});
    if (!clients || !clients->is_array())
        return std::nullopt;

    std::vector<std::string> boxes;
    for (std::size_t i = 0; i < clients->size(); ++i) {
        const json::Value& client = (*clients)[i];

        // Only windows actually visible on the active workspace are offered.
        const json::Value& mapped = client["mapped"];
        if (mapped.is_bool() && !mapped.as_bool())
            continue;

        const json::Value& workspace_of_client = client["workspace"];
        if (!workspace_of_client.is_object() || !workspace_of_client["id"].is_number())
            continue;
        if (static_cast<int>(std::lround(workspace_of_client["id"].as_number())) != active_id)
            continue;

        const auto at   = read_pair(client["at"]);
        const auto size = read_pair(client["size"]);
        if (!at || !size)
            continue;

        const Rect rect{at->first, at->second, size->first, size->second};
        if (!rect.valid())
            continue;

        std::string label = sanitize_label(client["class"].as_string());
        if (label.empty())
            label = "window";

        boxes.push_back(rect.to_geometry() + " " + label);
    }

    if (boxes.empty())
        return std::nullopt;

    std::string input;
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        if (i > 0)
            input.push_back('\n');
        input += boxes[i];
    }

    const CommandResult result = run({"slurp", "-r", "-f", "%x,%y %wx%h"}, input);
    if (!result.ok())
        return std::nullopt;

    const std::string region = trim(result.out);
    return region.empty() ? std::nullopt : std::optional{region};
}

std::optional<bool> layer_surface_present(const std::string& namespace_name) {
    const auto data = hyprctl_json({"layers"});
    if (!data || !data->is_object())
        return std::nullopt;

    // { "<output>": { "levels": { "0": [ { "namespace": ..., "x": ... } ] } } }
    for (const auto& monitor : data->as_object()) {
        const json::Value& levels = monitor.second["levels"];
        if (!levels.is_object())
            continue;

        for (const auto& level : levels.as_object()) {
            const json::Value& surfaces = level.second;
            if (!surfaces.is_array())
                continue;

            for (std::size_t i = 0; i < surfaces.size(); ++i) {
                if (surfaces[i]["namespace"].as_string() == namespace_name)
                    return true;
            }
        }
    }
    return false;
}

void wait_for_layer_surface_gone(const std::string& namespace_name,
                                 std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    for (;;) {
        const auto present = layer_surface_present(namespace_name);
        // Gone, or the compositor cannot be queried: either way, do not block.
        if (!present.has_value() || !*present)
            return;
        if (std::chrono::steady_clock::now() >= deadline)
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds{15});
    }
}

}  // namespace shot
