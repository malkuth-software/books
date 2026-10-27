#include "page/font_set.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <hb-ot.h>
#include <hb.h>

#include <array>
#include <cstring>

namespace pl::page {
namespace {

struct LoadedFace final {
    bool loaded{false};
    FT_Face outline_face{nullptr};
    hb_face_t* shaping_face{nullptr};
    hb_font_t* shaping_font{nullptr};
    // The size last given to FT_Set_Char_Size, so a run of glyphs at one
    // size sets it once. Zero means none yet.
    std::uint32_t outline_size_26_6{0U};
};

std::size_t index_of(FaceId face) noexcept {
    const std::size_t index = static_cast<std::size_t>(face);
    return index;
}

hb_script_t script_of(TextScript script) noexcept {
    switch (script) {
    case TextScript::latin:
        return HB_SCRIPT_LATIN;
    case TextScript::greek:
        return HB_SCRIPT_GREEK;
    case TextScript::hebrew:
        return HB_SCRIPT_HEBREW;
    }
    return HB_SCRIPT_LATIN;
}

const char* language_of(TextScript script) noexcept {
    switch (script) {
    case TextScript::latin:
        return "en";
    case TextScript::greek:
        return "el";
    case TextScript::hebrew:
        return "he";
    }
    return "en";
}

} // namespace

struct FontSet::State final {
    FT_Library library{nullptr};
    std::array<LoadedFace, face_count> faces{};
    // Reused by every shape() call; cleared before use.
    hb_buffer_t* buffer{nullptr};
};

FontSet::FontSet() : state_(std::make_unique<State>()) {
    const FT_Error error = FT_Init_FreeType(&(*state_).library);
    if (error != 0) {
        (*state_).library = nullptr;
    }
    (*state_).buffer = hb_buffer_create();
}

FontSet::~FontSet() {
    for (LoadedFace& face : (*state_).faces) {
        if (face.shaping_font != nullptr) {
            hb_font_destroy(face.shaping_font);
        }
        if (face.shaping_face != nullptr) {
            hb_face_destroy(face.shaping_face);
        }
        if (face.outline_face != nullptr) {
            FT_Done_Face(face.outline_face);
        }
    }
    if ((*state_).buffer != nullptr) {
        hb_buffer_destroy((*state_).buffer);
    }
    if ((*state_).library != nullptr) {
        FT_Done_FreeType((*state_).library);
    }
}

bool FontSet::ready() const noexcept {
    const bool is_ready = (*state_).library != nullptr && (*state_).buffer != nullptr;
    return is_ready;
}

bool FontSet::load(FaceId face, std::span<const std::uint8_t> bytes) {
    LoadedFace& slot = (*state_).faces[index_of(face)];
    if (!ready() || slot.loaded || bytes.empty()) {
        return false;
    }
    FT_Face outline_face = nullptr;
    const FT_Error error = FT_New_Memory_Face((*state_).library, bytes.data(), static_cast<FT_Long>(bytes.size()),
                                              0, &outline_face);
    if (error != 0) {
        return false;
    }
    // The blob borrows the caller's bytes, as the FreeType face does.
    hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(bytes.data()),
                                     static_cast<unsigned int>(bytes.size()), HB_MEMORY_MODE_READONLY, nullptr,
                                     nullptr);
    hb_face_t* shaping_face = hb_face_create(blob, 0U);
    hb_blob_destroy(blob);
    if (hb_face_get_glyph_count(shaping_face) == 0U) {
        hb_face_destroy(shaping_face);
        FT_Done_Face(outline_face);
        return false;
    }
    slot.outline_face = outline_face;
    slot.shaping_face = shaping_face;
    slot.shaping_font = hb_font_create(shaping_face);
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
    hb_codepoint_t glyph = 0U;
    const hb_bool_t found = hb_font_get_nominal_glyph(slot.shaping_font, static_cast<hb_codepoint_t>(code_point), &glyph);
    if (found == 0) {
        return 0U;
    }
    return static_cast<std::uint32_t>(glyph);
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
    const hb_tag_t wanted = HB_TAG(tag[0], tag[1], tag[2], tag[3]);
    unsigned int start = 0U;
    while (true) {
        std::array<hb_tag_t, 32U> tags{};
        unsigned int count = static_cast<unsigned int>(tags.size());
        hb_ot_layout_table_get_feature_tags(slot.shaping_face, HB_OT_TAG_GSUB, start, &count, tags.data());
        for (unsigned int index = 0U; index < count; ++index) {
            if (tags[index] == wanted) {
                return true;
            }
        }
        if (count < tags.size()) {
            return false;
        }
        start += count;
    }
}

LayoutUnit FontSet::glyph_advance(FaceId face, LayoutUnit em, std::uint32_t glyph) const {
    const LoadedFace& slot = (*state_).faces[index_of(face)];
    if (!slot.loaded) {
        return 0;
    }
    hb_font_set_scale(slot.shaping_font, em, em);
    const hb_position_t advance = hb_font_get_glyph_h_advance(slot.shaping_font, glyph);
    return static_cast<LayoutUnit>(advance);
}

GlyphBox FontSet::glyph_box(FaceId face, LayoutUnit em, std::uint32_t glyph) const {
    GlyphBox box;
    const LoadedFace& slot = (*state_).faces[index_of(face)];
    if (!slot.loaded) {
        return box;
    }
    hb_font_set_scale(slot.shaping_font, em, em);
    hb_glyph_extents_t extents{};
    const hb_bool_t found = hb_font_get_glyph_extents(slot.shaping_font, glyph, &extents);
    if (found == 0) {
        return box;
    }
    // HarfBuzz gives the top-left corner and a height that is negative.
    box.x_min = extents.x_bearing;
    box.x_max = extents.x_bearing + extents.width;
    box.y_max = extents.y_bearing;
    box.y_min = extents.y_bearing + extents.height;
    return box;
}

FaceMetrics FontSet::metrics(FaceId face, LayoutUnit em) const {
    FaceMetrics result;
    const LoadedFace& slot = (*state_).faces[index_of(face)];
    if (!slot.loaded) {
        return result;
    }
    hb_font_set_scale(slot.shaping_font, em, em);
    hb_font_extents_t extents{};
    static_cast<void>(hb_font_get_h_extents(slot.shaping_font, &extents));
    result.ascender = extents.ascender;
    result.descender = -extents.descender;

    hb_position_t position = 0;
    if (hb_ot_metrics_get_position(slot.shaping_font, HB_OT_METRICS_TAG_X_HEIGHT, &position) != 0 && position > 0) {
        result.x_height = position;
    } else {
        result.x_height = glyph_box(face, em, glyph_of(face, U'x')).y_max;
    }
    if (hb_ot_metrics_get_position(slot.shaping_font, HB_OT_METRICS_TAG_CAP_HEIGHT, &position) != 0 &&
        position > 0) {
        result.cap_height = position;
    } else {
        result.cap_height = glyph_box(face, em, glyph_of(face, U'H')).y_max;
    }
    return result;
}

void FontSet::shape(FaceId face, LayoutUnit em, std::string_view utf8, TextScript script, bool right_to_left,
                    ShapeFeatures features, ShapedRun& run) const {
    ShapedRun shaped;
    const LoadedFace& slot = (*state_).faces[index_of(face)];
    if (!slot.loaded || utf8.empty()) {
        run = std::move(shaped);
        return;
    }
    hb_buffer_t* buffer = (*state_).buffer;
    hb_buffer_clear_contents(buffer);
    hb_buffer_add_utf8(buffer, utf8.data(), static_cast<int>(utf8.size()), 0U, static_cast<int>(utf8.size()));
    hb_buffer_set_direction(buffer, right_to_left ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_set_script(buffer, script_of(script));
    hb_buffer_set_language(buffer, hb_language_from_string(language_of(script), -1));

    std::array<hb_feature_t, 5U> requested{};
    unsigned int requested_count = 0U;
    if (features.capitals_to_small_capitals) {
        requested[requested_count] = hb_feature_t{HB_TAG('c', '2', 's', 'c'), 1U, HB_FEATURE_GLOBAL_START,
                                                  HB_FEATURE_GLOBAL_END};
        ++requested_count;
    }
    if (features.old_style_figures) {
        requested[requested_count] = hb_feature_t{HB_TAG('o', 'n', 'u', 'm'), 1U, HB_FEATURE_GLOBAL_START,
                                                  HB_FEATURE_GLOBAL_END};
        ++requested_count;
    }
    if (features.lining_figures) {
        requested[requested_count] = hb_feature_t{HB_TAG('l', 'n', 'u', 'm'), 1U, HB_FEATURE_GLOBAL_START,
                                                  HB_FEATURE_GLOBAL_END};
        ++requested_count;
    }
    if (features.mark_positioning_off) {
        requested[requested_count] = hb_feature_t{HB_TAG('m', 'a', 'r', 'k'), 0U, HB_FEATURE_GLOBAL_START,
                                                  HB_FEATURE_GLOBAL_END};
        ++requested_count;
        requested[requested_count] = hb_feature_t{HB_TAG('m', 'k', 'm', 'k'), 0U, HB_FEATURE_GLOBAL_START,
                                                  HB_FEATURE_GLOBAL_END};
        ++requested_count;
    }
    hb_buffer_set_cluster_level(buffer, features.cluster_per_character ? HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS
                                                                        : HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);

    hb_font_set_scale(slot.shaping_font, em, em);
    hb_shape(slot.shaping_font, buffer, requested.data(), requested_count);

    unsigned int glyph_count = 0U;
    const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &glyph_count);
    const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &glyph_count);
    shaped.glyphs.resize(glyph_count);
    LayoutUnit advance = 0;
    for (unsigned int index = 0U; index < glyph_count; ++index) {
        ShapedGlyph& glyph = shaped.glyphs[index];
        glyph.glyph = infos[index].codepoint;
        glyph.cluster = infos[index].cluster;
        glyph.x_advance = positions[index].x_advance;
        glyph.x_offset = positions[index].x_offset;
        glyph.y_offset = positions[index].y_offset;
        advance += positions[index].x_advance;
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
    if (slot.outline_size_26_6 != size_26_6) {
        // At 72 dpi one point is one pixel, so the character size is the em
        // in 26.6 pixels.
        const FT_Error size_error = FT_Set_Char_Size(slot.outline_face, 0, static_cast<FT_F26Dot6>(size_26_6), 72U, 72U);
        if (size_error != 0) {
            slot.outline_size_26_6 = 0U;
            return false;
        }
        slot.outline_size_26_6 = size_26_6;
    }
    const FT_Error load_error = FT_Load_Glyph(slot.outline_face, glyph, FT_LOAD_NO_BITMAP | FT_LOAD_TARGET_LIGHT | FT_LOAD_FORCE_AUTOHINT);
    if (load_error != 0) {
        return false;
    }
    const FT_GlyphSlot glyph_slot = (*slot.outline_face).glyph;
    if ((*glyph_slot).format != FT_GLYPH_FORMAT_OUTLINE) {
        return false;
    }
    if (embolden_26_6 != 0) {
        static_cast<void>(FT_Outline_Embolden(&(*glyph_slot).outline, embolden_26_6));
    }
    if (shift_x_26_6 != 0 || shift_y_26_6 != 0) {
        FT_Outline_Translate(&(*glyph_slot).outline, shift_x_26_6, shift_y_26_6);
    }
    const FT_Error render_error = FT_Render_Glyph(glyph_slot, FT_RENDER_MODE_NORMAL);
    if (render_error != 0) {
        return false;
    }
    const FT_Bitmap& bitmap = (*glyph_slot).bitmap;
    GlyphCoverage coverage;
    coverage.left = (*glyph_slot).bitmap_left;
    coverage.top = (*glyph_slot).bitmap_top;
    coverage.width = bitmap.width;
    coverage.height = bitmap.rows;
    coverage.coverage.resize(static_cast<std::size_t>(bitmap.width) * bitmap.rows);
    for (unsigned int row = 0U; row < bitmap.rows; ++row) {
        // A positive pitch means rows run top to bottom, which is what
        // FreeType produces for a rendered outline.
        const unsigned char* source = bitmap.buffer + static_cast<std::ptrdiff_t>(row) * bitmap.pitch;
        if (bitmap.width > 0U) {
            std::memcpy(coverage.coverage.data() + static_cast<std::size_t>(row) * bitmap.width, source,
                        bitmap.width);
        }
    }
    destination = std::move(coverage);
    return true;
}

}  // namespace pl::page
