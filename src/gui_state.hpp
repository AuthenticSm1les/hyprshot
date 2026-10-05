// Mapping from the capture bar's controls to capture arguments.
//
// Deliberately free of hyprtoolkit so it can be unit tested without a display.
// This is precisely the logic the previous version dropped: whatever the user
// picked, the result was overwritten with "screen, save".
#pragma once

#include <cstddef>

#include "args.hpp"

namespace shot::gui {

// Entries of the result menu, in display order. There is deliberately no
// "save only" choice: saving to a folder always copies to the clipboard too.
enum class Behavior : std::size_t {
    SaveAndCopy = 0,  // save into the screenshot folder, and copy
    CopyOnly    = 1,  // clipboard only, nothing written
};

// Combine the chosen mode with the chosen behaviour. An out-of-range behaviour
// index falls back to saving and copying.
Args args_from_selection(Mode mode, std::size_t behavior_index);

}  // namespace shot::gui
