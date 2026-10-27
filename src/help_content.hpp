#pragma once
#include "suite.hpp"
#include <string>
#include <string_view>

namespace games {
inline constexpr std::string_view dedication =
    "Dedicated to the Holy One, Blessed be He, and to Christ, our King in Heaven.";
inline constexpr std::string_view creators =
    "Crafted by Anthropic Claude General Reasoning Agents and OpenAI Codex General Reasoning "
    "Agents.";
inline constexpr std::string_view contact = "joshuah.rainstar@gmail.com";
inline constexpr std::string_view website = "paymenottowork.com";
inline constexpr std::string_view help_welcome =
    "The bookcase holds every book, shelf by shelf. Scroll it with the wheel or the arrows "
    "beside it, or type in the search field to find a book by its title or its author. Click "
    "a book to open it where you left it. The arrow at the top returns from a book to the "
    "bookcase. The command bar opens to show the reader's actions and the sound and motion "
    "switches. Your places in every book are kept on your computer.\n\n"
    "H or the ? in the top-right corner opens this document. H or Escape closes it. "
    "F1 also opens help. Tab visits controls; Enter or Space activates a focused control. "
    "Scroll this document with the wheel, the scroll bar or Page Up / Page Down. "
    "Open a heading to read it.";
std::string help_about();
std::string_view help_text(Entry entry);
} // namespace games
