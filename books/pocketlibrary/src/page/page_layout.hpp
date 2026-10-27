#pragma once
// One page as laid out: everything the rasterizer, the hit map and the accessibility
// graph need, in layout units, and nothing that depends on the window.
#include "page/text_layout.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pl::page {

// A filled rectangle in page coordinates, y downward. When y1 == y0 it is a hairline:
// one device pixel high at any scale.
struct PlacedRule final {
    LayoutUnit x0{0};
    LayoutUnit y0{0};
    LayoutUnit x1{0};
    LayoutUnit y1{0};
    Ink ink{Ink::soft};
};

// One stretch of text as a screen reader should hear it (a verse, an entry, a
// paragraph), with the box its words take on this page.
struct PageSpeech final {
    std::string text{};
    LayoutUnit x0{0};
    LayoutUnit y0{0};
    LayoutUnit x1{0};
    LayoutUnit y1{0};
};

struct PageLayout final {
    // The running head, as the book's own short title.
    std::string head{};
    // The page's number within its section, from 1.
    std::uint32_t number{0U};
    std::vector<PlacedRun> runs{};
    std::vector<PlacedRule> rules{};
    // Sorted by line (top to bottom), then left to right.
    std::vector<PlacedWord> words{};
    std::vector<PageSpeech> speech{};
};

}  // namespace pl::page
