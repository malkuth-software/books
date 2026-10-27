#include "game_data.hpp"
#include "runtime_paths.hpp"
#include <filesystem>
#include <fstream>
#include <system_error>

namespace games {
namespace {
bool valid_name(std::string_view name) {
    if (name.empty() || name.size() > 64 || name.front() == '.') {
        return false;
    }
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) {
            return false;
        }
    }
    return true;
}
bool valid_path(std::string_view relative) {
    std::size_t start = 0;
    while (start <= relative.size()) {
        std::size_t end = relative.find('/', start);
        if (end == std::string_view::npos) {
            end = relative.size();
        }
        if (!valid_name(relative.substr(start, end - start))) {
            return false;
        }
        start = end + 1;
    }
    return true;
}
std::filesystem::path entry(std::string_view game, std::string_view name) {
    return state_directory() / "game-data" / std::string(game) / std::string(name);
}
std::optional<std::string> read_bounded(const std::filesystem::path& path, std::size_t limit) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    // The buffer is the file's size (one byte more, to notice a file that grew while it
    // was read), not the limit's: a part of the library is tens of megabytes, a sound a
    // few kilobytes.
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > limit) {
        return std::nullopt;
    }
    std::string bytes(static_cast<std::size_t>(size) + 1, '\0');
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const std::size_t read = static_cast<std::size_t>(file.gcount());
    if (read > size) {
        return std::nullopt;
    }
    bytes.resize(read);
    return bytes;
}
}  // namespace

bool save_game_data(std::string_view game, std::string_view name, std::string_view bytes) {
    if (!valid_name(game) || !valid_name(name) || bytes.size() > game_data_limit) {
        return false;
    }
    const std::filesystem::path path = entry(game, name);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        file.flush();
        if (!file) {
            file.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

std::optional<std::string> load_game_data(std::string_view game, std::string_view name) {
    if (!valid_name(game) || !valid_name(name)) {
        return std::nullopt;
    }
    return read_bounded(entry(game, name), game_data_limit);
}

bool erase_game_data(std::string_view game, std::string_view name) {
    if (!valid_name(game) || !valid_name(name)) {
        return false;
    }
    std::error_code error;
    const bool removed = std::filesystem::remove(entry(game, name), error);
    return removed && !error;
}

std::optional<std::string> load_game_asset(std::string_view relative) {
    if (!valid_path(relative)) {
        return std::nullopt;
    }
    // No asset is larger than a part of the library, and a part is kept well under this
    // (a block of new books is tens of megabytes).
    constexpr std::size_t asset_limit = 128 * 1024 * 1024;
    return read_bounded(std::filesystem::path(asset_directory()) / std::string(relative), asset_limit);
}

}  // namespace games
