#include "capsule_title_face.hpp"

#include "runtime_paths.hpp"

#if defined(GAMES_GUI_FORMS_APP_FONTS)
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>
#endif

namespace games {

#if defined(GAMES_GUI_FORMS_APP_FONTS)
namespace {

// The family's name as looks would write it; one load per process.
constexpr std::string_view title_family_name = "Barlow Condensed";

// The face's bytes, or none when the file is missing or unreadable.
std::shared_ptr<const std::vector<std::byte>> read_face(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if (!in)
        return {};
    const std::streamoff end = in.tellg();
    if (end <= 0 || static_cast<std::uint64_t>(end) > gf::maximum_app_font_bytes)
        return {};
    std::shared_ptr<std::vector<std::byte>> bytes =
        std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(end));
    in.seekg(0);
    in.read(reinterpret_cast<char*>((*bytes).data()), static_cast<std::streamsize>(end));
    if (!in)
        return {};
    return bytes;
}

} // namespace
#endif

CapsuleTitleFamily::~CapsuleTitleFamily() {
    release();
}

CapsuleTitleFaceState CapsuleTitleFamily::load() {
    if (state_ != CapsuleTitleFaceState::unloaded)
        return state_;
#if defined(GAMES_GUI_FORMS_APP_FONTS)
    state_ = CapsuleTitleFaceState::refused;
    try {
        const std::filesystem::path file =
            std::filesystem::path(asset_directory()) / "fonts" / "BarlowCondensed-Bold.ttf";
        const std::shared_ptr<const std::vector<std::byte>> bytes = read_face(file);
        if (bytes) {
            std::array<gf::AppFontFace, 1> faces{};
            faces[0].bytes = bytes;
            faces[0].weight = 700;
            gf::AppFontLoad loaded = gf::load_app_font_family(title_family_name, faces);
            if (loaded.validation.status == gf::AppFontStatus::loaded) {
                family_ = std::move(loaded.family);
                state_ = CapsuleTitleFaceState::loaded;
            }
        }
    } catch (...) {
        state_ = CapsuleTitleFaceState::refused;
    }
#else
    state_ = CapsuleTitleFaceState::waiting;
#endif
    return state_;
}

void CapsuleTitleFamily::release() noexcept {
#if defined(GAMES_GUI_FORMS_APP_FONTS)
    static_cast<void>(family_.unload());
#endif
    state_ = CapsuleTitleFaceState::unloaded;
}

CapsuleTitleFaceState CapsuleTitleFamily::state() const noexcept {
#if defined(GAMES_GUI_FORMS_APP_FONTS)
    // A family quarantined since draws nothing: it reads as refused.
    if (state_ == CapsuleTitleFaceState::loaded && !gf::font_family_drawable(family_.id()))
        return CapsuleTitleFaceState::refused;
#endif
    return state_;
}

gf::FontFamilyId CapsuleTitleFamily::id() const noexcept {
#if defined(GAMES_GUI_FORMS_APP_FONTS)
    if (state() == CapsuleTitleFaceState::loaded)
        return family_.id();
#endif
    return gf::FontFamilyId{};
}

gf::FontSpec CapsuleTitleFace::font(double size) const {
    gf::FontSpec spec{};
    spec.role = gf::FontRole::control;
    spec.weight = 700;
    const gf::FontFamilyId family = family_;
    if (family != gf::FontFamilyId{}) {
        spec.size = size;
        spec.family = family;
    } else {
        // The bundled control face is wider than Barlow Condensed: a little smaller, drawn
        // tighter, so the folded capsule keeps its length.
        spec.size = size * .86;
        spec.letter_spacing = -.3;
    }
    return spec;
}

} // namespace games
