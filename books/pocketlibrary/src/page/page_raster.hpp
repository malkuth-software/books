#pragma once

#include "page/page_layout.hpp"
#include "page/font_set.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace pl::page {

// Where pixels are written: premultiplied BGRA, 8 bits per channel, rows top
// to bottom, `row_bytes` apart. The memory is the caller's (in the window, a
// live surface's buffer) and every pixel written is opaque.
struct PixelTarget final {
    std::uint8_t* pixels{nullptr};
    std::size_t row_bytes{0U};
    std::uint32_t width{0U};
    std::uint32_t height{0U};
};

// A page's place on the target, in device pixels. The page is drawn at one
// layout unit to height / page_height_lu pixels; the width should be the
// height times the page's proportions.
struct PageRect final {
    std::int32_t x{0};
    std::int32_t y{0};
    std::uint32_t width{0U};
    std::uint32_t height{0U};
};

// The largest page that keeps its proportions inside `width` by `height`
// device pixels (R020 A), two to the spread or one when folded.
struct SpreadGeometry final {
    bool folded{false};
    PageRect left{};
    PageRect right{};
};

// The paper a page is printed on: premultiplied BGRA (opaque), exactly the page's
// size in device pixels, rows `row_bytes` apart. Made by the paper engine.
struct PaperImage final {
    const std::uint8_t* pixels{nullptr};
    std::size_t row_bytes{0U};
    std::uint32_t width{0U};
    std::uint32_t height{0U};
};

// A window narrower than it is tall shows one page; otherwise a spread.
[[nodiscard]] SpreadGeometry fit_spread(std::uint32_t width, std::uint32_t height) noexcept;

// The inks a page is printed in. The default is the day's: warm black, the words of
// Christ in red, callers in the accent. A dark paper takes light inks.
struct InkPalette final {
    Rgb body{ink_rgb};
    Rgb red{red_rgb};
    Rgb accent{0x7aU, 0x2eU, 0x22U};
};

struct RasterCounters final {
    std::uint64_t glyphs_drawn{0U};
    std::uint64_t glyph_cache_misses{0U};
    std::uint64_t glyph_cache_entries{0U};
    // Coverage bytes held by the glyph cache.
    std::uint64_t glyph_cache_bytes{0U};
};

// Draws pages to pixels in the toolkit's text profile: FreeType light auto-hinting,
// greyscale, horizontal phase kept to 1/64 pixel, coverage blended over the paper in
// sRGB values with gamma 1.0. Holds
// the glyph-coverage cache. The FontSet is borrowed and must outlive the
// rasterizer.
class PageRasterizer final {
  public:
    explicit PageRasterizer(const FontSet& fonts);

    // Fills the whole target with one colour.
    void fill(const PixelTarget& target, Rgb color) const;
    // Lays the paper over the page's rectangle (a flat paper colour when `paper` is
    // empty or not the page's size) and prints the page on it.
    void draw_page(const PageLayout& page, const PixelTarget& target, PageRect rect, const PaperImage& paper);

    // The inks every later page is printed in (the gilt stays gold).
    void set_palette(const InkPalette& palette) noexcept {
        palette_ = palette;
    }
    [[nodiscard]] const InkPalette& palette() const noexcept {
        return palette_;
    }

    // Drops every cached glyph: after the window changes size the old
    // sizes are not wanted again.
    void clear_cache();

    [[nodiscard]] const RasterCounters& counters() const noexcept {
        return counters_;
    }

  private:
    struct CachedGlyph final {
        std::int32_t left{0};
        std::int32_t top{0};
        std::uint32_t width{0U};
        std::uint32_t height{0U};
        // Offset of the coverage rows in coverage_.
        std::size_t offset{0U};
    };

    [[nodiscard]] const CachedGlyph* cached_glyph(FaceId face, std::uint32_t size_26_6, std::uint32_t glyph,
                                                  std::uint32_t phase_x, std::uint32_t quarter_y,
                                                  std::int32_t embolden_26_6);
    void blend_coverage(const PixelTarget& target, PageRect clip, std::int32_t left, std::int32_t top,
                        const CachedGlyph& glyph, Ink ink) const;
    // A rectangle with edges in 1/64 pixel, antialiased by area.
    void blend_rectangle(const PixelTarget& target, PageRect clip, std::int64_t x0, std::int64_t y0,
                         std::int64_t x1, std::int64_t y1, Ink ink) const;
    void blend_pixel(std::uint8_t* pixel, std::uint32_t coverage, Ink ink) const noexcept;
    struct InkValue final {
        Rgb color{};
        std::uint32_t alpha{255U};
    };
    [[nodiscard]] InkValue ink_value(Ink ink) const noexcept;

    const FontSet* fonts_;
    std::unordered_map<std::uint64_t, CachedGlyph> glyphs_{};
    std::vector<std::uint8_t> coverage_{};
    RasterCounters counters_{};
    InkPalette palette_{};
};

}  // namespace pl::page
