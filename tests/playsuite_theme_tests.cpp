// PlaySuite's theme package (themes/playsuite), Pocket Library's look: it compiles over
// Windows Professional with no finding but W201 and W402 (an error refuses the theme; a warning
// drops a declaration, and the look would no longer be PlaySuite's exactly), the
// application's theme carries the package's rules beyond the base theme's, and a window
// in the light scheme shows that theme with no fallback; and the shell's controls carry
// the classes the sheet styles (the skin, the help link, the gloss tones).
#include "collection.hpp"
#include "help_book.hpp"
#include "playsuite_theme.hpp"
#include "suite.hpp"
#include "test_paths.hpp"
#include "gui_forms/application.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace gf = gui_forms;

static void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

static bool has_class(const gf::Control& control, const std::string& name) {
    const std::vector<std::string> names = control.classes.read();
    const bool found = std::find(names.begin(), names.end(), name) != names.end();
    return found;
}

int main() {
    try {
        const std::filesystem::path scratch = games_test::scratch_directory("pocket-theme-");
        games_test::isolate_saves(scratch);
        gf::ApplicationConfig config{};
        config.theme.base = gf::ThemeBase::windows_professional;
        config.theme.packages.push_back(games::playsuite_theme());
        gf::ApplicationCreateResult created = gf::Application::create(std::move(config));
        std::size_t unexpected = 0;
        for (const gf::ThemeCompileDiagnostic& finding : created.findings) {
            // W402 is GUI.Forms' at 96d3696: the package is light only and writes no dark
            // rule, but the dark rules of the base it is layered over (windows_professional)
            // are counted as the package's. The light scheme is forced, so nothing is dropped.
            if (finding.code == "W201" || finding.code == "W402")
                continue;
            ++unexpected;
            std::cerr << finding.code << ' ' << finding.name << ' ' << finding.file << ':' << finding.line << ':'
                      << finding.column << ' ' << finding.rule << ' ' << finding.property << ' ' << finding.detail
                      << '\n';
        }
        require(static_cast<bool>(created), "the package is taken");
        require(unexpected == 0, "no finding but W201 and W402");
        {
            std::shared_ptr<gf::Control> root = gf::make_control<gf::Control>(gf::StableId("theme.root"));
            gf::Window window(*created.application, root, gf::Size{1060, 680});
            gf::PresentationOverrides light{};
            light.colour_scheme = gf::ColourScheme::light;
            window.set_presentation_overrides(light);
            const std::shared_ptr<const gf::Theme> theme = window.theme_ptr();
            require(theme == (*(*created.application).resources()).theme_ptr(), "the window shows the application's theme");
            const gf::CompiledThemeRules& rules = (*theme).engine().rules();
            require(rules.rule_count() > rules.built_in_rule_count(), "the package's rules are there");
            require(!window.theme_fallback().has_value(), "no fallback in the light scheme");
        }
        {
            const std::shared_ptr<games::Collection> shell =
                gf::make_control<games::Collection>(gf::StableId("games.table"), false);
            require(has_class(*shell, "skin-ivory"), "the shell takes the ivory skin class");
            const std::shared_ptr<games::HelpGlyph> glyph =
                gf::make_control<games::HelpGlyph>(gf::StableId("theme.help"), "?");
            (*glyph).set_variant(games::HelpGlyphVariant::help_link);
            require(has_class(*glyph, "help-glyph") && has_class(*glyph, "help-link"), "the help link's classes");
            const std::shared_ptr<games::SuiteButton> button =
                gf::make_control<games::SuiteButton>(gf::StableId("theme.gloss"), "Play", games::GlossTone::navy);
            require(has_class(*button, "gloss") && has_class(*button, "navy"), "a gloss button's tone class");
            (*button).set_tone(games::GlossTone::smoke);
            require(has_class(*button, "smoke") && !has_class(*button, "navy"), "the tone class follows the tone");
        }
        created.application.reset();
        static_cast<void>(gf::Application::shutdown());
        std::cout << "pocket library playsuite theme: ok\n";
    } catch (const std::exception& error) {
        std::cerr << "pocket library playsuite theme: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
