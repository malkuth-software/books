// games::RoutedInput (src/routed_input.hpp): the views' routed pointer and key handlers
// over GUI.Forms' interaction raisers. Checks, through the window's own dispatch: the
// handler sees the press in window coordinates before the MouseDown subscribers; a
// handled press ends the route at the target (the parent's bubble does not see it) and
// an unhandled move reaches the bubble; the key subscribers run before the handler and
// a key they handle never reaches it; enter and leave reach the handler; a key up
// raised because the window lost activation does not.
#include "routed_input.hpp"

#include "gui_forms/window.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gf = gui_forms;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "routed_input_tests: " << message << "\n";
        std::exit(1);
    }
}

class Probe final : public games::RoutedInput<gf::Control> {
  public:
    explicit Probe(gf::StableId id) : games::RoutedInput<gf::Control>(std::move(id)) {
        focusable = true;
    }
    // What ran, in order: "pointer <action>", "key <down|up>", "mouse_down event",
    // "key_down event".
    std::vector<std::string> trace;
    gf::Point last_position{};
    bool subscriber_handles_keys = false;
    void observe_mouse_down(const gf::MouseEventArgs& arguments) {
        static_cast<void>(arguments);
        trace.push_back("mouse_down event");
    }
    void observe_key_down(gf::KeyEventArgs& arguments) {
        trace.push_back("key_down event");
        if (subscriber_handles_keys)
            arguments.handled = true;
    }

  protected:
    void handle_pointer(gf::PointerEvent& event) override {
        static const char* const names[] = {"move", "down", "up", "wheel", "enter", "leave"};
        trace.push_back(std::string("pointer ") + names[static_cast<int>(event.action)]);
        last_position = event.position;
        // A press is the probe's own; a move is left to the route.
        if (event.action == gf::PointerAction::down)
            event.handled = true;
    }
    void handle_key(gf::KeyEvent& event) override {
        trace.push_back(event.action == gf::KeyAction::down ? "key down" : "key up");
        event.handled = true;
    }
};

// The probe's parent: places it at (10, 10) and records the pointer bubble.
class Frame final : public gf::Control {
  public:
    explicit Frame(gf::StableId id) : Control(std::move(id)) {}
    std::shared_ptr<Probe> probe;
    std::vector<gf::PointerAction> bubbled;
    void arrange(gf::Rect bounds) override {
        arrange_self(bounds);
        set_child_layout(probe, {10, 10, 100, 100});
    }
    void on_pointer_bubble(gf::PointerEvent& event) override {
        bubbled.push_back(event.action);
        gf::Control::on_pointer_bubble(event);
    }
};

bool traced(const Probe& probe, const std::string& entry) {
    for (const std::string& line : probe.trace)
        if (line == entry)
            return true;
    return false;
}

std::size_t index_of(const Probe& probe, const std::string& entry) {
    for (std::size_t index = 0; index < probe.trace.size(); ++index)
        if (probe.trace[index] == entry)
            return index;
    return probe.trace.size();
}

bool bubbled(const Frame& frame, gf::PointerAction action) {
    for (gf::PointerAction seen : frame.bubbled)
        if (seen == action)
            return true;
    return false;
}

} // namespace

int main() {
    std::shared_ptr<Frame> frame = gf::make_control<Frame>(gf::StableId("test.frame"));
    std::shared_ptr<Probe> probe = gf::make_control<Probe>(gf::StableId("test.probe"));
    (*frame).probe = probe;
    (*frame).add_child(probe);
    gf::Window window(frame, {200, 200});
    window.perform_layout();
    window.set_active(true);
    std::vector<gf::SubscriptionToken> subscriptions;
    subscriptions.push_back((*probe).mouse_down().subscribe(
        *probe, gf::Delegate<const gf::MouseEventArgs&>::bind<Probe, &Probe::observe_mouse_down>(*probe)));
    subscriptions.push_back((*probe).key_down().subscribe(
        *probe, gf::Delegate<gf::KeyEventArgs&>::bind<Probe, &Probe::observe_key_down>(*probe)));

    // A move into the probe: the crossing reaches the handler, and the unhandled move
    // bubbles to the frame.
    gf::PointerEvent move{};
    move.action = gf::PointerAction::move;
    move.position = {50, 60};
    static_cast<void>(window.dispatch_pointer(move));
    require(traced(*probe, "pointer enter"), "the enter reaches the handler");
    require(traced(*probe, "pointer move"), "the move reaches the handler");
    require(bubbled(*frame, gf::PointerAction::move), "an unhandled move bubbles to the parent");

    // A press: the handler first, in window coordinates, then the subscribers; handled
    // ends the route at the probe.
    (*probe).trace.clear();
    (*frame).bubbled.clear();
    gf::PointerEvent press{};
    press.action = gf::PointerAction::down;
    press.button = gf::PointerButton::primary;
    press.position = {50, 60};
    static_cast<void>(window.dispatch_pointer(press));
    require(traced(*probe, "pointer down"), "the press reaches the handler");
    require((*probe).last_position.x == 50 && (*probe).last_position.y == 60,
            "the handler sees the window position");
    require(index_of(*probe, "pointer down") < index_of(*probe, "mouse_down event"),
            "the handler runs before the MouseDown subscribers");
    require(!bubbled(*frame, gf::PointerAction::down), "a handled press does not bubble");
    gf::PointerEvent release = press;
    release.action = gf::PointerAction::up;
    static_cast<void>(window.dispatch_pointer(release));

    // Keys: the subscribers first, then the handler.
    require(window.request_focus(probe), "the probe takes focus");
    (*probe).trace.clear();
    gf::KeyEvent key{};
    key.physical_key = gf::PhysicalKey::a;
    static_cast<void>(window.dispatch_key(key));
    require(index_of(*probe, "key_down event") < index_of(*probe, "key down"),
            "the KeyDown subscribers run before the handler");
    gf::KeyEvent key_release = key;
    key_release.action = gf::KeyAction::up;
    static_cast<void>(window.dispatch_key(key_release));
    require(traced(*probe, "key up"), "a key up reaches the handler");

    // A key the subscribers handle never reaches the handler.
    (*probe).trace.clear();
    (*probe).subscriber_handles_keys = true;
    static_cast<void>(window.dispatch_key(key));
    require(traced(*probe, "key_down event") && !traced(*probe, "key down"),
            "a key the subscribers handled does not reach the handler");
    (*probe).subscriber_handles_keys = false;

    // A key held while the window loses activation: its release does not reach the handler.
    (*probe).trace.clear();
    key.physical_key = gf::PhysicalKey::b;
    static_cast<void>(window.dispatch_key(key));
    require(traced(*probe, "key down"), "the held key's down reaches the handler");
    (*probe).trace.clear();
    window.set_active(false);
    require(!traced(*probe, "key up"), "a release by deactivation does not reach the handler");

    // Leaving the window: the leave reaches the handler.
    window.set_active(true);
    (*probe).trace.clear();
    gf::PointerEvent away{};
    away.action = gf::PointerAction::move;
    away.position = {190, 190};
    static_cast<void>(window.dispatch_pointer(away));
    require(traced(*probe, "pointer leave"), "the leave reaches the handler");
    std::cout << "routed_input_tests: ok\n";
    return 0;
}
