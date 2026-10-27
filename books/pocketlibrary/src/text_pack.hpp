#pragma once
// The library's text before it is coded: a book's normalized records (book.hpp) packed
// into bytes the coder finds easy. Built from the whole library once, shared by every
// book:
//
//   - every record becomes a control byte (0x01..0x1A) and its fields; a verse or a
//     chapter that only counts on from the one before is a single byte;
//   - every character the library uses often is a single byte; any other is an escape
//     (0x7F) and its UTF-8;
//   - every word of the library's dictionary is one, two or three bytes; a capitalized
//     or all-capital word is its lowercase code after a flag byte.
//
// Packing is exact: unpack(pack(text)) == text for any normalized records, including
// words and characters the tables have never seen.
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pl {

struct PackTables {
    // Code points (all above U+007F) set as single bytes, from 0x80 up.
    std::vector<char32_t> characters;
    // Words, lowercase ASCII letters, most valuable first.
    std::vector<std::string> words;
    // How the bytes from 0x80 + characters.size() are shared among the words: so many
    // one-byte codes, then two-byte prefixes, then three-byte prefixes.
    int single_words = 0;
    int double_prefixes = 0;
    int triple_prefixes = 0;
};

// Learns the tables from every book's normalized records. Deterministic.
[[nodiscard]] PackTables learn_tables(const std::vector<std::string_view>& books);

// The tables as bytes (stored once in the library file), and back. False on damage.
[[nodiscard]] std::string save_tables(const PackTables& tables);
[[nodiscard]] bool load_tables(std::string_view bytes, PackTables& tables);

class TextPacker {
  public:
    explicit TextPacker(PackTables tables);
    // Packs normalized records. False when they are not well-formed records (a field
    // holding a byte the packing reserves), leaving `out` empty.
    [[nodiscard]] bool pack(std::string_view records, std::string& out) const;
    // Unpacks bytes made by pack(). False, with `out` empty, when they are damaged.
    // `out`'s capacity is used as it is: reserving the text's length saves growing it.
    [[nodiscard]] bool unpack(std::string_view packed, std::string& out) const;

  private:
    PackTables tables_;
    // For packing: a word to its index, a character to its index, each word's code.
    std::unordered_map<std::string, int> word_index_;
    std::unordered_map<char32_t, int> character_index_;
    std::vector<std::string> codes_;
    // For unpacking: every word followed by a space, end to end, and where each starts
    // (one more entry than words); each table character as UTF-8.
    std::string spellings_;
    std::vector<std::uint32_t> spelling_at_;
    std::vector<std::string> character_spellings_;
    class Output;
    void pack_text(std::string_view text, std::string& out) const;
    [[nodiscard]] bool unpack_text(std::string_view packed, std::size_t& at, Output& out) const;
};

}  // namespace pl
