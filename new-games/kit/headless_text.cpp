#include "headless_text.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <stdexcept>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "third_party/stb_truetype.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifndef KIT_FONT_DIR
#define KIT_FONT_DIR "assets/fonts"
#endif

namespace kit {
namespace {

struct LoadedFont {
    std::vector<unsigned char> bytes;
    stbtt_fontinfo info{};
    bool ready = false;
};

std::array<LoadedFont, 4> fonts;

const char* file_name(Face face) {
    switch (face) {
    case Face::serif:
        return "LibreBaskerville-Regular.ttf";
    case Face::serif_bold:
        return "LibreBaskerville-Bold.ttf";
    case Face::mono:
        return "Cousine-Regular.ttf";
    case Face::mono_bold:
        return "Cousine-Bold.ttf";
    }
    return "LibreBaskerville-Regular.ttf";
}

LoadedFont& load(Face face) {
    LoadedFont& font = fonts[static_cast<std::size_t>(face)];
    if (font.ready) {
        return font;
    }
    const std::string path = font_directory() + "/" + file_name(face);
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const std::streamoff length = file.tellg();
    if (!file || length <= 0 || length > 4 * 1024 * 1024) {
        throw std::runtime_error("Headless text cannot read the font " + path +
                                 " (set PLAYSUITE_FONT_DIR to the repository's assets/fonts)");
    }
    font.bytes.resize(static_cast<std::size_t>(length));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(font.bytes.data()), static_cast<std::streamsize>(length));
    if (!file || stbtt_InitFont(&font.info, font.bytes.data(), 0) == 0) {
        throw std::runtime_error("Headless text cannot parse the font " + path);
    }
    font.ready = true;
    return font;
}

// Decodes UTF-8 into code points. Malformed bytes become U+FFFD.
std::vector<int> decode(const std::string& utf8) {
    std::vector<int> points;
    std::size_t index = 0;
    while (index < utf8.size()) {
        const unsigned char lead = static_cast<unsigned char>(utf8[index]);
        int extra = 0;
        int value = 0xFFFD;
        if (lead < 0x80) {
            value = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            extra = 1;
            value = lead & 0x1F;
        } else if ((lead & 0xF0) == 0xE0) {
            extra = 2;
            value = lead & 0x0F;
        } else if ((lead & 0xF8) == 0xF0) {
            extra = 3;
            value = lead & 0x07;
        }
        ++index;
        bool complete = true;
        for (int count = 0; count < extra; ++count) {
            if (index >= utf8.size() || (static_cast<unsigned char>(utf8[index]) & 0xC0) != 0x80) {
                complete = false;
                break;
            }
            value = (value << 6) | (static_cast<unsigned char>(utf8[index]) & 0x3F);
            ++index;
        }
        points.push_back(complete ? value : 0xFFFD);
    }
    return points;
}

double advance_of(const LoadedFont& font, double scale, int point, int next) {
    int advance = 0;
    int bearing = 0;
    stbtt_GetCodepointHMetrics(&font.info, point, &advance, &bearing);
    double width = advance * scale;
    if (next != 0) {
        width += stbtt_GetCodepointKernAdvance(&font.info, point, next) * scale;
    }
    return width;
}

double run_width(const LoadedFont& font, double scale, const std::vector<int>& points,
                 std::size_t first, std::size_t last) {
    double width = 0;
    for (std::size_t index = first; index < last; ++index) {
        const int next = index + 1 < last ? points[index + 1] : 0;
        width += advance_of(font, scale, points[index], next);
    }
    return width;
}

struct Line {
    std::size_t first = 0;
    std::size_t last = 0;  // one past the final code point
};

// Greedy word wrap on spaces; '\n' always breaks. A word wider than the line stays whole.
std::vector<Line> break_lines(const LoadedFont& font, double scale, const std::vector<int>& points,
                              double wrap_width) {
    std::vector<Line> lines;
    std::size_t start = 0;
    while (start <= points.size()) {
        std::size_t paragraph_end = start;
        while (paragraph_end < points.size() && points[paragraph_end] != '\n') {
            ++paragraph_end;
        }
        std::size_t line_start = start;
        while (true) {
            std::size_t line_end = paragraph_end;
            if (wrap_width > 0) {
                std::size_t fit = line_start;
                std::size_t cursor = line_start;
                while (cursor < paragraph_end) {
                    std::size_t word_end = cursor;
                    while (word_end < paragraph_end && points[word_end] == ' ') {
                        ++word_end;
                    }
                    while (word_end < paragraph_end && points[word_end] != ' ') {
                        ++word_end;
                    }
                    if (run_width(font, scale, points, line_start, word_end) > wrap_width &&
                        fit > line_start) {
                        break;
                    }
                    fit = word_end;
                    cursor = word_end;
                }
                line_end = fit;
            }
            Line line;
            line.first = line_start;
            line.last = line_end;
            lines.push_back(line);
            line_start = line_end;
            while (line_start < paragraph_end && points[line_start] == ' ') {
                ++line_start;
            }
            if (line_start >= paragraph_end) {
                break;
            }
        }
        if (paragraph_end >= points.size()) {
            break;
        }
        start = paragraph_end + 1;
    }
    return lines;
}

}  // namespace

std::string font_directory() {
    const char* override_dir = std::getenv("PLAYSUITE_FONT_DIR");
    if (override_dir != nullptr && override_dir[0] != '\0') {
        return std::string(override_dir);
    }
    return std::string(KIT_FONT_DIR);
}

TextMask render_text(const std::string& utf8, Face face, double size, double wrap_width,
                     bool aliased) {
    const LoadedFont& font = load(face);
    const double em = std::max(size, 1.0);
    const double scale = stbtt_ScaleForMappingEmToPixels(&font.info, static_cast<float>(em));
    int ascent = 0;
    int descent = 0;
    int gap = 0;
    stbtt_GetFontVMetrics(&font.info, &ascent, &descent, &gap);
    const double line_height = (ascent - descent + gap) * scale;
    const std::vector<int> points = decode(utf8);
    const std::vector<Line> lines = break_lines(font, scale, points, wrap_width);
    double widest = 0;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        widest = std::max(widest, run_width(font, scale, points, lines[index].first, lines[index].last));
    }
    TextMask mask;
    // One pixel of padding on every side, as the application's masks have.
    mask.w = std::max(1, static_cast<int>(std::ceil(widest))) + 2;
    mask.h = std::max(1, static_cast<int>(std::ceil(line_height * lines.size()))) + 2;
    mask.a.assign(static_cast<std::size_t>(mask.w) * mask.h, 0);
    std::vector<unsigned char> glyph;
    for (std::size_t row = 0; row < lines.size(); ++row) {
        const double baseline = 1 + row * line_height + ascent * scale;
        double pen = 1;
        for (std::size_t index = lines[row].first; index < lines[row].last; ++index) {
            const int point = points[index];
            const int next = index + 1 < lines[row].last ? points[index + 1] : 0;
            const float shift_x = static_cast<float>(pen - std::floor(pen));
            const float shift_y = static_cast<float>(baseline - std::floor(baseline));
            int x0 = 0;
            int y0 = 0;
            int x1 = 0;
            int y1 = 0;
            stbtt_GetCodepointBitmapBoxSubpixel(&font.info, point, static_cast<float>(scale),
                                                static_cast<float>(scale), shift_x, shift_y, &x0,
                                                &y0, &x1, &y1);
            const int glyph_w = x1 - x0;
            const int glyph_h = y1 - y0;
            if (glyph_w > 0 && glyph_h > 0) {
                glyph.assign(static_cast<std::size_t>(glyph_w) * glyph_h, 0);
                stbtt_MakeCodepointBitmapSubpixel(&font.info, glyph.data(), glyph_w, glyph_h,
                                                  glyph_w, static_cast<float>(scale),
                                                  static_cast<float>(scale), shift_x, shift_y,
                                                  point);
                const int left = static_cast<int>(std::floor(pen)) + x0;
                const int top = static_cast<int>(std::floor(baseline)) + y0;
                for (int y = 0; y < glyph_h; ++y) {
                    const int out_y = top + y;
                    if (out_y < 0 || out_y >= mask.h) {
                        continue;
                    }
                    for (int x = 0; x < glyph_w; ++x) {
                        const int out_x = left + x;
                        if (out_x < 0 || out_x >= mask.w) {
                            continue;
                        }
                        const std::size_t target = static_cast<std::size_t>(out_y) * mask.w + out_x;
                        const int sum = mask.a[target] + glyph[static_cast<std::size_t>(y) * glyph_w + x];
                        mask.a[target] = static_cast<std::uint8_t>(std::min(sum, 255));
                    }
                }
            }
            pen += advance_of(font, scale, point, next);
        }
    }
    if (aliased) {
        for (std::size_t index = 0; index < mask.a.size(); ++index) {
            mask.a[index] = mask.a[index] >= 128 ? 255 : 0;
        }
    }
    return mask;
}

}  // namespace kit
