#include "paper_sheet.hpp"
#include "gui_forms/window.hpp"
#include "paper.hpp"
#include <cmath>
#include <cstddef>
#include <vector>
namespace games {
namespace {
// The tile's side in points. Large enough that its repeat is not found by eye on a
// help page, small enough to make in a millisecond or two.
constexpr int kTilePoints = 256;
} // namespace
gf::Color PaperSheet::colour() {
    return gf::Color::rgba(255, 252, 215);
}
void PaperSheet::prepare(gf::Window& window) {
    const double scale = std::max(0.5, window.scale());
    if (image_.value && scale == scale_)
        return;
    release(window);
    // Uncoated book paper in the sheet's own cream: a flat fibre mat, no lit relief.
    paper::Parameters p = paper::preset(paper::Preset::book);
    p.pixels_per_point = scale;
    p.colour = {255.0 / 255.0, 252.0 / 255.0, 215.0 / 255.0};
    p.seed = 1909;
    const int side = static_cast<int>(std::lround(kTilePoints * scale));
    paper::Image tile;
    if (!paper::generate_tile(p, side, side, tile))
        return;
    // Opaque, so premultiplied BGRA is the same bytes reordered.
    const std::size_t count = static_cast<std::size_t>(tile.width) * static_cast<std::size_t>(tile.height);
    std::vector<std::byte> pixels(count * 4);
    for (std::size_t i = 0; i < count; ++i) {
        pixels[4 * i] = static_cast<std::byte>(tile.rgba[4 * i + 2]);
        pixels[4 * i + 1] = static_cast<std::byte>(tile.rgba[4 * i + 1]);
        pixels[4 * i + 2] = static_cast<std::byte>(tile.rgba[4 * i]);
        pixels[4 * i + 3] = std::byte{255};
    }
    const gf::ImageLoadResult loaded = window.load_bgra32_premultiplied(
        static_cast<std::uint32_t>(tile.width), static_cast<std::uint32_t>(tile.height),
        static_cast<std::uint64_t>(tile.width) * 4, pixels);
    if (!loaded)
        return;
    image_ = loaded.image;
    scale_ = scale;
    pixels_ = side;
}
void PaperSheet::release(gf::Window& window) noexcept {
    if (image_.value)
        static_cast<void>(window.remove_image(image_));
    image_ = {};
    scale_ = 0;
    pixels_ = 0;
}
void PaperSheet::paint(gf::Painter& p, gf::Rect bounds) const {
    p.fill_rect(bounds, colour());
    if (image_.value)
        p.fill_image_pattern(image_, {static_cast<double>(pixels_), static_cast<double>(pixels_)}, bounds,
                             {static_cast<double>(kTilePoints), static_cast<double>(kTilePoints)});
}
} // namespace games
