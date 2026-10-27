#pragma once
// The library's type: its four faces read from the game's assets, the typesetter that
// lays them out and the rasterizer that prints them. One per view, on the view's
// thread. Without its faces (a test run with no prepared assets) it is not ready, and
// the scenes print plain pages.
#include "page/font_set.hpp"
#include "page/page_raster.hpp"
#include "page/text_layout.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace pl {

class Printer {
  public:
    Printer();
    ~Printer();
    // Reads the faces once. True when all four loaded.
    bool load();
    [[nodiscard]] bool ready() const {
        return ready_;
    }
    [[nodiscard]] page::FontSet& fonts() {
        return *fonts_;
    }
    [[nodiscard]] page::Typesetter& typesetter() {
        return *typesetter_;
    }
    [[nodiscard]] page::PageRasterizer& rasterizer() {
        return *rasterizer_;
    }

  private:
    // The fonts borrow these bytes: they live as long as the printer.
    std::vector<std::vector<std::uint8_t>> bytes_;
    std::unique_ptr<page::FontSet> fonts_;
    std::unique_ptr<page::Typesetter> typesetter_;
    std::unique_ptr<page::PageRasterizer> rasterizer_;
    bool tried_ = false;
    bool ready_ = false;
};

}  // namespace pl
