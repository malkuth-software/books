// PlaySuite's theme package (themes/playsuite), Pocket Library's look: it compiles over
// Windows Professional with no finding but W201 (an error refuses the theme; a warning
// drops a declaration, and the look would no longer be PlaySuite's exactly), the
// application's theme carries the package's rules beyond the base theme's, and a window
// in the light scheme shows that theme with no fallback.
#include "playsuite_theme.hpp"
#include "test_paths.hpp"
#include "gui_forms/application.hpp"
#include "gui_forms/window.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace gf = gui_forms;

static void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
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
            if (finding.code == "W201")
                continue;
            ++unexpected;
            std::cerr << finding.code << ' ' << finding.name << ' ' << finding.file << ':' << finding.line << ':'
                      << finding.column << ' ' << finding.rule << ' ' << finding.property << ' ' << finding.detail
                      << '\n';
        }
        require(static_cast<bool>(created), "the package is taken");
        require(unexpected == 0, "no finding but W201");
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
        created.application.reset();
        static_cast<void>(gf::Application::shutdown());
        std::cout << "pocket library playsuite theme: ok\n";
    } catch (const std::exception& error) {
        std::cerr << "pocket library playsuite theme: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
