#pragma once
#include "capsule.hpp"
#include "game_module.hpp"
#include "gui_forms/timer.hpp"
#include "gui_forms/window.hpp"
#include "help_book.hpp"
#include "help_route.hpp"
#include "presentation.hpp"
#include "settings_sheet.hpp"
#include "suite_settings.hpp"
#include "text_sprites.hpp"
namespace games {

// The Pocket Library's shell, PlaySuite's with one volume on its shelf: the library's
// view filling the window, the command capsule floating over it, Help and the Settings
// screen. The masters live in the SettingsStore; the shell observes it and hands every
// change to the library and to the audio volumes, wherever the change came from.
class Collection final : public gf::Control, public HelpHost {
  public:
    explicit Collection(gf::StableId id, bool dev = false);
    ~Collection() override;
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    void on_pointer_preview(gf::PointerEvent& event) override;
    void on_key_preview(gf::KeyEvent& event) override;
    void activate();
    // The capsule's Back: the library's own step back (a book closed to the bookcase).
    void back();
    void show_help(std::string_view topic = {}) override;
    void close_help();
    void show_settings();
    void close_settings();
    [[nodiscard]] bool settings_open() const {
        return settings_ && (*settings_).visible();
    }
    bool dispatch_action(std::string_view action) { return (*games_.at(active_)).scripted_action(action); }
    void dispatch_command(const std::string& id) { run_command(id); }
    [[nodiscard]] bool help_open() const {
        return help_ && (*help_).visible();
    }
    [[nodiscard]] Entry active() const {
        return active_;
    }
    // Space reserved above games that present their own live surfaces.

  private:
    TextSprites sprites_;
    SuiteModel model_; // the masters every switch, check box and slider binds to
    std::shared_ptr<CommandCapsule> capsule_;
    static constexpr double rail_height = 50;
    double current_rail_height_ = rail_height;
    std::shared_ptr<HelpGlyph> help_link_;
    std::shared_ptr<HelpBook> help_;
    gf::FocusScopeId help_focus_{};
    std::shared_ptr<SettingsSheet> settings_;
    gf::FocusScopeId settings_focus_{};
    SettingsObservation masters_{};
    bool music_shown_ = true, sound_shown_ = true, reduced_shown_ = false;
    ModuleContext modules_;
    std::map<Entry,std::unique_ptr<GameInstance>> games_;
    std::unique_ptr<gf::Timer> timer_{};
    std::chrono::steady_clock::time_point last_tick_{};
    double refresh_ = 0;
    double quiet_ = 0;  // seconds since the last input or capsule motion
    Entry active_ = entries.front();
    bool reduced_ = false;
    gf::Point pointer_{-1000, -1000};
    double capsule_width_limit_ = 0;
    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void tick();
    void wake();
    void ensure_view(Entry entry);
    void visibility();
    void preferences();
    void masters_changed();
    [[nodiscard]] bool overlay(const std::shared_ptr<gf::Control>& child) const;
    void refresh_commands();
    void run_command(const std::string& id);
    bool forward_game_key(const gf::KeyEvent& event);
    void toggle(int which);
    void clicked_help();
    // A hosted game draws its own live surface below the rail and plays its own music.
    [[nodiscard]] bool hosted(Entry entry) const;
    // A game whose top edge the floating capsule would cover keeps a rail for it.
    [[nodiscard]] bool railed(Entry entry) const;
    [[nodiscard]] std::shared_ptr<gf::Control> view(Entry entry) const;
    [[nodiscard]] CommandSource* source(Entry entry) const;
};
} // namespace games
