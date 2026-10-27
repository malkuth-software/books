#include "runtime_paths.hpp"
#include <cstdlib>
#include <stdexcept>
#include <vector>
namespace games {
namespace {
std::filesystem::path assets{};
std::filesystem::path environment_path(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::filesystem::path(value) : std::filesystem::path{};
}
}
void initialize_assets(const char* executable) {
    std::filesystem::path override_path = environment_path("GAMES_ASSET_DIR");
    if (!override_path.empty()) {
        assets = std::filesystem::absolute(override_path);
        return;
    }
    std::filesystem::path program(executable);
    if (!program.has_parent_path() && !std::filesystem::exists(program)) {
        const char* raw = std::getenv("PATH");
        std::string search = raw != nullptr ? raw : "";
#ifdef _WIN32
        constexpr char separator = ';';
        if (!program.has_extension()) { program += ".exe"; }
#else
        constexpr char separator = ':';
#endif
        std::size_t start = 0;
        while (start <= search.size()) {
            std::size_t end = search.find(separator, start);
            if (end == std::string::npos) { end = search.size(); }
            std::filesystem::path candidate = std::filesystem::path(search.substr(start, end - start)) / program;
            if (std::filesystem::is_regular_file(candidate)) { program = candidate; break; }
            if (end == search.size()) { break; }
            start = end + 1;
        }
    }
    std::filesystem::path directory = std::filesystem::absolute(program).parent_path();
    const std::filesystem::path candidates[] = {directory / "assets", directory.parent_path() / "Resources/assets"};
    for (const std::filesystem::path& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate / "pocketlibrary/books/parts.txt")) {
            assets = candidate;
            return;
        }
    }
    throw std::runtime_error("The library's assets are missing beside the executable.");
}
std::string asset_directory() {
    if (!assets.empty()) { return assets.string(); }
    std::filesystem::path override_path = environment_path("GAMES_ASSET_DIR");
    if (!override_path.empty()) { return override_path.string(); }
    throw std::runtime_error("The library's assets were not initialized.");
}
namespace {
// The per-user folder named `windows` under %APPDATA%, `mac` under Application Support,
// or `xdg` under the XDG state folder.
std::filesystem::path user_folder(const wchar_t* windows, const char* mac, const char* xdg) {
#ifdef _WIN32
    static_cast<void>(mac);
    static_cast<void>(xdg);
    const wchar_t* roaming = _wgetenv(L"APPDATA");
    if (roaming != nullptr && roaming[0] != 0) { return std::filesystem::path(roaming) / windows; }
#else
    static_cast<void>(windows);
    std::filesystem::path home = environment_path("HOME");
#ifdef __APPLE__
    static_cast<void>(xdg);
    if (!home.empty()) { return home / "Library/Application Support" / mac; }
#else
    static_cast<void>(mac);
    std::filesystem::path state = environment_path("XDG_STATE_HOME");
    if (!state.empty() && state.is_absolute()) { return state / xdg; }
    if (!home.empty()) { return home / ".local/state" / xdg; }
#endif
#endif
    return {};
}
}
std::filesystem::path state_directory() {
#ifdef _WIN32
    const wchar_t* isolated = _wgetenv(L"GAMES_STATE_DIR");
    if (isolated != nullptr && isolated[0] != 0) { return std::filesystem::path(isolated); }
#else
    std::filesystem::path isolated = environment_path("GAMES_STATE_DIR");
    if (!isolated.empty()) { return isolated; }
#endif
    const std::filesystem::path folder =
        user_folder(L"Rainstar/Pocket Library", "Rainstar/Pocket Library", "rainstar/pocketlibrary");
    if (folder.empty()) { throw std::runtime_error("No per-user save directory is available."); }
    return folder;
}
void carry_over_playsuite_places() {
    const std::filesystem::path from =
        user_folder(L"Rainstar/Games", "Rainstar/Games", "rainstar/games") / "game-data/pocketlibrary";
    const std::filesystem::path to = state_directory() / "game-data/pocketlibrary";
    std::error_code error;
    if (from.empty() || std::filesystem::exists(to, error) || !std::filesystem::is_directory(from, error)) { return; }
    std::filesystem::create_directories(to.parent_path(), error);
    std::filesystem::copy(from, to, std::filesystem::copy_options::recursive, error);
}
}
