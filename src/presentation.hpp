#pragma once
#include <array>
#include "gui_forms/basic_controls.hpp"
#include <memory>
#include <string>
#include <vector>
namespace games {
namespace gf = gui_forms;
enum class ButtonSkin { slate, ivory, blue, mint };
class GameButton : public gf::Button {
  public:
    GameButton(gf::StableId id, std::string text);
    // The skin is the button's class beside game-button (themes/playsuite styles
    // button.game-button.ivory and the rest).
    void set_skin(ButtonSkin skin);
    [[nodiscard]] gf::AnatomyStateSet anatomy_states() const override;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;

  private:
    ButtonSkin skin_ = ButtonSkin::ivory;
};
// The class a GameButton takes for its skin ("ivory", ...).
const char* skin_class(ButtonSkin skin);
// The class a subtree takes for its skin ("skin-ivory", ...): themes/playsuite gives its
// labels and editors the skin's text, fields and fonts (once games_theme's overrides).
const char* skin_subtree_class(ButtonSkin skin);
// Replaces `control`'s skin-* class with `skin`'s, keeping its other classes.
void set_skin_class(gf::Control& control, ButtonSkin skin);
gf::Color game_accent(int game);
// smooth blends the partly covered pixels at span ends; tiled shapes pass false.
void paint_polygon(gf::Painter& painter, const std::vector<gf::Point>& points, gf::Color color,
                   bool smooth = true);
// A vertical gradient for a polygon: four colors at the fractions `at` of the way from
// top to bottom (by default the house gloss, split at the middle).
struct PolygonShade {
    std::array<gf::Color, 4> stops{};
    double top = 0, bottom = 1;
    std::array<double, 4> at{0, .48, .52, 1};
};
void paint_polygon(gf::Painter& painter, const std::vector<gf::Point>& points,
                   const PolygonShade& shade);
// The window's device scale; polygon fills step one device row at a time.
void set_polygon_scale(double device_scale);
void paint_dialog(gf::Painter& painter, gf::Rect bounds, const std::string& title,
                  gf::Color accent);
} // namespace games
