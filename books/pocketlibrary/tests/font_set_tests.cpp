// The page compositor's faces on STXlib's glyph engine: loading, glyphs,
// metrics, shaping with language-tagged runs and features, and rasterizing
// with light fitting and thickening. Reads the library's own fonts from the
// source tree (PL_FONT_DIR); no window.
#include "page/font_set.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    std::vector<std::uint8_t> bytes;
    if (!stream) {
        return bytes;
    }
    const std::istreambuf_iterator<char> first(stream);
    const std::istreambuf_iterator<char> last;
    const std::string text(first, last);
    bytes.assign(text.begin(), text.end());
    return bytes;
}

std::span<const std::uint8_t> span_of(const std::vector<std::uint8_t>& bytes) {
    const std::span<const std::uint8_t> view(bytes.data(), bytes.size());
    return view;
}

// The em the pages are set at, and a 16-pixel em for rasterizing.
constexpr pl::page::LayoutUnit em = pl::page::medium_em_lu;
constexpr std::uint32_t sixteen_pixels_26_6 = 16U * 64U;

void test_loading(pl::page::FontSet& fonts, const std::vector<std::uint8_t>& garamond) {
    check(fonts.ready(), "the engine needs no start-up");
    check(!fonts.loaded(pl::page::FaceId::eb_garamond_regular), "nothing is loaded at first");
    const std::vector<std::uint8_t> not_a_font{1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
    check(!fonts.load(pl::page::FaceId::literata_regular, span_of(not_a_font)), "bytes that are no font are refused");
    check(!fonts.loaded(pl::page::FaceId::literata_regular), "a refused load loads nothing");
    check(fonts.load(pl::page::FaceId::eb_garamond_regular, span_of(garamond)), "EB Garamond loads");
    check(fonts.loaded(pl::page::FaceId::eb_garamond_regular), "EB Garamond is loaded");
    check(!fonts.load(pl::page::FaceId::eb_garamond_regular, span_of(garamond)), "a face loads once");
}

void test_glyphs_and_metrics(const pl::page::FontSet& fonts) {
    const pl::page::FaceId face = pl::page::FaceId::eb_garamond_regular;
    const std::uint32_t capital = fonts.glyph_of(face, U'H');
    check(capital != 0U, "H has a glyph");
    check(fonts.has_glyph(face, U'x'), "x has a glyph");
    check(fonts.glyph_of(pl::page::FaceId::literata_italic, U'H') == 0U, "an unloaded face maps nothing");
    check(fonts.glyph_advance(face, em, capital) > 0, "H advances");
    const pl::page::GlyphBox box = fonts.glyph_box(face, em, capital);
    check(!box.empty() && box.y_min <= 0 && box.y_max > 0, "H stands on the baseline");
    const pl::page::FaceMetrics metrics = fonts.metrics(face, em);
    check(metrics.ascender > metrics.cap_height, "the ascender is above the capitals");
    check(metrics.cap_height > metrics.x_height && metrics.x_height > 0, "capitals are taller than x");
    check(metrics.descender > 0, "the descender is positive");
    check(!fonts.has_feature(face, "zzzz"), "no face has feature zzzz");
    check(!fonts.has_feature(face, "abc"), "a tag is four letters");
}

void test_shaping(const pl::page::FontSet& fonts) {
    const pl::page::FaceId face = pl::page::FaceId::eb_garamond_regular;
    pl::page::ShapedRun run;
    fonts.shape(face, em, "office", pl::page::TextScript::latin, false, pl::page::ShapeFeatures{}, run);
    check(!run.glyphs.empty() && run.glyphs.size() < 6U, "standard ligatures join office's ffi");
    check(run.advance > 0, "a shaped run advances");
    pl::page::LayoutUnit sum = 0;
    for (const pl::page::ShapedGlyph& glyph : run.glyphs) {
        sum += glyph.x_advance;
    }
    check(sum == run.advance, "the run's advance is its glyphs' sum");

    // Clusters are byte offsets into the shaped text: the alpha with tonos is
    // two bytes, so the next letter's cluster is 2.
    fonts.shape(face, em, "\xce\xac\xce\xb2", pl::page::TextScript::greek, false, pl::page::ShapeFeatures{}, run);
    check(run.glyphs.size() == 2U, "two Greek letters give two glyphs");
    if (run.glyphs.size() == 2U) {
        check(run.glyphs[0].cluster == 0U && run.glyphs[1].cluster == 2U, "clusters are byte offsets");
    }

    // Right to left: glyphs come back in visual order, so the first glyph is
    // the last character's.
    fonts.shape(face, em, "ab", pl::page::TextScript::latin, true, pl::page::ShapeFeatures{}, run);
    check(run.glyphs.size() == 2U && run.glyphs[0].cluster == 1U, "a right-to-left run is in visual order");

    pl::page::ShapeFeatures capitals;
    capitals.capitals_to_small_capitals = true;
    pl::page::ShapedRun plain;
    pl::page::ShapedRun small;
    fonts.shape(face, em, "LORD", pl::page::TextScript::latin, false, pl::page::ShapeFeatures{}, plain);
    fonts.shape(face, em, "LORD", pl::page::TextScript::latin, false, capitals, small);
    check(plain.glyphs.size() == small.glyphs.size(), "small capitals keep the glyph count");
    if (fonts.has_feature(face, "c2sc")) {
        check(!small.glyphs.empty() && small.glyphs[0].glyph != plain.glyphs[0].glyph,
              "c2sc substitutes the capitals");
    }

    fonts.shape(face, em, "", pl::page::TextScript::latin, false, pl::page::ShapeFeatures{}, run);
    check(run.glyphs.empty() && run.advance == 0, "empty text shapes to nothing");
    fonts.shape(pl::page::FaceId::literata_regular, em, "a", pl::page::TextScript::latin, false,
                pl::page::ShapeFeatures{}, run);
    check(run.glyphs.empty(), "an unloaded face shapes nothing");
}

void test_rasterizing(const pl::page::FontSet& fonts) {
    const pl::page::FaceId face = pl::page::FaceId::eb_garamond_regular;
    const std::uint32_t capital = fonts.glyph_of(face, U'H');
    pl::page::GlyphCoverage plain;
    check(fonts.rasterize(face, sixteen_pixels_26_6, capital, 0, 0, 0, plain), "H rasterizes");
    check(plain.width > 0U && plain.height > 0U && plain.top > 0, "H has ink above the baseline");
    check(plain.coverage.size() == static_cast<std::size_t>(plain.width) * plain.height, "one byte a pixel");
    bool inked = false;
    for (const std::uint8_t value : plain.coverage) {
        if (value == 255U) {
            inked = true;
        }
    }
    check(inked, "a 16-pixel H has full pixels");

    pl::page::GlyphCoverage thick;
    check(fonts.rasterize(face, sixteen_pixels_26_6, capital, 0, 0, 64, thick), "a thickened H rasterizes");
    check(thick.width >= plain.width && thick.left == plain.left, "thickening grows to the right, the left kept");

    pl::page::GlyphCoverage shifted;
    check(fonts.rasterize(face, sixteen_pixels_26_6, capital, 32, 0, 0, shifted), "a shifted H rasterizes");
    check(shifted.coverage != plain.coverage || shifted.width != plain.width, "half a pixel moves the coverage");

    const std::uint32_t space = fonts.glyph_of(face, U' ');
    pl::page::GlyphCoverage blank;
    check(fonts.rasterize(face, sixteen_pixels_26_6, space, 0, 0, 0, blank), "a space rasterizes");
    check(blank.coverage.empty(), "a space has no ink");

    pl::page::GlyphCoverage untouched;
    untouched.width = 7U;
    check(!fonts.rasterize(face, sixteen_pixels_26_6, pl::page::synthetic_dagger_glyph, 0, 0, 0, untouched),
          "a glyph the font has not is refused");
    check(untouched.width == 7U, "a refused glyph leaves the destination unchanged");
}

} // namespace

int main() {
    const std::string directory = PL_FONT_DIR;
    const std::vector<std::uint8_t> garamond = read_file(directory + "/eb-garamond.ttf");
    check(!garamond.empty(), "eb-garamond.ttf is read");
    if (garamond.empty()) {
        return EXIT_FAILURE;
    }
    pl::page::FontSet fonts;
    test_loading(fonts, garamond);
    test_glyphs_and_metrics(fonts);
    test_shaping(fonts);
    test_rasterizing(fonts);
    if (failures == 0)
        std::printf("pocketlibrary font set tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
