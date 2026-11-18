#pragma once
#include "capsule_title_face.hpp"
#include "command_capsule_control.hpp"  // generated from controls/command-capsule.control
#include "suite.hpp"
#include "suite_model.hpp"
#include <functional>
#include <memory>
namespace games {

// The command capsule floats at the top of the library. Folded, it shows the way back
// (from a book to the bookcase), the library's name and its primary commands. It opens smoothly
// while the pointer rests on it (or when pinned with the "more" button), revealing the
// rest of the game's commands, the master music, sound and motion switches and the
// Settings button. The switches are bound to the shell's master commands: whatever
// changes a master, every switch shows it at once, slashed in red when off.
//
// The capsule is a GUI.Forms composite (controls/command-capsule.control, generated into
// CommandCapsuleControl by gui_forms_add_control): its pill is the document's themed part and
// its title the document's label, drawn by GUI.Forms' text engine in Barlow Condensed, loaded
// by the shell and borrowed here (CapsuleTitleFace; the owner admits app-loaded faces inside
// composites).
// This class adds what the document cannot say: the commands, which change with the game, the
// master switches, and the wrapping, opening layout.
class CommandCapsule final : public CommandCapsuleControl {
  public:
    // `title_family` is the shell's, which owns the family; the capsule copies its number.
    CommandCapsule(gf::StableId id, const CapsuleTitleFamily& title_family);
    static constexpr bool initialize_tree_after_construction = true;
    // Room round the pill for its soft shadow: the control is this much larger than the pill.
    static constexpr double shadow_margin = 14;
    void initialize_control_tree();
    void arrange(gf::Rect bounds) override;
    [[nodiscard]] bool hit_test_local(gf::Point local) const override;

    void set_game(std::string title, std::vector<GameCommand> commands);
    // Binds the switches to the masters; the model must outlive the capsule's use of it.
    void bind_masters(SuiteModel& model);
    // Widest the capsule may grow; it wraps its commands onto more rows beyond this.
    void set_maximum_width(double width);
    // The rectangle the capsule wants inside the given area, at the current opening.
    [[nodiscard]] gf::Rect placement(gf::Rect area) const;
    // The drawn pill at the current opening, excluding the shadow margin.
    [[nodiscard]] gf::Size pill_size() const;
    // Advances the opening animation; returns true while moving.
    bool step(double dt, bool pointer_inside, bool reduced);
    [[nodiscard]] bool open() const {
        return open_ > .001;
    }
    [[nodiscard]] bool unsettled(bool inside) const {
        return open_ != (inside || pinned_ ? 1.0 : 0.0);
    }
    void fold();

    std::function<void()> back;
    std::function<void(const std::string&)> command;

  private:
    CapsuleTitleFace title_face_;
    // The document's title label (@title), its font from title_face_.
    std::shared_ptr<gf::Label> title_label_;
    std::string title_;
    std::vector<GameCommand> commands_;
    std::shared_ptr<SuiteButton> back_, more_;
    std::vector<std::shared_ptr<SuiteButton>> buttons_;
    std::array<std::shared_ptr<SuiteButton>, 4> switches_{};
    double open_ = 0, idle_ = 0, max_width_ = 1000;
    bool pinned_ = false;
    struct Slot {
        std::shared_ptr<SuiteButton> button;
        gf::Rect rect;
        bool folded;
    };
    std::vector<Slot> slots_;
    gf::Size folded_size_{}, open_size_{};
    double title_width_ = 0;
    void layout_slots();
    void on_back();
    void on_more();
    void on_command(int index);
};
} // namespace games
