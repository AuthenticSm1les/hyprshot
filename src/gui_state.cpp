#include "gui_state.hpp"

namespace shot::gui {

Args args_from_selection(Mode mode, std::size_t behavior_index) {
    Args args;
    args.mode = mode;

    switch (static_cast<Behavior>(behavior_index)) {
        case Behavior::SaveAndCopy:
            args.copy = true;
            break;
        case Behavior::CopyOnly:
            args.copy_only = true;
            break;
        default:
            args.copy = true;  // unknown index: behave as "save and copy"
            break;
    }

    return args;
}

}  // namespace shot::gui
