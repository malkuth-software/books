#pragma once
// Saves and assets for a game without the game opening files itself. A
// contributed game uses only these: tools/check_contribution.py refuses games
// that reach the file system, the environment or threads directly unless
// PlaySuite approves it (tools/game_approvals.json).
//
// `game` is the game's id from GAME.json. Names and asset paths are checked:
// a name is 1 to 64 of a-z, 0-9, '_', '-' and '.', not starting with '.'; an
// asset path is such names joined by '/'. Nothing outside the game's save folder
// or the prepared assets can be named.
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace games {

inline constexpr std::size_t game_data_limit = 1024 * 1024;

// Replaces the entry atomically: on failure the previous entry is unchanged.
// False when the name is invalid, the data exceeds game_data_limit or the write fails.
bool save_game_data(std::string_view game, std::string_view name, std::string_view bytes);
// The entry, or nothing when it is missing, invalid or larger than the limit.
[[nodiscard]] std::optional<std::string> load_game_data(std::string_view game, std::string_view name);
bool erase_game_data(std::string_view game, std::string_view name);
// A file from the prepared assets ("audio/tg_press.ogg"), or nothing.
[[nodiscard]] std::optional<std::string> load_game_asset(std::string_view relative);

}  // namespace games
