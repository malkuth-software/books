// The paper swatch sheet: every preset as a repeating tile shown twice across so the
// seam can be judged, then one sheet with a gutter and edge shading, written as one
// PPM for a person to look at. Timing goes to stdout.
//
//   paper_swatches sheet.ppm [side] [pixels_per_point] [zoom]
//
// The defaults show each tile 192 pixels across at one pixel per point. A
// pixels_per_point of 2 shows the paper as a 2x display draws it; a zoom enlarges
// the pixels so the fibres and tooth can be inspected.
#include "paper.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct Sheet {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb;
};

void place(Sheet& sheet, const paper::Image& image, int left, int top, int zoom, int repeat_x) {
    for (int y = 0; y < image.height * zoom; ++y) {
        for (int x = 0; x < image.width * zoom * repeat_x; ++x) {
            const int sx = left + x;
            const int sy = top + y;
            if (sx < 0 || sy < 0 || sx >= sheet.width || sy >= sheet.height) {
                continue;
            }
            const int source_x = (x / zoom) % image.width;
            const std::size_t from = (static_cast<std::size_t>(y / zoom) * image.width + source_x) * 4;
            const std::size_t to = (static_cast<std::size_t>(sy) * sheet.width + sx) * 3;
            sheet.rgb[to] = image.rgba[from];
            sheet.rgb[to + 1] = image.rgba[from + 1];
            sheet.rgb[to + 2] = image.rgba[from + 2];
        }
    }
}

bool write_ppm(const std::string& path, const Sheet& sheet) {
    FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    std::fprintf(file, "P6 %d %d 255\n", sheet.width, sheet.height);
    const std::size_t written = std::fwrite(sheet.rgb.data(), 1, sheet.rgb.size(), file);
    std::fclose(file);
    const bool ok = written == sheet.rgb.size();
    return ok;
}

double milliseconds_since(std::chrono::steady_clock::time_point start) {
    const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
    return elapsed.count();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: paper_swatches sheet.ppm [side] [pixels_per_point] [zoom]\n");
        return 2;
    }
    const std::string path = argv[1];
    const int side = argc > 2 ? std::atoi(argv[2]) : 192;
    const double ppp = argc > 3 ? std::atof(argv[3]) : 1.0;
    const int zoom = argc > 4 ? std::max(1, std::atoi(argv[4])) : 1;
    const int gap = 12;
    const int tile_cell = side * zoom * 2 + gap;
    Sheet sheet;
    sheet.width = gap + tile_cell * 2;
    sheet.height = gap + tile_cell * ((paper::preset_count + 1) / 2) + side * zoom + gap;
    sheet.rgb.assign(static_cast<std::size_t>(sheet.width) * sheet.height * 3, 90);
    for (int index = 0; index < paper::preset_count; ++index) {
        const paper::Preset kind = static_cast<paper::Preset>(index);
        paper::Parameters p = paper::preset(kind);
        p.pixels_per_point = ppp;
        paper::Image image;
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        const bool ok = paper::generate_tile(p, side, side, image);
        const double took = milliseconds_since(start);
        if (!ok) {
            std::fprintf(stderr, "%s: refused\n", paper::preset_name(kind));
            return 1;
        }
        std::printf("%-10s %4d x %4d tile at %.2g px/pt: %.2f ms\n", paper::preset_name(kind), side, side, ppp,
                    took);
        const int column = index % 2;
        const int row = index / 2;
        place(sheet, image, gap + column * tile_cell, gap + row * tile_cell, zoom, 2);
    }
    // A whole sheet, bound at the left, with a soft fall-off at its edges.
    paper::Parameters page = paper::preset(paper::Preset::wove);
    page.pixels_per_point = ppp;
    paper::Shading shading;
    shading.gutter = paper::Gutter::left;
    shading.edge_width = 18.0;
    paper::Image bound;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    const bool ok = paper::generate_sheet(page, side * 4, side, shading, bound);
    const double took = milliseconds_since(start);
    if (!ok) {
        std::fprintf(stderr, "sheet: refused\n");
        return 1;
    }
    std::printf("%-10s %4d x %4d sheet at %.2g px/pt: %.2f ms\n", "bound", side * 4, side, ppp, took);
    place(sheet, bound, gap, gap + tile_cell * ((paper::preset_count + 1) / 2), zoom, 1);
    if (!write_ppm(path, sheet)) {
        std::fprintf(stderr, "could not write %s\n", path.c_str());
        return 1;
    }
    std::printf("wrote %s (%d x %d)\n", path.c_str(), sheet.width, sheet.height);
    return 0;
}
