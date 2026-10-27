// Prints a spread of any book in the library without a window: the book is read from
// the library's parts, a section laid out, and two facing pages drawn on India paper.
//
//   page_preview <books dir> <fonts dir> <book id> <section> <first page> <out.ppm> <page height px>
//
// The books dir holds the parts and their list, parts.txt (assets/books).
#include "../src/book.hpp"
#include "../src/book_layout.hpp"
#include "../src/container.hpp"
#include "../src/page/font_set.hpp"
#include "../src/page/page_raster.hpp"
#include "paper.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return bytes;
}

bool read_book(pl::LibraryFile& library, const std::string& id, pl::Work& work) {
    std::string text;
    return library.read(library.find(id), text) && pl::read_work(id, text, work);
}

std::vector<std::uint8_t> sheet_for(std::uint32_t width, std::uint32_t height, paper::Gutter gutter) {
    paper::Parameters stock = paper::preset(paper::Preset::india);
    stock.pixels_per_point = 2.0;
    stock.seed = gutter == paper::Gutter::right ? 1611 : 1612;
    paper::Shading shading;
    shading.gutter = gutter;
    paper::Image sheet;
    std::vector<std::uint8_t> bgra;
    if (!paper::generate_sheet(stock, static_cast<int>(width), static_cast<int>(height), shading, sheet))
        return bgra;
    bgra.resize(sheet.rgba.size());
    for (std::size_t i = 0; i < sheet.rgba.size(); i += 4) {
        bgra[i] = sheet.rgba[i + 2];
        bgra[i + 1] = sheet.rgba[i + 1];
        bgra[i + 2] = sheet.rgba[i];
        bgra[i + 3] = 255;
    }
    return bgra;
}

double milliseconds_since(std::chrono::steady_clock::time_point start) {
    const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
    return elapsed.count();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 8) {
        std::fprintf(stderr, "page_preview <books dir> <fonts dir> <book id> <section> <first page> <out.ppm> <height>\n");
        return 2;
    }
    pl::LibraryFile library;
    std::ifstream list(std::string(argv[1]) + "/parts.txt");
    std::string name;
    bool opened = false;
    while (std::getline(list, name)) {
        if (name.empty())
            continue;
        const std::vector<std::uint8_t> part = read_file(std::string(argv[1]) + "/" + name);
        opened = opened ? library.add_block(part) : library.open(part);
        if (!opened) {
            std::fprintf(stderr, "%s does not open\n", name.c_str());
            return 1;
        }
    }
    const std::string id = argv[3];
    const std::chrono::steady_clock::time_point read_start = std::chrono::steady_clock::now();
    pl::Work work;
    if (!read_book(library, id, work)) {
        std::fprintf(stderr, "no book %s\n", id.c_str());
        return 1;
    }
    const double read_ms = milliseconds_since(read_start);
    pl::Work bible;
    pl::Versification places;
    pl::LayoutOptions options;
    if (id == "concordance" && read_book(library, "bsb", bible)) {
        pl::versify(bible, places);
        options.bible = &bible;
        options.places = &places;
    }

    const std::string fonts_dir = argv[2];
    const std::vector<std::uint8_t> literata = read_file(fonts_dir + "/literata.ttf");
    const std::vector<std::uint8_t> literata_italic = read_file(fonts_dir + "/literata-italic.ttf");
    const std::vector<std::uint8_t> garamond = read_file(fonts_dir + "/eb-garamond.ttf");
    const std::vector<std::uint8_t> garamond_italic = read_file(fonts_dir + "/eb-garamond-italic.ttf");
    pl::page::FontSet fonts;
    if (!fonts.load(pl::page::FaceId::literata_regular, literata) ||
        !fonts.load(pl::page::FaceId::literata_italic, literata_italic) ||
        !fonts.load(pl::page::FaceId::eb_garamond_regular, garamond) ||
        !fonts.load(pl::page::FaceId::eb_garamond_italic, garamond_italic)) {
        std::fprintf(stderr, "the faces do not load\n");
        return 1;
    }
    std::vector<pl::Section> sections;
    pl::plan_sections(work, sections);
    const std::size_t section = static_cast<std::size_t>(std::stoul(argv[4]));
    if (section >= sections.size()) {
        std::fprintf(stderr, "%s has %zu sections\n", id.c_str(), sections.size());
        return 1;
    }
    pl::page::Typesetter setter(fonts);
    std::vector<pl::page::PageLayout> pages;
    const std::chrono::steady_clock::time_point layout_start = std::chrono::steady_clock::now();
    pl::lay_out_section(work, sections[section], options, setter, pages);
    const double layout_ms = milliseconds_since(layout_start);

    const std::uint32_t page_height = static_cast<std::uint32_t>(std::stoul(argv[7]));
    const std::uint32_t page_width = page_height * 715U / 1000U;
    const std::uint32_t margin = page_height / 30;
    const std::uint32_t width = page_width * 2 + margin * 2;
    const std::uint32_t height = page_height + margin * 2;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4, 0);
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = 0x2e;
        pixels[i + 1] = 0x2b;
        pixels[i + 2] = 0x2b;
        pixels[i + 3] = 255;
    }
    const pl::page::PixelTarget target{pixels.data(), static_cast<std::size_t>(width) * 4, width, height};
    const std::vector<std::uint8_t> left_sheet = sheet_for(page_width, page_height, paper::Gutter::right);
    const std::vector<std::uint8_t> right_sheet = sheet_for(page_width, page_height, paper::Gutter::left);
    pl::page::PageRasterizer rasterizer(fonts);
    std::size_t first = static_cast<std::size_t>(std::stoul(argv[5]));
    first -= first % 2;
    const std::chrono::steady_clock::time_point draw_start = std::chrono::steady_clock::now();
    for (std::size_t side = 0; side < 2; ++side) {
        pl::page::PageRect rect;
        rect.x = static_cast<std::int32_t>(margin + side * page_width);
        rect.y = static_cast<std::int32_t>(margin);
        rect.width = page_width;
        rect.height = page_height;
        const std::vector<std::uint8_t>& sheet = side == 0 ? left_sheet : right_sheet;
        const pl::page::PaperImage paper_image{sheet.data(), static_cast<std::size_t>(page_width) * 4, page_width,
                                               page_height};
        const pl::page::PageLayout empty;
        const pl::page::PageLayout& layout = first + side < pages.size() ? pages[first + side] : empty;
        rasterizer.draw_page(layout, target, rect, paper_image);
    }
    const double draw_ms = milliseconds_since(draw_start);
    std::printf("%s, section %zu (%s): %zu pages; read %.1f ms, laid out in %.1f ms, spread drawn in %.1f ms\n",
                id.c_str(), section, sections[section].title.c_str(), pages.size(), read_ms, layout_ms, draw_ms);
    std::ofstream out(argv[6], std::ios::binary);
    out << "P6\n" << width << " " << height << "\n255\n";
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        const char rgb[3] = {static_cast<char>(pixels[i + 2]), static_cast<char>(pixels[i + 1]),
                             static_cast<char>(pixels[i])};
        out.write(rgb, 3);
    }
    return out.good() ? 0 : 1;
}
