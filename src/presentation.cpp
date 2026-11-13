#include "presentation.hpp"
#include "suite.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace games {
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
const char* skin_class(ButtonSkin skin) {
    switch (skin) {
    case ButtonSkin::ivory:
        return "ivory";
    case ButtonSkin::blue:
        return "blue";
    case ButtonSkin::mint:
        return "mint";
    case ButtonSkin::slate:
        break;
    }
    return "slate";
}
const char* skin_subtree_class(ButtonSkin skin) {
    switch (skin) {
    case ButtonSkin::ivory:
        return "skin-ivory";
    case ButtonSkin::blue:
        return "skin-blue";
    case ButtonSkin::mint:
        return "skin-mint";
    case ButtonSkin::slate:
        break;
    }
    return "skin-slate";
}
void set_skin_class(gf::Control& control, ButtonSkin skin) {
    std::vector<std::string> names = control.classes.read();
    std::vector<std::string> kept;
    kept.reserve(names.size() + 1);
    for (const std::string& name : names) {
        if (name.rfind("skin-", 0) != 0)
            kept.push_back(name);
    }
    kept.push_back(skin_subtree_class(skin));
    control.classes = std::move(kept);
}
GameButton::GameButton(gf::StableId id, std::string label)
    : Button(std::move(id), std::move(label)) {
    font = gf::FontSpec{gf::FontRole::content, 14, 600, false, .1};
    use_mnemonic = false;
    content_padding = gf::Insets{9, 2, 9, 2};
    classes = std::vector<std::string>{"game-button", skin_class(skin_)};
}
void GameButton::set_skin(ButtonSkin skin) {
    if (skin_ == skin)
        return;
    skin_ = skin;
    classes = std::vector<std::string>{"game-button", skin_class(skin_)};
    invalidate(gf::Dirty::paint);
}
gf::AnatomyStateSet GameButton::anatomy_states() const {
    // Chosen (selected) is the theme's :checked: gold whatever the skin.
    gf::AnatomyStateSet states = gf::Button::anatomy_states();
    if (selected.read())
        states.insert(gf::AnatomyState::checked);
    return states;
}
void GameButton::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Rect b = client_rectangle();
    const gf::Rect r{1, 1, b.width - 2, b.height - 3};
    const gf::AnatomyStateSet states = anatomy_states();
    const bool down = pressed_visual();
    // The face's rim was a 1-point stroke centred on r (radius 5) and its highlight and
    // lowlight lines a point inside it; as the theme's border and first keyline ring,
    // inside the part, that is r grown by half a point.
    paint_skin_part(p, *this, gf::PartRole::face, outset(r, .5), states, 5.5);
    const ButtonContentInk ink{gf::ControlVisualRole::button, states, std::nullopt};
    paint_button_content(p, r, text.read(), ink, {down ? 1.0 : 0, down ? 1.0 : 0}, selected.read());
    // The focus ring was a 1-point stroke centred on {4, 4, w - 8, h - 9}, radius 3.
    if (focus_cue_visible())
        paint_skin_part(p, *this, gf::PartRole::focus, {3.5, 3.5, b.width - 7, b.height - 8}, states, 3.5);
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
