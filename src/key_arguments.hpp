#pragma once
// games::key_arguments_of: the KeyEventArgs of a routed key event. PlaySuite's
// views handle keys in GUI.Forms' raisers (on_key_down, on_key_up), over the
// raisers' KeyEventArgs; a view that also takes the keys bubbling up from its
// focused children (on_key_bubble, which carries the routed KeyEvent) runs the
// same handling on this record and copies handled back. GUI.Forms 0.6 converts
// only the other way in public (key_event_of); its own key_arguments is
// internal to the window.
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/events/key_events/key_events.hpp"

namespace games {

[[nodiscard]] inline gui_forms::KeyEventArgs key_arguments_of(const gui_forms::KeyEvent& event) noexcept {
    gui_forms::KeyEventArgs arguments{};
    arguments.key_code = gui_forms::key_code_from_physical_key(event.physical_key);
    arguments.physical_key = event.physical_key;
    arguments.scan_code = event.scan_code;
    arguments.modifiers = event.modifiers;
    arguments.repeat = event.repeat;
    arguments.handled = event.handled;
    return arguments;
}

} // namespace games
