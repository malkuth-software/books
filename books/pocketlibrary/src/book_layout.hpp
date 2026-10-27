#pragma once
// Turns a book into pages: Stage 4's page compositor (the Pocket Bible) carried from
// three proving passages to any work of the library. A work is laid out a section at a
// time (a book of the Bible, a run of dictionary entries), when it is opened, in units
// that do not depend on the window, so its pages are the same at every size and scale.
//
// English only. The Bible is set in EB Garamond, every other book in Literata, unless the
// reader chooses one face for every book.
#include "book.hpp"
#include "page/page_layout.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace pl {

// A run of chapters laid out together and turned through as one.
struct Section {
    std::size_t division = 0;
    std::size_t first_chapter = 0;
    std::size_t end_chapter = 0;  // one past the last
    std::string title;            // for a contents page: "Genesis", "H1–H100"
};

// The work's sections, in reading order.
void plan_sections(const Work& work, std::vector<Section>& out);

// The face the reader chose: each book as it is set, or one face for all.
enum class Typeface { as_set, literata, garamond };

struct LayoutOptions {
    page::TypeStep step = page::TypeStep::medium;
    Typeface typeface = Typeface::as_set;
    // For the concordance: the BSB's versification, to name the verses it lists. When
    // empty, a verse is given by its place number.
    const Work* bible = nullptr;
    const Versification* places = nullptr;
};

// The faces a work is set in.
[[nodiscard]] page::FaceId roman_face(const Work& work, Typeface typeface = Typeface::as_set);
// The body type's size for a work at a step: Literata, whose lowercase is taller, is
// set 8% under EB Garamond's size so the two read alike.
[[nodiscard]] page::LayoutUnit body_em(const Work& work, page::TypeStep step, Typeface typeface = Typeface::as_set);
[[nodiscard]] page::FaceId italic_face(const Work& work, Typeface typeface = Typeface::as_set);

// Lays out one section. `pages` is replaced; there is always an even number, the
// first a left-hand page.
void lay_out_section(const Work& work, const Section& section, const LayoutOptions& options,
                     page::Typesetter& typesetter, std::vector<page::PageLayout>& pages);

}  // namespace pl
