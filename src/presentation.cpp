#include "presentation.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace games {
static gf::Color mix(gf::Color a, gf::Color b, double t) {
    return gf::Color::rgba(static_cast<unsigned char>(a.red * (1 - t) + b.red * t),
                           static_cast<unsigned char>(a.green * (1 - t) + b.green * t),
                           static_cast<unsigned char>(a.blue * (1 - t) + b.blue * t),
                           static_cast<unsigned char>(a.alpha * (1 - t) + b.alpha * t));
}
static void gradient(gf::Painter& p, gf::Rect r, gf::Color a, gf::Color b) {
    const gf::GradientStop stops[] = {{0, a}, {1, b}};
    p.fill_linear_gradient(r, {r.x, r.y}, {r.x, r.y + r.height}, stops);
}
gf::Color game_accent(int game) {
    const gf::Color colors[] = {gf::Color::rgba(213, 184, 113), gf::Color::rgba(125, 191, 238),
                                gf::Color::rgba(191, 145, 231), gf::Color::rgba(148, 205, 171),
                                gf::Color::rgba(119, 212, 221), gf::Color::rgba(129, 182, 225),
                                gf::Color::rgba(228, 172, 121), gf::Color::rgba(167, 219, 181),
                                gf::Color::rgba(243, 203, 100), gf::Color::rgba(192, 198, 142)};
    return colors[std::clamp(game, 0, 9)];
}
GameButton::GameButton(gf::StableId id, std::string label)
    : Button(std::move(id), std::move(label)) {
    set_font({gf::FontRole::content, 14, 600, false, .1});
    set_use_mnemonic(false);
    set_content_padding({9, 2, 9, 2});
}
void GameButton::set_skin(ButtonSkin skin) {
    skin_ = skin;
    invalidate(gf::Dirty::paint);
}
void GameButton::on_paint(gf::Painter& p, gf::Rect) {
    gf::Rect b = client_rectangle();
    gf::Rect r{1, 1, b.width - 2, b.height - 3};
    bool down = pressed_visual(), hot = hovered_visual(), chosen = selected(),
         light = skin_ == ButtonSkin::ivory || skin_ == ButtonSkin::blue;
    gf::Color top = light ? gf::Color::rgba(247, 246, 235) : gf::Color::rgba(56, 79, 99),
              bottom = light ? gf::Color::rgba(212, 218, 202) : gf::Color::rgba(30, 49, 69),
              border = light ? gf::Color::rgba(119, 145, 136) : gf::Color::rgba(82, 107, 126),
              ink = light ? gf::Color::rgba(39, 65, 60) : gf::Color::rgba(226, 236, 242);
    if (skin_ == ButtonSkin::blue) {
        top = gf::Color::rgba(244, 251, 255);
        bottom = gf::Color::rgba(197, 223, 241);
        border = gf::Color::rgba(113, 157, 191);
        ink = gf::Color::rgba(31, 81, 120);
    }
    if (skin_ == ButtonSkin::mint) {
        top = gf::Color::rgba(77, 109, 102);
        bottom = gf::Color::rgba(39, 68, 66);
        border = gf::Color::rgba(125, 161, 137);
    }
    if (chosen) {
        top = gf::Color::rgba(235, 212, 153);
        bottom = gf::Color::rgba(189, 150, 82);
        border = gf::Color::rgba(130, 100, 49);
        ink = gf::Color::rgba(45, 49, 49);
    }
    if (hot) {
        top = mix(top, gf::Color::rgba(245, 246, 229), .12);
        border = mix(border, gf::Color::rgba(181, 220, 225), .55);
    }
    if (down)
        std::swap(top, bottom);
    if (!enabled()) {
        top = mix(top, bottom, .5);
        ink = mix(ink, bottom, .55);
        border = mix(border, bottom, .4);
    }
    if (!down)
        p.draw_box_shadow(r, 5, {0, 2}, 2, 0, gf::Color::rgba(0, 8, 17, 70));
    p.save();
    p.clip_rounded_rect(r, 5);
    gradient(p, r, top, bottom);
    p.restore();
    p.stroke_rounded_rect(r, 5, border, 1);
    p.draw_line({r.x + 5, r.y + 1}, {r.x + r.width - 5, r.y + 1},
                gf::Color::rgba(255, 255, 255,
                                down    ? 20
                                : light ? 195
                                        : 53),
                1);
    p.draw_line({r.x + 4, r.y + r.height - 1}, {r.x + r.width - 4, r.y + r.height - 1},
                gf::Color::rgba(6, 20, 35, light ? 30 : 90), 1);
    paint_button_content(p, r, text(), ink, {down ? 1.0 : 0, down ? 1.0 : 0}, chosen);
    if (focus_cue_visible())
        p.stroke_rounded_rect(
            {4, 4, b.width - 8, b.height - 9}, 3,
            light ? gf::Color::rgba(45, 103, 131) : gf::Color::rgba(170, 216, 233), 1);
}
std::shared_ptr<const gf::Theme> games_theme(ButtonSkin skin) {
    gf::ThemeDefinition theme = gf::windows_professional_theme_definition();
    theme.id = "games-authored-" + std::to_string(static_cast<int>(skin));
    bool light = skin == ButtonSkin::blue || skin == ButtonSkin::ivory;
    theme.structure.typography.control = {gf::FontRole::content, 14, 400, false};
    theme.structure.typography.field = {gf::FontRole::content, 16, 400, false};
    theme.compatibility.text = light ? gf::Color::rgba(35, 63, 82) : gf::Color::rgba(229, 235, 241);
    theme.compatibility.border =
        light ? gf::Color::rgba(119, 156, 181) : gf::Color::rgba(87, 117, 138);
    for (std::size_t i = 0; i < gf::control_surface_state_count; ++i) {
        gf::ControlVisualRecipe& editor =
            theme.roles[static_cast<std::size_t>(gf::ControlVisualRole::editor)].ordinary[i];
        editor.material.fills = {gf::MaterialFillLayer::solid(light ? gf::Color::rgba(252, 253, 250)
                                                                    : gf::Color::rgba(12, 27, 43))};
        editor.material.corner_radius = 4;
        editor.material.border = gf::MaterialBorder{
            light ? gf::Color::rgba(115, 153, 184) : gf::Color::rgba(104, 147, 171), 1};
        editor.text = light ? gf::Color::rgba(26, 59, 83) : gf::Color::rgba(239, 240, 222);
        editor.focus_ring = gf::Color::rgba(140, 192, 222);
    }
    return gf::Theme::create(std::move(theme));
}
static double polygon_scale = 1;
void set_polygon_scale(double device_scale) {
    polygon_scale = std::clamp(device_scale, 1.0, 4.0);
}
namespace {
gf::Color shade_at(const PolygonShade& shade, double y) {
    const double t = std::clamp((y - shade.top) / std::max(1e-6, shade.bottom - shade.top), 0.0, 1.0);
    const std::array<double, 4>& at = shade.at;
    std::size_t i = 0;
    while (i < 2 && t > at[i + 1])
        ++i;
    const double f = std::clamp((t - at[i]) / std::max(1e-6, at[i + 1] - at[i]), 0.0, 1.0);
    const gf::Color a = shade.stops[i];
    const gf::Color b = shade.stops[i + 1];
    return gf::Color::rgba(static_cast<unsigned char>(std::lround(a.red + (b.red - a.red) * f)),
                           static_cast<unsigned char>(std::lround(a.green + (b.green - a.green) * f)),
                           static_cast<unsigned char>(std::lround(a.blue + (b.blue - a.blue) * f)),
                           static_cast<unsigned char>(std::lround(a.alpha + (b.alpha - a.alpha) * f)));
}
void fill_polygon_rows(gf::Painter& p, const std::vector<gf::Point>& points, gf::Color solid,
                       const PolygonShade* shade, bool smooth);
} // namespace
void paint_polygon(gf::Painter& p, const std::vector<gf::Point>& points, gf::Color color,
                   bool smooth) {
    fill_polygon_rows(p, points, color, nullptr, smooth);
}
void paint_polygon(gf::Painter& p, const std::vector<gf::Point>& points, const PolygonShade& shade) {
    fill_polygon_rows(p, points, shade.stops[0], &shade, true);
}
namespace {
// Fills one device row per scanline, so translucent fills never overlap themselves, and
// blends the partly covered pixel at each end of a span for smoother slanted edges.
void fill_polygon_rows(gf::Painter& p, const std::vector<gf::Point>& points, gf::Color solid,
                       const PolygonShade* shade, bool smooth) {
    if (points.size() < 3)
        return;
    double ymin = 100000, ymax = -100000;
    for (gf::Point q : points) {
        ymin = std::min(ymin, q.y);
        ymax = std::max(ymax, q.y);
    }
    const double step = 1 / polygon_scale;
    std::vector<double> intersections;
    for (double top = std::floor(ymin * polygon_scale) * step; top < ymax; top += step) {
        const double y = top + step * .5;
        const gf::Color color = shade ? shade_at(*shade, y) : solid;
        intersections.clear();
        for (std::size_t i = 0; i < points.size(); ++i) {
            gf::Point a = points[i], b = points[(i + 1) % points.size()];
            if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y))
                intersections.push_back(a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y));
        }
        std::sort(intersections.begin(), intersections.end());
        for (std::size_t i = 1; i < intersections.size(); i += 2) {
            const double x0 = intersections[i - 1], x1 = intersections[i];
            if (!smooth) {
                // Tiled shapes share edges; hard spans meet without blended seams.
                const double l = std::round(x0 * polygon_scale) * step,
                             r = std::round(x1 * polygon_scale) * step;
                if (r > l)
                    p.fill_rect({l, top, r - l, step}, color);
                continue;
            }
            const double inner0 = std::ceil(x0 * polygon_scale) * step,
                         inner1 = std::floor(x1 * polygon_scale) * step;
            if (inner1 <= inner0) {
                gf::Color c = color;
                c.alpha = static_cast<unsigned char>(
                    std::lround(color.alpha * std::clamp((x1 - x0) * polygon_scale, 0.0, 1.0)));
                p.fill_rect({std::floor(x0 * polygon_scale) * step, top, step, step}, c);
                continue;
            }
            p.fill_rect({inner0, top, inner1 - inner0, step}, color);
            gf::Color edge = color;
            edge.alpha = static_cast<unsigned char>(
                std::lround(color.alpha * (inner0 - x0) * polygon_scale));
            if (edge.alpha)
                p.fill_rect({inner0 - step, top, step, step}, edge);
            edge.alpha = static_cast<unsigned char>(
                std::lround(color.alpha * (x1 - inner1) * polygon_scale));
            if (edge.alpha)
                p.fill_rect({inner1, top, step, step}, edge);
        }
    }
}
} // namespace
void paint_dialog(gf::Painter& p, gf::Rect r, const std::string& title, gf::Color accent) {
    p.draw_box_shadow(r, 10, {0, 12}, 30, 0, gf::Color::rgba(0, 4, 12, 160));
    p.fill_rounded_rect(r, 10, gf::Color::rgba(25, 42, 61));
    p.save();
    p.clip_rounded_rect(r, 10);
    gradient(p, {r.x, r.y, r.width, 61}, gf::Color::rgba(57, 79, 97), gf::Color::rgba(31, 51, 72));
    p.restore();
    p.stroke_rounded_rect(r, 10, gf::Color::rgba(118, 142, 154), 1);
    p.draw_line({r.x + 1, r.y + 61}, {r.x + r.width - 1, r.y + 61}, gf::Color::rgba(15, 30, 47), 1);
    p.draw_text_utf8({r.x + 26, r.y + 41}, title, {gf::FontRole::content, 25, 700, false, .3},
                     gf::Color::rgba(240, 234, 209));
    p.draw_line({r.x + 27, r.y + 53}, {r.x + 71, r.y + 53}, accent, 2);
}
} // namespace games
