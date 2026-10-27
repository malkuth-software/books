#pragma once
// The bookcase: PlaySuite's own shelf (its walnut wall and oak plank, src/wood.hpp) with
// the library's books standing on it spine out, each bound in its cloth or leather and
// lettered in gold. Pure drawing into the view's canvas; the view keeps the state.
#include "library.hpp"
#include "platform/raster.hpp"
#include "printer.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace pl {

// The shell's two woods, made at one display scale: premultiplied BGRA tiles.
struct WoodTiles {
    double scale = 0;
    int wall_width = 0, wall_height = 0;
    int plank_width = 0, plank_height = 0;
    std::vector<std::byte> wall;
    std::vector<std::byte> plank;
};
// Makes the tiles for `scale` (device pixels a point), unless they already are.
void make_wood_tiles(double scale, WoodTiles& tiles);

// Fills a rectangle of device pixels with a tile repeated from the canvas's origin,
// darkened by `shade` (1 leaves it as it is).
void fill_with_tile(Canvas& canvas, const std::vector<std::byte>& tile, int tile_width, int tile_height, int x,
                    int y, int width, int height, float shade);

// Where each book stands, in points, in the bookcase's own coordinates (the view
// scrolls over them).
struct SpineBox {
    double x = 0, y = 0, width = 0, height = 0;
    std::size_t plank = 0;
    bool hidden = false;  // left off the shelf by a search
};
// A plank: its top, where its books stand, and the category lettered on its label.
// A category's share-alike books stand together on planks of their own at its end,
// labelled as such (`share_alike`).
struct PlankBox {
    double top = 0;
    std::string label;
    bool share_alike = false;
};
struct ShelfLayout {
    double width = 0, height = 0;  // the view, in points
    double content_height = 0;     // the whole bookcase, in points
    std::vector<SpineBox> spines;  // one per shelf book, in shelf order
    std::vector<PlankBox> planks;
};

// Books stand in shelf order, a new plank for each category, for its share-alike books
// and wherever a plank is full. `shown` says which books a search keeps (one flag a book); empty keeps all.
[[nodiscard]] ShelfLayout lay_out_shelf(double width, double height, const std::vector<ShelfBook>& books,
                                        const std::vector<bool>& shown = {});
// The furthest the bookcase scrolls, in points.
[[nodiscard]] double scroll_limit(const ShelfLayout& layout);
// The book under a point of the view, or -1. `scroll` is how far the case is scrolled.
[[nodiscard]] int spine_at(const ShelfLayout& layout, double scroll, double x, double y);
// The book on the plank above (-1) or below (1) nearest to `book` across, or `book`.
[[nodiscard]] int spine_beside(const ShelfLayout& layout, int book, int planks);
// The scroll that brings `book` into view from `scroll`, moving as little as it can.
[[nodiscard]] double scroll_to_show(const ShelfLayout& layout, int book, double scroll);

// `lifted` is the book drawn a little raised (under the pointer or the keyboard), or -1;
// a paper tag over it gives its title and author in full. `empty_note`, when not empty,
// is lettered on the bare wall (a search that found nothing).
void draw_shelf(Canvas& canvas, double scale, const ShelfLayout& layout, double scroll,
                const std::vector<ShelfBook>& books, int lifted, const WoodTiles& wood, Printer& printer,
                const std::string& empty_note = {});
// Where the bookcase leaves room above its first plank, in points, for the search field.
inline constexpr double shelf_top_room = 44;

// The game's own help, a card of the library's paper over whatever is showing: shown
// when the game runs outside the shell, whose help book it otherwise opens.
void draw_help_card(Canvas& canvas, double scale, Printer& printer, const std::vector<std::string>& paragraphs);
[[nodiscard]] std::vector<std::string> help_paragraphs();

}  // namespace pl
