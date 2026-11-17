#include "page/font_set.hpp"

#include "stxlib/font_file.hpp"
#include "stxlib/glyph_face.hpp"
#include "stxlib/glyph_shape.hpp"
#include "stxlib/result.hpp"
#include "stxlib/shared.hpp"
#include "stxlib/vector.hpp"
#include "stxlib/view.hpp"

#include <array>
#include <cstring>
#include <utility>

namespace pl::page {
namespace {

// One loaded face: the program's typeface (the font read once), the face
// this thread draws with, and the shaper this thread shapes with. The face
// and the shaper each hold the typeface while they are open; members are
// destroyed in reverse order, so both let it go before `typeface` does.
struct LoadedFace final {
    bool loaded{false};
    stxlib::Shared<stxlib::glyphs::Typeface> typeface{};
    stxlib::glyphs::Face drawing{};
    stxlib::glyphs::Shaper shaper{};
};

std::size_t index_of(FaceId face) noexcept {
    const std::size_t index = static_cast<std::size_t>(face);
    return index;
}

// The script of a run, as an ISO 15924 tag.
std::uint32_t script_tag_of(TextScript script) noexcept {
    switch (script) {
    case TextScript::latin:
        return stxlib::glyphs::text_tag('L', 'a', 't', 'n');
    case TextScript::greek:
        return stxlib::glyphs::text_tag('G', 'r', 'e', 'k');
    case TextScript::hebrew:
        return stxlib::glyphs::text_tag('H', 'e', 'b', 'r');
    }
    return stxlib::glyphs::text_tag('L', 'a', 't', 'n');
}

// The language system a run is tagged with, as an OpenType tag: English,
// Greek and Hebrew, the languages the library's scripts are read in (the
// tags HarfBuzz chose for "en", "el" and "he").
std::uint32_t language_tag_of(TextScript script) noexcept {
    switch (script) {
    case TextScript::latin:
        return stxlib::glyphs::text_tag('E', 'N', 'G', ' ');
    case TextScript::greek:
        return stxlib::glyphs::text_tag('E', 'L', 'L', ' ');
    case TextScript::hebrew:
        return stxlib::glyphs::text_tag('I', 'W', 'R', ' ');
    }
    return stxlib::glyphs::text_tag('E', 'N', 'G', ' ');
}

// A value in font units at `em` layout units to the em, rounded to the
// nearest, halves away from zero.
LayoutUnit scaled(std::int64_t font_units, LayoutUnit em, std::uint32_t units_per_em) noexcept {
    if (units_per_em == 0U) {
        return 0;
    }
    const std::int64_t product = font_units * static_cast<std::int64_t>(em);
    const std::int64_t divisor = static_cast<std::int64_t>(units_per_em);
    const std::int64_t half = divisor / 2;
    const std::int64_t rounded = product >= 0 ? (product + half) / divisor : -((-product + half) / divisor);
    return static_cast<LayoutUnit>(rounded);
}

// One UTF-8 sequence decoded. A byte that does not begin a well-formed
// sequence is U+FFFD and one byte long.
struct DecodedScalar final {
    std::uint32_t scalar{0xfffdU};
    std::size_t length{1U};
};

DecodedScalar decode_at(std::string_view utf8, std::size_t offset) noexcept {
    DecodedScalar decoded;
    const std::uint32_t lead = static_cast<unsigned char>(utf8[offset]);
    std::size_t length = 0U;
    std::uint32_t scalar = 0U;
    std::uint32_t least = 0U;
    if (lead < 0x80U) {
        decoded.scalar = lead;
        return decoded;
    }
    if (lead >= 0xc2U && lead <= 0xdfU) {
        length = 2U;
        scalar = lead & 0x1fU;
        least = 0x80U;
    } else if (lead >= 0xe0U && lead <= 0xefU) {
        length = 3U;
        scalar = lead & 0x0fU;
        least = 0x800U;
    } else if (lead >= 0xf0U && lead <= 0xf4U) {
        length = 4U;
        scalar = lead & 0x07U;
        least = 0x10000U;
    } else {
        return decoded;
    }
    if (utf8.size() - offset < length) {
        return decoded;
    }
    for (std::size_t index = 1U; index < length; ++index) {
        const std::uint32_t continuation = static_cast<unsigned char>(utf8[offset + index]);
        if ((continuation & 0xc0U) != 0x80U) {
            return decoded;
        }
        scalar = (scalar << 6U) | (continuation & 0x3fU);
    }
    const bool surrogate = scalar >= 0xd800U && scalar <= 0xdfffU;
    if (scalar < least || scalar > 0x10ffffU || surrogate) {
        return decoded;
    }
    decoded.scalar = scalar;
    decoded.length = length;
    return decoded;
}

stxlib::glyphs::TextFeature global_feature(std::uint32_t tag, std::uint32_t value) noexcept {
    stxlib::glyphs::TextFeature feature;
    feature.tag = tag;
    feature.value = value;
    return feature;
}

} // namespace

struct FontSet::State final {
    std::array<LoadedFace, face_count> faces{};
    // Reused by every shape() call: the run's scalars, the byte offset of
    // each in the shaped text (its cluster), and the shaper's glyphs.
    stxlib::Vector<std::uint32_t> characters{};
    stxlib::Vector<std::uint32_t> clusters{};
    stxlib::Vector<stxlib::glyphs::ShapedGlyph> shaped{};
    // Reused by every rasterize() call: one glyph's mask.
    stxlib::Vector<std::uint8_t> mask{};
};

FontSet::FontSet() : state_(std::make_unique<State>()) {}

// The faces, shapers and typefaces are released by their own destructors;
// the font bytes are the caller's.
FontSet::~FontSet() {}

bool FontSet::ready() const noexcept {
    return true;
}

bool FontSet::load(FaceId face, std::span<const std::uint8_t> bytes) {
    LoadedFace& slot = (*state_).faces[index_of(face)];
    if (slot.loaded || bytes.empty()) {
        return false;
    }
    const stxlib::View<std::uint8_t> font_bytes(bytes.data(), bytes.size());
    stxlib::OperationResult<stxlib::Shared<stxlib::glyphs::Typeface>> opened =
        stxlib::glyphs::open_typeface(font_bytes, 0U);
    if (opened.failed()) {
        return false;
    }
    stxlib::Shared<stxlib::glyphs::Typeface> typeface = std::move(opened).take_value();
    const stxlib::glyphs::Font* const font = (*typeface).font();
    if (font == nullptr || (*font).glyph_count == 0U || (*font).units_per_em == 0U) {
        return false;
    }
    stxlib::glyphs::Face drawing;
    if (drawing.open(typeface).failed()) {
        return false;
    }
    stxlib::glyphs::Shaper shaper;
    if (shaper.open(typeface).failed()) {
        return false;
    }
    slot.typeface = std::move(typeface);
    slot.drawing = std::move(drawing);
    slot.shaper = std::move(shaper);
    slot.loaded = true;
    return true;
}

bool FontSet::loaded(FaceId face) const noexcept {
    const bool is_loaded = (*state_).faces[index_of(face)].loaded;
    return is_loaded;
}

std::uint32_t FontSet::glyph_of(FaceId face, char32_t code_point) const {
    const LoadedFace& slot = (*state_).faces[index_of(face)];
    if (!slot.loaded) {
        return 0U;
    }
    const stxlib::glyphs::Font& font = *(*slot.typeface).font();
    const std::uint32_t glyph = stxlib::glyphs::glyph_of(font, static_cast<std::uint32_t>(code_point));
    return glyph;
}

bool FontSet::has_glyph(FaceId face, char32_t code_point) const {
    const bool present = glyph_of(face, code_point) != 0U;
    return present;
}

bool FontSet::has_feature(FaceId face, std::string_view tag) const {
    const LoadedFace& slot = (*state_).faces[index_of(face)];
    if (!slot.loaded || tag.size() != 4U) {
        return false;
    }
    const std::uint32_t wanted = stxlib::glyphs::text_tag(tag[0], tag[1], tag[2], tag[3]);
    const stxlib::glyphs::Font& font = *(*slot.typeface).font();
    const bool listed = stxlib::glyphs::has_feature(font, wanted);
    return listed;
}

LayoutUnit FontSet::glyph_advance(FaceId face, LayoutUnit em, std::uint32_t glyph) const {
    const LoadedFace& slot = (*state_).faces[index_of(face)];
    if (!slot.loaded) {
        return 0;
    }
    const stxlib::glyphs::Font& font = *(*slot.typeface).font();
    const std::int32_t advance = stxlib::glyphs::advance_of(font, glyph);
    const LayoutUnit result = scaled(advance, em, font.units_per_em);
    return result;
}

GlyphBox FontSet::glyph_box(FaceId face, LayoutUnit em, std::uint32_t glyph) const {
    GlyphBox box;
    const LoadedFace& slot = (*state_).faces[index_of(face)];
    if (!slot.loaded || em <= 0) {
        return box;
    }
    // The size is given in layout units to the em, so the box (the
    // unfitted outline's, control points included) comes back in layout
    // units.
    stxlib::glyphs::InkBox ink;
    const stxlib::OperationStatus measured = (*slot.typeface).ink_box(glyph, static_cast<std::int64_t>(em), ink);
    if (measured.failed()) {
        return box;
    }
    box.x_min = ink.left;
    box.x_max = ink.right;
    box.y_min = ink.bottom;
    box.y_max = ink.top;
    return box;
}

FaceMetrics FontSet::metrics(FaceId face, LayoutUnit em) const {
    FaceMetrics result;
    const LoadedFace& slot = (*state_).faces[index_of(face)];
    if (!slot.loaded) {
        return result;
    }
    const stxlib::glyphs::Font& font = *(*slot.typeface).font();
    // The face's ascender and descender by FreeType's rule (font_file.hpp);
    // the descender is negative in the font and positive here.
    result.ascender = scaled(font.ascender, em, font.units_per_em);
    result.descender = -scaled(font.descender, em, font.units_per_em);
    if (font.x_height > 0) {
        result.x_height = scaled(font.x_height, em, font.units_per_em);
    } else {
        result.x_height = glyph_box(face, em, glyph_of(face, U'x')).y_max;
    }
    if (font.cap_height > 0) {
        result.cap_height = scaled(font.cap_height, em, font.units_per_em);
    } else {
        result.cap_height = glyph_box(face, em, glyph_of(face, U'H')).y_max;
    }
    return result;
}

void FontSet::shape(FaceId face, LayoutUnit em, std::string_view utf8, TextScript script, bool right_to_left,
                    ShapeFeatures features, ShapedRun& run) const {
    ShapedRun shaped;
    LoadedFace& slot = (*state_).faces[index_of(face)];
    if (!slot.loaded || utf8.empty() || em <= 0) {
        run = std::move(shaped);
        return;
    }
    // At most one scalar a byte: sized to the bytes, then filled.
    stxlib::Vector<std::uint32_t>& characters = (*state_).characters;
    stxlib::Vector<std::uint32_t>& clusters = (*state_).clusters;
    if (characters.resize(utf8.size()).failed() || clusters.resize(utf8.size()).failed()) {
        run = std::move(shaped);
        return;
    }
    std::size_t count = 0U;
    std::size_t offset = 0U;
    while (offset < utf8.size()) {
        const DecodedScalar decoded = decode_at(utf8, offset);
        characters[count] = decoded.scalar;
        clusters[count] = static_cast<std::uint32_t>(offset);
        ++count;
        offset += decoded.length;
    }

    std::array<stxlib::glyphs::TextFeature, 5U> requested{};
    std::size_t requested_count = 0U;
    if (features.capitals_to_small_capitals) {
        requested[requested_count] = global_feature(stxlib::glyphs::text_tag('c', '2', 's', 'c'), 1U);
        ++requested_count;
    }
    if (features.old_style_figures) {
        requested[requested_count] = global_feature(stxlib::glyphs::text_tag('o', 'n', 'u', 'm'), 1U);
        ++requested_count;
    }
    if (features.lining_figures) {
        requested[requested_count] = global_feature(stxlib::glyphs::text_tag('l', 'n', 'u', 'm'), 1U);
        ++requested_count;
    }
    if (features.mark_positioning_off) {
        requested[requested_count] = global_feature(stxlib::glyphs::text_tag('m', 'a', 'r', 'k'), 0U);
        ++requested_count;
        requested[requested_count] = global_feature(stxlib::glyphs::text_tag('m', 'k', 'm', 'k'), 0U);
        ++requested_count;
    }

    // The size is the em in layout units, so advances and offsets come back
    // in layout units: the unhinted advances scaled to the em, the same at
    // every window size.
    stxlib::glyphs::TextRun text;
    text.characters = stxlib::View<std::uint32_t>(characters.begin(), count);
    text.clusters = stxlib::View<std::uint32_t>(clusters.begin(), count);
    text.direction = right_to_left ? stxlib::glyphs::TextDirection::right_to_left
                                   : stxlib::glyphs::TextDirection::left_to_right;
    text.script = script_tag_of(script);
    text.language = language_tag_of(script);
    text.size = static_cast<std::int64_t>(em);
    text.features = stxlib::View<stxlib::glyphs::TextFeature>(requested.data(), requested_count);

    stxlib::Vector<stxlib::glyphs::ShapedGlyph>& glyphs = (*state_).shaped;
    const stxlib::OperationStatus status = slot.shaper.shape(text, glyphs);
    if (status.failed()) {
        run = std::move(shaped);
        return;
    }
    shaped.glyphs.resize(glyphs.count());
    LayoutUnit advance = 0;
    for (std::size_t index = 0U; index < glyphs.count(); ++index) {
        const stxlib::glyphs::ShapedGlyph& source = glyphs[index];
        ShapedGlyph& glyph = shaped.glyphs[index];
        glyph.glyph = source.glyph;
        glyph.cluster = source.cluster;
        glyph.x_advance = source.x_advance;
        glyph.x_offset = source.x_offset;
        glyph.y_offset = source.y_offset;
        advance += source.x_advance;
    }
    shaped.advance = advance;
    run = std::move(shaped);
}

bool FontSet::rasterize(FaceId face, std::uint32_t size_26_6, std::uint32_t glyph, std::int32_t shift_x_26_6,
                        std::int32_t shift_y_26_6, std::int32_t embolden_26_6, GlyphCoverage& destination) const {
    LoadedFace& slot = (*state_).faces[index_of(face)];
    if (!slot.loaded || size_26_6 == 0U) {
        return false;
    }
    std::int32_t thicken = embolden_26_6;
    if (thicken < 0) {
        thicken = 0;
    } else if (thicken > 1024) {
        thicken = 1024;
    }
    stxlib::glyphs::DrawStyle style;
    style.size = static_cast<std::int64_t>(size_26_6);
    style.fit = stxlib::glyphs::Fit::light;
    style.thicken = thicken;
    stxlib::glyphs::Point shift;
    shift.x = shift_x_26_6;
    shift.y = shift_y_26_6;
    stxlib::glyphs::Bounds bounds;
    stxlib::Vector<std::uint8_t>& mask = (*state_).mask;
    const stxlib::OperationStatus drawn = slot.drawing.mask(glyph, style, shift, bounds, mask);
    if (drawn.failed()) {
        return false;
    }
    // The mask's bounds agree with what FreeType's bitmap gave: `left`
    // pixels right of the pen, `top` the y of the top row's top edge, y
    // upward, rows top to bottom with no padding.
    const std::size_t bytes = static_cast<std::size_t>(bounds.width) * bounds.height;
    if (bytes > mask.count()) {
        return false;
    }
    GlyphCoverage coverage;
    coverage.left = bounds.left;
    coverage.top = bounds.top;
    coverage.width = bounds.width;
    coverage.height = bounds.height;
    coverage.coverage.resize(bytes);
    if (bytes > 0U) {
        std::memcpy(coverage.coverage.data(), mask.begin(), bytes);
    }
    destination = std::move(coverage);
    return true;
}

}  // namespace pl::page
