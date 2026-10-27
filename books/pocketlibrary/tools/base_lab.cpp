// Every book of a folder coded after a shared base: the sizes, and the decoding speed
// (best of five). Not shipped.
//
//   base_lab <folder of packed books> <base file> <effort>
#include "lz_coder.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
static std::vector<std::uint8_t> read(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
int main(int, char** argv) {
    const std::vector<std::uint8_t> base_text = read(argv[2]);
    const int effort = std::stoi(argv[3]);
    std::vector<std::uint8_t> base_code;
    pl::LzBase base;
    pl::lz_encode_base(base_text, base_code, effort, base);
    pl::LzBase decoded_base;
    double base_seconds = 1e9;
    bool base_ok = true;
    for (int r = 0; r < 5; ++r) {
        const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        base_ok = pl::lz_decode_base(base_code, base_text.size(), decoded_base) && base_ok;
        base_seconds = std::min(base_seconds, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }
    std::size_t raw = 0, coded = 0;
    double decode = 0;
    bool ok = base_ok;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(argv[1])) {
        const std::vector<std::uint8_t> text = read(entry.path().string());
        std::vector<std::uint8_t> code;
        pl::lz_encode(text, code, effort, base);
        std::vector<std::uint8_t> back;
        double best = 1e9;
        for (int r = 0; r < 5; ++r) {
            const std::chrono::steady_clock::time_point a = std::chrono::steady_clock::now();
            ok = pl::lz_decode(code, text.size(), back, decoded_base) && back == text && ok;
            best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count());
        }
        decode += best;
        raw += text.size();
        coded += code.size();
    }
    std::printf("base %zu bytes, coded %zu, decoded in %.0f ms; books raw %zu coded %zu; decode %.0f MB/s %s\n",
                base_text.size(), base_code.size(), base_seconds * 1000, raw, coded, raw / decode / 1e6,
                ok ? "all round trip" : "FAILED");
}
