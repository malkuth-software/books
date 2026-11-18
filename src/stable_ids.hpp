#pragma once
// Stable IDs built at run time. GUI.Forms admits '.'-separated segments of lowercase
// ASCII letters and digits joined by single '-' (StableId throws std::invalid_argument
// on anything else). PlaySuite builds some IDs from names kept in other spellings:
// puzzle save slugs ("atom_probe"), setting keys ("suite.card_back"), command and game
// ids. stable_id spells such a name in the admitted form: capitals lowered, every run
// of other characters one '-' between letters or digits, '.' kept as the separator.
// Distinct names that differ only in those characters map to one ID; the callers'
// names do not.
#include "gui_forms/basic_controls.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace games {

[[nodiscard]] inline gui_forms::StableId stable_id(std::string_view name) {
    std::string spelled;
    spelled.reserve(name.size());
    for (std::size_t index = 0; index < name.size(); ++index) {
        char character = name[index];
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
        const bool letter = (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9');
        if (letter) {
            spelled.push_back(character);
        } else if (character == '.') {
            if (!spelled.empty() && spelled.back() == '-') {
                spelled.pop_back();
            }
            spelled.push_back('.');
        } else if (!spelled.empty() && spelled.back() != '-' && spelled.back() != '.') {
            spelled.push_back('-');
        }
    }
    if (!spelled.empty() && spelled.back() == '-') {
        spelled.pop_back();
    }
    gui_forms::StableId result(spelled);
    return result;
}

} // namespace games
