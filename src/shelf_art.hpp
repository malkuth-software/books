#pragma once
#include "presentation.hpp"
#include "suite.hpp"
#include <algorithm>
#include <cmath>
namespace games::shelf_art {
inline constexpr gf::Color rgb(int r,int g,int b,int a=255) { return gf::Color::rgba(r,g,b,a); }
inline void disc(gf::Painter& p, double cx, double cy, double r, gf::Color c) {
    p.fill_rounded_rect({cx - r, cy - r, 2 * r, 2 * r}, r, c);
}
// Suits are drawn as shapes so they never depend on font coverage.
inline void paint_suit(gf::Painter& p, double cx, double cy, double s, int suit, gf::Color c) {
    if (suit == 0) { // spade
        disc(p, cx - s * .24, cy + s * .02, s * .27, c);
        disc(p, cx + s * .24, cy + s * .02, s * .27, c);
        paint_polygon(
            p, {{cx - s * .5, cy + .02 * s}, {cx, cy - s * .55}, {cx + s * .5, cy + .02 * s}}, c);
        paint_polygon(p, {{cx, cy}, {cx - s * .2, cy + s * .5}, {cx + s * .2, cy + s * .5}}, c);
    } else if (suit == 1) { // heart
        disc(p, cx - s * .24, cy - s * .16, s * .27, c);
        disc(p, cx + s * .24, cy - s * .16, s * .27, c);
        paint_polygon(
            p, {{cx - s * .5, cy - s * .08}, {cx + s * .5, cy - s * .08}, {cx, cy + s * .52}}, c);
    } else if (suit == 2) { // diamond
        paint_polygon(
            p, {{cx, cy - s * .55}, {cx + s * .4, cy}, {cx, cy + s * .55}, {cx - s * .4, cy}}, c);
    } else { // club
        disc(p, cx, cy - s * .26, s * .24, c);
        disc(p, cx - s * .26, cy + s * .08, s * .24, c);
        disc(p, cx + s * .26, cy + s * .08, s * .24, c);
        paint_polygon(p, {{cx, cy}, {cx - s * .18, cy + s * .52}, {cx + s * .18, cy + s * .52}}, c);
    }
}
inline void paint_card(gf::Painter& p, gf::Rect r, int suit, const char* rank) {
    p.draw_box_shadow(r, r.width * .09, {1, 2}, 3, 0, rgb(0, 0, 0, 90));
    p.fill_rounded_rect(r, r.width * .09, rgb(252, 248, 236));
    p.stroke_rounded_rect(r, r.width * .09, rgb(120, 110, 92), 1);
    gf::Color ink = suit == 1 || suit == 2 ? rgb(184, 24, 46) : rgb(24, 26, 32);
    paint_suit(p, r.x + r.width * .5, r.y + r.height * .58, r.width * .46, suit, ink);
    p.draw_text_utf8({r.x + r.width * .1, r.y + r.height * .28}, rank,
                     {gf::FontRole::content, r.width * .3, 700, false}, ink);
}
inline void paint_card_back(gf::Painter& p, gf::Rect r, gf::Color c) {
    p.draw_box_shadow(r, r.width * .09, {1, 2}, 3, 0, rgb(0, 0, 0, 90));
    p.fill_rounded_rect(r, r.width * .09, rgb(250, 246, 234));
    gf::Rect inner{r.x + r.width * .1, r.y + r.width * .1, r.width * .8, r.height - r.width * .2};
    p.fill_rounded_rect(inner, r.width * .06, c);
    for (double d = -inner.height; d < inner.width; d += r.width * .18) {
        double x0 = inner.x + std::max(0.0, d), y0 = inner.y + std::max(0.0, -d);
        double len = std::min(inner.width - std::max(0.0, d), inner.height - std::max(0.0, -d));
        p.draw_line({x0, y0}, {x0 + len, y0 + len}, rgb(255, 255, 255, 55), 1);
    }
}
}
