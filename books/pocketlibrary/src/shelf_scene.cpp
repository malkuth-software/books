#include "shelf_scene.hpp"

#include "wood.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pl {
namespace {

namespace gf = gui_forms;

// The shell's tile sizes and woods (src/wood.cpp, ShelfWood::make).
constexpr double wall_tile_width = 184, wall_tile_height = 368;
constexpr double plank_tile_width = 420, plank_tile_height = 40;

struct Pixel {
    float blue = 0, green = 0, red = 0;
};

std::uint32_t grain(int x, int y) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8DA6B343U ^ static_cast<std::uint32_t>(y) * 0xD8163841U;
    h ^= h >> 13;
    h *= 0x85EBCA77U;
    h ^= h >> 16;
    return h;
}

void put(Canvas& canvas, int x, int y, float red, float green, float blue) {
    if (x < 0 || y < 0 || x >= canvas.w || y >= canvas.h)
        return;
    std::uint8_t* pixel = canvas.px.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(canvas.w) +
                                               static_cast<std::size_t>(x)) * 4;
    pixel[0] = static_cast<std::uint8_t>(std::clamp(blue, 0.f, 255.f));
    pixel[1] = static_cast<std::uint8_t>(std::clamp(green, 0.f, 255.f));
    pixel[2] = static_cast<std::uint8_t>(std::clamp(red, 0.f, 255.f));
    pixel[3] = 255;
}

// How bright a spine is across its width: rounded like a bound back, lit from the
// upper left.
float spine_light(double across) {
    const double round = std::pow(std::sin(3.14159265358979 * std::clamp(across, 0.0, 1.0)), 0.7);
    return static_cast<float>(0.52 + 0.55 * round - 0.12 * across);
}

// Raised bands across a leather spine, as fractions of its height; cloth spines carry
// gilt rules instead.
constexpr double band_positions[] = {0.10, 0.17, 0.83, 0.90};

void draw_spine_body(Canvas& canvas, const ShelfBook& book, int x0, int y0, int width, int height, double scale) {
    const float red = book.binding.red, green = book.binding.green, blue = book.binding.blue;
    const int band = std::max(2, static_cast<int>(std::lround(2.5 * scale)));
    for (int y = 0; y < height; ++y) {
        const double down = (static_cast<double>(y) + 0.5) / static_cast<double>(height);
        float along = 1.0f;
        // The head and the tail of the spine curl away from the light.
        if (y < band)
            along = 0.72f;
        else if (y >= height - band)
            along = 0.62f;
        if (book.binding.leather) {
            for (const double position : band_positions) {
                const int at = static_cast<int>(position * height);
                if (y == at - 1)
                    along = 1.32f;
                else if (y == at)
                    along = 1.18f;
                else if (y == at + 1)
                    along = 0.68f;
            }
        }
        for (int x = 0; x < width; ++x) {
            const double across = (static_cast<double>(x) + 0.5) / static_cast<double>(width);
            float light = spine_light(across) * along;
            if (book.binding.leather) {
                // The pebble of the leather: a fine, quiet grain.
                light *= 0.96f + 0.08f * static_cast<float>(grain(x0 + x, y0 + y) & 255U) / 255.f;
            } else {
                // The weave of book cloth: faint threads both ways.
                const float weave = ((x0 + x + y0 + y) % 2 == 0) ? 1.025f : 0.975f;
                light *= weave;
            }
            put(canvas, x0 + x, y0 + y, red * light, green * light, blue * light);
        }
    }
    // A share-alike book carries a small green paper label low on its spine, between the
    // tail's rules, as a library marks a shelf's special stock.
    if (book.share_alike()) {
        const int label_height = std::max(3, static_cast<int>(std::lround(height * 0.035)));
        const int label_top = y0 + static_cast<int>(height * 0.86);
        const int inset = std::max(1, width / 5);
        for (int y = label_top; y < label_top + label_height; ++y) {
            for (int x = x0 + inset; x < x0 + width - inset; ++x) {
                const float paper = 0.93f + 0.07f * static_cast<float>(grain(x, y) & 255U) / 255.f;
                put(canvas, x, y, 78 * paper, 156 * paper, 92 * paper);
            }
        }
    }
    // Gilt rules: beside the bands on leather, near head and tail on cloth.
    const page::Rgb gilt{0xd8U, 0xb2U, 0x62U};
    const double rules_leather[] = {0.08, 0.92};
    const double rules_cloth[] = {0.06, 0.075, 0.925, 0.94};
    const double* rules = book.binding.leather ? rules_leather : rules_cloth;
    const int rule_count = book.binding.leather ? 2 : 4;
    for (int r = 0; r < rule_count; ++r) {
        const int y = y0 + static_cast<int>(rules[r] * height);
        for (int x = 1; x + 1 < width; ++x) {
            const double across = (static_cast<double>(x) + 0.5) / static_cast<double>(width);
            const float light = std::min(1.15f, spine_light(across) + 0.15f);
            put(canvas, x0 + x, y, gilt.red * light, gilt.green * light, gilt.blue * light);
        }
    }
}

// Sets one line of lettering along the spine: laid out across a band `length` device
// pixels long and `thickness` across, printed, then turned a quarter clockwise so it
// reads from head to tail.
void letter_spine(Canvas& canvas, Printer& printer, const std::string& text, page::FaceId face, double em_pixels,
                  double from, double to, int x0, int y0, int length, int thickness, const Pixel& ground,
                  bool capitals) {
    const double centre_along = (from + to) / 2;
    const double room = (to - from) * length;
    if (text.empty() || length <= 0 || thickness <= 0 || em_pixels < 3)
        return;
    const double to_units = 1.0e6 / static_cast<double>(thickness);
    page::TextStyle style;
    style.face = face;
    style.em = static_cast<page::LayoutUnit>(em_pixels * to_units);
    style.ink = page::Ink::gilt;
    if (capitals)
        style.tracking = style.em / 9;
    page::Typesetter& setter = printer.typesetter();
    const page::ShapedRun& shaped = setter.shaped(style, text, false);
    const double width_pixels = static_cast<double>(shaped.advance) / to_units;
    double scale_down = 1;
    if (width_pixels > room)
        scale_down = room / width_pixels;
    if (scale_down < 1) {
        style.em = static_cast<page::LayoutUnit>(static_cast<double>(style.em) * scale_down);
        style.tracking = capitals ? style.em / 9 : 0;
    }
    const page::ShapedRun& fitted = setter.shaped(style, text, false);
    page::PageLayout lettering;
    page::PlacedRun run;
    run.face = style.face;
    run.em = style.em;
    run.ink = page::Ink::gilt;
    const double start = centre_along * length - static_cast<double>(fitted.advance) / to_units / 2;
    run.x = static_cast<page::LayoutUnit>(std::max(0.0, start) * to_units);
    const page::FaceMetrics metrics = printer.fonts().metrics(style.face, style.em);
    const page::LayoutUnit cap = metrics.cap_height > 0 ? metrics.cap_height : style.em * 7 / 10;
    run.baseline = static_cast<page::LayoutUnit>(500000 + cap / 2);
    run.glyphs = fitted.glyphs;
    lettering.runs.push_back(run);

    // The band is printed on the spine's own colour, then only the pixels the gold
    // touched are carried onto the spine.
    const std::size_t pixels = static_cast<std::size_t>(length) * static_cast<std::size_t>(thickness);
    std::vector<std::uint8_t> ground_pixels(pixels * 4);
    for (std::size_t i = 0; i < pixels; ++i) {
        ground_pixels[4 * i] = static_cast<std::uint8_t>(ground.blue);
        ground_pixels[4 * i + 1] = static_cast<std::uint8_t>(ground.green);
        ground_pixels[4 * i + 2] = static_cast<std::uint8_t>(ground.red);
        ground_pixels[4 * i + 3] = 255;
    }
    std::vector<std::uint8_t> band(ground_pixels);
    const page::PixelTarget target{band.data(), static_cast<std::size_t>(length) * 4,
                                   static_cast<std::uint32_t>(length), static_cast<std::uint32_t>(thickness)};
    page::PageRect rect;
    rect.width = static_cast<std::uint32_t>(length);
    rect.height = static_cast<std::uint32_t>(thickness);
    const page::PaperImage paper{ground_pixels.data(), static_cast<std::size_t>(length) * 4,
                                 static_cast<std::uint32_t>(length), static_cast<std::uint32_t>(thickness)};
    printer.rasterizer().draw_page(lettering, target, rect, paper);
    for (int y = 0; y < thickness; ++y) {
        for (int x = 0; x < length; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(length) +
                                    static_cast<std::size_t>(x)) * 4;
            if (std::memcmp(band.data() + at, ground_pixels.data() + at, 3) == 0)
                continue;
            put(canvas, x0 + (thickness - 1 - y), y0 + x, band[at + 2], band[at + 1], band[at]);
        }
    }
}

}  // namespace

void make_wood_tiles(double scale, WoodTiles& tiles) {
    const double used = std::max(1.0, scale);
    if (tiles.scale == used && !tiles.wall.empty())
        return;
    tiles.scale = used;
    tiles.wall_width = static_cast<int>(std::lround(wall_tile_width * used));
    tiles.wall_height = static_cast<int>(std::lround(wall_tile_height * used));
    tiles.plank_width = static_cast<int>(std::lround(plank_tile_width * used));
    tiles.plank_height = static_cast<int>(std::lround(plank_tile_height * used));
    // Walnut boards on the wall; a warmer oak for the shelf, as the shell has them.
    const games::WoodLook walnut{gf::Color::rgba(74, 47, 28), gf::Color::rgba(36, 21, 11), 7, 1.4, 1, 23};
    const games::WoodLook oak{gf::Color::rgba(168, 116, 70), gf::Color::rgba(104, 64, 34), 5, 1.1, 1, 41};
    tiles.wall = games::make_wood(tiles.wall_width, tiles.wall_height, false, walnut);
    tiles.plank = games::make_wood(tiles.plank_width, tiles.plank_height, true, oak);
}

void fill_with_tile(Canvas& canvas, const std::vector<std::byte>& tile, int tile_width, int tile_height, int x,
                    int y, int width, int height, float shade) {
    if (tile.empty() || tile_width <= 0 || tile_height <= 0)
        return;
    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(canvas.w, x + width);
    const int bottom = std::min(canvas.h, y + height);
    for (int row = top; row < bottom; ++row) {
        const std::byte* source_row = tile.data() + static_cast<std::size_t>(row % tile_height) *
                                                        static_cast<std::size_t>(tile_width) * 4;
        std::uint8_t* target = canvas.px.data() + (static_cast<std::size_t>(row) * static_cast<std::size_t>(canvas.w) +
                                                   static_cast<std::size_t>(left)) * 4;
        for (int column = left; column < right; ++column) {
            const std::byte* source = source_row + static_cast<std::size_t>(column % tile_width) * 4;
            target[0] = static_cast<std::uint8_t>(static_cast<float>(source[0]) * shade);
            target[1] = static_cast<std::uint8_t>(static_cast<float>(source[1]) * shade);
            target[2] = static_cast<std::uint8_t>(static_cast<float>(source[2]) * shade);
            target[3] = 255;
            target += 4;
        }
    }
}

namespace {

// The bookcase's measures, in points.
constexpr double side_margin = 28;
constexpr double top_margin = shelf_top_room;
constexpr double plank_clearance = 34;  // from a plank up to the tallest book's head room
constexpr double plank_depth = 30;      // the plank and its label below the books

void draw_plank(Canvas& canvas, double scale, double x, double w, double y, const WoodTiles& wood) {
    const double top = y - 3, front = y + 5, thick = 12;
    canvas.save();
    canvas.set_transform(Mat{});
    canvas.scale(scale, scale);
    canvas.begin();
    canvas.rect(x - 4, front + thick, w + 8, 20);
    canvas.fill(Paint::lin(0, front + thick, 0, front + thick + 20,
                           {{0, rgb(8, 4, 2, 150 / 255.f)}, {.35f, rgb(8, 4, 2, 70 / 255.f)}, {1, rgb(8, 4, 2, 0)}}));
    canvas.restore();
    const int left = static_cast<int>(std::lround(x * scale));
    const int width = static_cast<int>(std::lround(w * scale));
    fill_with_tile(canvas, wood.plank, wood.plank_width, wood.plank_height, left,
                   static_cast<int>(std::lround(top * scale)), width, static_cast<int>(std::lround((front - top) * scale)),
                   1.0f);
    fill_with_tile(canvas, wood.plank, wood.plank_width, wood.plank_height, left,
                   static_cast<int>(std::lround(front * scale)), width, static_cast<int>(std::lround(thick * scale)), 1.0f);
    canvas.save();
    canvas.set_transform(Mat{});
    canvas.scale(scale, scale);
    canvas.begin();
    canvas.rect(x, top, w, front - top);
    canvas.fill(Paint::lin(0, top, 0, front,
                           {{0, rgb(20, 10, 4, 150 / 255.f)}, {.6f, rgb(20, 10, 4, 30 / 255.f)},
                            {1, rgb(255, 232, 196, 40 / 255.f)}}));
    canvas.begin();
    canvas.rect(x, front, w, thick);
    canvas.fill(Paint::lin(0, front, 0, front + thick,
                           {{0, rgb(255, 236, 204, 36 / 255.f)}, {.4f, rgb(0, 0, 0, 0)}, {1, rgb(10, 5, 2, 110 / 255.f)}}));
    canvas.restore();
}

// A small paper label pinned to the plank's front edge, lettered with the category.
void draw_label(Canvas& canvas, double scale, double x, double y, const std::string& text, Printer& printer) {
    if (text.empty() || !printer.ready())
        return;
    page::TextStyle style;
    style.face = page::FaceId::literata_regular;
    const double em_points = 9.5;
    const int height = static_cast<int>(std::lround(15 * scale));
    const double units = 1.0e6 / static_cast<double>(height);
    style.em = static_cast<page::LayoutUnit>(em_points * scale * units);
    style.uppercase = true;
    style.tracking = style.em / 8;
    style.ink = page::Ink::body;
    const page::ShapedRun& shaped = printer.typesetter().shaped(style, text, false);
    const double text_points = static_cast<double>(shaped.advance) / units / scale;
    const int width = static_cast<int>(std::lround((text_points + 14) * scale));
    page::PageRect rect;
    rect.x = static_cast<std::int32_t>(std::lround(x * scale));
    rect.y = static_cast<std::int32_t>(std::lround(y * scale));
    rect.width = static_cast<std::uint32_t>(std::max(1, width));
    rect.height = static_cast<std::uint32_t>(height);
    page::PageLayout label;
    page::PlacedRun run;
    run.face = style.face;
    run.em = style.em;
    run.ink = style.ink;
    run.x = static_cast<page::LayoutUnit>(7 * scale * units);
    const page::FaceMetrics metrics = printer.fonts().metrics(style.face, style.em);
    run.baseline = 500000 + metrics.cap_height / 2;
    run.glyphs = shaped.glyphs;
    label.runs.push_back(run);
    const page::PixelTarget target{canvas.px.data(), static_cast<std::size_t>(canvas.w) * 4,
                                   static_cast<std::uint32_t>(canvas.w), static_cast<std::uint32_t>(canvas.h)};
    printer.rasterizer().draw_page(label, target, rect, page::PaperImage{});
}

// A slip of paper centred on (x, y) in points, kept inside the view: its first line in
// roman, a second (the author) in italic beneath. Long lines are set smaller to fit.
void draw_note(Canvas& canvas, double scale, double x, double y, const std::vector<std::string>& lines,
               Printer& printer) {
    if (!printer.ready() || lines.empty() || lines.front().empty())
        return;
    const bool two = lines.size() > 1 && !lines[1].empty();
    const double pad = 9;
    const double first_em = 12.5, second_em = 10.5;
    const double height_points = two ? 40 : 26;
    const int height = std::max(1, static_cast<int>(std::lround(height_points * scale)));
    const double units = 1.0e6 / static_cast<double>(height);
    page::Typesetter& setter = printer.typesetter();
    page::TextStyle first;
    first.face = page::FaceId::literata_regular;
    first.em = static_cast<page::LayoutUnit>(first_em * scale * units);
    first.ink = page::Ink::body;
    page::TextStyle second = first;
    second.face = page::FaceId::literata_italic;
    second.em = static_cast<page::LayoutUnit>(second_em * scale * units);
    second.ink = page::Ink::soft;
    // The widest the slip may be: the view less a margin each side.
    const double room = std::max(60.0, static_cast<double>(canvas.w) / scale - 24) - 2 * pad;
    double shrink = 1;
    const double first_points = static_cast<double>(setter.shaped(first, lines[0], false).advance) / units / scale;
    double widest = first_points;
    if (two)
        widest = std::max(widest, static_cast<double>(setter.shaped(second, lines[1], false).advance) / units / scale);
    if (widest > room) {
        shrink = room / widest;
        first.em = static_cast<page::LayoutUnit>(static_cast<double>(first.em) * shrink);
        second.em = static_cast<page::LayoutUnit>(static_cast<double>(second.em) * shrink);
        widest = room;
    }
    const double width_points = widest + 2 * pad;
    const double left = std::clamp(x - width_points / 2, 12.0, std::max(12.0, canvas.w / scale - 12 - width_points));
    const double top = std::clamp(y - height_points / 2, 4.0, std::max(4.0, canvas.h / scale - 4 - height_points));
    // Its shadow on whatever is behind.
    canvas.save();
    canvas.set_transform(Mat{});
    canvas.scale(scale, scale);
    canvas.begin();
    canvas.rrect(left + 1.5, top + 2.5, width_points, height_points, 3);
    canvas.fill(rgb(6, 3, 1, 0.45f));
    canvas.restore();
    page::PageRect rect;
    rect.x = static_cast<std::int32_t>(std::lround(left * scale));
    rect.y = static_cast<std::int32_t>(std::lround(top * scale));
    rect.width = static_cast<std::uint32_t>(std::max(1, static_cast<int>(std::lround(width_points * scale))));
    rect.height = static_cast<std::uint32_t>(height);
    page::PageLayout note;
    const page::LayoutUnit middle = static_cast<page::LayoutUnit>(width_points / 2 * scale * units);
    const page::ShapedRun& set_first = setter.shaped(first, lines[0], false);
    page::PlacedRun run;
    run.face = first.face;
    run.em = first.em;
    run.ink = first.ink;
    run.x = middle - set_first.advance / 2;
    run.baseline = static_cast<page::LayoutUnit>((two ? 17.5 : 17.0) * scale * units);
    run.glyphs = set_first.glyphs;
    note.runs.push_back(run);
    if (two) {
        const page::ShapedRun& set_second = setter.shaped(second, lines[1], false);
        page::PlacedRun by;
        by.face = second.face;
        by.em = second.em;
        by.ink = second.ink;
        by.x = middle - set_second.advance / 2;
        by.baseline = static_cast<page::LayoutUnit>(32.0 * scale * units);
        by.glyphs = set_second.glyphs;
        note.runs.push_back(by);
    }
    const page::PixelTarget target{canvas.px.data(), static_cast<std::size_t>(canvas.w) * 4,
                                   static_cast<std::uint32_t>(canvas.w), static_cast<std::uint32_t>(canvas.h)};
    printer.rasterizer().draw_page(note, target, rect, page::PaperImage{});
}

}  // namespace

ShelfLayout lay_out_shelf(double width, double height, const std::vector<ShelfBook>& books,
                          const std::vector<bool>& shown) {
    ShelfLayout layout;
    layout.width = width;
    layout.height = height;
    // Books stand a little under half the view tall, so a plank and the next show.
    const double book_height = std::clamp(height * 0.42, 110.0, 240.0);
    const double unit = std::clamp(book_height * 0.13, 15.0, 34.0);
    const double pitch = book_height + plank_clearance + plank_depth;
    const double right = width - side_margin;
    double x = side_margin;
    std::string category;
    bool share_alike = false;
    bool any = false;
    for (std::size_t n = 0; n < books.size(); ++n) {
        const ShelfBook& book = books[n];
        if (n < shown.size() && !shown[n]) {
            SpineBox box;
            box.hidden = true;
            box.plank = layout.planks.empty() ? 0 : layout.planks.size() - 1;
            layout.spines.push_back(box);
            continue;
        }
        const double spine = std::max(14.0, unit * book.heft);
        const bool new_category = !any || book.category != category;
        const bool new_section = !new_category && book.share_alike() && !share_alike;
        if (new_category || new_section || x + spine > right) {
            PlankBox plank;
            plank.top = top_margin + plank_clearance + book_height + pitch * static_cast<double>(layout.planks.size());
            plank.share_alike = book.share_alike();
            if (new_category || new_section)
                plank.label = book.share_alike() ? book.category + " \u00b7 Share-alike" : book.category;
            layout.planks.push_back(plank);
            x = side_margin + 6;
            category = book.category;
            share_alike = book.share_alike();
            any = true;
        }
        SpineBox box;
        box.plank = layout.planks.size() - 1;
        box.width = std::floor(spine);
        box.height = std::floor(book_height * std::clamp(0.80 + 0.07 * book.heft, 0.80, 1.0));
        box.x = std::round(x);
        box.y = layout.planks.back().top - box.height;
        layout.spines.push_back(box);
        x += spine + 3;
    }
    layout.content_height = layout.planks.empty() ? height : layout.planks.back().top + plank_depth + top_margin;
    return layout;
}

double scroll_limit(const ShelfLayout& layout) {
    return std::max(0.0, layout.content_height - layout.height);
}

int spine_at(const ShelfLayout& layout, double scroll, double x, double y) {
    const double at = y + scroll;
    for (std::size_t n = 0; n < layout.spines.size(); ++n) {
        const SpineBox& box = layout.spines[n];
        if (!box.hidden && x >= box.x && x < box.x + box.width && at >= box.y - 8 && at < box.y + box.height)
            return static_cast<int>(n);
    }
    return -1;
}

int spine_beside(const ShelfLayout& layout, int book, int planks) {
    if (book < 0 || static_cast<std::size_t>(book) >= layout.spines.size())
        return book;
    const SpineBox& from = layout.spines[static_cast<std::size_t>(book)];
    const long target = static_cast<long>(from.plank) + planks;
    if (target < 0 || static_cast<std::size_t>(target) >= layout.planks.size())
        return book;
    const double across = from.x + from.width / 2;
    int best = book;
    double nearest = 1e18;
    for (std::size_t n = 0; n < layout.spines.size(); ++n) {
        const SpineBox& box = layout.spines[n];
        if (box.hidden || box.plank != static_cast<std::size_t>(target))
            continue;
        const double distance = std::fabs(box.x + box.width / 2 - across);
        if (distance < nearest) {
            nearest = distance;
            best = static_cast<int>(n);
        }
    }
    return best;
}

double scroll_to_show(const ShelfLayout& layout, int book, double scroll) {
    if (book < 0 || static_cast<std::size_t>(book) >= layout.spines.size() ||
        layout.spines[static_cast<std::size_t>(book)].hidden || layout.planks.empty())
        return scroll;
    const SpineBox& box = layout.spines[static_cast<std::size_t>(book)];
    const double top = box.y - plank_clearance;
    const double bottom = layout.planks[box.plank].top + plank_depth;
    double next = scroll;
    if (top < next)
        next = top;
    if (bottom > next + layout.height)
        next = bottom - layout.height;
    return std::clamp(next, 0.0, scroll_limit(layout));
}

void draw_shelf(Canvas& canvas, double scale, const ShelfLayout& layout, double scroll,
                const std::vector<ShelfBook>& books, int lifted, const WoodTiles& wood, Printer& printer,
                const std::string& empty_note) {
    // The wall scrolls with the case.
    const int shift = static_cast<int>(std::lround(scroll * scale));
    const int tile_shift = wood.wall_height > 0 ? shift % wood.wall_height : 0;
    fill_with_tile(canvas, wood.wall, wood.wall_width, wood.wall_height, 0, -tile_shift, canvas.w,
                   canvas.h + tile_shift, 1.0f);
    for (const PlankBox& plank : layout.planks) {
        const double y = plank.top - scroll;
        if (y < -plank_depth - 300 || y > layout.height + 300)
            continue;
        draw_plank(canvas, scale, 10, layout.width - 20, y, wood);
        draw_label(canvas, scale, side_margin, y + 9, plank.label, printer);
    }
    // Each book's shadow on the wall, thrown down and to the right.
    canvas.save();
    canvas.set_transform(Mat{});
    canvas.scale(scale, scale);
    for (std::size_t n = 0; n < books.size() && n < layout.spines.size(); ++n) {
        const SpineBox& box = layout.spines[n];
        if (box.hidden)
            continue;
        const double raise = static_cast<int>(n) == lifted ? 6 : 0;
        const double y = box.y - raise - scroll;
        if (y > layout.height || y + box.height < 0)
            continue;
        canvas.begin();
        canvas.rrect(box.x + 3, y + 4, box.width + 2, box.height - 4, 3);
        canvas.fill(rgb(6, 3, 1, 0.45f));
    }
    canvas.restore();
    for (std::size_t n = 0; n < books.size() && n < layout.spines.size(); ++n) {
        const ShelfBook& book = books[n];
        const SpineBox& box = layout.spines[n];
        if (box.hidden)
            continue;
        const double raise = static_cast<int>(n) == lifted ? 6 : 0;
        const double top = box.y - raise - scroll;
        if (top > layout.height || top + box.height < 0)
            continue;
        const int x0 = static_cast<int>(std::lround(box.x * scale));
        const int y0 = static_cast<int>(std::lround(top * scale));
        const int width = std::max(4, static_cast<int>(std::lround(box.width * scale)));
        const int height = std::max(8, static_cast<int>(std::lround(box.height * scale)));
        draw_spine_body(canvas, book, x0, y0, width, height, scale);
        if (!printer.ready())
            continue;
        const float light = spine_light(0.5);
        Pixel ground;
        ground.red = std::min(255.f, book.binding.red * light);
        ground.green = std::min(255.f, book.binding.green * light);
        ground.blue = std::min(255.f, book.binding.blue * light);
        const int inset = std::max(1, width / 10);
        const int band = width - 2 * inset;
        const double title_em = band * 0.46;
        // The title in the upper stretch of the spine, between the head's rules or bands
        // and the middle; the subtitle, smaller, in the lower stretch above the tail's.
        letter_spine(canvas, printer, book.title, page::FaceId::literata_regular, title_em, 0.22, 0.60, x0 + inset,
                     y0, height, band, ground, true);
        letter_spine(canvas, printer, book.subtitle, page::FaceId::literata_italic, title_em * 0.62, 0.64, 0.80,
                     x0 + inset, y0, height, band, ground, false);
    }
    if (!empty_note.empty())
        draw_note(canvas, scale, layout.width / 2, layout.height * 0.4, {empty_note}, printer);
    if (lifted >= 0 && static_cast<std::size_t>(lifted) < books.size() &&
        static_cast<std::size_t>(lifted) < layout.spines.size() && !layout.spines[static_cast<std::size_t>(lifted)].hidden) {
        const ShelfBook& book = books[static_cast<std::size_t>(lifted)];
        const SpineBox& box = layout.spines[static_cast<std::size_t>(lifted)];
        // Over the book's head, in the room beneath the plank above; where that room is
        // under the capsule and the search field at the top of the view, over the book's
        // upper half instead.
        const double above = box.y - 6 - scroll - 26;
        const double centre = above - 20 >= shelf_top_room ? above : box.y - scroll + box.height * 0.3;
        draw_note(canvas, scale, box.x + box.width / 2, centre, {book.title, book.subtitle}, printer);
    }
}

std::vector<std::string> help_paragraphs() {
    return {
        "Pocket Library",
        "Choose a book from the shelf: click it, or move along the shelf with the arrow keys and press Enter. "
        "Point at a book to read its title and author.",
        "To find a book, type part of its title or its author's name in the search field (Command or Control F); "
        "Enter opens the first found, Escape brings back the whole library.",
        "Turn the pages with the arrow keys, Page Up and Page Down, the space bar, the wheel, or a click on "
        "either page. C shows the book's contents; choose a part to go to it. I shows where the book comes "
        "from and its licence. Escape or Back returns to the library.",
        "Every book opens where you left it. The type size, typeface, paper and its texture are on the "
        "Settings screen.",
        "Press Escape or F1 to close this card.",
    };
}

void draw_help_card(Canvas& canvas, double scale, Printer& printer, const std::vector<std::string>& paragraphs) {
    // The room dims behind the card.
    for (std::size_t i = 0; i < canvas.px.size(); i += 4) {
        canvas.px[i] = static_cast<std::uint8_t>(canvas.px[i] * 45 / 100);
        canvas.px[i + 1] = static_cast<std::uint8_t>(canvas.px[i + 1] * 45 / 100);
        canvas.px[i + 2] = static_cast<std::uint8_t>(canvas.px[i + 2] * 45 / 100);
    }
    const int width = std::min(canvas.w * 9 / 10, static_cast<int>(std::lround(520 * scale)));
    const int height = std::min(canvas.h * 9 / 10, static_cast<int>(std::lround(340 * scale)));
    page::PageRect rect;
    rect.x = (canvas.w - width) / 2;
    rect.y = (canvas.h - height) / 2;
    rect.width = static_cast<std::uint32_t>(std::max(1, width));
    rect.height = static_cast<std::uint32_t>(std::max(1, height));
    page::PageLayout card;
    if (printer.ready()) {
        // Layout units are millionths of the card's height here.
        const double units = 1.0e6 / static_cast<double>(rect.height);
        const page::LayoutUnit margin = static_cast<page::LayoutUnit>(26 * scale * units);
        const page::LayoutUnit measure = static_cast<page::LayoutUnit>(width * units) - 2 * margin;
        page::LayoutUnit top = margin;
        for (std::size_t n = 0; n < paragraphs.size(); ++n) {
            page::TextStyle style;
            style.face = page::FaceId::literata_regular;
            style.em = static_cast<page::LayoutUnit>((n == 0 ? 20 : 13.5) * scale * units);
            page::ParagraphInput input;
            input.text = paragraphs[n];
            input.styles = {style};
            input.style_of_byte.assign(input.text.size(), 0);
            input.word_of_byte.assign(input.text.size(), page::no_word);
            const page::FaceMetrics metrics = printer.fonts().metrics(style.face, style.em);
            page::ParagraphFrame frame;
            frame.alignment = n == 0 ? page::Alignment::centered : page::Alignment::start;
            frame.width = measure;
            frame.line_height = style.em * 140 / 100;
            frame.baseline = (frame.line_height - metrics.ascender - metrics.descender) / 2 + metrics.ascender;
            std::vector<page::LineBox> lines;
            printer.typesetter().set_paragraph(input, frame, lines);
            for (const page::LineBox& line : lines) {
                for (const page::PlacedRun& source : line.runs) {
                    page::PlacedRun run = source;
                    run.x += margin;
                    run.baseline += top;
                    card.runs.push_back(run);
                }
                top += line.height;
            }
            top += style.em * 6 / 10;
        }
    }
    const page::PixelTarget target{canvas.px.data(), static_cast<std::size_t>(canvas.w) * 4,
                                   static_cast<std::uint32_t>(canvas.w), static_cast<std::uint32_t>(canvas.h)};
    printer.rasterizer().draw_page(card, target, rect, page::PaperImage{});
}

}  // namespace pl
