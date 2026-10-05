// shot — a screenshot utility for Hyprland.
//
// Capture modes (screen, window, section) with save and clipboard
// options, plus an optional graphical capture bar. See usage_text() in
// args.cpp for the command-line interface.
#include <chrono>
#include <csignal>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "args.hpp"
#include "capture.hpp"
#include "gui.hpp"
#include "hyprland.hpp"
#include "notify.hpp"
#include "process.hpp"
#include "version.hpp"

using namespace shot;

namespace {

constexpr int EXIT_FAILURE_GENERAL    = 1;
constexpr int EXIT_USAGE              = 2;
constexpr int EXIT_MISSING_DEPENDENCY = 3;
constexpr int EXIT_CAPTURE_FAILED     = 4;

int fail(const std::string& program, const std::string& message, int code) {
    std::cerr << program << ": error: " << message << "\n";
    return code;
}

// Commands that must exist before this run can succeed.
std::vector<std::string> required_commands(const Args& args) {
    std::vector<std::string> commands{"grim"};

    if (args.mode == Mode::Window)
        commands.emplace_back("hyprctl");
    if (args.mode == Mode::Window || args.mode == Mode::Section)
        commands.emplace_back("slurp");
    if (args.copy || args.copy_only)
        commands.emplace_back("wl-copy");

    return commands;
}

// Resolve the region for the modes that need one.
std::optional<CapturePlan> make_plan(const Args& args, std::string& error) {
    CapturePlan plan;
    plan.monitor = args.monitor;

    switch (args.mode) {
        case Mode::Screen:
            return plan;
        case Mode::Window: {
            const auto region = select_window();
            if (!region) {
                error = "no window selected (cancelled, or no window on this workspace)";
                return std::nullopt;
            }
            plan.region = *region;
            return plan;
        }
        case Mode::Section: {
            const auto region = select_region();
            if (!region) {
                error = "selection cancelled";
                return std::nullopt;
            }
            plan.region = *region;
            return plan;
        }
    }
    // Every mode is handled above; this keeps -Wreturn-type quiet without
    // pretending a fourth mode exists.
    return std::nullopt;
}

int run_capture(const Args& args, const std::string& program) {
    for (const auto& command : required_commands(args)) {
        if (!command_exists(command)) {
            return fail(program,
                        "'" + command + "' not found — install it (e.g. sudo pacman -S " +
                            command + ")",
                        EXIT_MISSING_DEPENDENCY);
        }
    }

    if (args.delay > 0.0)
        std::this_thread::sleep_for(std::chrono::duration<double>(args.delay));

    std::string error;
    const auto  plan = make_plan(args, error);
    if (!plan)
        return fail(program, error, EXIT_FAILURE_GENERAL);

    // Copy-only streams the capture straight into the clipboard, so nothing is
    // ever written to disk.
    if (args.copy_only) {
        if (const auto failure = capture_to_clipboard(*plan))
            return fail(program, *failure, EXIT_CAPTURE_FAILED);

        std::cerr << "copied to clipboard\n";
        return 0;
    }

    const auto destination = resolve_destination(args.dir, args.file, error);
    if (!destination)
        return fail(program, error, EXIT_FAILURE_GENERAL);

    if (const auto failure = capture_to_file(*plan, *destination))
        return fail(program, *failure, EXIT_CAPTURE_FAILED);

    if (args.copy) {
        if (const auto failure = copy_file_to_clipboard(*destination))
            return fail(program, *failure, EXIT_CAPTURE_FAILED);
    }

    std::cout << destination->string() << std::endl;
    if (args.copy)
        std::cerr << "copied to clipboard\n";

    notify_saved_screenshot(*destination);

    if (args.open_file) {
        if (command_exists("xdg-open"))
            spawn_detached({"xdg-open", destination->string()});
        else
            std::cerr << program << ": warning: xdg-open not found; not opening the screenshot\n";
    }

    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    // Feeding a child that has already exited must not kill this process.
    std::signal(SIGPIPE, SIG_IGN);

    const std::string program = program_name(argc > 0 ? argv[0] : nullptr);

    // Hidden helper used by the parent process of a saved-screenshot
    // notification, re-entered through this same binary.
    if (argc == 3 && std::string(argv[1]) == "--notify-saved") {
        handle_notification_action(argv[2]);
        return 0;
    }

    bool gui_requested = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--gui") {
            gui_requested = true;
            break;
        }
    }

    if (gui_requested && argc > 2) {
        std::cerr << program << ": error: --gui cannot be combined with other arguments\n";
        std::cerr << "Try '" << program << " --help' for more information.\n";
        return EXIT_USAGE;
    }

    const bool use_gui = argc == 1 || gui_requested;

    Args args;
    if (use_gui) {
        const gui::Result chosen = gui::run();
        switch (chosen.outcome) {
            case gui::Outcome::Captured:
                args = chosen.args;
                // The bar's layer surface is destroyed asynchronously, so wait
                // for the compositor to actually drop it. Without this the bar
                // is still on screen when grim runs, and appears in its own
                // screenshot.
                wait_for_layer_surface_gone(gui::kNamespace, std::chrono::milliseconds{750});
                break;
            case gui::Outcome::Cancelled:
                return 0;  // the user dismissed the bar
            case gui::Outcome::Unavailable:
                return fail(program,
                            "the graphical capture bar is unavailable; use 'shot "
                            "screen|window|section' instead",
                            EXIT_FAILURE_GENERAL);
        }
    } else {
        const ParseResult parsed = parse_args(argc, argv);
        switch (parsed.status) {
            case ParseStatus::ShowHelp:
                std::cout << usage_text(program);
                return 0;
            case ParseStatus::ShowVersion:
                std::cout << program << " " << kVersion << "\n";
                return 0;
            case ParseStatus::Error:
                std::cerr << program << ": error: " << parsed.message << "\n";
                std::cerr << "Try '" << program << " --help' for more information.\n";
                return EXIT_USAGE;
            case ParseStatus::Ok:
                args = parsed.args;
                break;
        }
    }

    return run_capture(args, program);
}
