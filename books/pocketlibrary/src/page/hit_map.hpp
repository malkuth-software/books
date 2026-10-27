#pragma once

#include "page/page_layout.hpp"
#include "page/page_raster.hpp"

#include <cstdint>
#include <vector>

namespace pl::page {

// A word's rectangle on the window, in device pixels: [x0, x1) by [y0, y1).
struct HitWord final {
    std::int32_t x0{0};
    std::int32_t y0{0};
    std::int32_t x1{0};
    std::int32_t y1{0};
    WordIdentity word{};
};

// Pointer to word identity for one page as it stands on the window
// (ARCHITECTURE_HYPOTHESES section 3, step 6). The words of a line share its
// top and bottom, and lines do not overlap, so the rectangles sorted by top
// and then by left edge are searched twice by bisection: once for the line,
// once along it.
class HitMap final {
  public:
    // Replaces the map with `page` as drawn at `rect`.
    void assign(const PageLayout& page, PageRect rect);

    // The word at a device pixel, or nullptr between words and off the page.
    // The pointer is valid until the next assign.
    [[nodiscard]] const HitWord* find(std::int32_t x, std::int32_t y) const noexcept;

    [[nodiscard]] const std::vector<HitWord>& words() const noexcept {
        return words_;
    }

  private:
    std::vector<HitWord> words_{};
};

}  // namespace pl::page
