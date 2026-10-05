#include "args.hpp"

#include <charconv>
#include <cmath>
#include <string_view>
#include <vector>

namespace shot {
namespace {

// Parse a seconds value. Rejects junk instead of throwing.
std::optional<double> parse_delay(std::string_view text) {
    if (text.empty())
        return std::nullopt;

    double     value = 0.0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc {} || result.ptr != text.data() + text.size())
        return std::nullopt;
    if (!std::isfinite(value) || value < 0.0)
        return std::nullopt;
    return value;
}

}  // namespace

std::string program_name(const char* argv0) {
    const std::string path  = argv0 != nullptr ? argv0 : "shot";
    const auto        slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

const char* mode_name(Mode mode) {
    switch (mode) {
        case Mode::Screen: return "screen";
        case Mode::Window: return "window";
        case Mode::Section: return "section";
    }
    return "screen";
}

std::optional<Mode> mode_from_string(const std::string& text) {
    if (text == "screen")
        return Mode::Screen;
    if (text == "window")
        return Mode::Window;
    if (text == "section")
        return Mode::Section;
    return std::nullopt;
}

std::string usage_text(const std::string& program) {
    return "Usage: " + program + " <screen|window|section> [options]\n"
           "\n"
           "Capture a screenshot on Hyprland. With no arguments, opens a small\n"
           "graphical capture bar instead.\n"
           "\n"
           "Modes:\n"
           "  screen    capture the whole screen\n"
           "  window    click a window to capture it\n"
           "  section   drag a region to capture\n"
           "\n"
           "Options:\n"
           "  -c, --copy          save to disk AND copy to the clipboard\n"
           "      --copy-only     copy to the clipboard without saving a file\n"
           "  -d, --dir PATH      save directory (default: ~/Pictures/Screenshots)\n"
           "  -f, --file NAME     output filename (default: timestamp)\n"
           "      --delay N       wait N seconds before capturing\n"
           "  -o, --open          open the saved file with xdg-open\n"
           "  -m, --monitor OUT   capture one output, e.g. eDP-1 (screen mode only;\n"
           "                      list outputs with 'hyprctl monitors')\n"
           "      --gui           open the graphical capture bar (same as no arguments;\n"
           "                      takes no other arguments)\n"
           "  -h, --help          show this help and exit\n"
           "      --version       show the version and exit\n"
           "\n"
           "Examples:\n"
           "  " + program + " screen\n"
           "  " + program + " window --copy\n"
           "  " + program + " section --copy-only\n"
           "  " + program + " screen --delay 3 --open\n"
           "  " + program + " screen --monitor DP-1 --dir ~/Pictures\n";
}

namespace {

// Options that take a value, tracked as an enum so the short and long
// spellings cannot drift apart.
enum class ValueOption { None, Dir, File, Monitor, Delay };

std::optional<ValueOption> value_option_for(const std::string& name) {
    if (name == "-d" || name == "--dir")
        return ValueOption::Dir;
    if (name == "-f" || name == "--file")
        return ValueOption::File;
    if (name == "-m" || name == "--monitor")
        return ValueOption::Monitor;
    if (name == "--delay")
        return ValueOption::Delay;
    return std::nullopt;
}

const char* option_spelling(ValueOption option) {
    switch (option) {
        case ValueOption::Dir: return "--dir";
        case ValueOption::File: return "--file";
        case ValueOption::Monitor: return "--monitor";
        case ValueOption::Delay: return "--delay";
        case ValueOption::None: return "";
    }
    return "";
}

// Apply a value to the option it belongs to. Returns false with `error` set
// when the value is not acceptable.
bool assign_value(ValueOption option, const std::string& value, Args& args, std::string& error) {
    switch (option) {
        case ValueOption::Dir: args.dir = value; return true;
        case ValueOption::File: args.file = value; return true;
        case ValueOption::Monitor: args.monitor = value; return true;
        case ValueOption::Delay: {
            const auto seconds = parse_delay(value);
            if (!seconds) {
                error = "invalid value for --delay: '" + value + "'";
                return false;
            }
            args.delay = *seconds;
            return true;
        }
        case ValueOption::None: return true;
    }
    return true;
}

}  // namespace

ParseResult parse_args(int argc, char** argv) {
    ParseResult result;

    std::vector<std::string> tokens;
    for (int i = 1; i < argc; ++i)
        tokens.emplace_back(argv[i]);

    bool        mode_set = false;
    ValueOption pending  = ValueOption::None;

    const auto fail = [&](std::string message) {
        result.status  = ParseStatus::Error;
        result.message = std::move(message);
        return result;
    };

    for (const std::string& token : tokens) {
        // A value promised by the previous option.
        if (pending != ValueOption::None) {
            const ValueOption option = pending;
            pending                  = ValueOption::None;

            std::string error;
            if (!assign_value(option, token, result.args, error))
                return fail(std::move(error));
            continue;
        }

        // Split "--option=value".
        std::string name  = token;
        std::string value;
        bool        has_value = false;
        if (token.rfind("--", 0) == 0) {
            const auto equals = token.find('=');
            if (equals != std::string::npos) {
                name      = token.substr(0, equals);
                value     = token.substr(equals + 1);
                has_value = true;
            }
        }

        if (name == "-h" || name == "--help") {
            result.status = ParseStatus::ShowHelp;
            return result;
        }
        if (name == "--version") {
            result.status = ParseStatus::ShowVersion;
            return result;
        }
        if (name == "-c" || name == "--copy") {
            result.args.copy = true;
            continue;
        }
        if (name == "--copy-only") {
            result.args.copy_only = true;
            continue;
        }
        if (name == "-o" || name == "--open") {
            result.args.open_file = true;
            continue;
        }

        if (const auto option = value_option_for(name)) {
            if (!has_value) {
                pending = *option;
                continue;
            }
            std::string error;
            if (!assign_value(*option, value, result.args, error))
                return fail(std::move(error));
            continue;
        }

        if (!name.empty() && name.front() == '-' && name != "-")
            return fail("unknown option '" + token + "'");

        // Positional: the mode.
        if (mode_set)
            return fail("unexpected extra argument '" + token + "'");
        const auto mode = mode_from_string(token);
        if (!mode)
            return fail("unknown mode '" + token + "' (expected screen, window or section)");
        result.args.mode = *mode;
        mode_set         = true;
    }

    if (pending != ValueOption::None)
        return fail(std::string("missing value for ") + option_spelling(pending));

    if (!mode_set)
        return fail("missing mode (expected screen, window or section)");

    if (result.args.copy && result.args.copy_only)
        return fail("--copy and --copy-only are mutually exclusive");

    if (result.args.open_file && result.args.copy_only)
        return fail("--open cannot be combined with --copy-only (nothing is saved)");

    if (!result.args.monitor.empty() && result.args.mode != Mode::Screen)
        return fail("--monitor is only valid with 'screen' mode");

    result.status = ParseStatus::Ok;
    return result;
}

}  // namespace shot
