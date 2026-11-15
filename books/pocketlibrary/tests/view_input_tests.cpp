// Pocket Library's input on GUI.Forms' raisers, through real window dispatch: F1 opens help
// in on_key_down, a press anywhere closes it in on_mouse_down, a key up changes nothing, and a
// key bubbling from a child reaches the view's key handling with its fields intact
// (gui_forms::key_event_arguments, public since cp33).
#include "gui_forms/events/key_events/key_events.hpp"
#include "pocketlibrary_view.hpp"
#include "view_contract_test.hpp"

#include <iostream>
#include <stdexcept>

namespace gf = gui_forms;
namespace {

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

bool help_shown(const pl::PocketLibraryView& view) {
    const std::vector<games::GameCommand> commands = view.commands();
    const games::GameCommand* help = kit::contract_find(commands, "help");
    const bool shown = help != nullptr && (*help).checked;
    return shown;
}

bool key(gf::Window& window, gf::KeyAction action, std::uint32_t physical_key) {
    gf::KeyEvent event{};
    event.action = action;
    event.physical_key = physical_key;
    const bool handled = window.dispatch_key(event);
    return handled;
}

void press(gf::Window& window, gf::Point point) {
    gf::PointerEvent event{};
    event.button = gf::PointerButton::primary;
    event.position = point;
    event.click_count = 1;
    event.action = gf::PointerAction::down;
    static_cast<void>(window.dispatch_pointer(event));
    event.action = gf::PointerAction::up;
    static_cast<void>(window.dispatch_pointer(event));
}

void help_opens_and_closes() {
    kit::contract_isolate_saves("pocketlibrary-input");
    const pl::Options options{.hosted = true, .dev = true};
    std::shared_ptr<pl::PocketLibraryView> view =
        gf::make_control<pl::PocketLibraryView>(games::stable_id("pocketlibrary"), options);
    (*view).set_cabinet(true, false, false, true);
    gf::Window window(view, {1180, 800});
    window.set_active(true);
    window.perform_layout();
    static_cast<void>(window.request_focus(view));
    require(!help_shown(*view), "help starts closed");
    require(key(window, gf::KeyAction::down, gf::PhysicalKey::f1), "F1 is handled in on_key_down");
    require(help_shown(*view), "F1 opens help");
    static_cast<void>(key(window, gf::KeyAction::up, gf::PhysicalKey::f1));
    require(help_shown(*view), "the key up changes nothing");
    press(window, {590, 400});
    require(!help_shown(*view), "a press anywhere closes help");
}

void bubbled_key_keeps_its_fields() {
    gf::KeyEvent event{};
    event.action = gf::KeyAction::down;
    event.physical_key = gf::PhysicalKey::f;
    event.modifiers = gf::Modifier::control;
    event.repeat = true;
    const gf::KeyEventArgs arguments = gf::key_event_arguments(event);
    require(arguments.physical_key == gf::PhysicalKey::f && arguments.modifiers == gf::Modifier::control &&
                arguments.repeat && !arguments.handled,
            "the bubbled key's fields are carried");
}

} // namespace

int main() {
    try {
        help_opens_and_closes();
        bubbled_key_keeps_its_fields();
        std::cout << "pocketlibrary view_input_tests: ok\n";
    } catch (const std::exception& error) {
        std::cerr << "pocketlibrary view_input_tests: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
