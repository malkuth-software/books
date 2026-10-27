// Learns the packing tables from every book of some folders, packs every book, proves
// each comes back byte for byte, and writes the packed books to a folder. Not shipped.
//
//   pack_lab <out folder> <folder of .tsv books> [more folders...]
#include "../src/text_pack.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3)
        return 2;
    std::vector<std::string> names, texts;
    for (int a = 2; a < argc; ++a) {
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(argv[a])) {
            if (entry.path().extension() != ".tsv")
                continue;
            std::ifstream file(entry.path(), std::ios::binary);
            names.push_back(entry.path().filename().string());
            texts.emplace_back((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        }
    }
    std::vector<std::string_view> views(texts.begin(), texts.end());
    const std::chrono::steady_clock::time_point a = std::chrono::steady_clock::now();
    pl::PackTables tables = pl::learn_tables(views);
    const std::string saved = pl::save_tables(tables);
    pl::PackTables loaded;
    if (!pl::load_tables(saved, loaded)) {
        std::printf("the tables do not load back\n");
        return 1;
    }
    const pl::TextPacker packer(loaded);
    std::filesystem::create_directories(argv[1]);
    std::size_t raw = 0, packed_total = 0;
    double pack_seconds = 0, unpack_seconds = 0;
    for (std::size_t n = 0; n < texts.size(); ++n) {
        std::string packed, back;
        const std::chrono::steady_clock::time_point p0 = std::chrono::steady_clock::now();
        if (!packer.pack(texts[n], packed)) {
            std::printf("%s does not pack\n", names[n].c_str());
            return 1;
        }
        const std::chrono::steady_clock::time_point p1 = std::chrono::steady_clock::now();
        const bool ok = packer.unpack(packed, back);
        const std::chrono::steady_clock::time_point p2 = std::chrono::steady_clock::now();
        pack_seconds += std::chrono::duration<double>(p1 - p0).count();
        unpack_seconds += std::chrono::duration<double>(p2 - p1).count();
        if (!ok || back != texts[n]) {
            std::size_t at = 0;
            while (at < back.size() && at < texts[n].size() && back[at] == texts[n][at])
                ++at;
            std::printf("%s does not come back (unpacked %s, first difference at %zu: '%.40s' / '%.40s')\n",
                        names[n].c_str(), ok ? "yes" : "no", at, texts[n].c_str() + std::min(at, texts[n].size()),
                        back.c_str() + std::min(at, back.size()));
            return 1;
        }
        std::ofstream out(std::string(argv[1]) + "/" + names[n], std::ios::binary);
        out.write(packed.data(), static_cast<std::streamsize>(packed.size()));
        raw += texts[n].size();
        packed_total += packed.size();
    }
    const double learn = std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count() - pack_seconds - unpack_seconds;
    std::printf("%zu books, all come back. raw %zu -> packed %zu (%.1f%%); tables: %zu characters, %zu words, %zu bytes "
                "saved; learned in %.1f s, packed at %.0f MB/s, unpacked at %.0f MB/s\n",
                texts.size(), raw, packed_total, 100.0 * packed_total / raw, loaded.characters.size(),
                loaded.words.size(), saved.size(), learn, raw / pack_seconds / 1e6, raw / unpack_seconds / 1e6);
    std::ofstream tables_file(std::string(argv[1]) + "/tables.bin", std::ios::binary);
    tables_file.write(saved.data(), static_cast<std::streamsize>(saved.size()));
    return 0;
}
