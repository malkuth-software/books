#include "text_pack.hpp"

#include <algorithm>
#include <cstring>
#include <map>

namespace pl {
namespace {

// The control bytes of the packed stream.
constexpr std::uint8_t paragraph = 0x01;      // P, then its style
constexpr std::uint8_t span_on = 0x02;        // Y name 1, then its name
constexpr std::uint8_t span_off = 0x03;       // Y name 0, then its name
constexpr std::uint8_t verse = 0x04;          // V, then its label, then end
constexpr std::uint8_t next_verse = 0x05;     // V counting on from the one before
constexpr std::uint8_t chapter = 0x06;        // C, then its label, then end
constexpr std::uint8_t next_chapter = 0x07;   // C counting on
constexpr std::uint8_t note = 0x08;           // N, then its label, then end
constexpr std::uint8_t note_end = 0x09;       // E
constexpr std::uint8_t note_verse = 0x0A;     // U, then its label, then end
constexpr std::uint8_t entry = 0x0B;          // K, then its key, then end
constexpr std::uint8_t heading = 0x0C;        // S, then its level byte, then its text, then end
constexpr std::uint8_t record = 0x0E;         // any other record: its fields, then end
constexpr std::uint8_t capitalized = 0x10;    // the word code after it is Capitalized
constexpr std::uint8_t all_capitals = 0x11;   // ... is ALL CAPITALS
constexpr std::uint8_t no_space = 0x12;       // the word code before it is not followed by a space
constexpr std::uint8_t field_break = 0x1E;    // between a record's fields
constexpr std::uint8_t field_end = 0x1F;      // after a label, a key, a heading or a record
constexpr std::uint8_t escape = 0x7F;         // a character outside the table, as UTF-8
constexpr std::uint8_t first_code = 0x80;

constexpr std::string_view styles[] = {"p",     "flush", "centre", "q1",    "q2",   "right", "acrostic", "list1",
                                       "list2", "blank", "title",  "verse", "note", "entry", "folio",    "quote"};
constexpr std::string_view span_names[] = {"italic", "small", "wj", "hebrew", "greek", "bold"};

constexpr std::size_t maximum_characters = 8;
constexpr int double_prefix_count = 32;

bool is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

std::size_t utf8_length(std::uint8_t lead) {
    if (lead < 0x80)
        return 1;
    if ((lead & 0xE0) == 0xC0)
        return 2;
    if ((lead & 0xF0) == 0xE0)
        return 3;
    if ((lead & 0xF8) == 0xF0)
        return 4;
    return 0;
}

char32_t decode_utf8(std::string_view text, std::size_t at, std::size_t length) {
    const std::uint8_t lead = static_cast<std::uint8_t>(text[at]);
    char32_t value = length == 1 ? lead : (length == 2 ? lead & 0x1F : (length == 3 ? lead & 0x0F : lead & 0x07));
    for (std::size_t i = 1; i < length; ++i)
        value = (value << 6) | (static_cast<std::uint8_t>(text[at + i]) & 0x3F);
    return value;
}

void append_utf8(char32_t c, std::string& out) {
    if (c < 0x80) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (c >> 18)));
        out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
}

// The fields of a record line.
std::vector<std::string_view> fields_of(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string_view::npos) {
            fields.push_back(line.substr(start));
            return fields;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
}

bool all_digits(std::string_view text) {
    if (text.empty() || text.size() > 6 || text[0] == '0')
        return false;
    for (const char c : text)
        if (c < '0' || c > '9')
            return false;
    return true;
}

long number_of(std::string_view text) {
    long value = 0;
    for (const char c : text)
        value = value * 10 + (c - '0');
    return value;
}

int index_of(const std::string_view* list, std::size_t count, std::string_view name) {
    for (std::size_t i = 0; i < count; ++i)
        if (list[i] == name)
            return static_cast<int>(i);
    return -1;
}

// A field may hold anything but the packing's own control bytes and line breaks.
bool plain_field(std::string_view text) {
    for (const char c : text) {
        const std::uint8_t b = static_cast<std::uint8_t>(c);
        if (b == field_break || b == field_end || b == '\n' || b == '\t')
            return false;
    }
    return true;
}

struct WordCount {
    std::string word;
    long count = 0;
};

bool more_valuable(const WordCount& a, const WordCount& b) {
    // A word is worth about what it saves: its length and the space after it, less a
    // code's length (one and a half bytes on average), times its count. Ties go by the
    // word, so the tables are the same on every machine.
    const long worth_a = a.count * (static_cast<long>(a.word.size()) * 2 - 1);
    const long worth_b = b.count * (static_cast<long>(b.word.size()) * 2 - 1);
    if (worth_a != worth_b)
        return worth_a > worth_b;
    return a.word < b.word;
}

}  // namespace

PackTables learn_tables(const std::vector<std::string_view>& books) {
    std::map<char32_t, long> characters;
    std::map<std::string, long> words;
    for (const std::string_view book : books) {
        std::size_t start = 0;
        while (start < book.size()) {
            std::size_t end = book.find('\n', start);
            if (end == std::string_view::npos)
                end = book.size();
            const std::string_view line = book.substr(start, end - start);
            start = end + 1;
            if (line.size() < 2 || line[0] != 'T' || line[1] != '\t')
                continue;
            const std::string_view text = line.substr(2);
            std::size_t i = 0;
            while (i < text.size()) {
                const std::uint8_t b = static_cast<std::uint8_t>(text[i]);
                if (is_letter(text[i])) {
                    std::size_t j = i;
                    while (j < text.size() && is_letter(text[j]))
                        ++j;
                    const std::string_view run = text.substr(i, j - i);
                    bool lower = true, upper = true;
                    for (std::size_t k = 0; k < run.size(); ++k) {
                        lower = lower && run[k] >= 'a';
                        upper = upper && run[k] <= 'Z';
                    }
                    bool capital = run[0] <= 'Z';
                    for (std::size_t k = 1; k < run.size(); ++k)
                        capital = capital && run[k] >= 'a';
                    if (lower || upper || capital) {
                        std::string folded(run);
                        for (char& c : folded)
                            if (c <= 'Z')
                                c = static_cast<char>(c + 32);
                        ++words[folded];
                    }
                    i = j;
                } else if (b >= 0x80) {
                    const std::size_t length = utf8_length(b);
                    if (length == 0 || i + length > text.size()) {
                        ++i;
                        continue;
                    }
                    ++characters[decode_utf8(text, i, length)];
                    i += length;
                } else {
                    ++i;
                }
            }
        }
    }
    PackTables tables;
    std::vector<std::pair<long, char32_t>> by_count;
    for (const std::pair<const char32_t, long>& entry : characters)
        by_count.push_back({entry.second, entry.first});
    std::sort(by_count.begin(), by_count.end(), std::greater<std::pair<long, char32_t>>());
    for (std::size_t i = 0; i < by_count.size() && tables.characters.size() < maximum_characters; ++i)
        if (by_count[i].first >= 1000)
            tables.characters.push_back(by_count[i].second);
    std::vector<WordCount> ranked;
    for (const std::pair<const std::string, long>& entry : words)
        if (entry.second >= 3)
            ranked.push_back(WordCount{entry.first, entry.second});
    std::sort(ranked.begin(), ranked.end(), more_valuable);
    const int free_bytes = 128 - static_cast<int>(tables.characters.size());
    tables.double_prefixes = double_prefix_count;
    tables.triple_prefixes = 1;
    tables.single_words = free_bytes - tables.double_prefixes - tables.triple_prefixes;
    const std::size_t capacity = static_cast<std::size_t>(tables.single_words) +
                                 static_cast<std::size_t>(tables.double_prefixes) * 256 +
                                 static_cast<std::size_t>(tables.triple_prefixes) * 65536;
    for (std::size_t i = 0; i < ranked.size() && i < capacity; ++i)
        tables.words.push_back(ranked[i].word);
    return tables;
}

std::string save_tables(const PackTables& tables) {
    std::string out = "pocket-library-pack 1\n";
    out += std::to_string(tables.characters.size()) + " " + std::to_string(tables.words.size()) + " " +
           std::to_string(tables.single_words) + " " + std::to_string(tables.double_prefixes) + " " +
           std::to_string(tables.triple_prefixes) + "\n";
    for (const char32_t c : tables.characters) {
        append_utf8(c, out);
        out.push_back('\n');
    }
    for (const std::string& word : tables.words) {
        out += word;
        out.push_back('\n');
    }
    return out;
}

bool load_tables(std::string_view bytes, PackTables& tables) {
    const std::string_view header = "pocket-library-pack 1\n";
    if (bytes.substr(0, header.size()) != header)
        return false;
    std::size_t at = header.size();
    const std::size_t line_end = bytes.find('\n', at);
    if (line_end == std::string_view::npos)
        return false;
    long numbers[5] = {0, 0, 0, 0, 0};
    int which = 0;
    for (std::size_t i = at; i < line_end && which < 5; ++i) {
        if (bytes[i] == ' ')
            ++which;
        else if (bytes[i] >= '0' && bytes[i] <= '9')
            numbers[which] = numbers[which] * 10 + (bytes[i] - '0');
        else
            return false;
    }
    at = line_end + 1;
    PackTables made;
    made.single_words = static_cast<int>(numbers[2]);
    made.double_prefixes = static_cast<int>(numbers[3]);
    made.triple_prefixes = static_cast<int>(numbers[4]);
    for (long n = 0; n < numbers[0] + numbers[1]; ++n) {
        const std::size_t end = bytes.find('\n', at);
        if (end == std::string_view::npos)
            return false;
        const std::string_view line = bytes.substr(at, end - at);
        at = end + 1;
        if (n < numbers[0]) {
            const std::size_t length = line.empty() ? 0 : utf8_length(static_cast<std::uint8_t>(line[0]));
            if (length == 0 || length != line.size())
                return false;
            made.characters.push_back(decode_utf8(line, 0, length));
        } else {
            made.words.push_back(std::string(line));
        }
    }
    const int used = static_cast<int>(made.characters.size()) + made.single_words + made.double_prefixes + made.triple_prefixes;
    if (used > 128 || made.single_words < 0 || made.double_prefixes < 0 || made.triple_prefixes < 0)
        return false;
    tables = std::move(made);
    return true;
}

TextPacker::TextPacker(PackTables chosen) : tables_(std::move(chosen)) {
    const PackTables& tables = tables_;
    for (const char32_t c : tables.characters) {
        std::string spelling;
        append_utf8(c, spelling);
        character_spellings_.push_back(spelling);
    }
    spelling_at_.reserve(tables.words.size() + 1);
    for (const std::string& word : tables.words) {
        spelling_at_.push_back(static_cast<std::uint32_t>(spellings_.size()));
        spellings_ += word;
        spellings_.push_back(' ');
    }
    spelling_at_.push_back(static_cast<std::uint32_t>(spellings_.size()));
    const int word_base = first_code + static_cast<int>(tables.characters.size());
    for (std::size_t i = 0; i < tables.characters.size(); ++i)
        character_index_[tables.characters[i]] = static_cast<int>(i);
    codes_.reserve(tables.words.size());
    for (std::size_t i = 0; i < tables.words.size(); ++i) {
        word_index_[tables.words[i]] = static_cast<int>(i);
        std::string code;
        const long index = static_cast<long>(i);
        const long singles = tables.single_words;
        const long doubles = static_cast<long>(tables.double_prefixes) * 256;
        if (index < singles) {
            code.push_back(static_cast<char>(word_base + index));
        } else if (index < singles + doubles) {
            const long k = index - singles;
            code.push_back(static_cast<char>(word_base + tables.single_words + k / 256));
            code.push_back(static_cast<char>(k % 256));
        } else {
            const long k = index - singles - doubles;
            code.push_back(static_cast<char>(word_base + tables.single_words + tables.double_prefixes + k / 65536));
            code.push_back(static_cast<char>((k / 256) % 256));
            code.push_back(static_cast<char>(k % 256));
        }
        codes_.push_back(code);
    }
}

void TextPacker::pack_text(std::string_view text, std::string& out) const {
    std::size_t i = 0;
    std::string folded;
    while (i < text.size()) {
        const char c = text[i];
        const std::uint8_t b = static_cast<std::uint8_t>(c);
        if (is_letter(c)) {
            std::size_t j = i;
            while (j < text.size() && is_letter(text[j]))
                ++j;
            const std::string_view run = text.substr(i, j - i);
            bool lower = true, upper = true, capital = run[0] <= 'Z';
            for (std::size_t k = 0; k < run.size(); ++k) {
                lower = lower && run[k] >= 'a';
                upper = upper && run[k] <= 'Z';
                if (k > 0)
                    capital = capital && run[k] >= 'a';
            }
            folded.assign(run);
            for (char& f : folded)
                if (f <= 'Z')
                    f = static_cast<char>(f + 32);
            const std::unordered_map<std::string, int>::const_iterator found = word_index_.find(folded);
            if (found != word_index_.end() && (lower || upper || capital)) {
                if (upper)
                    out.push_back(static_cast<char>(all_capitals));
                else if (capital)
                    out.push_back(static_cast<char>(capitalized));
                out += codes_[static_cast<std::size_t>((*found).second)];
                // A word code carries the space after it; a word without one is marked.
                if (j < text.size() && text[j] == ' ')
                    ++j;
                else
                    out.push_back(static_cast<char>(no_space));
            } else {
                out.append(run);
            }
            i = j;
        } else if (b < 0x80) {
            out.push_back(c);
            ++i;
        } else {
            const std::size_t length = utf8_length(b);
            const std::size_t taken = length == 0 || i + length > text.size() ? 1 : length;
            const char32_t value = taken == length ? decode_utf8(text, i, length) : 0;
            const std::unordered_map<char32_t, int>::const_iterator found = character_index_.find(value);
            if (taken == length && found != character_index_.end()) {
                out.push_back(static_cast<char>(first_code + (*found).second));
            } else {
                out.push_back(static_cast<char>(escape));
                out.append(text.substr(i, taken));
            }
            i += taken;
        }
    }
}

bool TextPacker::pack(std::string_view records, std::string& out) const {
    out.clear();
    std::string last_verse, last_chapter;
    std::size_t start = 0;
    while (start < records.size()) {
        std::size_t end = records.find('\n', start);
        if (end == std::string_view::npos)
            end = records.size();
        const std::string_view line = records.substr(start, end - start);
        start = end + 1;
        if (line.empty())
            continue;
        const std::vector<std::string_view> fields = fields_of(line);
        const std::string_view kind = fields[0];
        const std::size_t count = fields.size();
        if (kind == "T" && count == 2 && !fields[1].empty()) {
            for (const char c : fields[1])
                if (static_cast<std::uint8_t>(c) < 0x20) {
                    out.clear();
                    return false;
                }
            pack_text(fields[1], out);
            continue;
        }
        for (std::size_t f = 1; f < count; ++f)
            if (!plain_field(fields[f])) {
                out.clear();
                return false;
            }
        if (kind == "P" && count == 2 && index_of(styles, std::size(styles), fields[1]) >= 0) {
            out.push_back(static_cast<char>(paragraph));
            out.push_back(static_cast<char>(0x20 + index_of(styles, std::size(styles), fields[1])));
        } else if (kind == "Y" && count == 3 && index_of(span_names, std::size(span_names), fields[1]) >= 0 &&
                   (fields[2] == "1" || fields[2] == "0")) {
            out.push_back(static_cast<char>(fields[2] == "1" ? span_on : span_off));
            out.push_back(static_cast<char>(0x20 + index_of(span_names, std::size(span_names), fields[1])));
        } else if (kind == "V" && count == 2) {
            if (all_digits(fields[1]) && all_digits(last_verse) && number_of(fields[1]) == number_of(last_verse) + 1) {
                out.push_back(static_cast<char>(next_verse));
            } else {
                out.push_back(static_cast<char>(verse));
                out.append(fields[1]);
                out.push_back(static_cast<char>(field_end));
            }
            last_verse = std::string(fields[1]);
        } else if (kind == "C" && count == 2) {
            if (all_digits(fields[1]) && all_digits(last_chapter) &&
                number_of(fields[1]) == number_of(last_chapter) + 1) {
                out.push_back(static_cast<char>(next_chapter));
            } else {
                out.push_back(static_cast<char>(chapter));
                out.append(fields[1]);
                out.push_back(static_cast<char>(field_end));
            }
            last_chapter = std::string(fields[1]);
            last_verse.clear();
        } else if ((kind == "N" || kind == "U" || kind == "K") && count == 2) {
            out.push_back(static_cast<char>(kind == "N" ? note : (kind == "U" ? note_verse : entry)));
            out.append(fields[1]);
            out.push_back(static_cast<char>(field_end));
        } else if (kind == "E" && count == 1) {
            out.push_back(static_cast<char>(note_end));
        } else if (kind == "S" && count == 3 && fields[1].size() == 1) {
            out.push_back(static_cast<char>(heading));
            out.append(fields[1]);
            out.append(fields[2]);
            out.push_back(static_cast<char>(field_end));
        } else {
            out.push_back(static_cast<char>(record));
            for (std::size_t f = 0; f < count; ++f) {
                if (f > 0)
                    out.push_back(static_cast<char>(field_break));
                out.append(fields[f]);
            }
            out.push_back(static_cast<char>(field_end));
        }
    }
    return true;
}

// The unpacked text, written through a cursor into a string grown ahead of it, and cut
// to length at the end.
class TextPacker::Output {
  public:
    explicit Output(std::string& text) : text_(text) {
        text_.resize(std::max<std::size_t>(text_.capacity(), 4096));
    }
    // Room for `count` bytes at the cursor.
    char* room(std::size_t count) {
        if (used_ + count > text_.size())
            text_.resize(std::max(text_.size() * 2, used_ + count));
        return text_.data() + used_;
    }
    void advance(std::size_t count) {
        used_ += count;
    }
    void put(char c) {
        *room(1) = c;
        ++used_;
    }
    void put(std::string_view text) {
        std::memcpy(room(text.size()), text.data(), text.size());
        used_ += text.size();
    }
    void finish() {
        text_.resize(used_);
    }

  private:
    std::string& text_;
    std::size_t used_ = 0;
};

bool TextPacker::unpack_text(std::string_view packed, std::size_t& at, Output& out) const {
    const PackTables& tables = tables_;
    const int characters = static_cast<int>(tables.characters.size());
    const int word_base = first_code + characters;
    const int double_base = word_base + tables.single_words;
    const int triple_base = double_base + tables.double_prefixes;
    const int code_end = triple_base + tables.triple_prefixes;
    const long word_count = static_cast<long>(tables.words.size());
    const std::uint8_t* const bytes = reinterpret_cast<const std::uint8_t*>(packed.data());
    const std::size_t size = packed.size();
    out.put("T\t");
    while (at < size) {
        const std::uint8_t b = bytes[at];
        if (b >= 0x20 && b < escape) {
            out.put(static_cast<char>(b));
            ++at;
            continue;
        }
        if (b < 0x20 && b != capitalized && b != all_capitals)
            break;
        if (b == escape) {
            if (at + 1 >= size)
                return false;
            const std::size_t length = utf8_length(bytes[at + 1]);
            const std::size_t taken = length == 0 ? 1 : length;
            if (at + 1 + taken > size)
                return false;
            out.put(packed.substr(at + 1, taken));
            at += 1 + taken;
            continue;
        }
        int case_flag = 0;
        int code = b;
        if (b == capitalized || b == all_capitals) {
            case_flag = b == capitalized ? 1 : 2;
            ++at;
            if (at >= size)
                return false;
            code = bytes[at];
        }
        if (code < word_base) {
            if (case_flag != 0 || code < first_code)
                return false;
            out.put(character_spellings_[static_cast<std::size_t>(code - first_code)]);
            ++at;
            continue;
        }
        long index = 0;
        if (code < double_base) {
            index = code - word_base;
            at += 1;
        } else if (code < triple_base) {
            if (at + 1 >= size)
                return false;
            index = tables.single_words + static_cast<long>(code - double_base) * 256 + bytes[at + 1];
            at += 2;
        } else if (code < code_end) {
            if (at + 2 >= size)
                return false;
            index = tables.single_words + static_cast<long>(tables.double_prefixes) * 256 +
                    static_cast<long>(code - triple_base) * 65536 + static_cast<long>(bytes[at + 1]) * 256 +
                    bytes[at + 2];
            at += 3;
        } else {
            return false;
        }
        if (index >= word_count)
            return false;
        // The word and the space after it, the space taken back when a mark follows.
        const std::uint32_t from = spelling_at_[static_cast<std::size_t>(index)];
        const std::size_t length = spelling_at_[static_cast<std::size_t>(index) + 1] - from;
        char* const written = out.room(length);
        std::memcpy(written, spellings_.data() + from, length);
        if (case_flag == 1) {
            written[0] = static_cast<char>(written[0] - 32);
        } else if (case_flag == 2) {
            for (std::size_t k = 0; k + 1 < length; ++k)
                written[k] = static_cast<char>(written[k] - 32);
        }
        if (at < size && bytes[at] == no_space) {
            ++at;
            out.advance(length - 1);
        } else {
            out.advance(length);
        }
    }
    out.put('\n');
    return true;
}

bool TextPacker::unpack(std::string_view packed, std::string& text) const {
    text.clear();
    Output out(text);
    std::string last_verse, last_chapter;
    std::size_t at = 0;
    while (at < packed.size()) {
        const std::uint8_t b = static_cast<std::uint8_t>(packed[at]);
        if (b >= 0x20 || b == capitalized || b == all_capitals) {
            if (!unpack_text(packed, at, out)) {
                text.clear();
                return false;
            }
            continue;
        }
        ++at;
        if (b == paragraph || b == span_on || b == span_off) {
            if (at >= packed.size()) {
                text.clear();
                return false;
            }
            const int index = static_cast<std::uint8_t>(packed[at]) - 0x20;
            ++at;
            if (b == paragraph) {
                if (index < 0 || index >= static_cast<int>(std::size(styles))) {
                    text.clear();
                    return false;
                }
                out.put("P\t");
                out.put(styles[static_cast<std::size_t>(index)]);
            } else {
                if (index < 0 || index >= static_cast<int>(std::size(span_names))) {
                    text.clear();
                    return false;
                }
                out.put("Y\t");
                out.put(span_names[static_cast<std::size_t>(index)]);
                out.put(b == span_on ? "\t1" : "\t0");
            }
            out.put('\n');
        } else if (b == next_verse) {
            last_verse = std::to_string(number_of(last_verse) + 1);
            out.put("V\t");
            out.put(last_verse);
            out.put('\n');
        } else if (b == next_chapter) {
            last_chapter = std::to_string(number_of(last_chapter) + 1);
            last_verse.clear();
            out.put("C\t");
            out.put(last_chapter);
            out.put('\n');
        } else if (b == note_end) {
            out.put("E\n");
        } else {
            // A label, key or record runs to the end byte.
            const std::size_t end = packed.find(static_cast<char>(field_end), at);
            if (end == std::string_view::npos) {
                text.clear();
                return false;
            }
            const std::string_view body = packed.substr(at, end - at);
            at = end + 1;
            if (b == verse) {
                last_verse = std::string(body);
                out.put("V\t");
                out.put(body);
                out.put('\n');
            } else if (b == chapter) {
                last_chapter = std::string(body);
                last_verse.clear();
                out.put("C\t");
                out.put(body);
                out.put('\n');
            } else if (b == note || b == note_verse || b == entry) {
                out.put(b == note ? "N\t" : (b == note_verse ? "U\t" : "K\t"));
                out.put(body);
                out.put('\n');
            } else if (b == heading) {
                if (body.empty()) {
                    text.clear();
                    return false;
                }
                out.put("S\t");
                out.put(body[0]);
                out.put('\t');
                out.put(body.substr(1));
                out.put('\n');
            } else if (b == record) {
                char* const written = out.room(body.size() + 1);
                for (std::size_t i = 0; i < body.size(); ++i)
                    written[i] = static_cast<std::uint8_t>(body[i]) == field_break ? '\t' : body[i];
                written[body.size()] = '\n';
                out.advance(body.size() + 1);
            } else {
                text.clear();
                return false;
            }
        }
    }
    out.finish();
    return true;
}

}  // namespace pl
