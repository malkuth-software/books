// Counts the library: every book's words, and its pages as the reader sets them at each
// type size (small, medium, large). Not shipped.
//
//   library_count <library.plb> <fonts dir>
//
// Words are the running text and the notes, split at spaces; verse and chapter numbers,
// titles and concordance references are not words. Pages are every page the reader
// turns, a section's last left-hand page blank included.
#include "../src/book.hpp"
#include "../src/book_layout.hpp"
#include "../src/container.hpp"
#include "../src/page/font_set.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return bytes;
}

std::size_t words_in(const std::vector<pl::Inline>& inlines) {
    std::size_t count = 0;
    bool in_word = false;
    for (const pl::Inline& part : inlines) {
        if (part.kind != pl::InlineKind::text) {
            in_word = false;
            continue;
        }
        for (const char c : part.text) {
            const bool space = c == ' ' || c == '\t' || c == '\n';
            if (!space && !in_word)
                ++count;
            in_word = !space;
        }
    }
    return count;
}

std::size_t words_in(const pl::Work& work) {
    std::size_t count = 0;
    for (const pl::Division& division : work.divisions) {
        for (const pl::Chapter& chapter : division.chapters) {
            for (const pl::Block& block : chapter.blocks)
                count += words_in(block.inlines);
            for (const pl::Note& note : chapter.notes)
                count += words_in(note.inlines);
        }
    }
    return count;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "library_count <library.plb> <fonts dir>\n");
        return 2;
    }
    pl::LibraryFile library;
    if (!library.open(read_file(argv[1])))
        return 1;
    const std::string fonts_dir = argv[2];
    const std::vector<std::uint8_t> literata = read_file(fonts_dir + "/literata.ttf");
    const std::vector<std::uint8_t> literata_italic = read_file(fonts_dir + "/literata-italic.ttf");
    const std::vector<std::uint8_t> garamond = read_file(fonts_dir + "/eb-garamond.ttf");
    const std::vector<std::uint8_t> garamond_italic = read_file(fonts_dir + "/eb-garamond-italic.ttf");
    pl::page::FontSet fonts;
    if (!fonts.load(pl::page::FaceId::literata_regular, literata) ||
        !fonts.load(pl::page::FaceId::literata_italic, literata_italic) ||
        !fonts.load(pl::page::FaceId::eb_garamond_regular, garamond) ||
        !fonts.load(pl::page::FaceId::eb_garamond_italic, garamond_italic))
        return 1;
    pl::page::Typesetter setter(fonts);
    pl::Work bible;
    pl::Versification places;
    std::size_t total_words = 0;
    std::size_t total_pages[3] = {0, 0, 0};
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    for (std::size_t n = 0; n < library.books().size(); ++n) {
        const std::string id = library.books()[n].id;
        std::string text;
        pl::Work work;
        if (!library.read(n, text) || !pl::read_work(id, text, work)) {
            std::fprintf(stderr, "%s does not read\n", id.c_str());
            return 1;
        }
        if (id == "bsb") {
            bible = work;
            pl::versify(bible, places);
        }
        pl::LayoutOptions options;
        if (id == "concordance") {
            options.bible = &bible;
            options.places = &places;
        }
        std::vector<pl::Section> sections;
        pl::plan_sections(work, sections);
        std::size_t pages[3] = {0, 0, 0};
        for (int step = 0; step < 3; ++step) {
            options.step = static_cast<pl::page::TypeStep>(step);
            for (const pl::Section& section : sections) {
                std::vector<pl::page::PageLayout> laid;
                pl::lay_out_section(work, section, options, setter, laid);
                pages[step] += laid.size();
            }
            total_pages[step] += pages[step];
        }
        const std::size_t words = words_in(work);
        total_words += words;
        std::printf("%-40s %9zu words  pages %6zu small %6zu medium %6zu large\n", id.c_str(), words, pages[0],
                    pages[1], pages[2]);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("TOTAL %zu books: %zu words; pages %zu small, %zu medium, %zu large (%.0f s)\n",
                library.books().size(), total_words, total_pages[0], total_pages[1], total_pages[2], seconds);
    return 0;
}
