#pragma once

#include "page/font_set.hpp"
#include "page/page_units.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pl::page {

// Which text of a passage a word belongs to.
enum class TextStream : std::uint8_t { english, original, note };

// The identity of a word on a page: what a click, a selection or a slip is
// keyed to. English words are numbered within their verse in reading order;
// original words carry the source's own word number (the digits after '#' in
// STEPBible's reference); note words are numbered within their note, with
// `verse` holding the note's index.
struct WordIdentity final {
    std::uint8_t passage{0U};
    TextStream stream{TextStream::english};
    std::uint16_t verse{0U};
    std::uint16_t ordinal{0U};

    friend constexpr bool operator==(const WordIdentity& left, const WordIdentity& right) noexcept {
        return left.passage == right.passage && left.stream == right.stream && left.verse == right.verse &&
               left.ordinal == right.ordinal;
    }
};

inline constexpr std::uint32_t no_word = 0xffffffffU;

// How a stretch of text is set.
struct TextStyle final {
    FaceId face{FaceId::eb_garamond_regular};
    LayoutUnit em{medium_em_lu};
    TextScript script{TextScript::latin};
    ShapeFeatures features{};
    Ink ink{Ink::body};
    // Baseline shift, upward.
    LayoutUnit rise{0};
    // Added after every glyph that advances.
    LayoutUnit tracking{0};
    // ASCII letters are shaped as capitals: synthesized small capitals and
    // the running head.
    bool uppercase{false};
    // Outline thickening for synthesized small capitals, in layout units.
    LayoutUnit embolden{0};
};

// Glyphs of one face, size and ink on one baseline. In a LineBox, x is from
// the frame's left edge and baseline from the top of the line box; on a page
// both are page coordinates, y downward.
struct PlacedRun final {
    FaceId face{FaceId::eb_garamond_regular};
    LayoutUnit em{0};
    Ink ink{Ink::body};
    LayoutUnit embolden{0};
    LayoutUnit x{0};
    LayoutUnit baseline{0};
    std::vector<ShapedGlyph> glyphs{};
};

// A word's box. In a LineBox, x is from the frame's left edge and the box
// spans the line's height; on a page all four are page coordinates.
struct PlacedWord final {
    LayoutUnit x0{0};
    LayoutUnit y0{0};
    LayoutUnit x1{0};
    LayoutUnit y1{0};
    WordIdentity word{};
};

// Where a footnote caller goes; the page assigns its letter.
struct CallerSlot final {
    LayoutUnit x{0};
    std::uint32_t footnote{0U};
};

struct LineBox final {
    LayoutUnit height{0};
    // From the top of the box to the baseline of unraised text.
    LayoutUnit baseline{0};
    std::vector<PlacedRun> runs{};
    std::vector<PlacedWord> words{};
    std::vector<CallerSlot> callers{};
    // Set by the page builder, not by the typesetter.
    LayoutUnit space_before{0};
    bool keep_with_next{false};
    std::uint32_t chunk{0U};
};

// One paragraph's text with everything the typesetter needs to know about
// each byte of it. `style_of_byte` and `word_of_byte` have one entry per byte
// of `text`.
struct ParagraphInput final {
    std::string text{};
    std::vector<TextStyle> styles{};
    std::vector<std::uint8_t> style_of_byte{};
    // Index into `words`, or no_word for a space.
    std::vector<std::uint32_t> word_of_byte{};
    std::vector<WordIdentity> words{};

    // A verse numeral standing before the text at a byte offset.
    struct Numeral final {
        std::size_t offset{0U};
        std::string digits{};
    };
    // A footnote called after the text that ends at a byte offset.
    struct Caller final {
        std::size_t offset{0U};
        std::uint32_t footnote{0U};
    };
    std::vector<Numeral> numerals{};
    std::vector<Caller> callers{};
    TextStyle numeral_style{};
    // Between a numeral and the word it stands before.
    LayoutUnit numeral_gap{0};
    // Reserved for each caller, whatever letter the page gives it.
    LayoutUnit caller_width{0};
    bool right_to_left{false};
};

enum class Alignment : std::uint8_t {
    // Both edges, by whole-paragraph line breaking; the last line is set
    // from the start edge.
    justified,
    // The left edge of left-to-right text, the right edge of right-to-left.
    start,
    end,
    centered,
};

struct ParagraphFrame final {
    Alignment alignment{Alignment::justified};
    LayoutUnit width{content_width_lu};
    // Taken from the start edge of the first line and of the others.
    LayoutUnit first_indent{0};
    LayoutUnit rest_indent{0};
    // The first `narrowed_lines` lines lose `narrowed_by` more at the start
    // edge: room for a drop numeral.
    std::uint32_t narrowed_lines{0U};
    LayoutUnit narrowed_by{0};
    LayoutUnit line_height{0};
    LayoutUnit baseline{0};
};

struct TypesetterCounters final {
    std::uint64_t shape_requests{0U};
    std::uint64_t shape_cache_misses{0U};
    std::uint64_t shape_cache_entries{0U};
    // Glyphs held by the cache; its memory is about 20 bytes a glyph plus
    // the keys.
    std::uint64_t shape_cache_glyphs{0U};
    std::uint64_t shape_cache_key_bytes{0U};
    // Glyphs the face had no outline for and the compositor did not draw.
    std::uint64_t missing_glyphs{0U};
    // Glyphs the compositor drew itself (en dash, em dash, dagger).
    std::uint64_t synthesized_glyphs{0U};
    // Lines whose single word is wider than the measure.
    std::uint64_t overfull_lines{0U};
    // Justified lines set looser than the first pass allows.
    std::uint64_t loose_lines{0U};
};

// Shapes, breaks and sets paragraphs. Holds the shaping cache: a word in a
// face at a size is shaped once (ARCHITECTURE_HYPOTHESES section 3, step 2).
// The FontSet is borrowed and must outlive the Typesetter.
class Typesetter final {
  public:
    explicit Typesetter(const FontSet& fonts);

    // Appends the paragraph's lines to `lines`.
    void set_paragraph(const ParagraphInput& input, const ParagraphFrame& frame, std::vector<LineBox>& lines);

    // The shaped form of `utf8` in a style, from the cache. The reference is
    // valid until the next call that shapes.
    [[nodiscard]] const ShapedRun& shaped(const TextStyle& style, std::string_view utf8, bool right_to_left);
    [[nodiscard]] LayoutUnit space_advance(const TextStyle& style) const;

    [[nodiscard]] const TypesetterCounters& counters() const noexcept {
        return counters_;
    }
    [[nodiscard]] const FontSet& fonts() const noexcept {
        return *fonts_;
    }

  private:
    const FontSet* fonts_;
    std::unordered_map<std::string, ShapedRun> shape_cache_{};
    std::string key_scratch_{};
    std::string text_scratch_{};
    TypesetterCounters counters_{};
};

}  // namespace pl::page
