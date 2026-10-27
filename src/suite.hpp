#pragma once
#include "gui_forms/basic_controls.hpp"
#include "game_entries.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace games {
namespace gf = gui_forms;

struct EntryInfo {
    const char* title;
    const char* kind;
    const char* blurb;
    gf::Color cover_top, cover_bottom, accent;
    int category; // the shelf's section, from GAME.json (tools/game_catalog.py's CATEGORIES)
};
const EntryInfo& entry_info(Entry entry);
// The shelf's sections, in its order: cards, logic and puzzles, matching, skill, and the
// quiet corner (the living scenes and the library).
inline constexpr int category_count = 5;
const char* category_name(int category);
const char* suite_name();

// A command the library offers in the capsule. Primary commands stay visible
// while the capsule is folded; the rest appear when it opens. Commands are actions
// (New game, Undo, Hint, a panel); a choice that persists is a GameSetting.
struct GameCommand {
    std::string id;
    std::string label;
    bool enabled = true;
    bool checked = false;
    bool primary = false;
    // A switch such as Pause: navy while off, gold while on (checked).
    bool toggle = false;
};
// One entry of a game's section on the shared Settings screen. The game owns and
// saves the value; the screen shows `value` and reports changes to change_setting().
struct GameSetting {
    enum class Kind { toggle, choice, slider };
    std::string id;
    std::string label;
    Kind kind = Kind::toggle;
    double value = 0;                 // toggle: 0 or 1; choice: index; slider: minimum..maximum
    std::vector<std::string> choices; // choice only, in order
    double minimum = 0, maximum = 1, step = .1; // slider only
    std::string note;                 // optional; e.g. "Changing it deals again."
};
// What the shell asks of the library, beyond its view. The library declares its
// commands and settings; the shell supplies the rest: Back, Help, the Music, Sound
// and Motion switches, and the Settings screen with the volume sliders.
class CommandSource {
  public:
    virtual ~CommandSource() = default;
    [[nodiscard]] virtual std::vector<GameCommand> commands() const = 0;
    virtual void run_command(std::string_view id) = 0;
    [[nodiscard]] virtual std::vector<GameSetting> settings() const {
        return {};
    }
    virtual void change_setting(std::string_view, double) {}
    // The command a plain key runs (empty for none), for a key the shell hears while
    // the game's own view does not have the keyboard: a capsule button focused, or help
    // open. The shell then runs it, as the view would have.
    [[nodiscard]] virtual std::string key_command(std::uint32_t) const {
        return {};
    }
    // The capsule's Back, asked of the game first: a game with a way back of its own
    // (a book closed to its library) takes it and returns true; false lets the shell
    // show its shelf.
    virtual bool back() {
        return false;
    }
};

gf::Color mix_color(gf::Color a, gf::Color b, double t);
gf::Color with_alpha(gf::Color c, int alpha);
void fill_vertical(gf::Painter& p, gf::Rect r, gf::Color top, gf::Color bottom);
void paint_entry_emblem(gf::Painter& p, gf::Rect bounds, Entry entry);

// The paymenottowork house command surface: split gloss, slate border, gold when chosen.
enum class GlossTone { chrome, gold, navy, smoke };
struct GlossState {
    bool hot = false, down = false, enabled = true, focus = false;
};
void paint_gloss(gf::Painter& p, gf::Rect r, double radius, GlossTone tone, GlossState state);
gf::Color gloss_ink(GlossTone tone, bool enabled = true);

enum class Glyph { none, back, play, music, sound, motion, more, settings };
void paint_glyph(gf::Painter& p, gf::Rect r, Glyph glyph, gf::Color ink, bool crossed = false);

// How a switch bound to a command shows the command's checked state (the button's
// selected state): slashed in red, or lit gold.
enum class SwitchLook { none, crossed_when_off, crossed_when_on, lit_when_on };

class SuiteButton : public gf::Button {
  public:
    SuiteButton(gf::StableId id, std::string text, GlossTone tone = GlossTone::chrome);
    void set_tone(GlossTone tone);
    void set_glyph(Glyph glyph, bool crossed = false);
    // A radius of SuiteButton::round paints a true circle centred in the button.
    static constexpr double round = 1e9;
    void set_radius(double radius);
    void set_checked(bool checked);
    // Makes this a switch: bind it to a command and it shows the command's state,
    // naming itself for assistive technology by whether it is on.
    void set_switch(SwitchLook look, std::string on_name, std::string off_name);
    [[nodiscard]] bool crossed() const;
    [[nodiscard]] bool checked() const;
    [[nodiscard]] double preferred_width() const;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;

  private:
    GlossTone tone_;
    Glyph glyph_ = Glyph::none;
    bool crossed_ = false, checked_ = false;
    double radius_ = 4;
    SwitchLook look_ = SwitchLook::none;
    std::string on_name_, off_name_;
    void switched(bool on);
};
} // namespace games
