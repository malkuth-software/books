#include "page/hit_map.hpp"

#include <algorithm>
#include <cstddef>

namespace pl::page {
namespace {

// Layout units to whole device pixels on a page `page_height` pixels high.
std::int32_t to_pixel(LayoutUnit value, std::uint32_t page_height) noexcept {
    const std::int64_t scaled = static_cast<std::int64_t>(value) * page_height;
    const std::int64_t pixel = (scaled + page_height_lu / 2) / page_height_lu;
    return static_cast<std::int32_t>(pixel);
}

bool above_then_left(const HitWord& left, const HitWord& right) noexcept {
    if (left.y0 != right.y0) {
        return left.y0 < right.y0;
    }
    return left.x0 < right.x0;
}

} // namespace

void HitMap::assign(const PageLayout& page, PageRect rect) {
    words_.clear();
    words_.reserve(page.words.size());
    for (const PlacedWord& placed : page.words) {
        HitWord word;
        word.x0 = rect.x + to_pixel(placed.x0, rect.height);
        word.y0 = rect.y + to_pixel(placed.y0, rect.height);
        word.x1 = rect.x + to_pixel(placed.x1, rect.height);
        word.y1 = rect.y + to_pixel(placed.y1, rect.height);
        word.word = placed.word;
        if (word.x1 > word.x0 && word.y1 > word.y0) {
            words_.push_back(word);
        }
    }
    std::stable_sort(words_.begin(), words_.end(), above_then_left);
}

const HitWord* HitMap::find(std::int32_t x, std::int32_t y) const noexcept {
    // The last line whose top is at or above y: the first word past it by
    // bisection, then back one.
    std::size_t low = 0U;
    std::size_t high = words_.size();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2U;
        if (words_[middle].y0 <= y) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    if (low == 0U) {
        return nullptr;
    }
    const std::size_t line_end = low;
    const std::int32_t line_top = words_[line_end - 1U].y0;
    if (y >= words_[line_end - 1U].y1) {
        return nullptr;
    }
    // The first word of that line.
    low = 0U;
    high = line_end;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2U;
        if (words_[middle].y0 < line_top) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    const std::size_t line_begin = low;
    // Along the line: the last word whose left edge is at or before x.
    high = line_end;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2U;
        if (words_[middle].x0 <= x) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    if (low == line_begin) {
        return nullptr;
    }
    const HitWord& candidate = words_[low - 1U];
    if (x >= candidate.x1) {
        return nullptr;
    }
    return &candidate;
}

}  // namespace pl::page
