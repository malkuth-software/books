#pragma once

#include "page/page_units.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace pl::page {

// The library's faces (owner's choice, 2026-10-15): Literata for books in general,
// EB Garamond for the Bible.
enum class FaceId : std::uint8_t {
    literata_regular,
    literata_italic,
    eb_garamond_regular,
    eb_garamond_italic,
};
inline constexpr std::size_t face_count = 4U;

enum class TextScript : std::uint8_t { latin, greek, hebrew };

// OpenType features asked of the shaper beyond its defaults (kerning and
// standard ligatures stay on: F011 C). A face without the feature ignores it.
struct ShapeFeatures final {
    // 'c2sc': capitals as true small capitals (F011 B: LORD).
    bool capitals_to_small_capitals{false};
    // 'onum': old-style figures (F011 A). Off gives the face's default.
    bool old_style_figures{false};
    // 'lnum': lining figures, for verse numerals.
    bool lining_figures{false};
    // For the shaped-marks test only. 'mark' and 'mkmk' off: marks stay
    // where the shaper leaves them before the font's anchors are applied.
    bool mark_positioning_off{false};
    // For the same test: one cluster to the character instead of one to the
    // base with its marks, so a glyph can be traced to its mark.
    bool cluster_per_character{false};
};

// One glyph of a shaped run, in layout units at the run's em. Offsets follow
// HarfBuzz: x to the right, y upward, both from the pen position.
struct ShapedGlyph final {
    std::uint32_t glyph{0U};
    // Byte offset, in the shaped text, of the cluster this glyph belongs to.
    std::uint32_t cluster{0U};
    LayoutUnit x_advance{0};
    LayoutUnit x_offset{0};
    LayoutUnit y_offset{0};
};

// Glyphs in visual order, left to right, whatever the direction of the text.
struct ShapedRun final {
    std::vector<ShapedGlyph> glyphs{};
    LayoutUnit advance{0};
};

// Vertical metrics of a face at an em, in layout units, all positive.
struct FaceMetrics final {
    LayoutUnit ascender{0};
    LayoutUnit descender{0};
    LayoutUnit x_height{0};
    LayoutUnit cap_height{0};
};

// The ink box of one glyph at an em, in layout units, y upward from the
// baseline: [x_min, x_max) by [y_min, y_max).
struct GlyphBox final {
    LayoutUnit x_min{0};
    LayoutUnit y_min{0};
    LayoutUnit x_max{0};
    LayoutUnit y_max{0};
    [[nodiscard]] bool empty() const noexcept {
        return x_max <= x_min || y_max <= y_min;
    }
};

// Glyph numbers the compositor gives to marks it draws itself where a face
// lacks them (F302 note: what Portsmouth does not have "should be
// dynamically created by the engine"). No font has glyphs this high.
inline constexpr std::uint32_t synthetic_en_dash_glyph = 0xfffffff0U;
inline constexpr std::uint32_t synthetic_dagger_glyph = 0xfffffff1U;
inline constexpr std::uint32_t synthetic_em_dash_glyph = 0xfffffff2U;

// 8-bit coverage of one rasterized glyph. Row 0 is the top row. The bitmap's
// left edge is `left` pixels right of the pen and its top row is `top` pixels
// above the baseline.
struct GlyphCoverage final {
    std::int32_t left{0};
    std::int32_t top{0};
    std::uint32_t width{0U};
    std::uint32_t height{0U};
    std::vector<std::uint8_t> coverage{};
};

// Owns one FreeType library and, per loaded face, a FreeType face for
// outlines and a HarfBuzz font for shaping. The font bytes are borrowed: the
// caller keeps them alive and unchanged for the life of the FontSet.
//
// Not thread-safe: one FontSet belongs to one thread at a time.
class FontSet final {
  public:
    FontSet();
    ~FontSet();
    FontSet(const FontSet&) = delete;
    FontSet& operator=(const FontSet&) = delete;

    // False when the library could not start; no face can then be loaded.
    [[nodiscard]] bool ready() const noexcept;

    // Loads a face from TrueType or OpenType bytes. Returns false, loading
    // nothing, when the bytes are not a font either library accepts.
    [[nodiscard]] bool load(FaceId face, std::span<const std::uint8_t> bytes);
    [[nodiscard]] bool loaded(FaceId face) const noexcept;

    // True when the face maps the code point to a glyph of its own.
    [[nodiscard]] bool has_glyph(FaceId face, char32_t code_point) const;
    // True when the face's substitution table lists the feature tag
    // (four ASCII letters, for example "smcp").
    [[nodiscard]] bool has_feature(FaceId face, std::string_view tag) const;

    [[nodiscard]] FaceMetrics metrics(FaceId face, LayoutUnit em) const;
    // The glyph the face maps the code point to, or zero.
    [[nodiscard]] std::uint32_t glyph_of(FaceId face, char32_t code_point) const;
    [[nodiscard]] LayoutUnit glyph_advance(FaceId face, LayoutUnit em, std::uint32_t glyph) const;
    [[nodiscard]] GlyphBox glyph_box(FaceId face, LayoutUnit em, std::uint32_t glyph) const;

    // Shapes UTF-8 text of one script and one direction. Advances are the
    // face's unhinted advances scaled to `em`, so a run's width is the same
    // at every window size. `run` is replaced.
    void shape(FaceId face, LayoutUnit em, std::string_view utf8, TextScript script, bool right_to_left,
               ShapeFeatures features, ShapedRun& run) const;

    // Rasterizes one glyph with greyscale antialiasing and light hinting
    // (F007 A, F008 B). `size_26_6` is the em in 1/64 device pixels.
    // `shift_x_26_6` and `shift_y_26_6` move the outline by a fraction of a
    // pixel before rasterizing (y upward). `embolden_26_6` thickens the
    // outline by that much and is used only for synthesized small capitals.
    // Returns false, leaving `destination` unchanged, when the glyph cannot
    // be loaded; a glyph without ink gives an empty bitmap and true.
    [[nodiscard]] bool rasterize(FaceId face, std::uint32_t size_26_6, std::uint32_t glyph,
                                 std::int32_t shift_x_26_6, std::int32_t shift_y_26_6,
                                 std::int32_t embolden_26_6, GlyphCoverage& destination) const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace pl::page
