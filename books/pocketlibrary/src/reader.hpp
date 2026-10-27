#pragma once
// An open book: its sections, the spread in front of the reader, the pages laid out as
// they are wanted, and the India paper they are printed on. Also the book's contents,
// a spread of its sections to choose from. Drawing goes into the view's canvas.
#include "book_layout.hpp"
#include "library.hpp"
#include "platform/raster.hpp"
#include "printer.hpp"
#include "shelf_scene.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pl {

// The papers a book can be printed on, as the Settings screen names them; India first,
// the Pocket Bible's own.
inline constexpr const char* paper_names[] = {"India", "Book", "Wove",  "Laid",  "Rag",
                                              "Parchment", "Recycled", "Kraft", "Night", "Phosphor"};
inline constexpr int paper_count = 10;

// How the pages look, as the reader chose on the Settings screen.
struct PageLook {
    int paper = 0;        // into paper_names
    double texture = .5;  // the paper's markings: 0 flat, .5 as the paper is made, 1 twice that
    double gutter = .5;   // the shadow into the binding: 0 none, .5 as made, 1 twice as deep
    bool operator==(const PageLook&) const = default;
};

class Reader {
  public:
    // Opens `work` at `place` (its section and page are clamped to the book).
    void open(const Work& work, const LayoutOptions& options, const Place& place);
    void close();
    [[nodiscard]] bool is_open() const {
        return work_ != nullptr;
    }
    [[nodiscard]] Place place() const;
    // "Genesis, pages 3 and 4 of 136": the spread, for the window title and readers.
    [[nodiscard]] std::string describe() const;

    // Page turns; false when there is nothing further that way.
    bool next(Printer& printer);
    bool previous(Printer& printer);
    void to_section(std::size_t section);
    void show_contents(bool shown);
    [[nodiscard]] bool contents_shown() const {
        return contents_;
    }
    // The section of the contents entry under a device pixel, or -1.
    [[nodiscard]] int contents_entry_at(int x, int y) const;
    // Which page of the spread is under a device pixel: -1 the left, 1 the right, 0 none.
    [[nodiscard]] int side_at(int x, int y) const;

    // Draws the open spread over the room's wall. The type step is the reader's choice.
    void draw(Canvas& canvas, double scale, Printer& printer, const WoodTiles& wood);
    void set_step(page::TypeStep step);
    void set_typeface(Typeface typeface);
    void set_look(const PageLook& look);

  private:
    struct Entry {
        std::size_t page = 0;
        page::LayoutUnit y0 = 0, y1 = 0;
        std::size_t section = 0;
    };
    struct Sheet {
        std::uint32_t width = 0, height = 0;
        int gutter = 0;  // -1 left, 1 right, 0 none
        std::vector<std::uint8_t> pixels;
    };
    void ensure_laid_out(Printer& printer);
    void lay_out_contents(Printer& printer);
    void forget_contents();
    [[nodiscard]] const std::vector<std::uint8_t>& sheet(std::uint32_t width, std::uint32_t height, int gutter,
                                                         double scale);

    const Work* work_ = nullptr;
    LayoutOptions options_;
    std::vector<Section> sections_;
    std::size_t section_ = 0;
    std::size_t page_ = 0;  // the left page of the spread; even
    bool contents_ = false;
    std::vector<page::PageLayout> pages_;
    std::size_t laid_section_ = static_cast<std::size_t>(-1);
    page::TypeStep laid_step_ = page::TypeStep::medium;
    Typeface laid_typeface_ = Typeface::as_set;
    PageLook look_;
    std::vector<page::PageLayout> contents_pages_;
    std::vector<Entry> entries_;
    std::size_t contents_page_ = 0;
    std::vector<Sheet> sheets_;
    // Where the spread was last drawn, in device pixels.
    page::SpreadGeometry geometry_{};
};

}  // namespace pl
