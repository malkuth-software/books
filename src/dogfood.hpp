#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace games::dogfood {
struct Action {
    int milliseconds{};
    std::string verb;
    std::string argument;
    double x{};
    double y{};
};
struct Options {
    bool help{};
    bool list{};
    bool standalone{};
    bool dev{};
    bool smoke{};
    bool profile{};
    int profile_entry{-1};
    std::string game;
    std::filesystem::path script;
};
Options parse_options(int argc, const char* const* argv);
std::vector<Action> read_script(const std::filesystem::path& path);
const char* usage();

// Owns only the newly-created temporary directory. An explicit GAMES_STATE_DIR
// is left intact, allowing deliberate development-save persistence across runs.
class DevelopmentState final {
  public:
    explicit DevelopmentState(bool enabled);
    ~DevelopmentState();
    DevelopmentState(const DevelopmentState&) = delete;
    DevelopmentState& operator=(const DevelopmentState&) = delete;
    const std::filesystem::path& temporary_path() const { return path_; }
  private:
    std::filesystem::path path_;
};
} // namespace games::dogfood
