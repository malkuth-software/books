#pragma once

#include <cstdint>

namespace pl::page {

// Every page is laid out in layout units that do not depend on the window:
// one unit is a millionth of the page height. Line and page breaks are
// therefore the same at every window size and display scale (owner answer
// R020 A: the page keeps its proportions and scales). Device pixels appear
// only when a page is rasterized.
using LayoutUnit = std::int32_t;

// Page proportions of the interview mockup the owner chose from
// (interview/interview.css: a spread of 1.43 : 1, a single page of 0.715 : 1).
inline constexpr LayoutUnit page_height_lu = 1'000'000;
inline constexpr LayoutUnit page_width_lu = 715'000;

// Text area: the mockup's insets of 6% top, 9% right, 7% bottom, 9% left.
inline constexpr LayoutUnit content_left_lu = 64'350;
inline constexpr LayoutUnit content_top_lu = 60'000;
inline constexpr LayoutUnit content_width_lu = 586'300;
inline constexpr LayoutUnit content_height_lu = 870'000;

// The em of body text at the medium step: 3% of the page width, which sets
// about 65 characters to a line (R023 C: 60 to 75).
inline constexpr LayoutUnit medium_em_lu = 21'450;

// Three type-size steps, each with its own page breaks (J003 B). The ratios
// are the mockup's: 0.88, 1 and 1.16.
enum class TypeStep : std::uint8_t { small, medium, large };
inline constexpr std::uint32_t type_step_count = 3U;


[[nodiscard]] constexpr LayoutUnit body_em_lu(TypeStep step) noexcept {
    switch (step) {
    case TypeStep::small:
        return medium_em_lu * 88 / 100;
    case TypeStep::medium:
        return medium_em_lu;
    case TypeStep::large:
        return medium_em_lu * 116 / 100;
    }
    return medium_em_lu;
}

// value * numerator / denominator, rounded to nearest, without overflow for
// the magnitudes a page holds.
[[nodiscard]] constexpr LayoutUnit scale_lu(LayoutUnit value, std::int64_t numerator,
                                            std::int64_t denominator) noexcept {
    const std::int64_t product = static_cast<std::int64_t>(value) * numerator;
    const std::int64_t half = denominator / 2;
    const std::int64_t rounded = product >= 0 ? (product + half) / denominator : -((-product + half) / denominator);
    return static_cast<LayoutUnit>(rounded);
}

// Straight 8-bit sRGB.
struct Rgb final {
    std::uint8_t red{0U};
    std::uint8_t green{0U};
    std::uint8_t blue{0U};
};

// The mockup's colours: India-paper ground, warm black ink (P007 B), red for
// the words of Christ (R010 B).
inline constexpr Rgb paper_rgb{0xf0U, 0xecU, 0xe0U};
inline constexpr Rgb ink_rgb{0x1dU, 0x18U, 0x12U};
inline constexpr Rgb red_rgb{0xa0U, 0x1cU, 0x14U};
// Flat surround until Stage 5 makes the wood.
inline constexpr Rgb surround_rgb{0x2bU, 0x2bU, 0x2eU};

// What a run of glyphs is drawn with. The compositor never mixes colours; it
// blends one ink over the paper by coverage.
enum class Ink : std::uint8_t {
    body,
    // Words of Christ, from the BSB's own \wj markup.
    red,
    // Verse numerals, running head, footnote text: ink at four fifths.
    soft,
    // Footnote callers, in the mockup's accent colour.
    accent,
    // Gold leaf: the lettering on a book's spine.
    gilt,
};

}  // namespace pl::page
