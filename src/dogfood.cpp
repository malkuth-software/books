#include "dogfood.hpp"
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace games::dogfood {
namespace {
int integer(std::string_view value) {
    int result{};
    const std::from_chars_result parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::runtime_error("Expected a whole number: " + std::string(value));
    return result;
}
std::string trimmed(std::string value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}
void set_state_path(const std::filesystem::path& path) {
#ifdef _WIN32
    if (_wputenv_s(L"GAMES_STATE_DIR", path.c_str()) != 0)
#else
    if (setenv("GAMES_STATE_DIR", path.c_str(), 1) != 0)
#endif
        throw std::runtime_error("Cannot isolate development saves");
}
}
const char* usage() {
    return "pocketlibrary [--game SLUG] [--standalone] [--dev] [--smoke-test]\n"
           "          [--script FILE] | --list-games | --profile-idle ID | --help\n"
           "--standalone requires --game. --script requires --dev and ends with quit.\n"
           "Script lines: milliseconds quit|help|shelf|open SLUG|command ID|key NAME|\n"
           "              click X Y|hover X Y|down X Y|up X Y|resize WIDTH HEIGHT|capture FILE.ppm|action GAME_SPECIFIC_CODE\n"
           "Scripts are chronological, at most five minutes. --dev uses temporary\n"
           "saves unless GAMES_STATE_DIR explicitly selects a development directory.\n";
}
Options parse_options(int argc, const char* const* argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help" || argument == "-h") options.help = true;
        else if (argument == "--list-games") options.list = true;
        else if (argument == "--standalone") options.standalone = true;
        else if (argument == "--dev") options.dev = true;
        else if (argument == "--smoke-test") options.smoke = true;
        else if (argument == "--game" || argument == "--script" || argument == "--profile-idle") {
            if (index + 1 == argc || std::string_view(argv[index + 1]).starts_with("--"))
                throw std::runtime_error("Missing value after " + std::string(argument));
            const std::string value(argv[++index]);
            if (value.empty()) throw std::runtime_error("Empty option value");
            if (argument == "--game") options.game = value;
            else if (argument == "--script") options.script = value;
            else { options.profile = true; options.profile_entry = integer(value); }
        } else throw std::runtime_error("Unknown option: " + std::string(argument));
    }
    if (options.standalone && options.game.empty())
        throw std::runtime_error("--standalone requires --game SLUG");
    if (!options.script.empty() && !options.dev)
        throw std::runtime_error("--script requires --dev so automation cannot change production saves");
    if (options.profile && (options.standalone || !options.game.empty() || options.smoke || !options.script.empty()))
        throw std::runtime_error("--profile-idle cannot be combined with another launch mode");
    if (options.smoke && !options.script.empty())
        throw std::runtime_error("Choose --smoke-test or --script; each owns its close deadline");
    if (options.profile_entry < -1)
        throw std::runtime_error("Profile entry must be -1 for the shelf or a permanent game id");
    return options;
}
std::vector<Action> read_script(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("Cannot open native input script: " + path.string());
    std::vector<Action> actions;
    std::string line;
    int previous{};
    int line_number{};
    while (std::getline(input, line)) {
        ++line_number;
        if (line.size() > 1024 || line_number > 20000)
            throw std::runtime_error("Native input script exceeds its size limit");
        line = trimmed(line);
        if (line.empty() || line.front() == '#') continue;
        if (actions.size() >= 10000)
            throw std::runtime_error("Native input script has too many actions");
        if (!actions.empty() && actions.back().verb == "quit")
            throw std::runtime_error("quit must be the script's final action");
        std::istringstream fields(line);
        std::string timestamp;
        Action action;
        if (!(fields >> timestamp >> action.verb))
            throw std::runtime_error("Expected milliseconds and verb on script line " + std::to_string(line_number));
        action.milliseconds = integer(timestamp);
        if (action.milliseconds < previous || action.milliseconds > 300000 || action.milliseconds < 0)
            throw std::runtime_error("Script times must be chronological and in 0..300000 milliseconds");
        previous = action.milliseconds;
        std::getline(fields, action.argument);
        action.argument = trimmed(action.argument);
        if (action.verb == "quit" || action.verb == "help" || action.verb == "shelf") {
            if (!action.argument.empty()) throw std::runtime_error(action.verb + " takes no arguments");
        } else if (action.verb == "open" || action.verb == "command" || action.verb == "key" || action.verb == "action" || action.verb == "capture") {
            if (action.argument.empty()) throw std::runtime_error(action.verb + " needs an argument");
            if (action.verb != "action" && action.verb != "capture" && action.argument.find_first_of(" \t") != std::string::npos)
                throw std::runtime_error(action.verb + " expects one name");
        } else if (action.verb == "click" || action.verb == "hover" || action.verb == "down" || action.verb == "up" ||
                   action.verb == "resize") {
            std::istringstream coordinates(action.argument);
            std::string extra;
            if (!(coordinates >> action.x >> action.y) || (coordinates >> extra) ||
                !std::isfinite(action.x) || !std::isfinite(action.y))
                throw std::runtime_error(action.verb + " expects two finite numbers");
            const double minimum = action.verb == "resize" ? 100 : 0;
            if (action.x < minimum || action.y < minimum || action.x > 8192 || action.y > 8192)
                throw std::runtime_error(action.verb + " coordinates are outside the supported range");
        } else throw std::runtime_error("Unknown script verb: " + action.verb);
        actions.push_back(std::move(action));
    }
    if (input.bad()) throw std::runtime_error("Failed reading native input script");
    if (actions.empty() || actions.back().verb != "quit")
        throw std::runtime_error("Native input script must end with quit");
    return actions;
}
DevelopmentState::DevelopmentState(bool enabled) {
    if (!enabled) return;
#ifdef _WIN32
    const wchar_t* supplied = _wgetenv(L"GAMES_STATE_DIR");
#else
    const char* supplied = std::getenv("GAMES_STATE_DIR");
#endif
    if (supplied != nullptr && supplied[0] != 0) return;
    const std::chrono::steady_clock::duration ticks = std::chrono::steady_clock::now().time_since_epoch();
    const std::string stamp = std::to_string(ticks.count());
    for (int attempt = 0; attempt < 100; ++attempt) {
        const std::filesystem::path candidate = std::filesystem::temp_directory_path() /
            ("playsuite-dev-" + stamp + "-" + std::to_string(attempt));
        if (std::filesystem::create_directory(candidate)) {
            path_ = candidate;
            try { set_state_path(path_); }
            catch (...) {
                std::error_code ignored;
                std::filesystem::remove_all(path_, ignored);
                throw;
            }
            return;
        }
    }
    throw std::runtime_error("Cannot create a unique development save directory");
}
DevelopmentState::~DevelopmentState() {
    if (path_.empty()) return;
#ifdef _WIN32
    static_cast<void>(_wputenv_s(L"GAMES_STATE_DIR", L""));
#else
    static_cast<void>(unsetenv("GAMES_STATE_DIR"));
#endif
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
}
} // namespace games::dogfood
