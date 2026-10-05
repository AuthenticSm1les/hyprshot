// The graphical capture bar.
#pragma once

#include "args.hpp"

namespace shot::gui {

// Layer-shell namespace the capture bar registers under, so it can be found
// (and waited for) through `hyprctl layers`.
inline constexpr const char* kNamespace = "shot";

enum class Outcome {
    Captured,     // the user asked for a capture; `args` is filled in
    Cancelled,    // the user dismissed the bar
    Unavailable,  // the GUI could not be started; the reason was printed to stderr
};

struct Result {
    Outcome outcome = Outcome::Cancelled;
    Args    args;
};

// Show the capture bar and block until the user captures or dismisses it.
Result run();

}  // namespace shot::gui
