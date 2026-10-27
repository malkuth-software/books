#include "book_layout.hpp"

#include <algorithm>
#include <map>
#include <utility>

namespace pl {
namespace {

using page::Alignment;
using page::FaceId;
using page::Ink;
using page::LayoutUnit;
using page::LineBox;
using page::ParagraphFrame;
using page::ParagraphInput;
using page::PlacedRun;
using page::PlacedRule;
using page::ShapedRun;
using page::TextStyle;
using page::Typesetter;

// The page's proportions, as fractions of the body em in hundredths: Stage 4's, from
// the interview mockup the owner chose.
constexpr std::int64_t leading_percent = 132;
constexpr std::int64_t numeral_size_percent = 62;
constexpr std::int64_t numeral_rise_percent = 55;
constexpr std::int64_t numeral_gap_percent = 20;
constexpr std::int64_t heading_size_percent = 96;
constexpr std::int64_t heading_space_above_percent = 70;
constexpr std::int64_t heading_space_below_percent = 25;
constexpr std::int64_t head_size_percent = 78;
constexpr std::int64_t head_tracking_percent = 14;
constexpr std::int64_t head_padding_percent = 50;
constexpr std::int64_t head_margin_percent = 70;
constexpr std::int64_t folio_space_percent = 90;
constexpr std::int64_t note_size_percent = 76;
constexpr std::int64_t note_margin_percent = 60;
constexpr std::int64_t note_padding_percent = 35;
constexpr std::int64_t drop_size_percent = 290;
constexpr std::int64_t drop_padding_percent = 12;
constexpr std::int64_t capitals_tracking_percent = 3;
constexpr std::int64_t small_percent = 86;
constexpr std::int64_t parallel_size_percent = 82;
constexpr std::int64_t title_size_percent = 200;
constexpr std::int64_t subtitle_size_percent = 110;
constexpr std::uint32_t drop_lines = 2U;
// Entries of a dictionary or concordance laid out together.
constexpr std::size_t entries_per_section = 60;
// Chapters of a long work without divisions of its own, laid out together.
constexpr std::size_t chapters_per_section = 8;

LayoutUnit percent_of(LayoutUnit value, std::int64_t percent) {
    const LayoutUnit scaled = page::scale_lu(value, percent, 100);
    return scaled;
}

bool is_bible(const Work& work) {
    return work.id == "bsb" || work.id == "jps1917";
}

bool all_digits(const std::string& text) {
    if (text.empty())
        return false;
    for (const char c : text)
        if (c < '0' || c > '9')
            return false;
    return true;
}

// Every style a section uses.
struct Styles {
    LayoutUnit em = 0;
    TextStyle body;
    TextStyle heading;
    TextStyle chapter_heading;
    TextStyle parallel;
    TextStyle numeral;
    TextStyle caller;
    TextStyle note;
    TextStyle note_numeral;
    TextStyle head;
    TextStyle page_number;
    TextStyle drop;
    TextStyle title;
    TextStyle subtitle;
    FaceId roman = FaceId::literata_regular;
    FaceId italic = FaceId::literata_italic;
    LayoutUnit caller_width = 0;
};

// The small-capital form of a style: the face's own where it has them, otherwise
// capitals scaled to a little over the x-height and thickened to match.
TextStyle capitals_of(const TextStyle& base, const page::FontSet& fonts) {
    TextStyle capitals = base;
    capitals.tracking = percent_of(base.em, capitals_tracking_percent);
    if (fonts.has_feature(base.face, "c2sc")) {
        capitals.features.capitals_to_small_capitals = true;
        return capitals;
    }
    const page::FaceMetrics metrics = fonts.metrics(base.face, base.em);
    if (metrics.cap_height > 0 && metrics.x_height > 0) {
        capitals.em = page::scale_lu(base.em, static_cast<std::int64_t>(metrics.x_height) * 108,
                                     static_cast<std::int64_t>(metrics.cap_height) * 100);
    }
    capitals.uppercase = true;
    capitals.embolden = percent_of(base.em - capitals.em, 8);
    return capitals;
}

Styles make_styles(const Work& work, page::TypeStep step, Typeface typeface, const page::FontSet& fonts) {
    Styles styles;
    styles.roman = roman_face(work, typeface);
    styles.italic = italic_face(work, typeface);
    styles.em = body_em(work, step, typeface);
    const LayoutUnit em = styles.em;
    styles.body.face = styles.roman;
    styles.body.em = em;
    styles.body.features.old_style_figures = true;
    styles.heading.face = styles.italic;
    styles.heading.em = percent_of(em, heading_size_percent);
    styles.chapter_heading.face = styles.roman;
    styles.chapter_heading.em = percent_of(em, heading_size_percent);
    styles.chapter_heading.uppercase = true;
    styles.chapter_heading.tracking = percent_of(em, head_tracking_percent);
    styles.chapter_heading.ink = Ink::soft;
    styles.parallel.face = styles.italic;
    styles.parallel.em = percent_of(em, parallel_size_percent);
    styles.parallel.ink = Ink::soft;
    styles.numeral.face = styles.roman;
    styles.numeral.em = percent_of(em, numeral_size_percent);
    styles.numeral.features.lining_figures = true;
    styles.numeral.ink = Ink::soft;
    styles.numeral.rise = percent_of(styles.numeral.em, numeral_rise_percent);
    styles.caller.face = styles.italic;
    styles.caller.em = styles.numeral.em;
    styles.caller.ink = Ink::accent;
    styles.caller.rise = styles.numeral.rise;
    styles.note.face = styles.roman;
    styles.note.em = percent_of(em, note_size_percent);
    styles.note.ink = Ink::soft;
    styles.note_numeral = styles.numeral;
    styles.note_numeral.em = percent_of(styles.note.em, numeral_size_percent);
    styles.note_numeral.rise = percent_of(styles.note_numeral.em, numeral_rise_percent);
    styles.head.face = styles.roman;
    styles.head.em = percent_of(em, head_size_percent);
    styles.head.ink = Ink::soft;
    styles.head.uppercase = true;
    styles.head.tracking = percent_of(styles.head.em, head_tracking_percent);
    styles.page_number = styles.head;
    styles.page_number.uppercase = false;
    styles.page_number.tracking = 0;
    styles.page_number.features.lining_figures = true;
    styles.drop.face = styles.roman;
    styles.drop.em = percent_of(em, drop_size_percent);
    styles.drop.features.lining_figures = true;
    styles.title.face = styles.roman;
    styles.title.em = percent_of(em, title_size_percent);
    styles.subtitle.face = styles.italic;
    styles.subtitle.em = percent_of(em, subtitle_size_percent);
    styles.caller_width =
        fonts.glyph_advance(styles.caller.face, styles.caller.em, fonts.glyph_of(styles.caller.face, U'n'));
    return styles;
}

ParagraphFrame frame_for(const page::FontSet& fonts, const TextStyle& style, LayoutUnit line_height) {
    const page::FaceMetrics metrics = fonts.metrics(style.face, style.em);
    ParagraphFrame frame;
    frame.line_height = line_height;
    frame.baseline = (line_height - metrics.ascender - metrics.descender) / 2 + metrics.ascender;
    return frame;
}

// The set lines of a section, before any page exists.
struct Flow {
    std::vector<LineBox> lines;
    std::vector<std::size_t> line_chapter;          // per line, the chapter it belongs to
    std::vector<std::vector<LineBox>> notes;        // per footnote of the section
    std::vector<bool> page_break_before;            // per line: a division opens here
};

// One paragraph being gathered: its text, the style of every byte, its numerals and
// callers. Styles are made as the flags call for them.
struct ParagraphBuilder {
    ParagraphInput input;
    TextStyle base;
    FaceId italic = FaceId::literata_italic;
    std::map<std::uint16_t, std::uint8_t> style_index;
    std::vector<std::uint16_t> style_key;
    const page::FontSet* fonts = nullptr;
    bool word_open = false;

    static constexpr std::uint16_t capitals_key = 0x100;

    void reset(const TextStyle& paragraph_style, FaceId italic_face) {
        input = ParagraphInput{};
        base = paragraph_style;
        italic = italic_face;
        style_index.clear();
        style_key.clear();
        word_open = false;
    }

    std::uint8_t index_for(std::uint16_t key) {
        const std::map<std::uint16_t, std::uint8_t>::const_iterator found = style_index.find(key);
        if (found != style_index.end())
            return (*found).second;
        TextStyle style = base;
        if ((key & span_italic) != 0)
            style.face = style.face == italic ? base.face : italic;
        if ((key & span_red) != 0)
            style.ink = Ink::red;
        if ((key & span_small) != 0)
            style.em = percent_of(style.em, small_percent);
        if ((key & span_bold) != 0)
            style.embolden = percent_of(style.em, 3);
        if ((key & capitals_key) != 0)
            style = capitals_of(style, *fonts);
        const std::uint8_t index = static_cast<std::uint8_t>(input.styles.size());
        input.styles.push_back(style);
        style_key.push_back(key);
        style_index[key] = index;
        return index;
    }

    void append(std::string_view text, std::uint8_t flags) {
        const std::uint8_t style = index_for(flags);
        for (const char byte : text) {
            // A run of spaces reads as one; a paragraph does not open with one.
            if (byte == ' ' && (input.text.empty() || input.text.back() == ' '))
                continue;
            input.text.push_back(byte);
            input.style_of_byte.push_back(style);
            input.word_of_byte.push_back(page::no_word);
        }
    }

    void numeral(const std::string& label) {
        ParagraphInput::Numeral made;
        made.offset = input.text.size();
        made.digits = label;
        input.numerals.push_back(made);
    }

    void caller(std::uint32_t footnote) {
        ParagraphInput::Caller made;
        made.offset = input.text.size();
        made.footnote = footnote;
        input.callers.push_back(made);
    }

    void trim() {
        while (!input.text.empty() && input.text.back() == ' ') {
            input.text.pop_back();
            input.style_of_byte.pop_back();
            input.word_of_byte.pop_back();
        }
    }

    // A Bible prints the divine name as LORD and GOD in capitals; the page sets the
    // letters after the first in small capitals.
    void mark_small_capitals() {
        const std::string_view text = input.text;
        const std::string_view names[2] = {"LORD", "GOD"};
        for (const std::string_view name : names) {
            std::size_t position = text.find(name);
            while (position != std::string_view::npos) {
                const std::size_t after = position + name.size();
                const bool starts = position == 0 || !(text[position - 1] >= 'A' && text[position - 1] <= 'Z');
                const bool ends = after >= text.size() || !(text[after] >= 'A' && text[after] <= 'Z');
                if (starts && ends) {
                    for (std::size_t i = position + 1; i < after; ++i) {
                        const std::uint16_t key = style_key[input.style_of_byte[i]];
                        input.style_of_byte[i] = index_for(static_cast<std::uint16_t>(key | capitals_key));
                    }
                }
                position = text.find(name, after);
            }
        }
    }
};

// "Gen 1:1, 2, 5; 2:3; Exo 3:4": verses grouped by book and chapter.
std::string references(const std::vector<std::uint32_t>& places, const LayoutOptions& options) {
    std::string out;
    if (options.places == nullptr || options.bible == nullptr) {
        for (std::size_t i = 0; i < places.size(); ++i) {
            if (i > 0)
                out += ", ";
            out += std::to_string(places[i]);
        }
        return out;
    }
    const Versification& versification = *options.places;
    int last_division = -1;
    int last_chapter = -1;
    for (const std::uint32_t place : places) {
        if (place == 0 || place > versification.places.size())
            continue;
        const Versification::Place& where = versification.places[place - 1];
        const Division& division = (*options.bible).divisions[where.division];
        const std::string& chapter = division.chapters[where.chapter].label;
        if (where.division != last_division) {
            if (!out.empty())
                out += "; ";
            out += division.abbreviation + " " + chapter + ":" + where.verse;
        } else if (where.chapter != last_chapter) {
            out += "; " + chapter + ":" + where.verse;
        } else {
            out += ", " + where.verse;
        }
        last_division = where.division;
        last_chapter = where.chapter;
    }
    return out;
}

void set_note_lines(const Note& note, const Styles& styles, Typesetter& typesetter, std::vector<LineBox>& out) {
    ParagraphBuilder builder;
    builder.fonts = &typesetter.fonts();
    builder.reset(styles.note, styles.italic);
    builder.input.numeral_style = styles.note_numeral;
    builder.input.numeral_gap = percent_of(styles.note_numeral.em, numeral_gap_percent);
    builder.append(note.label, 0);
    builder.append(" ", 0);
    for (const Inline& item : note.inlines) {
        if (item.kind == InlineKind::text)
            builder.append(item.text, item.flags);
        else if (item.kind == InlineKind::verse)
            builder.numeral(item.text);
    }
    builder.trim();
    ParagraphFrame frame = frame_for(typesetter.fonts(), styles.note, percent_of(styles.note.em, leading_percent));
    frame.alignment = Alignment::start;
    frame.first_indent = styles.caller_width + percent_of(styles.note.em, 55);
    typesetter.set_paragraph(builder.input, frame, out);
}

// Appends the lines of one paragraph, marking them for the chapter they belong to.
std::size_t add_lines(Flow& flow, Typesetter& typesetter, const ParagraphInput& input, const ParagraphFrame& frame,
                      std::size_t chapter, LayoutUnit space_before, bool keep_with_next) {
    const std::size_t first = flow.lines.size();
    typesetter.set_paragraph(input, frame, flow.lines);
    for (std::size_t i = first; i < flow.lines.size(); ++i) {
        flow.line_chapter.push_back(chapter);
        flow.page_break_before.push_back(false);
        flow.lines[i].keep_with_next = keep_with_next;
    }
    if (flow.lines.size() > first)
        flow.lines[first].space_before = space_before;
    return flow.lines.size() - first;
}

void centred_line(Flow& flow, Typesetter& typesetter, const Styles& styles, const TextStyle& style,
                  const std::string& text, std::size_t chapter, LayoutUnit space_before) {
    ParagraphBuilder builder;
    builder.fonts = &typesetter.fonts();
    builder.reset(style, styles.italic);
    builder.append(text, 0);
    builder.trim();
    if (builder.input.text.empty())
        return;
    ParagraphFrame frame = frame_for(typesetter.fonts(), style, percent_of(style.em, leading_percent));
    frame.alignment = Alignment::centered;
    static_cast<void>(add_lines(flow, typesetter, builder.input, frame, chapter, space_before, true));
}

void build_flow(const Work& work, const Section& section, const Styles& styles, const LayoutOptions& options,
                Typesetter& typesetter, Flow& flow) {
    const page::FontSet& fonts = typesetter.fonts();
    const Division& division = work.divisions[section.division];
    const bool bible = is_bible(work);
    const LayoutUnit em = styles.em;
    const LayoutUnit line_height = percent_of(em, leading_percent);

    // A division opens with its titles.
    if (section.first_chapter == 0) {
        // Empty title lines are skipped (the BSB's USFM leaves \mt1 empty where a book
        // has one title line); the last line that remains is the main title.
        std::vector<std::string> titles;
        for (const std::string& line : division.titles)
            if (!line.empty())
                titles.push_back(line);
        if (titles.empty())
            titles.push_back(division.title);
        for (std::size_t i = 0; i < titles.size(); ++i) {
            const bool main = i + 1 == titles.size();
            centred_line(flow, typesetter, styles, main ? styles.title : styles.subtitle, titles[i],
                         section.first_chapter, i == 0 ? em * 3 : em / 3);
        }
        if (!flow.lines.empty())
            flow.lines.back().keep_with_next = false;
    }

    LayoutUnit pending = flow.lines.empty() ? 0 : em * 2;
    std::uint32_t note_base = 0;
    for (std::size_t c = section.first_chapter; c < section.end_chapter; ++c) {
        const Chapter& chapter = division.chapters[c];
        for (const Note& note : chapter.notes) {
            std::vector<LineBox> lines;
            set_note_lines(note, styles, typesetter, lines);
            flow.notes.push_back(std::move(lines));
        }
        // How the chapter opens: a drop numeral in a Bible, a heading in a treatise,
        // nothing before a dictionary entry.
        const bool drop = bible && all_digits(chapter.label);
        std::uint32_t drop_remaining = drop ? drop_lines : 0U;
        bool drop_placed = false;
        LayoutUnit drop_width = 0;
        if (drop) {
            const ShapedRun& numeral = typesetter.shaped(styles.drop, chapter.label, false);
            drop_width = numeral.advance + percent_of(styles.drop.em, drop_padding_percent);
            pending = std::max(pending, line_height / 2);
        } else if (!chapter.entry && !chapter.label.empty() &&
                   !(!chapter.blocks.empty() && chapter.blocks.front().style == BlockStyle::heading)) {
            const std::string heading = work.id == "berakot" ? "Folio " + chapter.label : "Chapter " + chapter.label;
            centred_line(flow, typesetter, styles, styles.chapter_heading, heading, c,
                         std::max(pending, percent_of(em, folio_space_percent)));
            pending = percent_of(em, heading_space_below_percent);
        }

        bool indent_next = false;
        ParagraphBuilder builder;
        builder.fonts = &fonts;
        for (const Block& block : chapter.blocks) {
            if (block.style == BlockStyle::blank) {
                if (drop_remaining == 0U || !drop_placed)
                    pending = std::max(pending, line_height / 2);
                continue;
            }
            const bool heading = block.style == BlockStyle::heading || block.style == BlockStyle::acrostic;
            const bool chapter_title = block.style == BlockStyle::heading && block.level == 0;
            const bool parallel = block.style == BlockStyle::parallel;
            TextStyle base = styles.body;
            if (chapter_title)
                base = styles.chapter_heading;
            else if (heading)
                base = styles.heading;
            else if (parallel)
                base = styles.parallel;
            else if (block.style == BlockStyle::note)
                base = styles.note;
            else if (block.style == BlockStyle::title || block.style == BlockStyle::right) {
                base = styles.heading;
                base.em = em;
            }
            builder.reset(base, styles.italic);
            builder.input.numeral_style = styles.numeral;
            builder.input.numeral_gap = percent_of(styles.numeral.em, numeral_gap_percent);
            builder.input.caller_width = styles.caller_width;

            for (const Inline& item : block.inlines) {
                if (item.kind == InlineKind::text) {
                    builder.append(item.text, item.flags);
                } else if (item.kind == InlineKind::verse) {
                    // The drop numeral stands for the chapter's first verse.
                    if (!(drop && item.text == "1"))
                        builder.numeral(item.text);
                } else {
                    builder.caller(note_base + item.note);
                }
            }
            if (block.style == BlockStyle::rendering) {
                builder.append(" (" + std::to_string(block.places.size()) + "): ", 0);
                builder.append(references(block.places, options), 0);
            }
            if (bible)
                builder.mark_small_capitals();
            builder.trim();
            if (builder.input.text.empty())
                continue;

            ParagraphFrame frame = frame_for(fonts, base, percent_of(base.em, leading_percent));
            LayoutUnit space = pending;
            switch (block.style) {
            case BlockStyle::heading:
            case BlockStyle::acrostic:
                frame.alignment = Alignment::centered;
                space = std::max(pending, percent_of(base.em, chapter_title ? folio_space_percent * 2
                                                                              : heading_space_above_percent));
                break;
            case BlockStyle::quote:
                frame.alignment = Alignment::justified;
                frame.first_indent = em * 2;
                frame.rest_indent = em * 2;
                frame.width = page::content_width_lu - em * 2;
                break;
            case BlockStyle::parallel:
                frame.alignment = Alignment::centered;
                break;
            case BlockStyle::paragraph:
            case BlockStyle::folio:
                frame.alignment = Alignment::justified;
                frame.first_indent = indent_next ? em : 0;
                break;
            case BlockStyle::flush:
            case BlockStyle::verse:
                frame.alignment = Alignment::justified;
                break;
            case BlockStyle::centre:
                frame.alignment = Alignment::centered;
                break;
            case BlockStyle::poetry:
                frame.alignment = Alignment::start;
                frame.first_indent = block.level > 1U ? em : 0;
                frame.rest_indent = frame.first_indent + em;
                break;
            case BlockStyle::list:
                frame.alignment = Alignment::start;
                frame.first_indent = block.level > 1U ? em * 2 : em;
                frame.rest_indent = frame.first_indent + em;
                break;
            case BlockStyle::right:
                frame.alignment = Alignment::end;
                break;
            case BlockStyle::title:
                frame.alignment = Alignment::start;
                break;
            case BlockStyle::note:
                frame.alignment = Alignment::justified;
                frame.first_indent = em;
                frame.rest_indent = em;
                break;
            case BlockStyle::entry:
            case BlockStyle::rendering:
                frame.alignment = Alignment::justified;
                frame.rest_indent = em;
                frame.first_indent = block.style == BlockStyle::rendering ? em : 0;
                if (block.style == BlockStyle::rendering)
                    frame.rest_indent = em * 2;
                break;
            case BlockStyle::blank:
                break;
            }
            const bool takes_drop = !heading && !parallel && drop_remaining > 0U;
            if (takes_drop) {
                frame.narrowed_lines = drop_remaining;
                frame.narrowed_by = drop_width;
            }
            const std::size_t first_line = flow.lines.size();
            const std::size_t count = add_lines(flow, typesetter, builder.input, frame, c, space, heading || parallel);
            pending = 0;
            if (count == 0)
                continue;
            if (takes_drop) {
                if (!drop_placed) {
                    const ShapedRun& numeral = typesetter.shaped(styles.drop, chapter.label, false);
                    PlacedRun run;
                    run.face = styles.drop.face;
                    run.em = styles.drop.em;
                    run.ink = styles.drop.ink;
                    run.x = 0;
                    run.baseline = frame.baseline + line_height * static_cast<LayoutUnit>(drop_lines - 1U);
                    run.glyphs = numeral.glyphs;
                    flow.lines[first_line].runs.push_back(run);
                    drop_placed = true;
                }
                drop_remaining -= static_cast<std::uint32_t>(std::min<std::size_t>(drop_remaining, count));
            }
            if (heading)
                pending = percent_of(base.em, heading_space_below_percent);
            indent_next = !heading && !parallel;
            if (block.style == BlockStyle::entry || block.style == BlockStyle::rendering)
                indent_next = false;
            if (block.style == BlockStyle::entry && block.inlines.size() > 0 && chapter.entry)
                pending = 0;
        }
        if (chapter.entry)
            pending = percent_of(em, 30);
        note_base += static_cast<std::uint32_t>(chapter.notes.size());
    }
}

// ---- Pages ---------------------------------------------------------------------------

struct PageMetrics {
    LayoutUnit head_height = 0;
    LayoutUnit foot_height = 0;
    LayoutUnit body_height = 0;
    LayoutUnit note_margin = 0;
    LayoutUnit note_padding = 0;
};

PageMetrics page_metrics(const Styles& styles) {
    PageMetrics metrics;
    const LayoutUnit head_line = percent_of(styles.head.em, leading_percent);
    metrics.head_height = head_line + percent_of(styles.head.em, head_padding_percent) +
                          percent_of(styles.head.em, head_margin_percent);
    metrics.foot_height = head_line + percent_of(styles.head.em, head_margin_percent);
    metrics.body_height = page::content_height_lu - metrics.head_height - metrics.foot_height;
    metrics.note_margin = percent_of(styles.note.em, note_margin_percent);
    metrics.note_padding = percent_of(styles.note.em, note_padding_percent);
    return metrics;
}

struct PageDraft {
    std::vector<std::size_t> lines;
    std::vector<std::uint32_t> notes;
    LayoutUnit body = 0;
    LayoutUnit notes_height = 0;
};

LayoutUnit note_height(const Flow& flow, std::uint32_t footnote) {
    LayoutUnit height = 0;
    for (const LineBox& line : flow.notes[footnote])
        height += line.height;
    return height;
}

bool try_add(const Flow& flow, const PageMetrics& metrics, std::size_t index, PageDraft& draft) {
    const LineBox& line = flow.lines[index];
    const LayoutUnit body = draft.body + (draft.lines.empty() ? 0 : line.space_before) + line.height;
    LayoutUnit notes_height = draft.notes_height;
    for (const page::CallerSlot& caller : line.callers) {
        if (notes_height == 0)
            notes_height = metrics.note_margin + metrics.note_padding;
        notes_height += note_height(flow, caller.footnote);
    }
    if (!draft.lines.empty() && body + notes_height > metrics.body_height)
        return false;
    draft.lines.push_back(index);
    draft.body = body;
    draft.notes_height = notes_height;
    for (const page::CallerSlot& caller : line.callers)
        draft.notes.push_back(caller.footnote);
    return true;
}

// A page must not end on a heading: lines that keep with the next are taken back.
std::size_t take_back_headings(const Flow& flow, PageDraft& draft) {
    std::size_t taken = 0;
    while (draft.lines.size() > 1 && flow.lines[draft.lines.back()].keep_with_next) {
        draft.lines.pop_back();
        ++taken;
    }
    return taken;
}

void place_text(const std::string& text, const TextStyle& style, LayoutUnit x, LayoutUnit baseline,
                Alignment alignment, Typesetter& typesetter, page::PageLayout& out) {
    const ShapedRun& shaped = typesetter.shaped(style, text, false);
    PlacedRun run;
    run.face = style.face;
    run.em = style.em;
    run.ink = style.ink;
    run.x = x;
    if (alignment == Alignment::end)
        run.x = x - shaped.advance;
    else if (alignment == Alignment::centered)
        run.x = x - shaped.advance / 2;
    run.baseline = baseline;
    run.glyphs = shaped.glyphs;
    out.runs.push_back(run);
}

void place_line(const LineBox& line, LayoutUnit top, page::PageLayout& out) {
    for (const PlacedRun& source : line.runs) {
        PlacedRun run = source;
        run.x += page::content_left_lu;
        run.baseline += top;
        out.runs.push_back(run);
    }
    for (const page::PlacedWord& source : line.words) {
        page::PlacedWord word = source;
        word.x0 += page::content_left_lu;
        word.x1 += page::content_left_lu;
        word.y0 = top;
        word.y1 = top + line.height;
        out.words.push_back(word);
    }
}

std::string running_head(const Work& work, const Division& division, const Flow& flow, const PageDraft& draft) {
    if (draft.lines.empty())
        return std::string();
    const Chapter& first = division.chapters[flow.line_chapter[draft.lines.front()]];
    const Chapter& last = division.chapters[flow.line_chapter[draft.lines.back()]];
    if (first.entry)
        return first.label == last.label ? first.label : first.label + " – " + last.label;
    if (is_bible(work) && all_digits(first.label))
        return division.short_title + " " + first.label;
    if (work.id == "berakot" && !first.label.empty())
        return division.short_title + " " + first.label;
    return division.short_title;
}

page::PageLayout make_page(const Work& work, const Division& division, const Flow& flow, const PageDraft& draft,
                           const PageMetrics& metrics, const Styles& styles, std::uint32_t number,
                           Typesetter& typesetter) {
    page::PageLayout out;
    out.number = number;
    const bool right_page = number % 2U == 0U;
    const page::FontSet& fonts = typesetter.fonts();
    const LayoutUnit head_line = percent_of(styles.head.em, leading_percent);
    const ParagraphFrame head_frame = frame_for(fonts, styles.head, head_line);
    out.head = running_head(work, division, flow, draft);
    if (!out.head.empty()) {
        const LayoutUnit x = right_page ? page::content_left_lu + page::content_width_lu : page::content_left_lu;
        place_text(out.head, styles.head, x, page::content_top_lu + head_frame.baseline,
                   right_page ? Alignment::end : Alignment::start, typesetter, out);
        PlacedRule rule;
        rule.x0 = page::content_left_lu;
        rule.x1 = page::content_left_lu + page::content_width_lu;
        rule.y0 = page::content_top_lu + head_line + percent_of(styles.head.em, head_padding_percent);
        rule.y1 = rule.y0;
        out.rules.push_back(rule);
    }
    // The page number at the foot, centred.
    if (!draft.lines.empty()) {
        const LayoutUnit foot = page::content_top_lu + page::content_height_lu;
        place_text(std::to_string(number), styles.page_number, page::content_left_lu + page::content_width_lu / 2,
                   foot - head_line + head_frame.baseline, Alignment::centered, typesetter, out);
    }

    LayoutUnit top = page::content_top_lu + metrics.head_height;
    char letter = 'a';
    bool first = true;
    for (const std::size_t index : draft.lines) {
        const LineBox& line = flow.lines[index];
        if (!first)
            top += line.space_before;
        first = false;
        place_line(line, top, out);
        for (const page::CallerSlot& caller : line.callers) {
            place_text(std::string(1, letter), styles.caller, page::content_left_lu + caller.x,
                       top + line.baseline - styles.caller.rise, Alignment::start, typesetter, out);
            letter = letter == 'z' ? 'a' : static_cast<char>(letter + 1);
        }
        top += line.height;
    }

    if (!draft.notes.empty()) {
        const LayoutUnit foot = page::content_top_lu + metrics.head_height + metrics.body_height;
        LayoutUnit note_top = foot - draft.notes_height + metrics.note_margin;
        PlacedRule rule;
        rule.x0 = page::content_left_lu;
        rule.x1 = page::content_left_lu + page::content_width_lu / 4;
        rule.y0 = note_top;
        rule.y1 = note_top;
        out.rules.push_back(rule);
        note_top += metrics.note_padding;
        TextStyle letter_style = styles.note;
        letter_style.face = styles.italic;
        letter_style.ink = Ink::accent;
        char note_letter = 'a';
        for (const std::uint32_t footnote : draft.notes) {
            const std::vector<LineBox>& lines = flow.notes[footnote];
            if (!lines.empty())
                place_text(std::string(1, note_letter), letter_style, page::content_left_lu,
                           note_top + lines.front().baseline, Alignment::start, typesetter, out);
            for (const LineBox& line : lines) {
                place_line(line, note_top, out);
                note_top += line.height;
            }
            note_letter = note_letter == 'z' ? 'a' : static_cast<char>(note_letter + 1);
        }
    }
    return out;
}

// The reader's choice of face, or the book's own: the Bible in Garamond.
bool set_in_garamond(const Work& work, Typeface typeface) {
    if (typeface == Typeface::as_set)
        return work.id == "bsb";
    return typeface == Typeface::garamond;
}

}  // namespace

FaceId roman_face(const Work& work, Typeface typeface) {
    return set_in_garamond(work, typeface) ? FaceId::eb_garamond_regular : FaceId::literata_regular;
}

LayoutUnit body_em(const Work& work, page::TypeStep step, Typeface typeface) {
    const LayoutUnit nominal = page::body_em_lu(step);
    return roman_face(work, typeface) == FaceId::literata_regular ? percent_of(nominal, 92) : nominal;
}

FaceId italic_face(const Work& work, Typeface typeface) {
    return set_in_garamond(work, typeface) ? FaceId::eb_garamond_italic : FaceId::literata_italic;
}

void plan_sections(const Work& work, std::vector<Section>& out) {
    out.clear();
    for (std::size_t d = 0; d < work.divisions.size(); ++d) {
        const Division& division = work.divisions[d];
        const std::size_t count = division.chapters.size();
        const bool entries = count > 0 && division.chapters.back().entry;
        const bool many = work.divisions.size() == 1 && count > chapters_per_section * 4;
        const std::size_t step = entries ? entries_per_section : (many ? chapters_per_section : count);
        for (std::size_t first = 0; first < count || (count == 0 && first == 0); first += std::max<std::size_t>(step, 1)) {
            Section section;
            section.division = d;
            section.first_chapter = first;
            section.end_chapter = std::min(count, first + std::max<std::size_t>(step, 1));
            if (step >= count) {
                section.title = division.short_title;
            } else {
                const std::string& from = division.chapters[first].label;
                const std::string& to = division.chapters[section.end_chapter - 1].label;
                section.title = from == to ? from : from + " – " + to;
            }
            out.push_back(section);
            if (count == 0)
                break;
        }
    }
}

void lay_out_section(const Work& work, const Section& section, const LayoutOptions& options, Typesetter& typesetter,
                     std::vector<page::PageLayout>& pages) {
    pages.clear();
    if (section.division >= work.divisions.size())
        return;
    const Division& division = work.divisions[section.division];
    const Styles styles = make_styles(work, options.step, options.typeface, typesetter.fonts());
    const PageMetrics metrics = page_metrics(styles);
    Flow flow;
    build_flow(work, section, styles, options, typesetter, flow);
    std::size_t index = 0;
    std::uint32_t number = 1;
    while (index < flow.lines.size()) {
        PageDraft draft;
        while (index < flow.lines.size() && try_add(flow, metrics, index, draft))
            ++index;
        if (index < flow.lines.size())
            index -= take_back_headings(flow, draft);
        pages.push_back(make_page(work, division, flow, draft, metrics, styles, number, typesetter));
        ++number;
    }
    if (pages.size() % 2U == 1U) {
        page::PageLayout blank;
        blank.number = number;
        pages.push_back(blank);
    }
}

}  // namespace pl
