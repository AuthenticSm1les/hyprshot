#include "gui.hpp"
#include "capture.hpp"
#include "gui_state.hpp"
#include "hyprland.hpp"

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <hyprtoolkit/core/Backend.hpp>
#include <hyprtoolkit/core/Input.hpp>
#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/Combobox.hpp>
#include <hyprtoolkit/element/Null.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/palette/Palette.hpp>
#include <hyprtoolkit/window/Window.hpp>
#include <hyprutils/memory/SharedPtr.hpp>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace shot::gui {
namespace {

using namespace Hyprtoolkit;
using namespace Hyprutils::Memory;

template <typename T>
using SP = CSharedPointer<T>;

// Raw wlr-layer-shell-unstable-v1 values: CWindowBuilder takes them as plain
// integers because hyprtoolkit does not wrap the enums.
constexpr uint32_t kAnchorBottom     = 2;
constexpr uint32_t kLayerTop         = 2;
constexpr uint32_t kKeyboardOnDemand = 2;

// -1 means "do not reserve space": the bar floats over other windows instead of
// resizing them while it is open.
constexpr int32_t kNoExclusiveZone = -1;

// Only the bottom edge is anchored, so the compositor centres the bar
// horizontally. Anchoring the left and right edges as well is what stretches a
// layer surface across the whole screen; change the width here instead.
constexpr float kBarWidth      = 660.F;
constexpr float kBarHeight     = 56.F;
constexpr float kComboboxWidth = 320.F;

// Everything the signal handlers touch. Held in one place so no callback
// outlives a local it captured.
struct State {
    SP<IBackend>         backend;
    SP<IWindow>          window;
    SP<CComboboxElement> behavior;

    bool capture_requested = false;
    bool tearing_down      = false;
    bool unavailable       = false;
    Args result;
};

State g_state;

// Leave the event loop, and release the bar while its Wayland connection is
// still alive.
//
// Two things make the ordering here load-bearing:
//  * ~CWaylandLayer destroys its wp_viewport with a wl_proxy call, so it needs a
//    live display. backend->destroy() only *posts* the loop stop - the platform
//    and its display are torn down in cleanup() as enterLoop() returns - so
//    releasing the window after enterLoop() is a use-after-free on the
//    connection. (That was the SIGSEGV: ~CCWpViewport -> wl_proxy_marshal_flags.)
//  * every caller sits inside a callback owned by an element in the tree being
//    destroyed, so doing this inline frees the caller's own widget.
void request_teardown() {
    if (g_state.tearing_down)
        return;
    g_state.tearing_down = true;

    g_state.backend->addIdle([] {
        // Drop every toolkit object while the connection is still up. The
        // window's destructor marshals a wp_viewport destroy, and anything left
        // referenced in g_state would only be destroyed during static teardown,
        // long after the toolkit has gone.
        g_state.behavior.reset();
        g_state.window.reset();
        g_state.backend->destroy();
    });
}

// Choosing a mode *is* the action: there is no separate confirm step, and no
// mode is remembered between runs. The behaviour combobox decides what happens
// to the result.
void capture_now(Mode mode) {
    if (g_state.tearing_down)
        return;

    const std::size_t behavior = g_state.behavior
                                     ? g_state.behavior->current()
                                     : static_cast<std::size_t>(Behavior::SaveAndCopy);

    g_state.result            = args_from_selection(mode, behavior);
    g_state.capture_requested = true;
    request_teardown();
}

// Abbreviate the home directory to "~" so the save label stays readable.
std::string short_path(const std::filesystem::path& path) {
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        const std::string home_str = home;
        const std::string full     = path.string();
        if (full == home_str)
            return "~";
        if (full.rfind(home_str + '/', 0) == 0)
            return "~" + full.substr(home_str.size());
    }
    return path.string();
}

}  // namespace

Result run() {
    Result result;

    g_state = State{};
    g_state.backend = IBackend::create();
    if (!g_state.backend) {
        std::cerr << "shot: the hyprtoolkit backend could not be created\n";
        result.outcome = Outcome::Unavailable;
        return result;
    }

    // A bar pinned to the bottom edge and centred horizontally.
    // preferredSize is mandatory: without it the layer surface is never
    // created at all.
    g_state.window = CWindowBuilder::begin()
                         ->type(HT_WINDOW_LAYER)
                         ->appTitle("shot")
                         ->appClass(kNamespace)
                         ->layer(kLayerTop)
                         ->anchor(kAnchorBottom)
                         ->exclusiveZone(kNoExclusiveZone)
                         ->kbInteractive(kKeyboardOnDemand)
                         ->preferredSize({kBarWidth, kBarHeight})
                         ->marginBottomRight({0.F, 16.F})  // {right, bottom}
                         ->commence();
    if (!g_state.window) {
        std::cerr << "shot: the capture bar window could not be created\n";
        g_state.backend->destroy();
        g_state.backend.reset();
        result.outcome = Outcome::Unavailable;
        return result;
    }

    // The bar draws its own surface: a layer surface gets no window border from
    // the compositor, so the rounding and the accent border come from the
    // palette, the same way hyprlauncher styles itself.
    auto background = CRectangleBuilder::begin()
                          ->color([] { return g_state.backend->getPalette()->m_colors.background; })
                          ->rounding(g_state.backend->getPalette()->m_vars.bigRounding)
                          ->borderColor([] {
                              return g_state.backend->getPalette()->m_colors.accent.darken(0.2F);
                          })
                          ->borderThickness(1)
                          ->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT,
                                  {1.F, 1.F}})
                          ->commence();

    // A plain rectangle is not clickable until it asks for mouse input.
    background->setReceivesMouse(true);
    background->setMouseButton([](Input::eMouseButton button, bool down) {
        if (down && button == Input::MOUSE_BUTTON_RIGHT)
            request_teardown();  // right-click anywhere on the bar cancels
    });

    auto row = CRowLayoutBuilder::begin()
                   ->gap(10)
                   ->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.F, 1.F}})
                   ->commence();
    row->setMargin(12);

    // Each mode is its own button: clicking one starts that capture straight
    // away, using whichever behaviour the menu currently shows.
    const auto add_mode = [&](const char* label, Mode mode) {
        row->addChild(CButtonBuilder::begin()
                          ->label(std::string(label))
                          ->size({CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1.F, 1.F}})
                          ->onMainClick([mode](SP<CButtonElement>) { capture_now(mode); })
                          ->commence());
    };
    add_mode("Screen", Mode::Screen);
    add_mode("Window", Mode::Window);
    add_mode("Section", Mode::Section);

    // Push the result menu to the right-hand edge.
    auto spacer = CNullBuilder::begin()->commence();
    spacer->setGrow(true);
    row->addChild(spacer);

    // There is no plain "save only" entry: saving always copies as well. The
    // label names the real destination rather than an abstract "disk".
    const std::string save_label = "Save to " + short_path(default_screenshot_dir());

    g_state.behavior = CComboboxBuilder::begin()
                           ->items({save_label, "Save to clipboard"})
                           ->currentItem(static_cast<std::size_t>(Behavior::SaveAndCopy))
                           ->size({CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE,
                                   {kComboboxWidth, 32.F}})
                           ->commence();
    row->addChild(g_state.behavior);

    background->addChild(row);
    g_state.window->m_rootElement->addChild(background);

    // A layer surface never receives closeRequest, and Escape only arrives once
    // the compositor has given the bar keyboard focus (on-demand).
    g_state.window->m_events.keyboardKey.listenStatic([](const Input::SKeyboardKeyEvent& event) {
        if (event.down && event.xkbKeysym == XKB_KEY_Escape)
            request_teardown();
    });
    g_state.window->m_events.layerClosed.listenStatic([] { request_teardown(); });

    g_state.window->open();

    // Safety net: a layer surface that never gets mapped would otherwise leave
    // this process sitting in the event loop with nothing on screen. If the
    // compositor can be queried and does not list our namespace, give up with a
    // clear message instead of hanging.
    g_state.backend->addTimer(std::chrono::seconds{5},
                              [](CAtomicSharedPointer<CTimer>, void*) {
                                  if (g_state.tearing_down)
                                      return;

                                  const auto present = layer_surface_present(kNamespace);
                                  if (present.has_value() && !*present) {
                                      std::cerr << "shot: the capture bar did not appear; the "
                                                   "compositor may not support wlr-layer-shell\n";
                                      g_state.unavailable = true;
                                      request_teardown();
                                  }
                              },
                              nullptr);

    g_state.backend->enterLoop();

    if (g_state.unavailable) {
        result.outcome = Outcome::Unavailable;
    } else if (g_state.capture_requested) {
        result.outcome = Outcome::Captured;
        result.args    = g_state.result;
    } else {
        result.outcome = Outcome::Cancelled;
    }

    // Those referencing Wayland objects were already released by the teardown
    // idle. The loop has exited, so cleanup() has run and dropping the backend
    // here is the documented safe point.
    g_state.backend.reset();

    return result;
}

}  // namespace shot::gui
