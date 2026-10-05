// Command-line parsing.
//
// Parsing is side-effect free: the result tells the caller what to do, so
// `--help` and `--version` are ordinary outcomes rather than calls to exit().
#pragma once

#include <optional>
#include <string>

namespace shot {

enum class Mode { Screen, Window, Section };

[[nodiscard]] const char* mode_name(Mode mode);
[[nodiscard]] std::optional<Mode> mode_from_string(const std::string& text);

struct Args {
    Mode        mode      = Mode::Screen;
    bool        copy      = false;  // save to disk and copy to the clipboard
    bool        copy_only = false;  // copy to the clipboard, save nothing
    bool        open_file = false;  // open the result with xdg-open
    std::string dir;                // save directory; empty means the default
    std::string file;               // output filename; empty means a timestamp
    std::string monitor;            // output name for screen mode
    double      delay = 0.0;        // seconds to wait before capturing
};

enum class ParseStatus { Ok, ShowHelp, ShowVersion, Error };

struct ParseResult {
    ParseStatus status = ParseStatus::Ok;
    Args        args;
    std::string message;  // human-readable error, for ParseStatus::Error
};

// Parse argv[1..]. `argv[0]` is used only for the program name in messages.
ParseResult parse_args(int argc, char** argv);

// Basename of argv[0], for user-facing messages.
[[nodiscard]] std::string program_name(const char* argv0);

// Help text for `program`.
std::string usage_text(const std::string& program);

}  // namespace shot
