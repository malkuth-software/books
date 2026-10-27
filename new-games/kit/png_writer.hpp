#pragma once
// A dependency-free PNG writer for the harness: opaque 8-bit RGB from the BGRA
// frames games render. The image data is stored uncompressed, which keeps this to
// one small header; the files are for looking at, not for shipping.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace kit {

inline std::uint32_t png_crc(const std::vector<std::uint8_t>& bytes, std::size_t first,
                             std::size_t count) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = first; index < first + count; ++index) {
        crc ^= bytes[index];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = (crc & 1U) != 0 ? 0xEDB88320U : 0U;
            crc = (crc >> 1) ^ mask;
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

inline void png_put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

inline void png_chunk(std::vector<std::uint8_t>& out, const char* type,
                      const std::vector<std::uint8_t>& data) {
    png_put32(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t start = out.size();
    for (int index = 0; index < 4; ++index) {
        out.push_back(static_cast<std::uint8_t>(type[index]));
    }
    out.insert(out.end(), data.begin(), data.end());
    png_put32(out, png_crc(out, start, out.size() - start));
}

// `bgra` holds height rows of width pixels, 4 bytes each, blue first (a Canvas's px).
// Returns false when the file cannot be written.
inline bool write_png(const std::string& path, int width, int height,
                      const std::vector<std::uint8_t>& bgra) {
    if (width <= 0 || height <= 0 ||
        bgra.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4) {
        return false;
    }
    // Scanlines: a zero filter byte, then RGB.
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (static_cast<std::size_t>(width) * 3 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const std::size_t row = static_cast<std::size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x) {
            const std::size_t pixel = row + static_cast<std::size_t>(x) * 4;
            raw.push_back(bgra[pixel + 2]);
            raw.push_back(bgra[pixel + 1]);
            raw.push_back(bgra[pixel]);
        }
    }
    // A zlib stream of stored (uncompressed) deflate blocks, 65535 bytes at most each.
    std::vector<std::uint8_t> packed;
    packed.push_back(0x78);
    packed.push_back(0x01);
    std::uint32_t adler_a = 1;
    std::uint32_t adler_b = 0;
    std::size_t offset = 0;
    while (offset < raw.size()) {
        const std::size_t count = raw.size() - offset < 65535 ? raw.size() - offset : 65535;
        const bool final_block = offset + count == raw.size();
        packed.push_back(final_block ? 1 : 0);
        packed.push_back(static_cast<std::uint8_t>(count & 0xFF));
        packed.push_back(static_cast<std::uint8_t>(count >> 8));
        packed.push_back(static_cast<std::uint8_t>(~count & 0xFF));
        packed.push_back(static_cast<std::uint8_t>((~count >> 8) & 0xFF));
        for (std::size_t index = offset; index < offset + count; ++index) {
            packed.push_back(raw[index]);
            adler_a = (adler_a + raw[index]) % 65521U;
            adler_b = (adler_b + adler_a) % 65521U;
        }
        offset += count;
    }
    png_put32(packed, (adler_b << 16) | adler_a);

    std::vector<std::uint8_t> file{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<std::uint8_t> header;
    png_put32(header, static_cast<std::uint32_t>(width));
    png_put32(header, static_cast<std::uint32_t>(height));
    header.push_back(8);  // bit depth
    header.push_back(2);  // colour type: RGB
    header.push_back(0);
    header.push_back(0);
    header.push_back(0);
    png_chunk(file, "IHDR", header);
    png_chunk(file, "IDAT", packed);
    png_chunk(file, "IEND", std::vector<std::uint8_t>{});
    std::FILE* output = std::fopen(path.c_str(), "wb");
    if (output == nullptr) {
        return false;
    }
    const std::size_t written = std::fwrite(file.data(), 1, file.size(), output);
    const int closed = std::fclose(output);
    return written == file.size() && closed == 0;
}

}  // namespace kit
