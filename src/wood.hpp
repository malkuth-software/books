#pragma once
// Wood for the shelf, made once: long grain bent by slow noise, darker latewood
// streaks, fine pores along the grain and now and then a knot. The pictures tile
// without seams, so a pattern fill covers any wall or plank.
#include "gui_forms/basic_controls.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>
namespace games {
namespace gf = gui_forms;

struct WoodLook {
    gf::Color light, dark;  // earlywood and latewood
    double rings = 9;       // streaks across the picture
    double bend = 1.6;      // how far the slow noise bends them, in rings
    int knots = 1;
    std::uint32_t seed = 1;
};

// Premultiplied BGRA, w * h, rows of w * 4 bytes. With `along_x` the grain runs
// across the picture (a plank's front), else down it (a wall's boards).
std::vector<std::byte> make_wood(int w, int h, bool along_x, const WoodLook& look);

// The shelf's two woods, registered with a window and released with it.
class ShelfWood {
  public:
    void make(gf::Window& window);
    void release(gf::Window& window);
    [[nodiscard]] bool ready() const {
        return wall.value != 0 && plank.value != 0;
    }
    // Logical size of each tile; the pictures are made at the window's scale.
    static constexpr double wall_w = 184, wall_h = 368, plank_w = 420, plank_h = 40;
    gf::ImageId wall{}, plank{};
    gf::Size wall_pixels{}, plank_pixels{};
};
} // namespace games
