#include "help_content.hpp"
#include "game_module.hpp"
namespace games {
std::string_view help_text(Entry entry) {
    return valid_entry(entry) ? game_descriptor(entry).help : "";
}
std::string help_about() {
    return "The Pocket Library is a reading room built with GUI.Forms: every book is set and "
           "printed on its page as you read it, from texts in the public domain or under "
           "Creative Commons licences, each named on its book's About card. It is free and is "
           "never sold. There are no accounts, advertisements or downloads; the whole library "
           "comes with the application and your places stay on your computer.\n\n"
           "Its companion, PlaySuite, holds card games, puzzles and living scenes: "
           "github.com/falseywinchnet/games.\n\n"
           "When reporting a problem, include the book, the Pocket Library's version, the "
           "operating system and the steps that led to it. A screenshot helps reproduce it.";
}
} // namespace games
