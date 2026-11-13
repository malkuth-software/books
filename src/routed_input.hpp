#pragma once
// The routed pointer and key handling of PlaySuite's views over GUI.Forms'
// interaction raisers. GUI.Forms 0.6 (cp29) removed Control::on_pointer and
// Control::on_key: the target phase of the pointer and key routes is now the
// raisers (on_mouse_down, on_mouse_up, on_mouse_move, on_mouse_wheel,
// on_key_down, on_key_up), which take the Windows Forms argument records.
// Every view here was written against the routed events, so this base
// rebuilds the routed event from each raiser's arguments and hands it to
// handle_pointer or handle_key; a view keeps its one handler. It follows
// GUI.Forms' own tests/support/routed_input_probe.hpp.
//
// Order: a mouse raiser runs the handler first, copies its handled back
// (which ends the route at the target, as before) and then raises the event
// through Base, unless the handler disposed the control. A key raiser raises
// the event through Base first; a key the subscribers left unhandled reaches
// the handler, and a key up the window raised because it lost activation
// (released_by_deactivation) does not, as before. Enter and leave come from
// the window's hover tracking (on_mouse_enter, on_mouse_leave) and carry no
// position: a handler that reads the position of a crossing sees the window
// origin, and the move that follows an enter sets the hover.
//
// Base is the GUI.Forms control the view derives from. The handlers run on
// the UI thread inside the window's dispatch, like the raisers.

#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/events/key_events/key_events.hpp"
#include "gui_forms/events/mouse_events/mouse_events.hpp"

namespace games {

template <typename Base>
class RoutedInput : public Base {
  public:
    using Base::Base;

  protected:
    // The view's routed handlers. The defaults ignore the event.
    virtual void handle_pointer(gui_forms::PointerEvent& event) {
        static_cast<void>(event);
    }
    virtual void handle_key(gui_forms::KeyEvent& event) {
        static_cast<void>(event);
    }

    void on_mouse_down(gui_forms::MouseEventArgs& arguments) override {
        route_mouse(arguments);
        if ((*this).is_alive())
            Base::on_mouse_down(arguments);
    }
    void on_mouse_up(gui_forms::MouseEventArgs& arguments) override {
        route_mouse(arguments);
        if ((*this).is_alive())
            Base::on_mouse_up(arguments);
    }
    void on_mouse_move(gui_forms::MouseEventArgs& arguments) override {
        route_mouse(arguments);
        if ((*this).is_alive())
            Base::on_mouse_move(arguments);
    }
    void on_mouse_wheel(gui_forms::MouseEventArgs& arguments) override {
        route_mouse(arguments);
        if ((*this).is_alive())
            Base::on_mouse_wheel(arguments);
    }
    void on_mouse_enter() override {
        route_crossing(gui_forms::PointerAction::enter);
        if ((*this).is_alive())
            Base::on_mouse_enter();
    }
    void on_mouse_leave() override {
        route_crossing(gui_forms::PointerAction::leave);
        if ((*this).is_alive())
            Base::on_mouse_leave();
    }
    void on_key_down(gui_forms::KeyEventArgs& arguments) override {
        Base::on_key_down(arguments);
        route_key(arguments, gui_forms::KeyAction::down);
    }
    void on_key_up(gui_forms::KeyEventArgs& arguments) override {
        Base::on_key_up(arguments);
        if (arguments.released_by_deactivation)
            return;
        route_key(arguments, gui_forms::KeyAction::up);
    }

  private:
    void route_mouse(gui_forms::MouseEventArgs& arguments) {
        gui_forms::PointerEvent event = gui_forms::pointer_event_of(arguments);
        handle_pointer(event);
        arguments.handled = event.handled;
    }
    void route_crossing(gui_forms::PointerAction action) {
        gui_forms::PointerEvent event{};
        event.action = action;
        event.phase = gui_forms::EventPhase::target;
        handle_pointer(event);
    }
    void route_key(gui_forms::KeyEventArgs& arguments, gui_forms::KeyAction action) {
        if (arguments.handled || !(*this).is_alive())
            return;
        gui_forms::KeyEvent event = gui_forms::key_event_of(arguments, action);
        handle_key(event);
        arguments.handled = event.handled;
    }
};

} // namespace games
