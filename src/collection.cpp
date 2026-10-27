#include "collection.hpp"
#include "audio.hpp"
#include "pcm_player.hpp"
#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/window.hpp"
namespace games {
namespace {
void repaint_tree(gf::Control& control) {
    control.invalidate(gf::Dirty::paint);
    for (const std::shared_ptr<gf::Control>& child : control.children())
        repaint_tree(*child);
}
} // namespace
Collection::Collection(gf::StableId id, bool dev) : Control(std::move(id)) {
    modules_.dev = dev;
    set_theme_override(games_theme(ButtonSkin::ivory));
}
Collection::~Collection() = default;
void Collection::initialize_control_tree() {
    sprites_.request_update = std::bind_front(&Collection::wake, this);
    capsule_ = gf::make_control<CommandCapsule>(gf::StableId("collection.capsule"), sprites_);
    add_child(capsule_);
    (*capsule_).back = std::bind_front(&Collection::back, this);
    (*capsule_).command = std::bind_front(&Collection::run_command, this);
    (*capsule_).bind_masters(model_);
    gf::on(model_.music.invoked(), *this, &Collection::toggle, 0);
    gf::on(model_.sound.invoked(), *this, &Collection::toggle, 1);
    gf::on(model_.reduced.invoked(), *this, &Collection::toggle, 2);
    gf::on(model_.settings.invoked(), *this, &Collection::toggle, 3);
    help_link_ = gf::make_control<HelpGlyph>(gf::StableId("collection.help"), "?");
    (*help_link_).set_font({gf::FontRole::content, 32, 700, false});
    (*help_link_).set_ink(gf::Color::rgba(240, 192, 84));
    (*help_link_).set_accessible_name("Help (H)");
    (*help_link_).set_paint_plane(gf::PaintPlane::overlay);
    gf::on((*help_link_).clicked(), *this, &Collection::clicked_help);
    help_ = gf::make_control<HelpBook>(gf::StableId("collection.help-book"));
    (*help_).close = std::bind_front(&Collection::close_help, this);
    (*help_).set_visible(false);
    settings_ = gf::make_control<SettingsSheet>(gf::StableId("collection.settings"));
    (*settings_).bind_masters(model_);
    (*settings_).close = std::bind_front(&Collection::close_settings, this);
    (*settings_).set_visible(false);
    add_child(help_link_);
    add_child(help_);
    add_child(settings_);
    masters_ = SettingsStore::shared().observe(std::bind_front(&Collection::masters_changed, this));
    const SuiteSettings& masters = SettingsStore::shared().values();
    music_shown_ = masters.music;
    sound_shown_ = masters.sound;
    reduced_shown_ = masters.reduced;
    // The one volume on the shelf is open from the start.
    ensure_view(active_);
    preferences();
    visibility();
    refresh_commands();
}
void Collection::ensure_view(Entry entry) {
    if (view(entry)) return;
    std::unique_ptr<GameInstance> instance = game_descriptor(entry).create(modules_);
    const std::shared_ptr<gf::Control> created = (*instance).control();
    games_[entry] = std::move(instance);
    // Card modules intentionally share a control and cabinet. Attach it only once.
    if (std::find(children().begin(), children().end(), created) == children().end()) {
        add_child(created);
        static_cast<void>(set_child_index((*created).runtime_id(), children().size()-1));
    }
}
void Collection::on_attached_to_window() {
    timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(16));
    gf::on((*timer_).tick(), *this, &Collection::tick);
    last_tick_ = std::chrono::steady_clock::now();
    (*timer_).start();
}
void Collection::on_detaching_from_window(gf::Window& window) noexcept {
    if (timer_)
        (*timer_).stop();
    timer_.reset();
    sprites_.release(window);
}
bool Collection::hosted(Entry entry) const { return game_descriptor(entry).hosted; }
bool Collection::railed(Entry entry) const { return game_descriptor(entry).rail; }
std::shared_ptr<gf::Control> Collection::view(Entry entry) const {
    const std::map<Entry,std::unique_ptr<GameInstance>>::const_iterator found = games_.find(entry);
    return found == games_.end() ? nullptr : (*(*found).second).control();
}
CommandSource* Collection::source(Entry entry) const {
    const std::map<Entry,std::unique_ptr<GameInstance>>::const_iterator found = games_.find(entry);
    return found == games_.end() ? nullptr : (*found).second.get();
}
void Collection::wake() {
    quiet_ = 0;
    if (!timer_)
        return;
    (*timer_).set_interval(std::chrono::milliseconds(16));
    (*timer_).start();
}
void Collection::tick() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const double dt = std::clamp(std::chrono::duration<double>(now - last_tick_).count(), 0.0, .25);
    last_tick_ = now;
    audio_poll();
    if (gf::Window* window = attached_window())
        set_polygon_scale((*window).scale());
    if (gf::Window* window = attached_window(); window && sprites_.update(*window)) {
        // New lettering: every control that may paint it, however deep, or the ones not
        // hovered keep the stand-in face.
        repaint_tree(*capsule_);
        (*capsule_).set_maximum_width(capsule_width_limit_);
        (*capsule_).invalidate(gf::Dirty::paint);
        invalidate(gf::Dirty::layout);
    }
    const gf::Rect area{0, 0, client_rectangle().width - 48, railed(active_) ? current_rail_height_ : 60.0};
    gf::Rect r = (*capsule_).placement(area);
    // The placement includes the shadow margin, which doubles as a forgiving hover border.
    const bool inside = pointer_.x >= r.x && pointer_.x <= r.x + r.width && pointer_.y >= r.y - 8 &&
                        pointer_.y <= r.y + r.height;
    if ((*capsule_).step(dt, inside, reduced_))
        invalidate(gf::Dirty::layout);
    refresh_ += dt;
    if (refresh_ > .2) {
        refresh_ = 0;
        refresh_commands();
    }
    // Commands can change after an asynchronous game action. Poll that cheap
    // model at 5 Hz for a few seconds after the last input; only hover animation, text
    // and audio preparation need 60 Hz. Then stop: every wake of the window costs a
    // repaint wherever the capsule floats over a live game, so a settled game must
    // not be woken at all. The next pointer or key wakes it again.
    const bool busy = (*capsule_).unsettled(inside) || sprites_.waiting() || audio_pending();
    quiet_ = busy ? 0 : quiet_ + dt;
    if (timer_) {
        if (quiet_ > 3.0) {
            refresh_commands();
            (*timer_).stop();
        } else
            (*timer_).set_interval(std::chrono::milliseconds(busy ? 16 : 200));
    }
}
void Collection::on_pointer_preview(gf::PointerEvent& e) {
    if (settings_open()) {
        const gf::Point local = (*settings_).point_from_window(e.position);
        const gf::Rect paper = (*settings_).paper();
        if (local.x < paper.x || local.y < paper.y || local.x >= paper.x + paper.width ||
            local.y >= paper.y + paper.height) {
            // A press on the dimmed background closes the screen, like Escape.
            if (e.action == gf::PointerAction::down && e.button == gf::PointerButton::primary)
                close_settings();
            e.handled = true;
            return;
        }
    }
    if (help_open()) {
        const gf::Point local = (*help_).point_from_window(e.position);
        const gf::Rect paper = (*help_).client_rectangle();
        if (local.x < 0 || local.y < 0 || local.x >= paper.width || local.y >= paper.height) {
            e.handled = true;
            return;
        }
    }
    wake();
    pointer_ = point_from_window(e.position);
    if (e.action == gf::PointerAction::leave && (pointer_.x < 0 || pointer_.y < 0))
        pointer_ = {-1000, -1000};
    gf::Control::on_pointer_preview(e);
}
void Collection::on_key_preview(gf::KeyEvent& e) {
    wake();
    const std::shared_ptr<gf::Control> focus =
        attached_window() ? (*attached_window()).focused_control() : nullptr;
    const bool entering_text = dynamic_cast<gf::TextBox*>(focus.get()) != nullptr ||
        (!help_open() && (*games_.at(active_)).editing_name());
    if (e.action == gf::KeyAction::down && e.modifiers == gf::Modifier::none &&
        (!entering_text || e.physical_key == gf::PhysicalKey::f1)) {
        const bool help_key =
            e.physical_key == gf::PhysicalKey::h || e.physical_key == gf::PhysicalKey::f1;
        const bool escape = e.physical_key == gf::PhysicalKey::escape;
        if (help_key || e.physical_key == gf::PhysicalKey::m ||
            ((help_open() || settings_open()) && escape)) {
            if (!e.repeat) {
                if (e.physical_key == gf::PhysicalKey::m)
                    toggle(0);
                else if (settings_open() && escape)
                    close_settings();
                else if (help_open())
                    close_help();
                else
                    show_help();
            }
            e.handled = true;
            return;
        }
    }
    // A game's key still works while help is open or a capsule button has the keyboard.
    const bool capsule_focus = focus && (*capsule_).contains(*focus);
    if (!entering_text && !settings_open() && (help_open() || capsule_focus) &&
        forward_game_key(e)) {
        e.handled = true;
        return;
    }
    if (help_open() || settings_open())
        return;
    gf::Control::on_key_preview(e);
}
void Collection::refresh_commands() {
    if (!view(active_))
        return;
    const EntryInfo& info = entry_info(active_);
    std::vector<GameCommand> commands;
    if (CommandSource* s = source(active_))
        commands = (*s).commands();
    struct Explanation {
        const std::vector<HelpTopic>& topics;
        bool operator()(const GameCommand& command) const {
            for (const HelpTopic& topic : topics) if (command.id == topic.id) return true;
            return command.id == "help" || command.id == "rules";
        }
    };
    std::erase_if(commands, Explanation{game_descriptor(active_).help_topics});
    struct Reserved {
        bool operator()(const GameCommand& command) const { return command.id == "settings"; }
    };
    std::erase_if(commands, Reserved{});
    (*capsule_).set_game(info.title, std::move(commands));
    // A game's values can change under the screen (a key, a command); show them.
    if (settings_open())
        (*settings_).refresh();
}
void Collection::run_command(const std::string& id) {
    // "settings" is the shell's own, like Help.
    if (id == "settings") {
        settings_open() ? close_settings() : show_settings();
        return;
    }
    bool explanation = id == "help" || id == "rules";
    for (const HelpTopic& topic : game_descriptor(active_).help_topics) explanation = explanation || id == topic.id;
    if (explanation) {
        show_help(id);
        return;
    }
    if (CommandSource* s = source(active_))
        (*s).run_command(id);
    refresh_commands();
    // Return keyboard focus to the game after a capsule click.
    if (attached_window() && view(active_))
        static_cast<void>((*attached_window()).request_focus(view(active_)));
}
// A plain key the active game names a command for (CommandSource::key_command): run
// as its view would run it, keyboard focus left where it is. True when the key was
// the game's.
bool Collection::forward_game_key(const gf::KeyEvent& event) {
    if (event.action != gf::KeyAction::down || event.modifiers != gf::Modifier::none)
        return false;
    CommandSource* s = source(active_);
    if (s == nullptr)
        return false;
    const std::string id = (*s).key_command(event.physical_key);
    if (id.empty())
        return false;
    if (!event.repeat) {
        (*s).run_command(id);
        refresh_commands();
    }
    return true;
}
bool Collection::overlay(const std::shared_ptr<gf::Control>& child) const {
    return child == capsule_ || child == help_ || child == help_link_ || child == settings_;
}
void Collection::visibility() {
    for (const std::shared_ptr<gf::Control>& child : children())
        if (!overlay(child))
            (*child).set_visible(false);
    (*view(active_)).set_visible(true);
    (*capsule_).set_visible(true);
    preferences();
    invalidate(gf::Dirty::layout | gf::Dirty::paint);
}
void Collection::arrange(gf::Rect b) {
    arrange_self(b);
    const gf::Rect full{0, 0, b.width, b.height};
    if (capsule_width_limit_ != b.width - 64) {
        capsule_width_limit_ = b.width - 64;
        (*capsule_).set_maximum_width(capsule_width_limit_);
    }
    const gf::Rect capsule_bounds = (*capsule_).placement({0, 0, b.width - 48, rail_height});
    current_rail_height_ = std::max(rail_height, capsule_bounds.y + capsule_bounds.height);
    const gf::Rect below{0, current_rail_height_, b.width,
                         std::max(0.0, b.height - current_rail_height_)};
    // Games have the whole window and the capsule floats over them, live surfaces included:
    // GUI.Forms repaints only the capsule's share of a live frame beneath it. A game whose
    // own controls sit where the capsule floats keeps a rail ("rail": true in GAME.json)
    // until they move.
    const bool on_rail = railed(active_);
    for (const std::shared_ptr<gf::Control>& child : children())
        if (!overlay(child) && (*child).visible())
            set_child_layout(child, on_rail ? below : full);
    gf::Rect capsule = (*capsule_).placement({0, 0, b.width - 48, on_rail ? current_rail_height_ : 60.0});
    if (on_rail)
        capsule.height = std::min(capsule.height, current_rail_height_ - capsule.y);
    set_child_layout(capsule_, capsule);
    // Help stands still at the top right.
    set_child_layout(help_link_, {b.width - 46, 8, 38, 38});
    const double paper_width = std::min(780.0, b.width - 32);
    set_child_layout(help_, {b.width - paper_width - 16, 16, paper_width, b.height - 32});
    set_child_layout(settings_, full);
}
void Collection::on_paint(gf::Painter& p, gf::Rect) {
    if (!railed(active_))
        return;
    // The rail above games that keep one: graphite with a gold hairline.
    const gf::Rect b = client_rectangle();
    fill_vertical(p, {0, 0, b.width, current_rail_height_}, gf::Color::rgba(49, 58, 73),
                  gf::Color::rgba(26, 31, 42));
    p.draw_line({0, current_rail_height_ - 1}, {b.width, current_rail_height_ - 1},
                gf::Color::rgba(255, 210, 122, 150), 1);
}
void Collection::preferences() {
    const SuiteSettings masters = SettingsStore::shared().values();
    reduced_ = masters.reduced;
    set_bus_gain(AudioBus::music, audio_gain(masters.music_volume));
    set_bus_gain(AudioBus::sound, audio_gain(masters.sound_volume));
    for (const std::pair<const Entry,std::unique_ptr<GameInstance>>& game : games_)
        (*game.second).preferences(active_ == game.first, masters.music, masters.sound, masters.reduced);
    if (settings_open())
        (*settings_).refresh();
}
void Collection::masters_changed() {
    const SuiteSettings masters = SettingsStore::shared().values();
    preferences();
    // Volumes need nothing more. A switch restarts or stops music
    // and lets the open game take up the new masters.
    if (masters.music == music_shown_ && masters.sound == sound_shown_ &&
        masters.reduced == reduced_shown_)
        return;
    music_shown_ = masters.music;
    sound_shown_ = masters.sound;
    reduced_shown_ = masters.reduced;
    // Music changes must not take keyboard focus out of help or settings.
    if (help_open() || settings_open()) {
        if (!hosted(active_))
            (*games_.at(active_)).activate();
    } else
        activate();
}
void Collection::activate() {
    wake();
    preferences();
    if (hosted(active_)) music_play("", false);
    (*games_.at(active_)).activate();
}
void Collection::back() {
    close_help();
    close_settings();
    CommandSource* s = source(active_);
    // At the bookcase there is nothing further back: the library is the whole shelf.
    if (s)
        static_cast<void>((*s).back());
    refresh_commands();
    if (attached_window() && view(active_))
        static_cast<void>((*attached_window()).request_focus(view(active_)));
}
void Collection::toggle(int which) {
    // The store saves and notifies; masters_changed() and every switch follow it.
    SettingsStore& store = SettingsStore::shared();
    if (which == 0)
        store.toggle_music();
    else if (which == 1)
        store.toggle_sound();
    else if (which == 2)
        store.toggle_reduced();
    else if (which == 3)
        settings_open() ? close_settings() : show_settings();
}
void Collection::show_settings() {
    close_help();
    const bool was_open = settings_open();
    (*settings_).show_for(std::string(entry_info(active_).title), source(active_));
    (*settings_).set_visible(true);
    model_.settings.set_checked(true);
    if (!was_open && attached_window()) {
        settings_focus_ = (*attached_window()).begin_focus_scope(settings_);
        static_cast<void>((*attached_window()).request_focus((*settings_).first_control()));
    }
    invalidate(gf::Dirty::layout | gf::Dirty::paint);
}
void Collection::close_settings() {
    if (!settings_open())
        return;
    if (attached_window() && settings_focus_)
        static_cast<void>((*attached_window()).end_focus_scope(settings_focus_));
    settings_focus_ = {};
    (*settings_).set_visible(false);
    model_.settings.set_checked(false);
    refresh_commands();
    invalidate(gf::Dirty::paint);
}
void Collection::clicked_help() {
    show_help();
}
void Collection::show_help(std::string_view topic) {
    close_settings();
    const bool was_open = help_open();
    (*help_).select(static_cast<int>(active_), topic);
    (*help_).set_visible(true);
    if (!was_open && attached_window())
        help_focus_ = (*attached_window()).begin_focus_scope(help_);
    invalidate(gf::Dirty::layout | gf::Dirty::paint);
}
void Collection::close_help() {
    if (!help_open())
        return;
    if (attached_window() && help_focus_)
        static_cast<void>((*attached_window()).end_focus_scope(help_focus_));
    help_focus_ = {};
    (*help_).set_visible(false);
    invalidate(gf::Dirty::paint);
}
} // namespace games
