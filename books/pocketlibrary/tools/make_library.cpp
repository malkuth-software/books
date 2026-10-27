// Builds the library's parts (container.hpp) from the normalized books import_books.py
// wrote, in the shelf index's order (shelf.txt), and proves them: every book is decoded
// again, checked against its SHA-256 and read as a work before a part is written.
//
//   make_library [options] generation <output dir> <normalized dir>
//       a new generation: the tables learned from every book, the base chosen, and each
//       other book coded into the block its shelf record names ("block<TAB>n")
//   make_library [options] block <base file> <number> <output dir> <normalized dir>
//       one block: the books the index puts in block <number>, coded against an
//       existing base, its tables and base frozen
//
//     --cache <dir>     keep codes there and code again only what changed
//     --base <id,id>    the books placed first in the base (default bsb,jps1917)
//     --base-mb <n>     then fill the base to n megabytes of packed text (default 16)
//     --effort <n>      the coder's match search (default 48)
//     --quiet           no line per book
//     --trial           a book that does not read as a work is reported, not fatal
//                       (for measuring trial imports; never for the shipped parts)
//
// Parts are named by their content: base-<16 hex digits of its SHA-256>.plb and
// block-<number>-<16 hex digits>.plb.
#include "../src/book.hpp"
#include "../src/container.hpp"
#include "../src/library.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

class FolderCache final : public pl::CodeCache {
  public:
    explicit FolderCache(std::string folder) : folder_(std::move(folder)) {
        std::filesystem::create_directories(folder_);
    }
    bool find(const pl::Sha256Digest& key, std::vector<std::uint8_t>& code) override {
        std::ifstream kept(path(key), std::ios::binary);
        if (!kept)
            return false;
        code.assign((std::istreambuf_iterator<char>(kept)), std::istreambuf_iterator<char>());
        ++found;
        return true;
    }
    void keep(const pl::Sha256Digest& key, const std::vector<std::uint8_t>& code) override {
        std::ofstream fresh(path(key), std::ios::binary);
        fresh.write(reinterpret_cast<const char*>(code.data()), static_cast<std::streamsize>(code.size()));
        ++made;
    }
    std::size_t found = 0, made = 0;

  private:
    std::string folder_;
    std::string path(const pl::Sha256Digest& key) const {
        return folder_ + "/" + pl::sha256_hex(key) + ".lz";
    }
};

bool read_file(const std::string& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    out.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return true;
}

// The books of the shelf index in a normalized folder (shelf.txt, as import_books.py
// writes it), in its order, each with the block its record names (1 when none).
bool read_folder(const std::string& folder, std::vector<pl::BookText>& books) {
    std::string index;
    if (!read_file(folder + "/shelf.txt", index)) {
        std::fprintf(stderr, "%s has no shelf.txt\n", folder.c_str());
        return false;
    }
    for (const pl::IndexedBook& indexed : pl::read_shelf_index(index)) {
        pl::BookText book;
        book.id = indexed.id;
        const std::size_t at = indexed.record.find("block\t");
        if (at != std::string::npos && (at == 0 || indexed.record[at - 1] == '\n'))
            book.block = std::atoi(indexed.record.c_str() + at + 6);
        if (book.block < 1) {
            std::fprintf(stderr, "%s: a block number must be 1 or more\n", book.id.c_str());
            return false;
        }
        if (!read_file(folder + "/" + book.id + ".tsv", book.text)) {
            std::fprintf(stderr, "missing %s.tsv\n", book.id.c_str());
            return false;
        }
        books.push_back(book);
    }
    return true;
}

std::string part_name(const std::string& kind, const std::vector<std::uint8_t>& bytes) {
    return kind + "-" + pl::sha256_hex(pl::sha256(bytes)).substr(0, 16) + ".plb";
}

bool write_file(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

// Every book of `books` read back from the parts, checked against its text and read as
// a work. False on the first that does not come back.
bool prove(pl::LibraryFile& library, const std::vector<pl::BookText>& books, bool quiet, bool trial) {
    const std::chrono::steady_clock::time_point preparing = std::chrono::steady_clock::now();
    if (!library.prepare()) {
        std::fprintf(stderr, "the tables or the base do not decode\n");
        return false;
    }
    const std::chrono::duration<double> prepared = std::chrono::steady_clock::now() - preparing;
    std::size_t text_total = 0, packed_total = 0, in_base = 0, coded_total = 0;
    double decode_total = 0;
    for (const pl::BookText& book : books) {
        const std::size_t n = library.find(book.id);
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        std::string text;
        const bool read = n < library.books().size() && library.read(n, text);
        const std::chrono::duration<double> decoded = std::chrono::steady_clock::now() - start;
        decode_total += decoded.count();
        if (!read || text != book.text) {
            std::fprintf(stderr, "%s does not come back\n", book.id.c_str());
            return false;
        }
        pl::Work work;
        if (!pl::read_work(book.id, text, work)) {
            std::fprintf(stderr, "%s does not read as a work\n", book.id.c_str());
            if (!trial)
                return false;
        }
        const pl::ShelvedBook& shelved = library.books()[n];
        text_total += shelved.length;
        packed_total += shelved.packed_length;
        coded_total += shelved.code_length;
        in_base += shelved.in_base() ? shelved.packed_length : 0;
        if (quiet)
            continue;
        std::size_t chapters = 0;
        for (const pl::Division& division : work.divisions)
            chapters += division.chapters.size();
        if (shelved.in_base())
            std::printf("%-36s text %9u  packed %9u  in the base         read in %6.1f ms  %4zu chapters\n",
                        shelved.id.c_str(), shelved.length, shelved.packed_length, decoded.count() * 1000, chapters);
        else
            std::printf("%-36s text %9u  packed %9u  coded %9u (%4.1f%%)  read in %6.1f ms  %4zu chapters\n",
                        shelved.id.c_str(), shelved.length, shelved.packed_length, shelved.code_length,
                        100.0 * shelved.code_length / shelved.length, decoded.count() * 1000, chapters);
    }
    const pl::SharedPart& base = library.base();
    std::printf("%zu books: text %zu, packed %zu; base %u packed (%zu of these books) coded %u, prepared in %.0f ms; "
                "other books coded %zu; read all in %.2f s\n",
                books.size(), text_total, packed_total, base.length, in_base, base.code_length,
                prepared.count() * 1000, coded_total, decode_total);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    pl::LibraryPlan plan;
    plan.base = {"bsb", "jps1917"};
    plan.base_bytes = 16'000'000;
    std::string cache_folder;
    bool quiet = false, trial = false;
    std::vector<std::string> paths;
    for (int a = 1; a < argc; ++a) {
        const std::string arg = argv[a];
        if (arg == "--cache" && a + 1 < argc) {
            cache_folder = argv[++a];
        } else if (arg == "--base" && a + 1 < argc) {
            plan.base.clear();
            const std::string list = argv[++a];
            std::size_t at = 0;
            while (at < list.size()) {
                const std::size_t comma = std::min(list.find(',', at), list.size());
                if (comma > at)
                    plan.base.push_back(list.substr(at, comma - at));
                at = comma + 1;
            }
        } else if (arg == "--base-mb" && a + 1 < argc) {
            plan.base_bytes = static_cast<std::size_t>(std::stod(argv[++a]) * 1e6);
        } else if (arg == "--effort" && a + 1 < argc) {
            plan.effort = std::stoi(argv[++a]);
        } else if (arg == "--quiet") {
            quiet = true;
        } else if (arg == "--trial") {
            trial = true;
        } else {
            paths.push_back(arg);
        }
    }
    const bool generation = paths.size() == 3 && paths[0] == "generation";
    const bool block = paths.size() == 5 && paths[0] == "block";
    if (!generation && !block) {
        std::fprintf(stderr,
                     "make_library [--cache dir] [--base ids] [--base-mb n] [--effort n] [--quiet] generation "
                     "<output dir> <normalized dir>\n"
                     "make_library [--cache dir] [--quiet] block <base file> <number> <output dir> <normalized dir>\n");
        return 2;
    }
    std::vector<pl::BookText> books;
    if (!read_folder(paths.back(), books))
        return 1;
    std::unique_ptr<FolderCache> cache;
    if (!cache_folder.empty()) {
        cache = std::make_unique<FolderCache>(cache_folder);
        plan.cache = cache.get();
    }
    const std::string output = paths[paths.size() - 2];
    std::filesystem::create_directories(output);
    const std::chrono::steady_clock::time_point coding = std::chrono::steady_clock::now();
    pl::Generation made;
    if (generation) {
        if (!pl::write_generation(books, plan, made)) {
            std::fprintf(stderr, "a book does not pack\n");
            return 1;
        }
    } else {
        std::string base;
        if (!read_file(paths[1], base)) {
            std::fprintf(stderr, "cannot read %s\n", paths[1].c_str());
            return 1;
        }
        made.base.assign(base.begin(), base.end());
        const int number = std::atoi(paths[2].c_str());
        pl::LibraryFile file;
        if (number < 1 || !file.open(made.base)) {
            std::fprintf(stderr, "the base does not open, or the block number is not 1 or more\n");
            return 1;
        }
        // The block's books: those the index puts in it, less any inside the base.
        std::vector<pl::BookText> members;
        for (const pl::BookText& book : books)
            if (book.block == number && file.find(book.id) == file.books().size())
                members.push_back(book);
        books = members;
        pl::NumberedBlock coded;
        coded.number = number;
        if (books.empty() || !pl::write_block(made.base, books, plan.cache, coded.bytes)) {
            std::fprintf(stderr, "the block has no books, a book does not pack, or the base does not decode\n");
            return 1;
        }
        made.blocks.push_back(coded);
    }
    const std::chrono::duration<double> coded = std::chrono::steady_clock::now() - coding;
    if (cache)
        std::printf("coded in %.1f s; codes: %zu made, %zu from the cache\n", coded.count(), (*cache).made,
                    (*cache).found);
    else
        std::printf("coded in %.1f s\n", coded.count());

    pl::LibraryFile library;
    if (!library.open(made.base)) {
        std::fprintf(stderr, "the base written does not open\n");
        return 1;
    }
    for (const pl::NumberedBlock& part : made.blocks) {
        if (!library.add_block(part.bytes)) {
            std::fprintf(stderr, "block %d does not join its base\n", part.number);
            return 1;
        }
    }
    if (!prove(library, books, quiet, trial))
        return 1;
    std::size_t total = 0;
    std::vector<std::string> names;
    if (generation) {
        names.push_back(part_name("base", made.base));
        total += made.base.size();
        if (!write_file(output + "/" + names.back(), made.base))
            return 1;
    }
    for (const pl::NumberedBlock& part : made.blocks) {
        names.push_back(part_name("block-" + std::to_string(part.number), part.bytes));
        total += part.bytes.size();
        if (!write_file(output + "/" + names.back(), part.bytes))
            return 1;
    }
    for (const std::string& name : names)
        std::printf("wrote %s/%s\n", output.c_str(), name.c_str());
    std::size_t text_total = 0;
    for (const pl::BookText& book : books)
        text_total += book.text.size();
    std::printf("%zu bytes in %zu parts (%.2f%% of the text)\n", total, names.size(),
                100.0 * static_cast<double>(total) / static_cast<double>(std::max<std::size_t>(text_total, 1)));
    return 0;
}
