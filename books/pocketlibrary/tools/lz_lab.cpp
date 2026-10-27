// Measures the LZ coder on each normalized book of a folder: size, encode and decode
// time, and that every byte comes back. Not shipped.
//
//   lz_lab <folder of .tsv books> [effort] [quiet]
#include "../src/lz_coder.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2)
        return 2;
    const std::string folder = argv[1];
    const int effort = argc > 2 ? std::stoi(argv[2]) : 256;
    const bool quiet = argc > 3;
    std::vector<std::string> names;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder))
        if (entry.path().extension() == ".tsv")
            names.push_back(entry.path().stem().string());
    std::sort(names.begin(), names.end());
    std::size_t raw_total = 0, coded_total = 0;
    double encode_total = 0, decode_total = 0;
    bool all_back = true;
    for (const std::string& name : names) {
        std::ifstream file(folder + "/" + name + ".tsv", std::ios::binary);
        const std::vector<std::uint8_t> text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::vector<std::uint8_t> code;
        const std::chrono::steady_clock::time_point a = std::chrono::steady_clock::now();
        pl::lz_encode(text, code, effort);
        const std::chrono::steady_clock::time_point b = std::chrono::steady_clock::now();
        std::vector<std::uint8_t> back;
        const bool ok = pl::lz_decode(code, text.size(), back);
        const std::chrono::steady_clock::time_point c = std::chrono::steady_clock::now();
        const std::chrono::duration<double> encode_time = b - a;
        const std::chrono::duration<double> decode_time = c - b;
        encode_total += encode_time.count();
        decode_total += decode_time.count();
        all_back = all_back && ok && back == text;
        if (!quiet)
        std::printf("%-30s raw %9zu  coded %8zu  %5.1f%%  encode %.2f s  decode %.1f ms  %s\n", name.c_str(), text.size(),
                    code.size(), 100.0 * static_cast<double>(code.size()) / static_cast<double>(text.size()),
                    encode_time.count(), decode_time.count() * 1000, ok && back == text ? "round trip" : "FAILED");
        raw_total += text.size();
        coded_total += code.size();
    }
    std::printf("all %zu books: raw %zu  coded %zu  %.2f%%  encode %.1f s  decode %.0f MB/s  %s\n", names.size(),
                raw_total, coded_total, 100.0 * static_cast<double>(coded_total) / static_cast<double>(raw_total),
                encode_total, static_cast<double>(raw_total) / decode_total / 1e6, all_back ? "all round trip" : "FAILED");
    return 0;
}
