#include "book.hpp"

#include <charconv>

namespace pl {
namespace {

// The fields of one record line, split at its TABs.
std::vector<std::string_view> fields_of(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string_view::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    return fields;
}

bool style_of(std::string_view name, BlockStyle& style, std::uint8_t& level) {
    level = 0;
    if (name == "p")
        style = BlockStyle::paragraph;
    else if (name == "flush")
        style = BlockStyle::flush;
    else if (name == "centre")
        style = BlockStyle::centre;
    else if (name == "q1" || name == "q2") {
        style = BlockStyle::poetry;
        level = name == "q1" ? 1 : 2;
    } else if (name == "right")
        style = BlockStyle::right;
    else if (name == "acrostic")
        style = BlockStyle::acrostic;
    else if (name == "list1" || name == "list2") {
        style = BlockStyle::list;
        level = name == "list1" ? 1 : 2;
    } else if (name == "blank")
        style = BlockStyle::blank;
    else if (name == "title")
        style = BlockStyle::title;
    else if (name == "verse")
        style = BlockStyle::verse;
    else if (name == "note")
        style = BlockStyle::note;
    else if (name == "quote")
        style = BlockStyle::quote;
    else if (name == "entry")
        style = BlockStyle::entry;
    else if (name == "folio")
        style = BlockStyle::folio;
    else
        return false;
    return true;
}

bool flag_of(std::string_view name, std::uint8_t& flag) {
    if (name == "italic")
        flag = span_italic;
    else if (name == "small")
        flag = span_small;
    else if (name == "wj")
        flag = span_red;
    else if (name == "hebrew")
        flag = span_hebrew;
    else if (name == "greek")
        flag = span_greek;
    else if (name == "bold")
        flag = span_bold;
    else
        return false;
    return true;
}

// Where the reader is in the work as it reads it.
struct Reader {
    Work& work;
    std::uint8_t flags = 0;
    bool in_note = false;

    Division& division() {
        if (work.divisions.empty()) {
            Division made;
            made.id = work.id;
            made.title = work.id;
            made.short_title = work.id;
            made.abbreviation = work.id;
            work.divisions.push_back(made);
        }
        return work.divisions.back();
    }
    Chapter& chapter() {
        Division& open = division();
        if (open.chapters.empty())
            open.chapters.push_back(Chapter{});
        return open.chapters.back();
    }
    Block& block() {
        Chapter& open = chapter();
        if (open.blocks.empty()) {
            Block made;
            made.style = BlockStyle::paragraph;
            open.blocks.push_back(made);
        }
        return open.blocks.back();
    }
    std::vector<Inline>& inlines() {
        if (in_note)
            return chapter().notes.back().inlines;
        return block().inlines;
    }
    void text(std::string_view value) {
        std::vector<Inline>& target = inlines();
        if (!target.empty() && target.back().kind == InlineKind::text && target.back().flags == flags) {
            target.back().text += value;
            return;
        }
        Inline made;
        made.text = std::string(value);
        made.flags = flags;
        target.push_back(made);
    }
};

bool read_record(Reader& reader, const std::vector<std::string_view>& fields) {
    const std::string_view kind = fields[0];
    const std::size_t count = fields.size();
    if (kind == "H" && count == 3) {
        reader.work.head.push_back({std::string(fields[1]), std::string(fields[2])});
    } else if (kind == "B" && (count == 4 || count == 5)) {
        Division made;
        made.id = std::string(fields[1]);
        made.title = std::string(fields[2]);
        made.short_title = std::string(fields[3]);
        made.abbreviation = std::string(count == 5 ? fields[4] : fields[3]);
        reader.work.divisions.push_back(made);
        reader.in_note = false;
    } else if (kind == "M" && count == 3) {
        reader.division().titles.push_back(std::string(fields[2]));
    } else if ((kind == "C" || kind == "K") && count == 2) {
        Chapter made;
        made.label = std::string(fields[1]);
        made.entry = kind == "K";
        reader.division().chapters.push_back(made);
        reader.in_note = false;
    } else if (kind == "S" && count == 3) {
        Block made;
        made.style = BlockStyle::heading;
        made.level = static_cast<std::uint8_t>(fields[1].empty() ? 0 : fields[1][0] - '0');
        Inline words;
        words.text = std::string(fields[2]);
        made.inlines.push_back(words);
        reader.chapter().blocks.push_back(made);
    } else if (kind == "R" && count == 2) {
        Block made;
        made.style = BlockStyle::parallel;
        Inline words;
        words.text = std::string(fields[1]);
        made.inlines.push_back(words);
        reader.chapter().blocks.push_back(made);
    } else if (kind == "P" && count == 2) {
        if (reader.in_note)
            return false;
        Block made;
        if (!style_of(fields[1], made.style, made.level))
            return false;
        reader.chapter().blocks.push_back(made);
    } else if (kind == "V" && count == 2) {
        Inline mark;
        mark.kind = InlineKind::verse;
        mark.text = std::string(fields[1]);
        reader.inlines().push_back(mark);
    } else if (kind == "U" && count == 2) {
        if (!reader.in_note)
            return false;
        Inline mark;
        mark.kind = InlineKind::verse;
        mark.text = std::string(fields[1]);
        reader.inlines().push_back(mark);
    } else if (kind == "T" && count == 2) {
        reader.text(fields[1]);
    } else if (kind == "Y" && count == 3) {
        std::uint8_t flag = 0;
        if (!flag_of(fields[1], flag))
            return false;
        if (fields[2] == "1")
            reader.flags = static_cast<std::uint8_t>(reader.flags | flag);
        else
            reader.flags = static_cast<std::uint8_t>(reader.flags & ~flag);
    } else if (kind == "N" && count == 2) {
        if (reader.in_note)
            return false;
        Chapter& open = reader.chapter();
        Inline call;
        call.kind = InlineKind::note_call;
        call.note = static_cast<std::uint32_t>(open.notes.size());
        reader.block().inlines.push_back(call);
        Note made;
        made.label = std::string(fields[1]);
        open.notes.push_back(made);
        reader.in_note = true;
    } else if (kind == "E" && count == 1) {
        if (!reader.in_note)
            return false;
        reader.in_note = false;
    } else if (kind == "G" && count == 3) {
        Block made;
        made.style = BlockStyle::rendering;
        Inline words;
        words.text = std::string(fields[1]);
        words.flags = span_italic;
        made.inlines.push_back(words);
        std::string_view list = fields[2];
        while (!list.empty()) {
            const std::size_t space = list.find(' ');
            const std::string_view number = list.substr(0, space);
            std::uint32_t value = 0;
            const std::from_chars_result parsed = std::from_chars(number.data(), number.data() + number.size(), value);
            if (parsed.ec != std::errc{} || value == 0)
                return false;
            made.places.push_back(value);
            if (space == std::string_view::npos)
                break;
            list.remove_prefix(space + 1);
        }
        reader.chapter().blocks.push_back(made);
    } else {
        return false;
    }
    return true;
}

}  // namespace

bool read_work(std::string_view id, std::string_view records, Work& work) {
    Work made;
    made.id = std::string(id);
    Reader reader{made};
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
        if (!read_record(reader, fields)) {
            work = Work{};
            return false;
        }
    }
    work = std::move(made);
    return true;
}

void versify(const Work& bible, Versification& out) {
    out.places.clear();
    for (std::size_t d = 0; d < bible.divisions.size(); ++d) {
        const Division& division = bible.divisions[d];
        for (std::size_t c = 0; c < division.chapters.size(); ++c) {
            for (const Block& block : division.chapters[c].blocks) {
                for (const Inline& item : block.inlines) {
                    if (item.kind != InlineKind::verse)
                        continue;
                    Versification::Place place;
                    place.division = static_cast<std::uint16_t>(d);
                    place.chapter = static_cast<std::uint16_t>(c);
                    place.verse = item.text;
                    out.places.push_back(place);
                }
            }
        }
    }
}

}  // namespace pl
