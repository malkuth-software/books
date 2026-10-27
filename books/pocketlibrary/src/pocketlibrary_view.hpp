#pragma once
// The Pocket Library as a PlaySuite control. This is the only file that knows about
// GUI.Forms and the shell. It shows the bookcase or an open book, turns input into
// choices and page turns, keeps the reader's place in every book, and publishes each
// finished picture to a LiveSurface.
//
// Nothing moves on its own: a frame is drawn when something changes, and an untouched
// library schedules nothing at all.
#include "suite.hpp"

#include "library.hpp"
#include "platform/raster.hpp"
#include "printer.hpp"
#include "reader.hpp"
#include "shelf_scene.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/threading.hpp"
#include "gui_forms/timer.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pl {
namespace gf = gui_forms;

struct Options {
    bool hosted = false;  // inside PlaySuite: the capsule carries the commands
    bool dev = false;     // a separate bookmarks file, for tests and development
};

class PocketLibraryView final : public gf::Control, public games::CommandSource {
  public:
    PocketLibraryView(gf::StableId id, Options options);
    ~PocketLibraryView() override;
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    void on_pointer(gf::PointerEvent& event) override;
    void on_key(gf::KeyEvent& event) override;
    void on_key_bubble(gf::KeyEvent& event) override {
        on_key(event);
    }

    // The shell calls these. activate: the game has just been shown; take the keyboard.
    void activate();
    // foreground false means the shelf or another game is showing: stop everything.
    void set_cabinet(bool foreground, bool music, bool sound, bool reduced = false);
    [[nodiscard]] std::vector<games::GameCommand> commands() const override;
    void run_command(std::string_view id) override;
    [[nodiscard]] std::vector<games::GameSetting> settings() const override;
    void change_setting(std::string_view id, double value) override;
    // The capsule's Back closes an open book to the library; on the bookcase it is the
    // shell's.
    bool back() override;

  private:
    Options options_;
    Library library_;
    Printer printer_;
    WoodTiles wood_;
    Reader reader_;
    Bookmarks marks_;
    ShelfLayout shelf_;
    int hover_ = -1;   // the book under the pointer
    int cursor_ = -1;  // the book chosen with the keyboard
    // The search over the bookcase: the field, what it holds, and which books it keeps
    // (one flag a shelf book; empty while nothing is sought).
    std::shared_ptr<gf::TextBox> search_;
    std::string query_;
    std::vector<bool> shown_;
    double scroll_ = 0;  // how far the bookcase is scrolled, in points
    page::TypeStep step_ = page::TypeStep::medium;
    Canvas frame_;
    std::shared_ptr<gf::LiveSurface> surface_;
    std::unique_ptr<gf::Timer> timer_;
    std::vector<gf::SubscriptionToken> subscriptions_;
    double scale_ = 1;
    double width_ = 0;  // the view, in points
    double height_ = 0;
    int device_width_ = 0;
    int device_height_ = 0;
    bool render_dirty_ = true;
    bool direct_ = false;
    bool front_ = true;
    bool help_ = false;  // the game's own help card is showing
    bool about_ = false;  // the open book's About card (its edition and licence) is showing
    unsigned turns_ = 0;  // pages turned, to vary each one's sound
    std::chrono::steady_clock::time_point last_tick_ = std::chrono::steady_clock::now();
    // Decodes the library's base while the shelf is shown; joined before a book is read.
    std::unique_ptr<gf::Worker> preparing_;

    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void tick();
    void request_frame();
    void render();
    void publish();
    void describe();

    static void prepare_library(const gf::CancellationFlag& cancellation, void* context);
    void finish_preparing() noexcept;
    void open_book(int index);
    void close_book();
    void turn(bool forward);
    void rustle(bool forward);
    void wake_audio();
    void to_contents(bool shown);
    void move_cursor(int step);
    void move_plank(int planks);
    void scroll_shelf(double points);
    void set_help(bool open);
    void set_about(bool open);
    [[nodiscard]] const ShelfBook* open_shelf_book() const;
    void searched(const std::string& text);
    void search_committed(const std::string& text);
    void search_cancelled();
    void focus_search();
    void lay_out_bookcase();
    void place_search();
    [[nodiscard]] bool search_focused() const;
    [[nodiscard]] bool on_shelf(int book) const;
    [[nodiscard]] int first_on_shelf() const;
    void apply_look();
    void persist();
    [[nodiscard]] std::string_view save_name() const;
};

}  // namespace pl
