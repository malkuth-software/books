// The paper engine's own tests: determinism, seamless tiling, that a flat sheet comes
// out at its colour and a textured one stays quiet, that each preset and parameter
// does what it says, the sheet's shading, refusal of bad sizes, and the cost at the
// sizes the shell uses.
#include "paper.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

double luminance(const paper::Image& image, std::size_t pixel) {
    const std::uint8_t* p = &image.rgba[pixel * 4];
    const double value = 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
    return value;
}

double mean_luminance(const paper::Image& image, int x0, int y0, int x1, int y1) {
    double sum = 0.0;
    int count = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            sum += luminance(image, static_cast<std::size_t>(y) * image.width + x);
            ++count;
        }
    }
    const double mean = count > 0 ? sum / count : 0.0;
    return mean;
}

double mean_luminance(const paper::Image& image) {
    const double mean = mean_luminance(image, 0, 0, image.width, image.height);
    return mean;
}

double deviation(const paper::Image& image) {
    const double mean = mean_luminance(image);
    const std::size_t count = static_cast<std::size_t>(image.width) * image.height;
    double sum = 0.0;
    for (std::size_t pixel = 0; pixel < count; ++pixel) {
        const double d = luminance(image, pixel) - mean;
        sum += d * d;
    }
    const double result = std::sqrt(sum / static_cast<double>(count));
    return result;
}

double colour_step(const paper::Image& image, int x0, int y0, int x1, int y1) {
    const std::size_t a = (static_cast<std::size_t>(y0) * image.width + x0) * 4;
    const std::size_t b = (static_cast<std::size_t>(y1) * image.width + x1) * 4;
    double sum = 0.0;
    for (int c = 0; c < 3; ++c) {
        sum += std::fabs(static_cast<double>(image.rgba[a + c]) - image.rgba[b + c]);
    }
    return sum;
}

// Across a seam, neighbouring pixels must differ no more than neighbours inside the
// picture do, averaged over several seeds so a fibre on one line does not decide it.
struct SeamTally {
    double seam_x = 0.0;
    double inside_x = 0.0;
    double seam_y = 0.0;
    double inside_y = 0.0;
};

void tally_seams(const paper::Image& image, SeamTally& tally) {
    const int w = image.width;
    const int h = image.height;
    for (int y = 0; y < h; ++y) {
        tally.seam_x += colour_step(image, w - 1, y, 0, y);
        tally.inside_x += colour_step(image, w / 2 - 1, y, w / 2, y);
    }
    for (int x = 0; x < w; ++x) {
        tally.seam_y += colour_step(image, x, h - 1, x, 0);
        tally.inside_y += colour_step(image, x, h / 2 - 1, x, h / 2);
    }
}

bool same_bytes(const paper::Image& a, const paper::Image& b) {
    const bool same = a.width == b.width && a.height == b.height && a.rgba == b.rgba;
    return same;
}

std::uint64_t digest(const paper::Image& image) {
    std::uint64_t h = 1469598103934665603ULL;
    for (std::uint8_t byte : image.rgba) {
        h ^= byte;
        h *= 1099511628211ULL;
    }
    return h;
}

double milliseconds_since(std::chrono::steady_clock::time_point start) {
    const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
    return elapsed.count();
}

paper::Parameters bare(paper::Colour colour) {
    paper::Parameters p;
    p.colour = colour;
    p.mottle = 0.0;
    p.tooth = 0.0;
    p.grain = 0.0;
    p.fibres = 0.0;
    p.blotch = 0.0;
    return p;
}

void test_determinism() {
    for (int index = 0; index < paper::preset_count; ++index) {
        const paper::Parameters p = paper::preset(static_cast<paper::Preset>(index));
        paper::Image a;
        paper::Image b;
        check(paper::generate_tile(p, 160, 120, a), "tile generates");
        check(paper::generate_tile(p, 160, 120, b), "tile generates again");
        check(same_bytes(a, b), "the same parameters give the same bytes");
        paper::Parameters other = p;
        other.seed += 1;
        paper::Image c;
        check(paper::generate_tile(other, 160, 120, c), "other seed generates");
        check(!same_bytes(a, c), "another seed gives different paper");
        check(a.width == 160 && a.height == 120 && a.rgba.size() == 160u * 120u * 4u, "size as asked");
        bool opaque = true;
        for (std::size_t i = 3; i < a.rgba.size(); i += 4) {
            opaque = opaque && a.rgba[i] == 255;
        }
        check(opaque, "paper is opaque");
    }
}

void test_flat_and_quiet() {
    const paper::Parameters flat = bare({0.6, 0.5, 0.4});
    paper::Image image;
    check(paper::generate_tile(flat, 64, 48, image), "flat generates");
    bool exact = true;
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        exact = exact && image.rgba[i] == 153 && image.rgba[i + 1] == 128 && image.rgba[i + 2] == 102;
    }
    check(exact, "with every amount zero the sheet is exactly its colour");

    for (int index = 0; index < paper::preset_count; ++index) {
        const paper::Preset kind = static_cast<paper::Preset>(index);
        const paper::Parameters p = paper::preset(kind);
        paper::Image tile;
        check(paper::generate_tile(p, 256, 256, tile), "preset generates");
        const double base = 255.0 * (0.2126 * p.colour.red + 0.7152 * p.colour.green + 0.0722 * p.colour.blue);
        const double mean = mean_luminance(tile);
        const double spread = deviation(tile);
        std::printf("%-10s mean %.1f (colour %.1f), deviation %.2f\n", paper::preset_name(kind), mean, base, spread);
        check(std::fabs(mean - base) < 8.0, "a preset keeps its colour on average");
        check(spread > 0.4, "a preset is textured");
        check(spread < 14.0, "a preset is quiet enough to read on");
    }
    paper::Image india;
    paper::Image rag;
    check(paper::generate_tile(paper::preset(paper::Preset::india), 256, 256, india), "india");
    check(paper::generate_tile(paper::preset(paper::Preset::rag), 256, 256, rag), "rag");
    check(deviation(india) < deviation(rag), "india paper is smoother than cotton rag");
    paper::Image book;
    check(paper::generate_tile(paper::preset(paper::Preset::book), 256, 256, book), "book");
    check(deviation(book) < deviation(rag) * 0.5, "machine-made book paper is far quieter than handmade rag");
}

void test_seams() {
    SeamTally tally;
    for (int index = 0; index < paper::preset_count; ++index) {
        for (std::uint32_t seed = 1; seed <= 4; ++seed) {
            paper::Parameters p = paper::preset(static_cast<paper::Preset>(index));
            p.seed = seed;
            paper::Image tile;
            check(paper::generate_tile(p, 200, 152, tile), "seam tile generates");
            tally_seams(tile, tally);
        }
    }
    std::printf("seams: across %.3f vs inside %.3f, down %.3f vs inside %.3f\n", tally.seam_x, tally.inside_x,
                tally.seam_y, tally.inside_y);
    check(tally.seam_x < tally.inside_x * 1.15 + 1.0, "the left and right edges join");
    check(tally.seam_y < tally.inside_y * 1.15 + 1.0, "the top and bottom edges join");

    // A sheet is not a tile: its edges are allowed to differ.
    paper::Image sheet;
    check(paper::generate_sheet(paper::preset(paper::Preset::wove), 200, 152, paper::Shading{}, sheet),
          "sheet generates");
    paper::Image tile;
    check(paper::generate_tile(paper::preset(paper::Preset::wove), 200, 152, tile), "tile generates");
    check(!same_bytes(sheet, tile), "a sheet is laid out differently from a tile");
}

void test_each_layer() {
    const paper::Colour cream{0.95, 0.92, 0.85};
    // Mottle alone: slow, so neighbours are near each other but the picture varies.
    paper::Parameters mottled = bare(cream);
    mottled.mottle = 0.05;
    paper::Image image;
    check(paper::generate_tile(mottled, 256, 256, image), "mottle generates");
    double neighbour = 0.0;
    for (int y = 0; y < 256; ++y) {
        for (int x = 1; x < 256; ++x) {
            neighbour += colour_step(image, x - 1, y, x, y);
        }
    }
    neighbour /= 256.0 * 255.0;
    check(deviation(image) > 1.0, "mottle varies the tone");
    check(neighbour < 1.0, "mottle is slow: neighbours differ by less than a step");

    // Grain alone: every pixel its own.
    paper::Parameters grainy = bare(cream);
    grainy.grain = 0.05;
    check(paper::generate_tile(grainy, 128, 128, image), "grain generates");
    neighbour = 0.0;
    for (int y = 0; y < 128; ++y) {
        for (int x = 1; x < 128; ++x) {
            neighbour += colour_step(image, x - 1, y, x, y);
        }
    }
    neighbour /= 128.0 * 127.0;
    check(neighbour > 4.0, "grain is fine: neighbours differ");
    check(std::fabs(mean_luminance(image) - 234.0) < 3.0, "grain is centred on the colour");

    // Fibres alone: a few pixels marked, most untouched, both lighter and darker.
    paper::Parameters fibrous = bare(cream);
    fibrous.fibres = 25.0;
    fibrous.fibre_contrast = 0.1;
    check(paper::generate_tile(fibrous, 256, 256, image), "fibres generate");
    int lighter = 0;
    int darker = 0;
    int plain = 0;
    for (std::size_t pixel = 0; pixel < 256u * 256u; ++pixel) {
        const int red = image.rgba[pixel * 4];
        if (red > 243) {
            ++lighter;
        } else if (red < 241) {
            ++darker;
        } else {
            ++plain;
        }
    }
    check(plain > 256 * 256 * 3 / 4, "fibres leave most of the sheet plain");
    check(lighter > 200 && darker > 200, "fibres are both lighter and darker than the sheet");

    // The web alone: a flat mat, zero on average; a denser mat is more even.
    paper::Parameters webbed = bare(cream);
    webbed.web = 2500.0;
    webbed.web_contrast = 0.04;
    check(paper::generate_tile(webbed, 256, 256, image), "web generates");
    const double sparse_spread = deviation(image);
    check(sparse_spread > 0.8, "the web shows");
    check(std::fabs(mean_luminance(image) - 234.0) < 1.5, "the web is zero on average");
    webbed.web = 10000.0;
    check(paper::generate_tile(webbed, 256, 256, image), "dense web generates");
    check(deviation(image) < sparse_spread, "a denser mat is more even");

    // Tooth alone: lit relief, zero on average.
    paper::Parameters toothed = bare(cream);
    toothed.tooth = 0.04;
    check(paper::generate_tile(toothed, 256, 256, image), "tooth generates");
    check(deviation(image) > 1.0, "tooth shows");
    check(std::fabs(mean_luminance(image) - 234.0) < 2.0, "tooth is zero on average");

    // Laid lines: the row means dip every spacing, so rows one spacing apart agree
    // better than rows half a spacing apart.
    paper::Parameters laid = bare(cream);
    laid.laid_spacing = 4.0;
    laid.laid_depth = 0.05;
    laid.pixels_per_point = 2.0;
    check(paper::generate_tile(laid, 64, 256, image), "laid generates");
    std::vector<double> rows(256, 0.0);
    for (int y = 0; y < 256; ++y) {
        rows[static_cast<std::size_t>(y)] = mean_luminance(image, 0, y, 64, y + 1);
    }
    double agree_period = 0.0;
    double agree_half = 0.0;
    for (int y = 0; y < 248; ++y) {
        agree_period += std::fabs(rows[static_cast<std::size_t>(y)] - rows[static_cast<std::size_t>(y + 8)]);
        agree_half += std::fabs(rows[static_cast<std::size_t>(y)] - rows[static_cast<std::size_t>(y + 4)]);
    }
    check(agree_period < 0.01 && agree_half > 1.0, "laid lines repeat every spacing");
    check(mean_luminance(image) < 234.0 - 0.5, "laid lines darken the sheet a little");

    // Chain lines run down the sheet.
    paper::Parameters chained = bare(cream);
    chained.chain_spacing = 32.0;
    chained.laid_depth = 0.05;
    check(paper::generate_tile(chained, 256, 32, image), "chain generates");
    std::vector<double> columns(256, 0.0);
    for (int x = 0; x < 256; ++x) {
        columns[static_cast<std::size_t>(x)] = mean_luminance(image, x, 0, x + 1, 32);
    }
    double chain_period = 0.0;
    double chain_half = 0.0;
    for (int x = 0; x < 224; ++x) {
        chain_period += std::fabs(columns[static_cast<std::size_t>(x)] - columns[static_cast<std::size_t>(x + 32)]);
        chain_half += std::fabs(columns[static_cast<std::size_t>(x)] - columns[static_cast<std::size_t>(x + 16)]);
    }
    check(chain_period < 0.01 && chain_half > 1.0, "chain lines repeat every spacing");

    // Blotch darkens in patches but keeps the average.
    paper::Parameters blotched = bare(cream);
    blotched.blotch = 0.1;
    check(paper::generate_tile(blotched, 256, 256, image), "blotch generates");
    check(deviation(image) > 1.5, "blotches show");
    check(std::fabs(mean_luminance(image) - 234.0) < 6.0, "blotches roughly keep the colour");

    // Warmth: darkening takes more from the blue than from the red.
    paper::Parameters warm = bare(cream);
    warm.mottle = 0.1;
    warm.warmth = 1.0;
    paper::Parameters grey = warm;
    grey.warmth = 0.0;
    paper::Image warm_image;
    paper::Image grey_image;
    check(paper::generate_tile(warm, 128, 128, warm_image), "warm generates");
    check(paper::generate_tile(grey, 128, 128, grey_image), "grey generates");
    double warm_blue_swing = 0.0;
    double grey_blue_swing = 0.0;
    for (std::size_t pixel = 0; pixel < 128u * 128u; ++pixel) {
        warm_blue_swing += std::fabs(static_cast<double>(warm_image.rgba[pixel * 4 + 2]) - 217.0);
        grey_blue_swing += std::fabs(static_cast<double>(grey_image.rgba[pixel * 4 + 2]) - 217.0);
    }
    check(warm_blue_swing < grey_blue_swing * 0.95, "warmth moves the blue channel less than grey darkening");
}

void test_scale() {
    // The same paper at 2 pixels per point has features twice as large in pixels, so a
    // 2x tile looks like the 1x tile enlarged: its deviation is about the same and its
    // neighbours agree more closely.
    paper::Parameters one = paper::preset(paper::Preset::wove);
    paper::Parameters two = one;
    two.pixels_per_point = 2.0;
    paper::Image at_one;
    paper::Image at_two;
    check(paper::generate_tile(one, 128, 128, at_one), "1x generates");
    check(paper::generate_tile(two, 256, 256, at_two), "2x generates");
    const double spread_one = deviation(at_one);
    const double spread_two = deviation(at_two);
    check(spread_two > spread_one * 0.6 && spread_two < spread_one * 1.6, "2x keeps the amount of texture");
    double step_one = 0.0;
    double step_two = 0.0;
    for (int y = 0; y < 128; ++y) {
        for (int x = 1; x < 128; ++x) {
            step_one += colour_step(at_one, x - 1, y, x, y);
        }
    }
    for (int y = 0; y < 256; ++y) {
        for (int x = 1; x < 256; ++x) {
            step_two += colour_step(at_two, x - 1, y, x, y);
        }
    }
    step_one /= 128.0 * 127.0;
    step_two /= 256.0 * 255.0;
    check(step_two < step_one, "2x features are larger in pixels, so neighbours agree more");
}

void test_sheet_shading() {
    const paper::Parameters p = bare({0.9, 0.9, 0.9});
    paper::Shading shading;
    shading.gutter = paper::Gutter::left;
    shading.gutter_width = 40.0;
    shading.gutter_depth = 0.2;
    paper::Image sheet;
    check(paper::generate_sheet(p, 200, 100, shading, sheet), "gutter sheet generates");
    const double left = mean_luminance(sheet, 0, 0, 10, 100);
    const double middle = mean_luminance(sheet, 95, 0, 105, 100);
    const double right = mean_luminance(sheet, 190, 0, 200, 100);
    check(left < middle - 10.0, "a left gutter darkens the left edge");
    check(std::fabs(middle - right) < 0.5, "a left gutter leaves the right alone");
    check(std::fabs(middle - 229.5) < 1.0, "the open page is its colour");

    shading.gutter = paper::Gutter::right;
    check(paper::generate_sheet(p, 200, 100, shading, sheet), "right gutter generates");
    check(mean_luminance(sheet, 190, 0, 200, 100) < mean_luminance(sheet, 0, 0, 10, 100) - 10.0,
          "a right gutter darkens the right edge");

    paper::Shading edges;
    edges.edge_width = 20.0;
    edges.edge_depth = 0.1;
    check(paper::generate_sheet(p, 200, 100, edges, sheet), "edge sheet generates");
    const double corner = mean_luminance(sheet, 0, 0, 4, 4);
    const double centre = mean_luminance(sheet, 96, 46, 104, 54);
    check(corner < centre - 8.0, "edge fall darkens the corners");
    check(std::fabs(centre - 229.5) < 1.0, "edge fall leaves the middle alone");

    // Shading at 2x reaches the same distance in points.
    paper::Parameters doubled = p;
    doubled.pixels_per_point = 2.0;
    paper::Image large;
    check(paper::generate_sheet(doubled, 400, 200, edges, large), "2x edge sheet generates");
    check(std::fabs(mean_luminance(large, 0, 0, 8, 8) - corner) < 2.0, "edge fall scales with the display");
}

void test_refusals() {
    paper::Image image;
    check(paper::generate_tile(paper::preset(paper::Preset::wove), 32, 32, image), "a small tile generates");
    const std::uint64_t before = digest(image);
    check(!paper::generate_tile(paper::preset(paper::Preset::wove), 0, 32, image), "zero width refused");
    check(!paper::generate_tile(paper::preset(paper::Preset::wove), 32, 3, image), "tiny height refused");
    check(!paper::generate_tile(paper::preset(paper::Preset::wove), paper::max_side + 1, 32, image),
          "oversize refused");
    check(!paper::generate_sheet(paper::preset(paper::Preset::wove), 32, -5, paper::Shading{}, image),
          "negative sheet refused");
    check(digest(image) == before && image.width == 32, "a refusal leaves the picture alone");

    paper::Parameters bad = paper::preset(paper::Preset::wove);
    bad.mottle = std::nan("");
    bad.fibres = 1e9;
    bad.pixels_per_point = -4.0;
    bad.colour.red = 7.0;
    paper::Image sane;
    check(paper::generate_tile(bad, 64, 64, sane), "bad parameters are clamped, not refused");
    check(sane.rgba[0] == 255 || sane.rgba[0] > 200, "a clamped colour stays in range");
}

void test_cost() {
    struct Case {
        int side;
        double ppp;
    };
    const Case cases[] = {{256, 1.0}, {512, 2.0}, {1024, 2.0}};
    for (const Case& c : cases) {
        paper::Parameters p = paper::preset(paper::Preset::book);
        p.pixels_per_point = c.ppp;
        paper::Image image;
        double best = 1e9;
        for (int repeat = 0; repeat < 3; ++repeat) {
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            check(paper::generate_tile(p, c.side, c.side, image), "cost tile generates");
            best = std::min(best, milliseconds_since(start));
        }
        std::printf("%4d x %4d tile at %.0fx: %.2f ms\n", c.side, c.side, c.ppp, best);
        // Generous: an unoptimised build on a slow runner, not the Release figure.
        check(best < 600.0 * (c.side / 512.0) * (c.side / 512.0) + 60.0, "a tile costs milliseconds");
    }
}

} // namespace

int main() {
    test_determinism();
    test_flat_and_quiet();
    test_seams();
    test_each_layer();
    test_scale();
    test_sheet_shading();
    test_refusals();
    test_cost();
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("paper engine: all checks passed\n");
    return 0;
}
