#include "page/page_raster.hpp"

#include <algorithm>
#include <cstring>

namespace pl::page {
namespace {

constexpr Rgb gilt_rgb{0xd8U, 0xb2U, 0x62U};
// Verse numerals, notes and the running head are the mockup's ink at four
// fifths (its opacity of .8 and .85).
constexpr std::uint32_t soft_alpha = 204U;

// The page's proportions as a ratio of whole numbers: 715 wide to 1000 high.
constexpr std::uint64_t page_ratio_width = 715U;
constexpr std::uint64_t page_ratio_height = 1000U;

// Layout units to 1/64 device pixel on a page `page_height` pixels high,
// rounded to nearest; exact for negative values too.
std::int64_t to_26_6(std::int64_t layout_units, std::uint32_t page_height) noexcept {
    const std::int64_t scaled = layout_units * static_cast<std::int64_t>(page_height) * 64;
    const std::int64_t half = page_height_lu / 2;
    const std::int64_t rounded = scaled >= 0 ? (scaled + half) / page_height_lu : -((-scaled + half) / page_height_lu);
    return rounded;
}

std::int64_t floor_shift_6(std::int64_t value) noexcept {
    // An arithmetic shift is floor division by 64 for negative values too.
    const std::int64_t whole = value >> 6;
    return whole;
}

PageRect page_rect_of(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) noexcept {
    PageRect rect;
    rect.x = static_cast<std::int32_t>(x);
    rect.y = static_cast<std::int32_t>(y);
    rect.width = width;
    rect.height = height;
    return rect;
}

} // namespace

SpreadGeometry fit_spread(std::uint32_t width, std::uint32_t height) noexcept {
    SpreadGeometry geometry;
    geometry.folded = width < height;
    const std::uint64_t pages = geometry.folded ? 1U : 2U;
    // The page height that makes `pages` pages exactly as wide as the target.
    const std::uint64_t height_for_width =
        static_cast<std::uint64_t>(width) * page_ratio_height / (page_ratio_width * pages);
    const std::uint32_t page_height = static_cast<std::uint32_t>(std::min<std::uint64_t>(height, height_for_width));
    const std::uint32_t page_width = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(page_height) * page_ratio_width + page_ratio_height / 2U) / page_ratio_height);
    const std::uint32_t used = page_width * static_cast<std::uint32_t>(pages);
    const std::uint32_t left = used < width ? (width - used) / 2U : 0U;
    const std::uint32_t top = page_height < height ? (height - page_height) / 2U : 0U;
    geometry.left = page_rect_of(left, top, page_width, page_height);
    if (!geometry.folded) {
        geometry.right = page_rect_of(left + page_width, top, page_width, page_height);
    }
    return geometry;
}

PageRasterizer::PageRasterizer(const FontSet& fonts) : fonts_(&fonts) {}

PageRasterizer::InkValue PageRasterizer::ink_value(Ink ink) const noexcept {
    switch (ink) {
    case Ink::body:
        return InkValue{palette_.body, 255U};
    case Ink::red:
        return InkValue{palette_.red, 255U};
    case Ink::soft:
        return InkValue{palette_.body, soft_alpha};
    case Ink::accent:
        return InkValue{palette_.accent, 255U};
    case Ink::gilt:
        return InkValue{gilt_rgb, 255U};
    }
    return InkValue{palette_.body, 255U};
}

void PageRasterizer::clear_cache() {
    glyphs_.clear();
    coverage_.clear();
    counters_.glyph_cache_entries = 0U;
    counters_.glyph_cache_bytes = 0U;
}

void PageRasterizer::fill(const PixelTarget& target, Rgb color) const {
    if (target.pixels == nullptr || target.width == 0U || target.height == 0U) {
        return;
    }
    std::uint8_t* first_row = target.pixels;
    for (std::uint32_t x = 0U; x < target.width; ++x) {
        std::uint8_t* pixel = first_row + static_cast<std::size_t>(x) * 4U;
        pixel[0] = color.blue;
        pixel[1] = color.green;
        pixel[2] = color.red;
        pixel[3] = 255U;
    }
    const std::size_t row_length = static_cast<std::size_t>(target.width) * 4U;
    for (std::uint32_t y = 1U; y < target.height; ++y) {
        std::memcpy(target.pixels + static_cast<std::size_t>(y) * target.row_bytes, first_row, row_length);
    }
}

void PageRasterizer::blend_pixel(std::uint8_t* pixel, std::uint32_t coverage, Ink ink) const noexcept {
    // The toolkit's text profile (GUI.Forms docs/user/text-engine.md; core/text/composite):
    // coverage is composited in sRGB values with gamma 1.0, coverage times the ink's
    // alpha rounded once, each channel rounded once.
    const InkValue value = ink_value(ink);
    const std::uint32_t weight = (coverage * value.alpha * 256U + 32640U) / 65280U;
    if (weight == 0U) {
        return;
    }
    const std::uint32_t source[3] = {value.color.blue, value.color.green, value.color.red};
    for (std::size_t channel = 0U; channel < 3U; ++channel) {
        const std::uint32_t below = pixel[channel];
        pixel[channel] = static_cast<std::uint8_t>((source[channel] * weight + below * (255U - weight) + 127U) / 255U);
    }
}

void PageRasterizer::blend_coverage(const PixelTarget& target, PageRect clip, std::int32_t left, std::int32_t top,
                                    const CachedGlyph& glyph, Ink ink) const {
    const std::int32_t clip_left = std::max<std::int32_t>(clip.x, 0);
    const std::int32_t clip_top = std::max<std::int32_t>(clip.y, 0);
    const std::int32_t clip_right =
        std::min<std::int32_t>(clip.x + static_cast<std::int32_t>(clip.width), static_cast<std::int32_t>(target.width));
    const std::int32_t clip_bottom = std::min<std::int32_t>(clip.y + static_cast<std::int32_t>(clip.height),
                                                            static_cast<std::int32_t>(target.height));
    const std::int32_t x_begin = std::max(left, clip_left);
    const std::int32_t x_end = std::min(left + static_cast<std::int32_t>(glyph.width), clip_right);
    const std::int32_t y_begin = std::max(top, clip_top);
    const std::int32_t y_end = std::min(top + static_cast<std::int32_t>(glyph.height), clip_bottom);
    if (x_begin >= x_end || y_begin >= y_end) {
        return;
    }
    const std::uint8_t* coverage = coverage_.data() + glyph.offset;
    for (std::int32_t y = y_begin; y < y_end; ++y) {
        const std::uint8_t* source = coverage + static_cast<std::size_t>(y - top) * glyph.width;
        std::uint8_t* row = target.pixels + static_cast<std::size_t>(y) * target.row_bytes;
        for (std::int32_t x = x_begin; x < x_end; ++x) {
            const std::uint32_t amount = source[x - left];
            if (amount != 0U) {
                blend_pixel(row + static_cast<std::size_t>(x) * 4U, amount, ink);
            }
        }
    }
}

void PageRasterizer::blend_rectangle(const PixelTarget& target, PageRect clip, std::int64_t x0, std::int64_t y0,
                                     std::int64_t x1, std::int64_t y1, Ink ink) const {
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    const std::int64_t clip_left = std::max<std::int64_t>(clip.x, 0);
    const std::int64_t clip_top = std::max<std::int64_t>(clip.y, 0);
    const std::int64_t clip_right = std::min<std::int64_t>(static_cast<std::int64_t>(clip.x) + clip.width, target.width);
    const std::int64_t clip_bottom =
        std::min<std::int64_t>(static_cast<std::int64_t>(clip.y) + clip.height, target.height);
    const std::int64_t first_column = std::max(floor_shift_6(x0), clip_left);
    const std::int64_t end_column = std::min(floor_shift_6(x1 + 63), clip_right);
    const std::int64_t first_row = std::max(floor_shift_6(y0), clip_top);
    const std::int64_t end_row = std::min(floor_shift_6(y1 + 63), clip_bottom);
    for (std::int64_t y = first_row; y < end_row; ++y) {
        // How much of this pixel row the rectangle covers, in 1/64.
        const std::int64_t covered_y = std::min(y1, (y + 1) * 64) - std::max(y0, y * 64);
        std::uint8_t* row = target.pixels + static_cast<std::size_t>(y) * target.row_bytes;
        for (std::int64_t x = first_column; x < end_column; ++x) {
            const std::int64_t covered_x = std::min(x1, (x + 1) * 64) - std::max(x0, x * 64);
            const std::int64_t amount = covered_x * covered_y * 255 / (64 * 64);
            if (amount > 0) {
                blend_pixel(row + static_cast<std::size_t>(x) * 4U, static_cast<std::uint32_t>(amount), ink);
            }
        }
    }
}

const PageRasterizer::CachedGlyph* PageRasterizer::cached_glyph(FaceId face, std::uint32_t size_26_6,
                                                                std::uint32_t glyph, std::uint32_t phase_x,
                                                                std::uint32_t quarter_y,
                                                                std::int32_t embolden_26_6) {
    // Face, size, glyph, the horizontal phase (1/64 pixel), the vertical quarter
    // and whether the outline is thickened: at one size a face is thickened by
    // one amount only.
    const std::uint64_t key = (static_cast<std::uint64_t>(face) << 60U) |
                              (static_cast<std::uint64_t>(size_26_6 & 0xfffffU) << 40U) |
                              (static_cast<std::uint64_t>(glyph & 0xffffffU) << 16U) |
                              (static_cast<std::uint64_t>(phase_x & 63U) << 4U) |
                              (static_cast<std::uint64_t>(quarter_y & 3U) << 2U) |
                              (embolden_26_6 != 0 ? 1U : 0U);
    const std::unordered_map<std::uint64_t, CachedGlyph>::iterator found = glyphs_.find(key);
    if (found != glyphs_.end()) {
        return &(*found).second;
    }
    ++counters_.glyph_cache_misses;
    GlyphCoverage rendered;
    const bool drawn =
        (*fonts_).rasterize(face, size_26_6, glyph, static_cast<std::int32_t>(phase_x),
                            static_cast<std::int32_t>(quarter_y * 16U), embolden_26_6, rendered);
    CachedGlyph entry;
    if (drawn) {
        entry.left = rendered.left;
        entry.top = rendered.top;
        entry.width = rendered.width;
        entry.height = rendered.height;
        entry.offset = coverage_.size();
        coverage_.insert(coverage_.end(), rendered.coverage.begin(), rendered.coverage.end());
    }
    const std::pair<std::unordered_map<std::uint64_t, CachedGlyph>::iterator, bool> inserted =
        glyphs_.emplace(key, entry);
    counters_.glyph_cache_entries = glyphs_.size();
    counters_.glyph_cache_bytes = coverage_.size();
    return &(*inserted.first).second;
}

void PageRasterizer::draw_page(const PageLayout& page, const PixelTarget& target, PageRect rect,
                               const PaperImage& paper) {
    if (target.pixels == nullptr || rect.width == 0U || rect.height == 0U) {
        return;
    }
    const std::uint32_t page_height = rect.height;
    const std::int64_t origin_x = static_cast<std::int64_t>(rect.x) * 64;
    const std::int64_t origin_y = static_cast<std::int64_t>(rect.y) * 64;

    // The paper: the sheet the paper engine made for this size, or a flat colour.
    const std::int64_t paper_left = std::max<std::int64_t>(rect.x, 0);
    const std::int64_t paper_top = std::max<std::int64_t>(rect.y, 0);
    const std::int64_t paper_right = std::min<std::int64_t>(static_cast<std::int64_t>(rect.x) + rect.width, target.width);
    const std::int64_t paper_bottom =
        std::min<std::int64_t>(static_cast<std::int64_t>(rect.y) + rect.height, target.height);
    if (paper_left >= paper_right || paper_top >= paper_bottom) {
        return;
    }
    std::uint8_t* first_paper_row = target.pixels + static_cast<std::size_t>(paper_top) * target.row_bytes +
                                    static_cast<std::size_t>(paper_left) * 4U;
    const std::size_t paper_pixels = static_cast<std::size_t>(paper_right - paper_left);
    const bool sheet = paper.pixels != nullptr && paper.width == rect.width && paper.height == rect.height;
    for (std::int64_t y = paper_top; sheet && y < paper_bottom; ++y) {
        const std::uint8_t* source = paper.pixels + static_cast<std::size_t>(y - rect.y) * paper.row_bytes +
                                     static_cast<std::size_t>(paper_left - rect.x) * 4U;
        std::memcpy(target.pixels + static_cast<std::size_t>(y) * target.row_bytes +
                        static_cast<std::size_t>(paper_left) * 4U,
                    source, paper_pixels * 4U);
    }
    for (std::size_t x = 0U; !sheet && x < paper_pixels; ++x) {
        std::uint8_t* pixel = first_paper_row + x * 4U;
        pixel[0] = paper_rgb.blue;
        pixel[1] = paper_rgb.green;
        pixel[2] = paper_rgb.red;
        pixel[3] = 255U;
    }
    for (std::int64_t y = paper_top + 1; !sheet && y < paper_bottom; ++y) {
        std::memcpy(target.pixels + static_cast<std::size_t>(y) * target.row_bytes +
                        static_cast<std::size_t>(paper_left) * 4U,
                    first_paper_row, paper_pixels * 4U);
    }

    for (const PlacedRule& rule : page.rules) {
        const std::int64_t x0 = origin_x + to_26_6(rule.x0, page_height);
        const std::int64_t x1 = origin_x + to_26_6(rule.x1, page_height);
        std::int64_t y0 = origin_y + to_26_6(rule.y0, page_height);
        std::int64_t y1 = origin_y + to_26_6(rule.y1, page_height);
        if (rule.y1 == rule.y0) {
            // A hairline: one whole pixel row, whatever the scale.
            y0 = floor_shift_6(y0 + 32) * 64;
            y1 = y0 + 64;
        }
        blend_rectangle(target, rect, x0, y0, x1, y1, rule.ink);
    }

    for (const PlacedRun& run : page.runs) {
        const std::uint32_t size_26_6 = static_cast<std::uint32_t>(to_26_6(run.em, page_height));
        if (size_26_6 == 0U) {
            continue;
        }
        const std::int32_t embolden_26_6 =
            run.embolden > 0 ? static_cast<std::int32_t>(std::max<std::int64_t>(to_26_6(run.embolden, page_height), 1))
                             : 0;
        // Baselines sit on whole pixels so light hinting has a grid to
        // snap the vertical features to.
        const std::int64_t baseline = floor_shift_6(origin_y + to_26_6(run.baseline, page_height) + 32);
        LayoutUnit pen = run.x;
        for (const ShapedGlyph& glyph : run.glyphs) {
            if (glyph.glyph == synthetic_en_dash_glyph || glyph.glyph == synthetic_em_dash_glyph ||
                glyph.glyph == synthetic_dagger_glyph) {
                // Marks the face lacks, drawn from its own measurements: the
                // hyphen gives the stroke and its height.
                const FaceMetrics metrics = (*fonts_).metrics(run.face, run.em);
                GlyphBox hyphen = (*fonts_).glyph_box(run.face, run.em, (*fonts_).glyph_of(run.face, U'-'));
                if (hyphen.empty()) {
                    hyphen.y_min = metrics.x_height * 45 / 100;
                    hyphen.y_max = hyphen.y_min + run.em * 6 / 100;
                }
                const LayoutUnit stroke = hyphen.y_max - hyphen.y_min;
                const std::int64_t base_y = baseline * 64;
                if (glyph.glyph != synthetic_dagger_glyph) {
                    const LayoutUnit side = run.em * 4 / 100;
                    blend_rectangle(target, rect, origin_x + to_26_6(pen + side, page_height),
                                    base_y - to_26_6(hyphen.y_max, page_height),
                                    origin_x + to_26_6(pen + glyph.x_advance - side, page_height),
                                    base_y - to_26_6(hyphen.y_min, page_height), run.ink);
                } else {
                    const LayoutUnit centre = pen + glyph.x_advance / 2;
                    const LayoutUnit half_bar = glyph.x_advance * 40 / 100;
                    const LayoutUnit bar_y = metrics.cap_height * 68 / 100;
                    blend_rectangle(target, rect, origin_x + to_26_6(centre - stroke / 2, page_height),
                                    base_y - to_26_6(metrics.cap_height, page_height),
                                    origin_x + to_26_6(centre + stroke / 2, page_height),
                                    base_y + to_26_6(metrics.descender * 6 / 10, page_height), run.ink);
                    blend_rectangle(target, rect, origin_x + to_26_6(centre - half_bar, page_height),
                                    base_y - to_26_6(bar_y + stroke / 2, page_height),
                                    origin_x + to_26_6(centre + half_bar, page_height),
                                    base_y - to_26_6(bar_y - stroke / 2, page_height), run.ink);
                }
                pen += glyph.x_advance;
                continue;
            }
            if (glyph.glyph == 0U) {
                pen += glyph.x_advance;
                continue;
            }
            // The pen in 1/64 pixel, split into a whole pixel and its phase; the
            // outline is shifted by the phase before it is rasterized, as the
            // toolkit places text (integer 26.6, horizontal phase kept).
            const std::int64_t glyph_x = origin_x + to_26_6(static_cast<std::int64_t>(pen) + glyph.x_offset, page_height);
            const std::int64_t whole_x = floor_shift_6(glyph_x);
            const std::uint32_t phase_x = static_cast<std::uint32_t>(glyph_x - whole_x * 64);
            const std::int64_t glyph_y = to_26_6(glyph.y_offset, page_height);
            std::int64_t whole_y = floor_shift_6(glyph_y);
            std::uint32_t quarter_y = static_cast<std::uint32_t>(((glyph_y - whole_y * 64) + 8) >> 4);
            if (quarter_y == 4U) {
                ++whole_y;
                quarter_y = 0U;
            }
            const CachedGlyph* cached =
                cached_glyph(run.face, size_26_6, glyph.glyph, phase_x, quarter_y, embolden_26_6);
            if ((*cached).width != 0U) {
                blend_coverage(target, rect, static_cast<std::int32_t>(whole_x) + (*cached).left,
                               static_cast<std::int32_t>(baseline - whole_y) - (*cached).top, *cached, run.ink);
                ++counters_.glyphs_drawn;
            }
            pen += glyph.x_advance;
        }
    }
}

}  // namespace pl::page
