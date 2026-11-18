#pragma once
// The capsule title's face: Barlow Condensed Bold (assets/fonts, OFL), loaded into GUI.Forms'
// text engine as an application font family (gui_forms/typography/app_fonts). The owner's
// partition (2026-11-15): an app's families draw inside composites and subsurfaces only;
// GUI.Forms admits them by the control's context (gf::admit_font), so the capsule composite's
// title label draws the family and a stock control given the same font would not.
//
// Ownership: the suite's shell (Collection) owns the family (CapsuleTitleFamily): it loads it
// when its tree is made, before the capsule, and releases it in its destructor, the normal
// teardown before the application shuts down. Nothing here is static: process termination
// releases none of it, as GUI.Forms' and STXlib's termination rule asks. A capsule borrows the
// family (CapsuleTitleFace) and never owns it.
//
// DEPENDENCY: GUI.Forms claude/app-fonts (2558e326c, being folded). The CMake option
// GAMES_GUI_FORMS_APP_FONTS (ON by default) compiles the load; a GUI.Forms without the API
// needs it OFF, and the title is then drawn in the bundled control face, which is also what
// GUI.Forms draws when the family is refused, quarantined or unloaded.
#include "gui_forms/basic_controls.hpp"

#if defined(GAMES_GUI_FORMS_APP_FONTS)
#include "gui_forms/typography/app_fonts/app_fonts.hpp"
#endif

#include <cstdint>

namespace games {
namespace gf = gui_forms;

enum class CapsuleTitleFaceState : std::uint8_t {
    // Not loaded yet, or released.
    unloaded,
    // Built without the app-font API (GAMES_GUI_FORMS_APP_FONTS off): the bundled fallback.
    waiting,
    // Loaded: the family draws.
    loaded,
    // The bytes were missing or refused (gf::AppFontStatus), or the family was quarantined or
    // unloaded since: the bundled fallback.
    refused,
};

// The family's owner. Move-only through its handle; one per shell. A family name loads once
// per process, so a second live owner's load is refused (and falls back).
class CapsuleTitleFamily final {
  public:
    CapsuleTitleFamily() = default;
    ~CapsuleTitleFamily();
    CapsuleTitleFamily(const CapsuleTitleFamily&) = delete;
    CapsuleTitleFamily& operator=(const CapsuleTitleFamily&) = delete;
    // Reads assets/fonts/BarlowCondensed-Bold.ttf from the asset directory and loads it (once;
    // later calls return the first outcome until release). Never throws.
    CapsuleTitleFaceState load();
    // Unloads the family (text already shaped with it stays drawable until its engine goes:
    // GUI.Forms defers the bytes' release). The state returns to `unloaded`.
    void release() noexcept;
    [[nodiscard]] CapsuleTitleFaceState state() const noexcept;
    // The family's number while it draws, zero otherwise.
    [[nodiscard]] gf::FontFamilyId id() const noexcept;

  private:
    CapsuleTitleFaceState state_ = CapsuleTitleFaceState::unloaded;
#if defined(GAMES_GUI_FORMS_APP_FONTS)
    gf::AppFontFamily family_{};
#endif
};

// A capsule's view of the family: its number, copied from the shell's CapsuleTitleFamily when
// the capsule is made (no reference is kept: the capsule, a child, is destroyed after the
// shell's members). A number whose family is unloaded or quarantined later draws nothing, and
// GUI.Forms' fallback (the font's role) draws the title.
class CapsuleTitleFace final {
  public:
    explicit CapsuleTitleFace(const CapsuleTitleFamily& family) : family_(family.id()) {}
    // The title's font at `size` points: the family first, the control role's bundled faces
    // after it (and alone without a family).
    [[nodiscard]] gf::FontSpec font(double size) const;

  private:
    gf::FontFamilyId family_{};
};

} // namespace games
