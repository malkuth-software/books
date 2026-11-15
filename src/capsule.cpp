#include "capsule.hpp"
#include <algorithm>
#include <cmath>
#include "stable_ids.hpp"
namespace games {
namespace {
constexpr double kPad = 5, kGap = 4, kButton = 32, kIcon = 34;
constexpr double kMargin = CommandCapsule::shadow_margin;
double ease(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3 - 2 * t);
}
// The title's size in points (Barlow Condensed Bold, as the hand-drawn lettering had it).
constexpr double kTitleSize = 19;
} // namespace
CommandCapsule::CommandCapsule(gf::StableId id, const CapsuleTitleFamily& title_family)
    : CommandCapsuleControl(std::move(id)), title_face_(title_family) {
    accessible_name = "Game commands";
    set_paint_plane(gf::PaintPlane::overlay);
}
void CommandCapsule::initialize_control_tree() {
    // The document's children (the title label) first, then the face it is drawn in.
    CommandCapsuleControl::initialize_control_tree();
    title_label_ = std::dynamic_pointer_cast<gf::Label>(child("title"));
    if (title_label_)
        (*title_label_).font = title_face_.font(kTitleSize);
    back_ = gf::make_control<SuiteButton>(gf::StableId("capsule.back"), "", GlossTone::smoke);
    (*back_).set_glyph(Glyph::back);
    (*back_).set_radius(SuiteButton::round);
    (*back_).accessible_name = "Back";
    add_child(back_);
    more_ = gf::make_control<SuiteButton>(gf::StableId("capsule.more"), "", GlossTone::smoke);
    (*more_).set_glyph(Glyph::more);
    (*more_).set_radius(SuiteButton::round);
    (*more_).accessible_name = "Keep the commands open";
    add_child(more_);
    for (int i = 0; i < 4; ++i) {
        switches_[i] = make_master_switch("capsule.", i);
        add_child(switches_[i]);
    }
    (*switches_[0]).visible = false;
    gf::on((*back_).clicked(), *this, &CommandCapsule::on_back);
    gf::on((*more_).clicked(), *this, &CommandCapsule::on_more);
}
void CommandCapsule::bind_masters(SuiteModel& model) {
    bind_master_switches(switches_, model);
}
void CommandCapsule::on_back() {
    if (back)
        back();
}
void CommandCapsule::on_more() {
    pinned_ = !pinned_;
    (*more_).set_checked(pinned_);
    if (!pinned_)
        idle_ = .3;
}
void CommandCapsule::on_command(int index) {
    if (command && index < static_cast<int>(commands_.size())) {
        const std::string id = commands_[static_cast<std::size_t>(index)].id;
        command(id);
    }
}
void CommandCapsule::set_game(std::string title, std::vector<GameCommand> commands) {
    bool same = title == title_ && commands.size() == commands_.size();
    for (std::size_t i = 0; same && i < commands.size(); ++i)
        same = commands[i].id == commands_[i].id && commands[i].label == commands_[i].label &&
               commands[i].primary == commands_[i].primary && commands[i].toggle == commands_[i].toggle;
    if (same) {
        for (std::size_t i = 0; i < commands.size(); ++i) {
            if ((*buttons_[i]).enabled.read() != commands[i].enabled)
                (*buttons_[i]).enabled = commands[i].enabled;
            (*buttons_[i]).set_checked(commands[i].checked);
        }
        commands_ = std::move(commands);
        return;
    }
    for (const std::shared_ptr<SuiteButton>& b : buttons_)
        static_cast<void>(remove_child((*b).runtime_id()));
    buttons_.clear();
    title_ = std::move(title);
    write_title(title_);
    commands_ = std::move(commands);
    for (const GameCommand& c : commands_) {
        std::shared_ptr<SuiteButton> b =
            gf::make_control<SuiteButton>(games::stable_id("capsule.cmd." + c.id), c.label,
                                          c.primary ? (c.toggle ? GlossTone::navy : GlossTone::gold)
                                                    : GlossTone::smoke);
        (*b).set_radius(15);
        (*b).font = gf::FontSpec{gf::FontRole::content, 13, 700, false, .2};
        (*b).enabled = c.enabled;
        (*b).set_checked(c.checked);
        (*b).accessible_name = c.label;
        add_child(b);
        gf::on((*b).clicked(), *this, &CommandCapsule::on_command, static_cast<int>(buttons_.size()));
        buttons_.push_back(std::move(b));
    }
    layout_slots();
    invalidate(gf::Dirty::layout | gf::Dirty::paint);
}
void CommandCapsule::set_maximum_width(double width) {
    max_width_ = std::max(200.0, width);
    layout_slots();
}
void CommandCapsule::fold() {
    pinned_ = false;
    (*more_).set_checked(false);
    open_ = 0;
    idle_ = 1;
}
void CommandCapsule::layout_slots() {
    slots_.clear();
    // The title label's own width, measured by GUI.Forms' text engine in its face.
    double title_text = 0;
    if (title_label_) {
        const gf::Size wanted = (*title_label_).measure({max_width_, kButton});
        title_text = wanted.width;
    }
    title_width_ = std::ceil(title_text) + 18;
    double x = kPad, y = kPad, right = 0;

    struct PlaceSlot {
        std::vector<Slot>& slots_;
        double max_width_;
        double& x;
        double& y;
        double& right;
        void operator()(const std::shared_ptr<SuiteButton>& b, double w, bool folded) const {
            if (!folded && x + w + kPad > max_width_ && x > kPad + kIcon + 1) {
                right = std::max(right, x - kGap);
                x = kPad + kIcon + kGap;
                y += kButton + kGap;
            }
            slots_.push_back({b, {kMargin + x, kMargin + y, w, kButton}, folded});
            x += w + kGap;
        }
    };
    PlaceSlot place{slots_, max_width_, x, y, right};
    place(back_, kIcon, true);
    x += title_width_ + kGap;
    for (std::size_t i = 0; i < commands_.size(); ++i)
        if (commands_[i].primary)
            place(buttons_[i], (*buttons_[i]).preferred_width(), true);
    place(more_, kIcon, true);
    folded_size_ = {x - kGap + kPad, kButton + 2 * kPad};
    for (std::size_t i = 0; i < commands_.size(); ++i)
        if (!commands_[i].primary)
            place(buttons_[i], (*buttons_[i]).preferred_width(), false);
    x += 8;
    // The library plays no music: the Music switch (the first) stays bound but is not placed.
    for (std::size_t i = 1; i < switches_.size(); ++i)
        place(switches_[i], kIcon, false);
    right = std::max(right, x - kGap);
    open_size_ = {std::max(folded_size_.width, right + kPad), y + kButton + kPad};
}
gf::Size CommandCapsule::pill_size() const {
    const double t = ease(open_);
    return {std::ceil(folded_size_.width + (open_size_.width - folded_size_.width) * t),
            std::ceil(folded_size_.height + (open_size_.height - folded_size_.height) * t)};
}
gf::Rect CommandCapsule::placement(gf::Rect area) const {
    const gf::Size pill = pill_size();
    const double w = pill.width, h = pill.height;
    // Expansion must not move Back or the primary commands away from a
    // pointer approaching them. Keep the same leading edge in both states.
    return {area.x + 8 - kMargin, area.y + 6 - kMargin, std::ceil(w) + 2 * kMargin,
            std::ceil(h) + 2 * kMargin};
}
bool CommandCapsule::step(double dt, bool inside, bool reduced) {
    if (inside || pinned_)
        idle_ = 0;
    else
        idle_ += dt;
    // Opening is immediate on hover; folding waits a moment so passing pointers don't flicker it.
    const double target = inside || pinned_ || (open_ > 0 && idle_ < .45) ? 1.0 : 0.0;
    double next = reduced ? target : open_ + std::clamp(target - open_, -dt * 5.5, dt * 6.5);
    if (std::abs(next - open_) < 1e-6)
        return false;
    open_ = next;
    return true;
}
void CommandCapsule::arrange(gf::Rect bounds) {
    for (const std::shared_ptr<gf::Control>& child : children())
        if ((*child).paint_plane() != gf::PaintPlane::overlay)
            (*child).set_paint_plane(gf::PaintPlane::overlay);
    // The composite lays out its parts (the pill) and its document children; the commands and
    // the title are placed here.
    CommandCapsuleControl::arrange(bounds);
    layout_slots();
    if (title_label_)
        set_child_layout(title_label_, {kMargin + kPad + kIcon + kGap + 9, kMargin + kPad, title_width_ - 18, kButton});
    // The pill, not the control bounds, decides what shows: a host may crop the shadow margin.
    const gf::Size pill = pill_size();
    for (const Slot& s : slots_) {
        const bool inside = s.rect.x + s.rect.width <= kMargin + pill.width - kPad + .5 &&
                            s.rect.y + s.rect.height <= kMargin + pill.height - kPad + .5;
        // Written only on a change, so a settled capsule (open under the pointer, or folded)
        // leaves its buttons untouched between a press and its release.
        if ((*s.button).visible.read() != inside)
            (*s.button).visible = inside;
        set_child_layout(s.button, s.rect);
    }
}
bool CommandCapsule::hit_test_local(gf::Point local) const {
    const gf::Size pill = pill_size();
    return local.x >= kMargin && local.y >= kMargin && local.x < kMargin + pill.width &&
           local.y < kMargin + pill.height;
}
} // namespace games
