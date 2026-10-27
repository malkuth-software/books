#pragma once
// A book of the library as the reader holds it: its divisions (the books of a Bible,
// a tractate, a dictionary), their chapters or entries, and the blocks and notes of
// each, read from the normalized records tools/import_books.py writes (its docstring
// is the grammar). Nothing here knows about pages or pixels.
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pl {

// How a stretch of text is set; several may hold at once.
enum SpanFlag : std::uint8_t {
    span_italic = 1U,
    span_small = 2U,   // set smaller, as the JPS sets its repeated verses
    span_red = 4U,     // the words of Christ (the BSB's \wj)
    span_hebrew = 8U,
    span_greek = 16U,
    span_bold = 32U,
};

enum class InlineKind : std::uint8_t {
    text,
    verse,      // a verse number opens here; `text` is its label
    note_call,  // a footnote is called here; `note` is its index in the chapter
};

struct Inline {
    InlineKind kind = InlineKind::text;
    std::string text;
    std::uint8_t flags = 0U;
    std::uint32_t note = 0U;
};

// The block styles of the normalized form (import_books.py's STYLES), and the
// headings that are records of their own there.
enum class BlockStyle : std::uint8_t {
    paragraph,     // p: indented first line
    flush,         // flush: no indent
    centre,
    poetry,        // q1, q2: `level` 1 or 2
    right,         // a line set to the right (Selah)
    acrostic,      // a Hebrew letter heading a stanza of Psalm 119
    list,          // list1, list2: `level` 1 or 2
    blank,         // a blank line between stanzas
    title,         // a psalm's title, a colophon
    verse,         // a verse to itself, as the JPS is printed
    note,          // a note printed in the text (Gorfinkle's)
    quote,         // a quotation set in from both sides
    entry,         // a dictionary or concordance entry
    folio,         // a passage of the Talmud
    heading,       // a section heading; `level` 0 (a chapter's or major title), 1 or 2
    parallel,      // the parallel passages under a heading
    rendering,     // a concordance line: an English rendering and its verses
};

struct Block {
    BlockStyle style = BlockStyle::paragraph;
    std::uint8_t level = 0U;
    std::vector<Inline> inlines;
    // Concordance only: the verses, as places in the BSB (1 for Genesis 1:1).
    std::vector<std::uint32_t> places;
};

struct Note {
    std::string label;  // the source's reference ("1:1")
    std::vector<Inline> inlines;
};

// A chapter, a folio side, a dictionary entry, or the front matter before the first.
struct Chapter {
    std::string label;  // "1", "2a", "H430"; empty for front matter
    bool entry = false; // a dictionary or concordance entry rather than a chapter
    std::vector<Block> blocks;
    std::vector<Note> notes;
};

struct Division {
    std::string id;      // "GEN", "Genesis", "Hebrew"
    std::string title;   // "The First Book of Moses: Genesis"
    std::string short_title;
    std::string abbreviation;         // in references ("Gen"); the short title when none
    std::vector<std::string> titles;  // its title lines, main title last
    std::vector<Chapter> chapters;
};

struct Work {
    std::string id;  // the normalized file's name: "bsb", "jps1917", ...
    // The head: where the text came from, its licence, what was dropped.
    std::vector<std::pair<std::string, std::string>> head;
    std::vector<Division> divisions;
};

// Reads a work from its normalized records. False, leaving `work` empty, when a record
// is malformed or out of place.
[[nodiscard]] bool read_work(std::string_view id, std::string_view records, Work& work);

// The BSB's verses in order, so a place (1 for Genesis 1:1) can be named.
struct Versification {
    struct Place {
        std::uint16_t division = 0;
        std::uint16_t chapter = 0;  // index into the division's chapters
        std::string verse;
    };
    std::vector<Place> places;  // places[0] is Genesis 1:1
};
void versify(const Work& bible, Versification& out);

}  // namespace pl
