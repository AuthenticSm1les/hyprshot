// Unit tests for the capture bar's selection -> arguments mapping.
//
// The GUI itself needs a live compositor, but this mapping is the part that was
// broken (every selection was discarded), so it is tested directly.
#include <cstddef>
#include <string>

#include "args.hpp"
#include "check.hpp"
#include "gui_state.hpp"

using namespace shot;

namespace {

void check_mode_preserved(gui::Behavior behavior, const char* behavior_name) {
    const Mode modes[] = {Mode::Screen, Mode::Window, Mode::Section};
    for (const Mode mode : modes) {
        const Args args = gui::args_from_selection(mode, static_cast<std::size_t>(behavior));
        check::expect_eq(mode_name(args.mode), std::string(mode_name(mode)),
                         std::string(behavior_name) + " keeps the " + mode_name(mode) + " mode");
    }
}

}  // namespace

int main() {
    check_mode_preserved(gui::Behavior::SaveAndCopy, "save to folder");
    check_mode_preserved(gui::Behavior::CopyOnly, "save to clipboard");

    // "Save to <folder>": writes a file and copies it. There is no longer any
    // save-without-copy option.
    {
        const Args args = gui::args_from_selection(Mode::Section, 0);
        check::expect(args.copy, "save to folder: copies to the clipboard");
        check::expect(!args.copy_only, "save to folder: still writes a file");
        check::expect(!args.open_file, "save to folder: does not auto-open");
    }

    // "Save to clipboard": nothing is written.
    {
        const Args args = gui::args_from_selection(Mode::Section, 1);
        check::expect(!args.copy, "save to clipboard: does not also set --copy");
        check::expect(args.copy_only, "save to clipboard: saves nothing");
    }

    // Whatever the bar can produce must be a combination the CLI accepts:
    // --copy and --copy-only are mutually exclusive, and --open needs a file.
    for (std::size_t index = 0; index < 2; ++index) {
        const Args args = gui::args_from_selection(Mode::Window, index);
        check::expect(!(args.copy && args.copy_only), "never sets both clipboard flags");
        check::expect(!(args.open_file && args.copy_only), "never opens with copy-only");
    }

    // Saving is always paired with a clipboard copy, so the old "quiet save"
    // case must not come back through an out-of-range index either.
    {
        const Args args = gui::args_from_selection(Mode::Window, 99);
        check::expect_eq(mode_name(args.mode), std::string("window"), "out-of-range keeps the mode");
        check::expect(args.copy, "out-of-range falls back to save and copy");
        check::expect(!args.copy_only, "out-of-range does not become copy-only");
    }

    return check::report("gui_state_test");
}
