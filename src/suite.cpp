#include "suite.hpp"
#include "game_module.hpp"
#include "shelf_art.hpp"
#include "presentation.hpp"
#include <algorithm>
#include <cmath>
namespace games {
using shelf_art::disc;
namespace {
constexpr gf::Color rgb(int r, int g, int b, int a = 255) {
    return gf::Color::rgba(static_cast<unsigned char>(r), static_cast<unsigned char>(g),
                           static_cast<unsigned char>(b), static_cast<unsigned char>(a));
}
} // namespace
const EntryInfo& entry_info(Entry entry) { return game_descriptor(entry).info; }
const char* suite_name() {
    return "Pocket Library";
}
const char* category_name(int category) {
    static constexpr const char* names[category_count] = {"Cards", "Logic & puzzles", "Matching", "Skill", "Quiet corner"};
    return category >= 0 && category < category_count ? names[category] : "";
}
gf::Color mix_color(gf::Color a, gf::Color b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    struct MixChannel {
        double t;
        unsigned char operator()(unsigned char x, unsigned char y) const {
            return static_cast<unsigned char>(std::lround(x * (1 - t) + y * t));
        }
    };
    MixChannel m{t};
    return gf::Color::rgba(m(a.red, b.red), m(a.green, b.green), m(a.blue, b.blue),
                           m(a.alpha, b.alpha));
}
gf::Color with_alpha(gf::Color c, int alpha) {
    c.alpha = static_cast<unsigned char>(std::clamp(alpha, 0, 255));
    return c;
}
void fill_vertical(gf::Painter& p, gf::Rect r, gf::Color top, gf::Color bottom) {
    const gf::GradientStop stops[] = {{0, top}, {1, bottom}};
    p.fill_linear_gradient(r, {r.x, r.y}, {r.x, r.y + r.height}, stops);
}
gf::Color gloss_ink(GlossTone tone, bool enabled) {
    gf::Color ink = tone == GlossTone::gold    ? rgb(59, 42, 7)
                    : tone == GlossTone::navy  ? rgb(255, 255, 255)
                    : tone == GlossTone::smoke ? rgb(242, 244, 248)
                                               : rgb(23, 43, 83);
    return enabled ? ink : with_alpha(ink, 110);
}
void paint_gloss(gf::Painter& p, gf::Rect r, double radius, GlossTone tone, GlossState s) {
    std::array<gf::Color, 4> c{};
    gf::Color border{};
    switch (tone) {
    case GlossTone::chrome:
        c = {rgb(255, 255, 255), rgb(219, 227, 239), rgb(189, 201, 220), rgb(230, 235, 243)};
        border = rgb(100, 114, 139);
        if (s.hot)
            c = {rgb(255, 255, 255), rgb(238, 243, 255), rgb(215, 227, 255), rgb(255, 255, 255)};
        break;
    case GlossTone::gold:
        c = {rgb(255, 244, 214), rgb(255, 226, 160), rgb(242, 199, 102), rgb(255, 233, 180)};
        border = rgb(138, 106, 34);
        if (s.hot)
            c = {rgb(255, 250, 230), rgb(255, 234, 178), rgb(248, 210, 120), rgb(255, 240, 200)};
        break;
    case GlossTone::navy:
        c = {rgb(90, 127, 214), rgb(42, 78, 168), rgb(23, 52, 127), rgb(58, 95, 189)};
        border = rgb(10, 24, 64);
        if (s.hot)
            c = {rgb(112, 148, 232), rgb(58, 96, 190), rgb(34, 66, 150), rgb(76, 114, 208)};
        break;
    case GlossTone::smoke:
        c = {rgb(88, 100, 128, 235), rgb(58, 68, 92, 235), rgb(40, 48, 68, 235),
             rgb(52, 62, 84, 235)};
        border = rgb(255, 255, 255, 80);
        if (s.hot)
            c = {rgb(112, 126, 158, 245), rgb(76, 88, 116, 245), rgb(54, 64, 88, 245),
                 rgb(70, 82, 108, 245)};
        break;
    }
    if (s.down)
        c = {c[2], c[2], c[1], c[0]};
    if (!s.enabled)
        for (gf::Color& x : c)
            x = mix_color(x, rgb(150, 156, 168, x.alpha), .45);
    if (!s.down && s.enabled)
        p.draw_box_shadow(r, radius, {0, 2}, 2, 0, rgb(10, 18, 40, 70));
    p.save();
    p.clip_rounded_rect(r, radius);
    const gf::GradientStop stops[] = {{0, c[0]}, {.48, c[1]}, {.52, c[2]}, {1, c[3]}};
    p.fill_linear_gradient(r, {r.x, r.y}, {r.x, r.y + r.height}, stops);
    // A bright line a little under a flat top, fading out before the corners. A pill or a
    // circle has none: just under its rim it reads as a second, top-only border, and its
    // gradient already carries the shine.
    const bool rounded_end = radius * 2 >= std::min(r.width, r.height) * .8;
    const double inset = radius * .6 + 2, x0 = r.x + inset, x1 = r.x + r.width - inset;
    if (!rounded_end && x1 - x0 > 4) {
        const gf::Color shine = rgb(255, 255, 255,
                                    tone == GlossTone::smoke ? 40
                                    : s.down                 ? 30
                                                             : 150);
        const double fade = std::min(.25, 10 / (x1 - x0));
        const gf::GradientStop line[] = {{0, rgb(255, 255, 255, 0)},
                                         {fade, shine},
                                         {1 - fade, shine},
                                         {1, rgb(255, 255, 255, 0)}};
        p.fill_linear_gradient({x0, r.y + 2, x1 - x0, 1}, {x0, r.y}, {x1, r.y}, line);
    }
    p.restore();
    p.stroke_rounded_rect(r, radius, border, 1);
    if (s.hot || s.focus)
        p.stroke_rounded_rect({r.x - 2, r.y - 2, r.width + 4, r.height + 4}, radius + 2,
                              rgb(255, 210, 122), 2);
}
void paint_glyph(gf::Painter& p, gf::Rect r, Glyph glyph, gf::Color ink, bool crossed) {
    const double cx = r.x + r.width * .5, cy = r.y + r.height * .5, s = std::min(r.width, r.height);
    switch (glyph) {
    case Glyph::none:
        return;
    case Glyph::back:
        p.draw_line({cx + s * .12, cy - s * .28}, {cx - s * .16, cy}, ink, 2.4);
        p.draw_line({cx - s * .16, cy}, {cx + s * .12, cy + s * .28}, ink, 2.4);
        break;
    case Glyph::play:
        paint_polygon(
            p, {{cx - s * .18, cy - s * .26}, {cx + s * .26, cy}, {cx - s * .18, cy + s * .26}},
            ink);
        break;
    case Glyph::music:
        disc(p, cx - s * .14, cy + s * .2, s * .12, ink);
        disc(p, cx + s * .2, cy + s * .12, s * .12, ink);
        p.draw_line({cx - s * .03, cy + s * .2}, {cx - s * .03, cy - s * .3}, ink, 1.8);
        p.draw_line({cx + s * .31, cy + s * .12}, {cx + s * .31, cy - s * .36}, ink, 1.8);
        p.draw_line({cx - s * .03, cy - s * .3}, {cx + s * .31, cy - s * .36}, ink, 3);
        break;
    case Glyph::sound:
        p.fill_rect({cx - s * .32, cy - s * .1, s * .14, s * .2}, ink);
        paint_polygon(p,
                      {{cx - s * .2, cy - s * .1},
                       {cx + s * .02, cy - s * .3},
                       {cx + s * .02, cy + s * .3},
                       {cx - s * .2, cy + s * .1}},
                      ink);
        if (!crossed) {
            p.draw_line({cx + s * .14, cy - s * .12}, {cx + s * .2, cy}, ink, 1.6);
            p.draw_line({cx + s * .2, cy}, {cx + s * .14, cy + s * .12}, ink, 1.6);
            p.draw_line({cx + s * .24, cy - s * .24}, {cx + s * .34, cy}, ink, 1.6);
            p.draw_line({cx + s * .34, cy}, {cx + s * .24, cy + s * .24}, ink, 1.6);
        }
        break;
    case Glyph::motion:
        // A ball with speed lines trailing behind it.
        disc(p, cx + s * .18, cy, s * .17, ink);
        p.draw_line({cx - s * .36, cy - s * .16}, {cx - s * .04, cy - s * .16}, ink, 1.8);
        p.draw_line({cx - s * .26, cy}, {cx - s * .02, cy}, ink, 1.8);
        p.draw_line({cx - s * .36, cy + s * .16}, {cx - s * .04, cy + s * .16}, ink, 1.8);
        break;
    case Glyph::more:
        for (int i = -1; i <= 1; ++i)
            disc(p, cx + i * s * .22, cy, s * .07, ink);
        break;
    case Glyph::settings:
        // A cog: eight teeth around a ring.
        for (int i = 0; i < 8; ++i) {
            const double a = i * 3.14159265358979 / 4;
            p.draw_line({cx + std::cos(a) * s * .2, cy + std::sin(a) * s * .2},
                        {cx + std::cos(a) * s * .36, cy + std::sin(a) * s * .36}, ink, s * .13);
        }
        p.stroke_rounded_rect({cx - s * .2, cy - s * .2, s * .4, s * .4}, s * .2, ink, s * .11);
        break;
    }
    if (crossed)
        p.draw_line({cx - s * .34, cy + s * .34}, {cx + s * .34, cy - s * .34}, rgb(200, 40, 50),
                    2.4);
}
void paint_entry_emblem(gf::Painter& p,gf::Rect r,Entry entry) { game_descriptor(entry).cover(p,r); }
SuiteButton::SuiteButton(gf::StableId id, std::string text, GlossTone tone)
    : Button(std::move(id), std::move(text)), tone_(tone) {
    set_use_mnemonic(false);
    set_font({gf::FontRole::content, 13, 700, false, .2});
}
void SuiteButton::set_tone(GlossTone tone) {
    if (tone_ == tone)
        return;
    tone_ = tone;
    invalidate(gf::Dirty::paint);
}
void SuiteButton::set_glyph(Glyph glyph, bool crossed) {
    if (glyph_ == glyph && crossed_ == crossed)
        return;
    glyph_ = glyph;
    crossed_ = crossed;
    invalidate(gf::Dirty::paint);
}
void SuiteButton::set_radius(double radius) {
    if (radius_ == radius)
        return;
    radius_ = radius;
    invalidate(gf::Dirty::paint);
}
void SuiteButton::set_checked(bool checked) {
    // Hosts refresh command states often; unchanged states must not cause repaints.
    if (checked_ == checked)
        return;
    checked_ = checked;
    invalidate(gf::Dirty::paint);
}
void SuiteButton::set_switch(SwitchLook look, std::string on_name, std::string off_name) {
    const bool first = look_ == SwitchLook::none;
    look_ = look;
    on_name_ = std::move(on_name);
    off_name_ = std::move(off_name);
    if (first)
        gf::on(selected_changed(), *this, &SuiteButton::switched);
    switched(selected());
}
void SuiteButton::switched(bool on) {
    set_accessible_name(on ? on_name_ : off_name_);
    invalidate(gf::Dirty::paint);
}
bool SuiteButton::crossed() const {
    return crossed_ || (look_ == SwitchLook::crossed_when_off && !selected()) ||
           (look_ == SwitchLook::crossed_when_on && selected());
}
bool SuiteButton::checked() const {
    return checked_ || (look_ == SwitchLook::lit_when_on && selected());
}
double SuiteButton::preferred_width() const {
    const double chars = static_cast<double>(text().size());
    const double label = chars > 0 ? chars * 7.3 + 24 : 0;
    const double glyph = glyph_ != Glyph::none ? (label > 0 ? 22 : 34) : 0;
    return std::max(34.0, label + glyph);
}
void SuiteButton::on_paint(gf::Painter& p, gf::Rect) {
    // The face leaves room inside the control for the hover ring, 2 points out with a
    // 2-point stroke, so the ring is never cut off (a circle's top would look flat).
    gf::Rect b = client_rectangle();
    gf::Rect r{3, 3, b.width - 6, b.height - 6};
    double radius = std::min(radius_, r.height * .5);
    if (radius_ == round) {
        const double side = std::min(r.width, r.height);
        r = {(b.width - side) * .5, 3, side, side};
        radius = side * .5;
    }
    GlossTone tone = checked() ? GlossTone::gold : tone_;
    // A switch's selected state is its command's; other buttons show selection as focus.
    const bool selection_cue = look_ == SwitchLook::none && selected();
    paint_gloss(p, r, radius, tone,
                {hovered_visual(), pressed_visual(), enabled(), focus_cue_visible() || selection_cue});
    gf::Color ink = gloss_ink(tone, enabled());
    double x = r.x + 10;
    const double shift = pressed_visual() ? 1 : 0;
    if (glyph_ != Glyph::none) {
        double s = std::min(20.0, r.height - 8);
        double gx = text().empty() ? r.x + (r.width - s) * .5 : x;
        paint_glyph(p, {gx + shift, r.y + (r.height - s) * .5 + shift, s, s}, glyph_, ink,
                    crossed());
        x = gx + s + 4;
    }
    if (!text().empty()) {
        gf::FontSpec f = font();
        gf::Size m = p.measure_text_utf8(text(), f);
        double tx = glyph_ == Glyph::none ? r.x + (r.width - m.width) * .5 : x;
        p.draw_text_utf8({tx + shift, r.y + (r.height + f.size * .72) * .5 + shift}, text(), f,
                         ink);
    }
}
} // namespace games
