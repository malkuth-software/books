#include "page/text_layout.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace pl::page {
namespace {

bool is_letter(char byte) noexcept {
    const unsigned char value = static_cast<unsigned char>(byte);
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value >= 0x80U;
}

// The code point that begins at `offset` of well-formed UTF-8; U+FFFD when
// the bytes there are not a complete sequence.
char32_t code_point_at(std::string_view text, std::size_t offset) noexcept {
    const std::size_t remaining = text.size() - offset;
    const unsigned char lead = static_cast<unsigned char>(text[offset]);
    if (lead < 0x80U) {
        return static_cast<char32_t>(lead);
    }
    std::size_t length = 0U;
    char32_t value = 0U;
    if ((lead & 0xe0U) == 0xc0U) {
        length = 2U;
        value = lead & 0x1fU;
    } else if ((lead & 0xf0U) == 0xe0U) {
        length = 3U;
        value = lead & 0x0fU;
    } else if ((lead & 0xf8U) == 0xf0U) {
        length = 4U;
        value = lead & 0x07U;
    } else {
        return U'\xfffd';
    }
    if (remaining < length) {
        return U'\xfffd';
    }
    for (std::size_t index = 1U; index < length; ++index) {
        const unsigned char next = static_cast<unsigned char>(text[offset + index]);
        value = (value << 6U) | (next & 0x3fU);
    }
    return value;
}

// One breakable piece of a paragraph: a word with what is attached to it.
// Everything is positioned from the segment's own left edge, in visual order.
struct Segment final {
    std::vector<PlacedRun> runs{};
    std::vector<PlacedWord> words{};
    std::vector<CallerSlot> callers{};
    LayoutUnit width{0};
    // The space after the segment, when the source has one: its natural
    // width and how far it may stretch and shrink.
    LayoutUnit glue{0};
    LayoutUnit stretch{0};
    LayoutUnit shrink{0};
    bool break_after{false};
    std::uint8_t level{0U};
};

// Where the paragraph's lines end: line k holds segments
// [breaks[k], breaks[k + 1]).
using LineBreaks = std::vector<std::size_t>;

struct Measure final {
    const ParagraphFrame* frame{nullptr};

    [[nodiscard]] LayoutUnit indent(std::size_t line) const noexcept {
        LayoutUnit taken = line == 0U ? (*frame).first_indent : (*frame).rest_indent;
        if (line < (*frame).narrowed_lines) {
            taken += (*frame).narrowed_by;
        }
        return taken;
    }
    // The first line number from which every line has the same width.
    [[nodiscard]] std::size_t steady_line() const noexcept {
        return std::max<std::size_t>(1U, (*frame).narrowed_lines);
    }
    [[nodiscard]] LayoutUnit width(std::size_t line) const noexcept {
        const LayoutUnit available = (*frame).width - indent(line);
        return std::max<LayoutUnit>(available, 1);
    }
};

// Running totals so the natural width of any run of segments is two
// subtractions: widths[i] sums the widths of segments before i, and the glue
// arrays sum what follows each segment before i.
struct Totals final {
    std::vector<std::int64_t> widths{};
    std::vector<std::int64_t> glue{};
    std::vector<std::int64_t> stretch{};
    std::vector<std::int64_t> shrink{};

    explicit Totals(const std::vector<Segment>& segments)
        : widths(segments.size() + 1U, 0), glue(segments.size() + 1U, 0), stretch(segments.size() + 1U, 0),
          shrink(segments.size() + 1U, 0) {
        for (std::size_t index = 0U; index < segments.size(); ++index) {
            widths[index + 1U] = widths[index] + segments[index].width;
            glue[index + 1U] = glue[index] + segments[index].glue;
            stretch[index + 1U] = stretch[index] + segments[index].stretch;
            shrink[index + 1U] = shrink[index] + segments[index].shrink;
        }
    }
    // Segments [first, end) set on one line: the glue after the last one is
    // not part of the line.
    [[nodiscard]] std::int64_t natural(std::size_t first, std::size_t end) const noexcept {
        return widths[end] - widths[first] + glue[end - 1U] - glue[first];
    }
    [[nodiscard]] std::int64_t stretch_of(std::size_t first, std::size_t end) const noexcept {
        return stretch[end - 1U] - stretch[first];
    }
    [[nodiscard]] std::int64_t shrink_of(std::size_t first, std::size_t end) const noexcept {
        return shrink[end - 1U] - shrink[first];
    }
};

constexpr std::int64_t badness_ceiling = 10'000;
// How the breaker ranks lines: one looser than badness_ceiling still counts
// by how loose it is, so where no line can be good the fullest is chosen, not
// the first (three words spread over a line, then the rest at ease).
constexpr std::int64_t loose_ceiling = 100LL * 64 * 64 * 64;
// First pass: no space wider than twice its natural width (ratio 2).
constexpr std::int64_t first_pass_tolerance = 800;
constexpr std::int64_t line_penalty = 10;

// 100 * (adjustment / flexibility)^3, the measure of how far a line's spaces
// are from their natural width, capped at `ceiling`.
std::int64_t badness_of(std::int64_t adjustment, std::int64_t flexibility,
                        std::int64_t ceiling = badness_ceiling) noexcept {
    if (adjustment == 0) {
        return 0;
    }
    if (flexibility <= 0) {
        return ceiling;
    }
    const std::int64_t magnitude = adjustment < 0 ? -adjustment : adjustment;
    // The ratio in 1/1024ths; beyond 64 the cube is over loose_ceiling anyway.
    const std::int64_t ratio = std::min<std::int64_t>(magnitude * 1024 / flexibility, 64 * 1024);
    const std::int64_t badness = 100 * ratio * ratio * ratio / (1024LL * 1024LL * 1024LL);
    return std::min(badness, ceiling);
}

struct BreakNode final {
    // The line that ends here holds segments up to, not including, this one.
    std::size_t position{0U};
    std::size_t line{0U};
    std::int64_t demerits{0};
    std::size_t previous{0U};
};

// Whole-paragraph line breaking in the manner of Knuth and Plass, without
// hyphenation (R006 B): the breaks minimize the summed demerits of every
// line, so an early break is taken where it packs the paragraph better.
// Returns false when no set of breaks keeps every line within `tolerance`.
bool break_total_fit(const std::vector<Segment>& segments, const Totals& totals, const Measure& measure,
                     std::int64_t tolerance, LineBreaks& breaks) {
    const std::size_t count = segments.size();
    std::vector<BreakNode> nodes;
    nodes.push_back(BreakNode{});
    std::vector<std::size_t> active;
    active.push_back(0U);
    std::vector<std::size_t> best_for_line;
    std::size_t final_node = std::numeric_limits<std::size_t>::max();

    for (std::size_t end = 1U; end <= count; ++end) {
        const bool last = end == count;
        if (!last && !segments[end - 1U].break_after) {
            continue;
        }
        // The best way to end line number `line` at `end`, one per line
        // number, because a narrowed first line makes the number matter.
        best_for_line.clear();
        std::vector<std::size_t> still_active;
        still_active.reserve(active.size());
        for (const std::size_t node_index : active) {
            const BreakNode node = nodes[node_index];
            const std::int64_t target = measure.width(node.line);
            const std::int64_t natural = totals.natural(node.position, end);
            const std::int64_t shrink = totals.shrink_of(node.position, end);
            const bool single = end == node.position + 1U;
            if (natural - shrink > target && !single) {
                // Too long from this start, here and at every later end.
                continue;
            }
            still_active.push_back(node_index);
            std::int64_t badness = 0;
            if (natural > target) {
                badness = single && natural - shrink > target ? badness_ceiling
                                                              : badness_of(natural - target, shrink, loose_ceiling);
            } else if (!last) {
                badness = badness_of(target - natural, totals.stretch_of(node.position, end), loose_ceiling);
            }
            if (badness > tolerance && !(single && natural > target)) {
                continue;
            }
            const std::int64_t line_demerits = (line_penalty + badness) * (line_penalty + badness);
            const std::int64_t demerits = node.demerits + line_demerits;
            // Only the width of a line depends on its number, and every line from the
            // first full-width one on has the same width: those numbers are one class,
            // so a break position holds a few nodes, not one per possible line count.
            const std::size_t line = std::min(node.line + 1U, measure.steady_line());
            bool placed = false;
            for (const std::size_t candidate : best_for_line) {
                if (nodes[candidate].line == line) {
                    if (demerits < nodes[candidate].demerits) {
                        nodes[candidate].demerits = demerits;
                        nodes[candidate].previous = node_index;
                    }
                    placed = true;
                    break;
                }
            }
            if (!placed) {
                BreakNode created;
                created.position = end;
                created.line = line;
                created.demerits = demerits;
                created.previous = node_index;
                nodes.push_back(created);
                best_for_line.push_back(nodes.size() - 1U);
            }
        }
        active = std::move(still_active);
        if (last) {
            for (const std::size_t candidate : best_for_line) {
                if (final_node == std::numeric_limits<std::size_t>::max() ||
                    nodes[candidate].demerits < nodes[final_node].demerits) {
                    final_node = candidate;
                }
            }
        } else {
            for (const std::size_t candidate : best_for_line) {
                active.push_back(candidate);
            }
        }
        if (active.empty() && !last) {
            return false;
        }
    }
    if (final_node == std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    LineBreaks reversed;
    std::size_t node_index = final_node;
    while (true) {
        reversed.push_back(nodes[node_index].position);
        if (nodes[node_index].position == 0U) {
            break;
        }
        node_index = nodes[node_index].previous;
    }
    breaks.assign(reversed.rbegin(), reversed.rend());
    return true;
}

// Line by line: each line takes as many segments as fit at their natural
// width. For text that is not justified, and the fallback when a paragraph
// cannot be justified at all.
void break_first_fit(const std::vector<Segment>& segments, const Totals& totals, const Measure& measure,
                     LineBreaks& breaks) {
    const std::size_t count = segments.size();
    breaks.clear();
    breaks.push_back(0U);
    std::size_t start = 0U;
    std::size_t last_fit = 0U;
    for (std::size_t end = 1U; end <= count; ++end) {
        const bool last = end == count;
        if (!last && !segments[end - 1U].break_after) {
            continue;
        }
        const std::int64_t target = measure.width(breaks.size() - 1U);
        if (totals.natural(start, end) > target && last_fit > start) {
            breaks.push_back(last_fit);
            start = last_fit;
        }
        last_fit = end;
    }
    breaks.push_back(count);
}

// Rule L2 of the Unicode bidirectional algorithm over whole segments: from
// the highest level down to the lowest odd level, reverse every run of
// segments at that level or higher. `order` receives logical indices in
// visual order, left to right.
void visual_order(const std::vector<Segment>& segments, std::size_t first, std::size_t end,
                  std::vector<std::size_t>& order) {
    order.clear();
    std::uint8_t highest = 0U;
    std::uint8_t lowest_odd = 255U;
    for (std::size_t index = first; index < end; ++index) {
        order.push_back(index);
        const std::uint8_t level = segments[index].level;
        highest = std::max(highest, level);
        if ((level & 1U) != 0U) {
            lowest_odd = std::min(lowest_odd, level);
        }
    }
    if (lowest_odd == 255U) {
        return;
    }
    for (std::uint8_t level = highest; level >= lowest_odd; --level) {
        std::size_t run_begin = 0U;
        while (run_begin < order.size()) {
            if (segments[order[run_begin]].level < level) {
                ++run_begin;
                continue;
            }
            std::size_t run_end = run_begin;
            while (run_end < order.size() && segments[order[run_end]].level >= level) {
                ++run_end;
            }
            std::reverse(order.begin() + static_cast<std::ptrdiff_t>(run_begin),
                         order.begin() + static_cast<std::ptrdiff_t>(run_end));
            run_begin = run_end;
        }
    }
}

} // namespace

Typesetter::Typesetter(const FontSet& fonts) : fonts_(&fonts) {}

LayoutUnit Typesetter::space_advance(const TextStyle& style) const {
    const std::uint32_t space = (*fonts_).glyph_of(style.face, U' ');
    const LayoutUnit advance = (*fonts_).glyph_advance(style.face, style.em, space);
    return advance;
}

const ShapedRun& Typesetter::shaped(const TextStyle& style, std::string_view utf8, bool right_to_left) {
    ++counters_.shape_requests;
    // Everything that changes the shaped result is in the key.
    key_scratch_.clear();
    key_scratch_.push_back(static_cast<char>(style.face));
    key_scratch_.push_back(static_cast<char>(style.script));
    const std::uint8_t flags = static_cast<std::uint8_t>(
        (style.features.capitals_to_small_capitals ? 1U : 0U) | (style.features.old_style_figures ? 2U : 0U) |
        (style.features.lining_figures ? 4U : 0U) | (style.uppercase ? 8U : 0U) | (right_to_left ? 16U : 0U));
    key_scratch_.push_back(static_cast<char>(flags));
    const std::int32_t sizes[2] = {style.em, style.tracking};
    key_scratch_.append(reinterpret_cast<const char*>(sizes), sizeof(sizes));
    key_scratch_.append(utf8);

    const std::unordered_map<std::string, ShapedRun>::iterator found = shape_cache_.find(key_scratch_);
    if (found != shape_cache_.end()) {
        return (*found).second;
    }
    ++counters_.shape_cache_misses;

    std::string_view shaped_text = utf8;
    if (style.uppercase) {
        text_scratch_.assign(utf8);
        for (char& character : text_scratch_) {
            if (character >= 'a' && character <= 'z') {
                character = static_cast<char>(character - 'a' + 'A');
            }
        }
        shaped_text = text_scratch_;
    }
    ShapedRun run;
    (*fonts_).shape(style.face, style.em, shaped_text, style.script, right_to_left, style.features, run);

    LayoutUnit advance = 0;
    for (ShapedGlyph& glyph : run.glyphs) {
        if (glyph.glyph == 0U) {
            // The face has no glyph here. Two marks are drawn by the
            // compositor; anything else is counted and left blank.
            const char32_t code_point = code_point_at(shaped_text, glyph.cluster);
            if (code_point == U'\x2013') {
                glyph.glyph = synthetic_en_dash_glyph;
                glyph.x_advance = style.em / 2;
                ++counters_.synthesized_glyphs;
            } else if (code_point == U'\x2020') {
                glyph.glyph = synthetic_dagger_glyph;
                glyph.x_advance = style.em * 44 / 100;
                ++counters_.synthesized_glyphs;
            } else {
                ++counters_.missing_glyphs;
            }
        } else if (code_point_at(shaped_text, glyph.cluster) == U'\x2014') {
            // A face may map the em dash to a glyph that is not a dash:
            // Portsmouth's is 483 thousandths of the em high where its hyphen
            // is 63 (experiments/004-shaped-marks). A dash more than three
            // hyphens high is drawn by the compositor instead.
            const GlyphBox dash = (*fonts_).glyph_box(style.face, style.em, glyph.glyph);
            const GlyphBox hyphen = (*fonts_).glyph_box(style.face, style.em, (*fonts_).glyph_of(style.face, U'-'));
            if (!hyphen.empty() && dash.y_max - dash.y_min > 3 * (hyphen.y_max - hyphen.y_min)) {
                glyph.glyph = synthetic_em_dash_glyph;
                glyph.x_advance = style.em;
                ++counters_.synthesized_glyphs;
            }
        }
        if (glyph.x_advance > 0) {
            glyph.x_advance += style.tracking;
        }
        advance += glyph.x_advance;
    }
    run.advance = advance;

    counters_.shape_cache_glyphs += run.glyphs.size();
    counters_.shape_cache_key_bytes += key_scratch_.size();
    const std::pair<std::unordered_map<std::string, ShapedRun>::iterator, bool> inserted =
        shape_cache_.emplace(key_scratch_, std::move(run));
    counters_.shape_cache_entries = shape_cache_.size();
    return (*inserted.first).second;
}

void Typesetter::set_paragraph(const ParagraphInput& input, const ParagraphFrame& frame,
                               std::vector<LineBox>& lines) {
    const std::string_view text = input.text;
    const std::size_t size = text.size();
    if (size == 0U) {
        return;
    }

    // Where a line may end. The library sets English, so the rules are English's: after
    // a run of spaces, after a hyphen between letters, and after an en or em dash. An
    // entry describes the position after its byte.
    std::vector<bool> opportunities(size, false);
    for (std::size_t index = 0U; index + 1U < size; ++index) {
        const char here = text[index];
        const char next = text[index + 1U];
        if (here == ' ' && next != ' ') {
            opportunities[index] = true;
        } else if (here == '-' && index > 0U && is_letter(text[index - 1U]) && is_letter(next)) {
            opportunities[index] = true;
        } else if (index >= 2U && static_cast<unsigned char>(text[index - 2U]) == 0xe2U &&
                   static_cast<unsigned char>(text[index - 1U]) == 0x80U &&
                   (static_cast<unsigned char>(here) == 0x93U || static_cast<unsigned char>(here) == 0x94U) &&
                   next != ' ') {
            opportunities[index] = true;
        }
    }
    // Every byte runs left to right.
    const std::vector<std::uint8_t> levels(size, 0U);

    // ---- Segments ------------------------------------------------------------
    std::vector<Segment> segments;
    std::size_t next_numeral = 0U;
    std::size_t next_caller = 0U;
    std::size_t begin = 0U;
    while (begin < size) {
        std::size_t end = begin;
        while (end < size) {
            const bool opportunity = opportunities[end];
            ++end;
            if (opportunity) {
                break;
            }
        }
        std::size_t content_end = end;
        while (content_end > begin && text[content_end - 1U] == ' ') {
            --content_end;
        }

        Segment segment;
        segment.break_after = end < size;
        segment.level = levels[begin];
        const bool reversed = (segment.level & 1U) != 0U;
        if (content_end < end) {
            const TextStyle& space_style = input.styles[input.style_of_byte[content_end]];
            segment.glue = space_advance(space_style);
            segment.stretch = segment.glue / 2;
            segment.shrink = segment.glue / 3;
        }

        // What stands before the segment's text and after it.
        const ShapedRun* numeral = nullptr;
        while (next_numeral < input.numerals.size() && input.numerals[next_numeral].offset < end) {
            // A second numeral in one segment replaces the first; the three
            // passages have none.
            numeral = &shaped(input.numeral_style, input.numerals[next_numeral].digits, false);
            ++next_numeral;
        }
        ShapedRun numeral_copy;
        if (numeral != nullptr) {
            numeral_copy = *numeral;
        }
        // A caller belongs to the segment that holds the byte before it.
        std::vector<std::uint32_t> called;
        while (next_caller < input.callers.size() && input.callers[next_caller].offset <= end) {
            called.push_back(input.callers[next_caller].footnote);
            ++next_caller;
        }

        LayoutUnit pen = 0;
        const LayoutUnit leading_width = numeral != nullptr ? numeral_copy.advance + input.numeral_gap : 0;
        if (!reversed && numeral != nullptr) {
            PlacedRun run;
            run.face = input.numeral_style.face;
            run.em = input.numeral_style.em;
            run.ink = input.numeral_style.ink;
            run.x = pen;
            run.baseline = -input.numeral_style.rise;
            run.glyphs = numeral_copy.glyphs;
            segment.runs.push_back(std::move(run));
            pen += leading_width;
        }
        if (reversed) {
            for (const std::uint32_t footnote : called) {
                segment.callers.push_back(CallerSlot{pen, footnote});
                pen += input.caller_width;
            }
        }

        // The text itself, in pieces of one style and one direction. In a
        // right-to-left segment the pieces are set in reverse.
        struct Piece final {
            std::size_t begin{0U};
            std::size_t end{0U};
        };
        std::vector<Piece> pieces;
        std::size_t piece_begin = begin;
        while (piece_begin < content_end) {
            std::size_t piece_end = piece_begin + 1U;
            while (piece_end < content_end &&
                   input.style_of_byte[piece_end] == input.style_of_byte[piece_begin] &&
                   levels[piece_end] == levels[piece_begin]) {
                ++piece_end;
            }
            pieces.push_back(Piece{piece_begin, piece_end});
            piece_begin = piece_end;
        }
        if (reversed) {
            std::reverse(pieces.begin(), pieces.end());
        }
        for (const Piece& piece : pieces) {
            const TextStyle& style = input.styles[input.style_of_byte[piece.begin]];
            const bool piece_reversed = (levels[piece.begin] & 1U) != 0U;
            const ShapedRun& shaped_piece =
                shaped(style, text.substr(piece.begin, piece.end - piece.begin), piece_reversed);
            PlacedRun run;
            run.face = style.face;
            run.em = style.em;
            run.ink = style.ink;
            run.embolden = style.embolden;
            run.x = pen;
            run.baseline = -style.rise;
            run.glyphs = shaped_piece.glyphs;
            LayoutUnit glyph_pen = pen;
            for (const ShapedGlyph& glyph : run.glyphs) {
                const std::uint32_t word = input.word_of_byte[piece.begin + glyph.cluster];
                const LayoutUnit glyph_end = glyph_pen + std::max<LayoutUnit>(glyph.x_advance, 0);
                if (word != no_word) {
                    bool merged = false;
                    for (PlacedWord& known : segment.words) {
                        if (known.word == input.words[word]) {
                            known.x0 = std::min(known.x0, glyph_pen);
                            known.x1 = std::max(known.x1, glyph_end);
                            merged = true;
                            break;
                        }
                    }
                    if (!merged) {
                        PlacedWord placed;
                        placed.x0 = glyph_pen;
                        placed.x1 = glyph_end;
                        placed.word = input.words[word];
                        segment.words.push_back(placed);
                    }
                }
                glyph_pen += glyph.x_advance;
            }
            pen += shaped_piece.advance;
            segment.runs.push_back(std::move(run));
        }

        if (!reversed) {
            for (const std::uint32_t footnote : called) {
                segment.callers.push_back(CallerSlot{pen, footnote});
                pen += input.caller_width;
            }
        } else if (numeral != nullptr) {
            PlacedRun run;
            run.face = input.numeral_style.face;
            run.em = input.numeral_style.em;
            run.ink = input.numeral_style.ink;
            run.x = pen + input.numeral_gap;
            run.baseline = -input.numeral_style.rise;
            run.glyphs = numeral_copy.glyphs;
            segment.runs.push_back(std::move(run));
            pen += leading_width;
        }
        segment.width = pen;
        segments.push_back(std::move(segment));
        begin = end;
    }
    if (segments.empty()) {
        return;
    }

    // ---- Lines ---------------------------------------------------------------
    const Totals totals(segments);
    const Measure measure{&frame};
    LineBreaks breaks;
    const bool justified = frame.alignment == Alignment::justified;
    if (justified) {
        if (!break_total_fit(segments, totals, measure, first_pass_tolerance, breaks)) {
            // Without hyphenation some paragraphs cannot keep every line
            // within the first tolerance; the second pass accepts any
            // stretch and still chooses the least bad whole.
            if (!break_total_fit(segments, totals, measure, loose_ceiling, breaks)) {
                break_first_fit(segments, totals, measure, breaks);
            }
        }
    } else {
        break_first_fit(segments, totals, measure, breaks);
    }

    std::vector<std::size_t> order;
    std::vector<LayoutUnit> adjusted_glue;
    for (std::size_t line = 0U; line + 1U < breaks.size(); ++line) {
        const std::size_t first = breaks[line];
        const std::size_t end = breaks[line + 1U];
        const bool last_line = end == segments.size();
        const LayoutUnit target = measure.width(line);
        const std::int64_t natural = totals.natural(first, end);

        // The width each space on the line is set to.
        adjusted_glue.assign(end - first, 0);
        std::int64_t content_width = natural;
        for (std::size_t index = first; index + 1U < end; ++index) {
            adjusted_glue[index - first] = segments[index].glue;
        }
        if (natural > target && totals.shrink_of(first, end) < natural - target) {
            ++counters_.overfull_lines;
        }
        // The last line is set at its natural width unless it is a little
        // too long, when its spaces close up like any other line's.
        const bool adjusts = justified && (!last_line || natural > target);
        if (adjusts && end - first > 1U) {
            std::int64_t extra = target - natural;
            const bool stretching = extra >= 0;
            const std::int64_t flexibility =
                stretching ? totals.stretch_of(first, end) : totals.shrink_of(first, end);
            if (!stretching && -extra > flexibility) {
                extra = -flexibility;
            }
            if (stretching && badness_of(extra, flexibility) > first_pass_tolerance) {
                ++counters_.loose_lines;
            }
            if (flexibility > 0) {
                // Each space takes its share of the difference; the running
                // totals keep the sum exact.
                std::int64_t flexibility_before = 0;
                std::int64_t given_before = 0;
                for (std::size_t index = first; index + 1U < end; ++index) {
                    flexibility_before += stretching ? segments[index].stretch : segments[index].shrink;
                    const std::int64_t given = extra * flexibility_before / flexibility;
                    adjusted_glue[index - first] += static_cast<LayoutUnit>(given - given_before);
                    given_before = given;
                }
                content_width = natural + given_before;
            }
        }

        const LayoutUnit indent = measure.indent(line);
        const LayoutUnit line_left = input.right_to_left ? 0 : indent;
        const LayoutUnit line_right = input.right_to_left ? frame.width - indent : frame.width;
        const LayoutUnit content = static_cast<LayoutUnit>(content_width);
        Alignment alignment = frame.alignment;
        if (alignment == Alignment::justified) {
            alignment = Alignment::start;
        }
        LayoutUnit pen = line_left;
        const bool to_right_edge = (alignment == Alignment::start && input.right_to_left) ||
                                   (alignment == Alignment::end && !input.right_to_left);
        if (to_right_edge) {
            pen = line_right - content;
        } else if (alignment == Alignment::centered) {
            pen = line_left + (line_right - line_left - content) / 2;
        }

        LineBox box;
        box.height = frame.line_height;
        box.baseline = frame.baseline;
        visual_order(segments, first, end, order);
        for (std::size_t visual = 0U; visual < order.size(); ++visual) {
            Segment& segment = segments[order[visual]];
            for (PlacedRun& run : segment.runs) {
                run.x += pen;
                run.baseline += frame.baseline;
                box.runs.push_back(std::move(run));
            }
            for (PlacedWord& word : segment.words) {
                word.x0 += pen;
                word.x1 += pen;
                word.y0 = 0;
                word.y1 = frame.line_height;
                box.words.push_back(word);
            }
            for (CallerSlot& caller : segment.callers) {
                caller.x += pen;
                box.callers.push_back(caller);
            }
            pen += segment.width;
            if (visual + 1U < order.size()) {
                // The space between two neighbours belongs to whichever
                // comes first in reading order.
                const std::size_t here = order[visual];
                const std::size_t there = order[visual + 1U];
                const std::size_t earlier = std::min(here, there);
                if (std::max(here, there) - earlier == 1U) {
                    pen += adjusted_glue[earlier - first];
                }
            }
        }
        lines.push_back(std::move(box));
    }
}

}  // namespace pl::page
