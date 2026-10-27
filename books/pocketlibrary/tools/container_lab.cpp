// Measures the text coder on the library's normalized books (import_books.py's output):
// the whole library as one stream, each block alone, and each block from a model that has
// first learned a primer. Not shipped; the numbers go to the container study's record.
//
//   container_lab <normalized dir> <block bytes> <primer bytes> [table bits]
#include "../src/text_coder.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return bytes;
}

// Cuts a book at its chapter and entry openings into blocks of at least `target` bytes.
std::vector<std::vector<std::uint8_t>> cut(const std::vector<std::uint8_t>& book, std::size_t target) {
    std::vector<std::vector<std::uint8_t>> blocks;
    std::vector<std::uint8_t> block;
    std::size_t line_start = 0;
    for (std::size_t i = 0; i < book.size(); ++i) {
        if (book[i] != '\n')
            continue;
        const bool opens = i + 2 - line_start >= 2 && (book[line_start] == 'C' || book[line_start] == 'K') &&
                           book[line_start + 1] == '\t';
        if (opens && block.size() >= target) {
            blocks.push_back(block);
            block.clear();
        }
        block.insert(block.end(), book.begin() + static_cast<std::ptrdiff_t>(line_start),
                     book.begin() + static_cast<std::ptrdiff_t>(i + 1));
        line_start = i + 1;
    }
    if (!block.empty())
        blocks.push_back(block);
    return blocks;
}

double seconds_since(std::chrono::steady_clock::time_point start) {
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    return elapsed.count();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4)
        return 2;
    const std::string folder = argv[1];
    const std::size_t block_bytes = static_cast<std::size_t>(std::stoul(argv[2]));
    const std::size_t primer_bytes = static_cast<std::size_t>(std::stoul(argv[3]));
    pl::CoderShape shape;
    if (argc > 4)
        shape.table_bits = std::stoi(argv[4]);
    const char* names[] = {"bsb", "jps1917", "berakot", "avot", "strongs-hebrew", "strongs-greek", "concordance"};
    std::vector<std::vector<std::uint8_t>> books;
    for (const char* name : names)
        books.push_back(read_file(folder + "/" + name + ".tsv"));
    // The primer: an equal share from the start of every book.
    std::vector<std::uint8_t> primer;
    for (const std::vector<std::uint8_t>& book : books) {
        const std::size_t share = std::min(book.size(), primer_bytes / books.size());
        primer.insert(primer.end(), book.begin(), book.begin() + static_cast<std::ptrdiff_t>(share));
    }
    pl::TextModel taught(shape);
    const std::chrono::steady_clock::time_point teach_start = std::chrono::steady_clock::now();
    taught.learn(primer);
    const double teach_seconds = seconds_since(teach_start);
    std::size_t raw_total = 0, coded_total = 0, block_total = 0;
    double decode_seconds = 0;
    std::size_t decoded_bytes = 0;
    for (std::size_t b = 0; b < books.size(); ++b) {
        const std::vector<std::vector<std::uint8_t>> blocks = cut(books[b], block_bytes);
        std::size_t raw = 0, coded = 0;
        for (const std::vector<std::uint8_t>& block : blocks) {
            pl::TextModel model = primer.empty() ? pl::TextModel(shape) : taught;
            std::vector<std::uint8_t> code;
            pl::encode(model, block, code);
            raw += block.size();
            coded += code.size();
            if (decoded_bytes < 2000000) {
                const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
                pl::TextModel reader = primer.empty() ? pl::TextModel(shape) : taught;
                std::vector<std::uint8_t> back;
                const bool ok = pl::decode(reader, code, block.size(), back);
                decode_seconds += seconds_since(start);
                decoded_bytes += block.size();
                if (!ok || back != block) {
                    std::printf("ROUND TRIP FAILED in %s\n", names[b]);
                    return 1;
                }
            }
        }
        std::printf("%-16s %4zu blocks  raw %9zu  coded %8zu  %5.1f%%\n", names[b], blocks.size(), raw, coded,
                    100.0 * static_cast<double>(coded) / static_cast<double>(raw));
        raw_total += raw;
        coded_total += coded;
        block_total += blocks.size();
    }
    std::printf("all              %4zu blocks  raw %9zu  coded %8zu  %5.1f%%  primer %zu bytes, taught in %.2f s; "
                "decode %.2f MB/s incl. model copy\n",
                block_total, raw_total, coded_total, 100.0 * static_cast<double>(coded_total) / static_cast<double>(raw_total),
                primer.size(), teach_seconds, static_cast<double>(decoded_bytes) / decode_seconds / 1e6);
    return 0;
}
