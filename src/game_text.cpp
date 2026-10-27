#include "game_text.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace games {
namespace {
std::vector<std::byte> read_font(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const std::streamoff length = input.tellg();
    if (!input || length <= 0 || length > 4 * 1024 * 1024) {
        throw std::runtime_error("Missing or oversized game font: " + path.string());
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(length));
    if (!input) { throw std::runtime_error("Incomplete game font: " + path.string()); }
    return bytes;
}
void require_success(const gui_forms::TextMaskResult result) {
    if (result.status != gui_forms::TextMaskStatus::success) {
        throw std::runtime_error("Game text service failed (status " +
            std::to_string(static_cast<int>(result.status)) + ", limit " +
            std::to_string(static_cast<int>(result.limit)) + ")");
    }
}
std::uint32_t blend_channel(const std::uint32_t destination, const double source,
    const double opacity) {
    const double mixed = source * opacity * 255 + destination * (1 - opacity);
    const std::uint32_t rounded = static_cast<std::uint32_t>(std::clamp(mixed + .5, 0.0, 255.0));
    return rounded;
}
}
GameText::GameText(const std::filesystem::path& directory, const std::string& family) {
    const std::array<std::string, 6> names{family + "-Regular.ttf", family + "-Bold.ttf",
        "Cousine-Regular.ttf", "Cousine-Bold.ttf", "Carlito-Regular.ttf", "Carlito-Bold.ttf"};
    std::array<std::vector<std::byte>, 6> bytes{};
    std::array<gui_forms::PreparedFontSource, 6> sources{};
    for (std::size_t index = 0; index < names.size(); ++index) {
        bytes[index] = read_font(directory / names[index]);
        sources[index].encoded = bytes[index];
    }
    gui_forms::TextMaskResult result = service_.create_font_bank(sources, fonts_);
    require_success(result);
    result = service_.open_session(nullptr, session_);
    require_success(result);
    requests_ = std::make_unique<TextRequests>(*session_, fonts_);
}
void GameText::begin() {
    const gui_forms::TextMaskResult result = (*requests_).poll();
    require_success(result);
    ready_ = true;
}
void GameText::cancel() {
    const gui_forms::TextMaskResult result = (*requests_).cancel_all();
    require_success(result);
    ready_ = false;
}
TextImage GameText::get(const std::string_view source, const bool bold,
    const double size, const double wrap, const double scale, const bool mono) {
    const gui_forms::TextMaskRequest request{.utf8 = source,
        .primary_face = static_cast<std::uint32_t>((mono ? 2 : 0) + (bold ? 1 : 0)),
        .size = size, .wrap_width = wrap, .device_scale = scale,
        .raster = mono ? gui_forms::TextMaskRaster::true_mono : gui_forms::TextMaskRaster::outline_gray};
    TextImage image{};
    const gui_forms::TextMaskResult result = (*requests_).request(request, image.mask);
    if (result.status == gui_forms::TextMaskStatus::pending || result.status == gui_forms::TextMaskStatus::busy) {
        ready_ = false;
        return image;
    }
    require_success(result);
    const gui_forms::TextMaskMetrics metrics = image.mask.metrics();
    image.w = static_cast<int>(std::ceil(metrics.logical_width));
    image.h = static_cast<int>(std::ceil(metrics.logical_height));
    return image;
}
void blit_game_text(std::span<std::uint32_t> destination, const int width,
    const int height, const std::size_t stride, const TextImage& text,
    const int x, const int y, const double red, const double green,
    const double blue, const double alpha, const double magnification) {
    blit_game_text(destination, width, height, stride, text, x, y, red, green, blue, alpha, magnification,
                   GameTextClip{0, 0, width, height}, false);
}

GameTextClip game_text_bounds(const TextImage& text, const int x, const int y, const double magnification) {
    if (!text.mask.has_value()) { return {}; }
    const gui_forms::TextMaskMetrics metrics = text.mask.metrics();
    const double left = x + metrics.ink_left_px * magnification;
    const double top = y + metrics.ink_top_px * magnification;
    return {static_cast<int>(std::floor(left)), static_cast<int>(std::floor(top)),
            static_cast<int>(std::ceil(left + metrics.width_px * magnification)) + 1,
            static_cast<int>(std::ceil(top + metrics.height_px * magnification)) + 1};
}

void blit_game_text(std::span<std::uint32_t> destination, const int width,
    const int height, const std::size_t stride, const TextImage& text,
    const int x, const int y, const double red, const double green,
    const double blue, const double alpha, const double magnification,
    const GameTextClip clip, const bool rgba) {
    if (width < 0 || height < 0 || stride < static_cast<std::size_t>(width) ||
        (height > 0 && stride > destination.size() / static_cast<std::size_t>(height)) ||
        !std::isfinite(magnification) || magnification < .5 || magnification > 16 || !std::isfinite(red) ||
        !std::isfinite(green) || !std::isfinite(blue) || !std::isfinite(alpha)) {
        throw std::invalid_argument("Invalid game text destination or color");
    }
    if (!text.mask.has_value() || text.mask.coverage().empty() || width == 0 || height == 0) { return; }
    const gui_forms::TextMaskMetrics metrics = text.mask.metrics();
    const std::span<const std::uint8_t> coverage = text.mask.coverage();
    if (metrics.width_px > 4096 || metrics.height_px > 4096 || metrics.stride_bytes < metrics.width_px ||
        (metrics.height_px > 0 && metrics.stride_bytes > coverage.size() / metrics.height_px)) {
        throw std::invalid_argument("Invalid game text coverage shape");
    }
    const double left = x + metrics.ink_left_px * magnification;
    const double top = y + metrics.ink_top_px * magnification;
    const double right = left + metrics.width_px * magnification;
    const double bottom = top + metrics.height_px * magnification;
    const int begin_x = std::max(clip.x0, static_cast<int>(std::clamp(std::ceil(left - .5), 0.0, static_cast<double>(width))));
    const int end_x = std::min(clip.x1, static_cast<int>(std::clamp(std::ceil(right - .5), 0.0, static_cast<double>(width))));
    const int begin_y = std::max(clip.y0, static_cast<int>(std::clamp(std::ceil(top - .5), 0.0, static_cast<double>(height))));
    const int end_y = std::min(clip.y1, static_cast<int>(std::clamp(std::ceil(bottom - .5), 0.0, static_cast<double>(height))));
    const double opacity = std::clamp(alpha, 0.0, 1.0);
    // The channel stored low is blue in BGRA, red in RGBA.
    const double r = std::clamp(rgba ? blue : red, 0.0, 1.0);
    const double g = std::clamp(green, 0.0, 1.0);
    const double b = std::clamp(rgba ? red : blue, 0.0, 1.0);
    for (int row = begin_y; row < end_y; ++row) {
        const std::size_t source_row = static_cast<std::size_t>((row + .5 - top) / magnification) * metrics.stride_bytes;
        const std::size_t destination_row = static_cast<std::size_t>(row) * stride;
        for (int column = begin_x; column < end_x; ++column) {
            const std::size_t source_column = static_cast<std::size_t>((column + .5 - left) / magnification);
            const double a = coverage[source_row + source_column] * (opacity / 255.0);
            if (a == 0) { continue; }
            std::uint32_t& pixel = destination[destination_row + static_cast<std::size_t>(column)];
            const std::uint32_t bb = blend_channel(pixel & 255, b, a);
            const std::uint32_t gg = blend_channel((pixel >> 8) & 255, g, a);
            const std::uint32_t rr = blend_channel((pixel >> 16) & 255, r, a);
            pixel = bb | (gg << 8) | (rr << 16) | 0xFF000000U;
        }
    }
}
}
