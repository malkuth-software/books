#include "shelf_art.hpp"

namespace games::modules {
// The box's emblem: three books standing on a shelf, bound in black leather, blue
// cloth and green cloth, each with a gilt band.
void pocketlibrary_cover(gf::Painter& painter, gf::Rect bounds) {
    const double size = std::min(bounds.width, bounds.height);
    const double base = bounds.y + bounds.height * .5 + size * .3;
    const double left = bounds.x + bounds.width * .5 - size * .3;
    const gf::Color bindings[3] = {gf::Color::rgba(27, 23, 21), gf::Color::rgba(31, 42, 68),
                                   gf::Color::rgba(47, 74, 51)};
    const double widths[3] = {.2, .16, .13};
    const double heights[3] = {.62, .54, .48};
    const gf::Color gilt = gf::Color::rgba(216, 178, 98);
    double x = left;
    for (int n = 0; n < 3; ++n) {
        const double w = size * widths[n];
        const double h = size * heights[n];
        painter.fill_rounded_rect({x, base - h, w, h}, size * .015, bindings[n]);
        painter.fill_rect({x + w * .15, base - h * .82, w * .7, size * .012}, gilt);
        painter.fill_rect({x + w * .15, base - h * .22, w * .7, size * .012}, gilt);
        x += w + size * .02;
    }
    painter.fill_rect({left - size * .06, base, size * .72, size * .05}, gf::Color::rgba(168, 116, 70));
}
}  // namespace games::modules
