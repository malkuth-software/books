#pragma once
#include "text_requests.hpp"
#include <algorithm>
#include <filesystem>
#include <string_view>

namespace games {
// How many bytes of `text` show `typed` bytes' worth of it, rounded up to a whole
// character. A speech bubble that types its line out must never ask for half of a character
// beyond ASCII (a note, a curly quote, an accent): the text service refuses broken UTF-8,
// and an exception out of a timer's tick stops that timer for good.
[[nodiscard]] inline int whole_characters(std::string_view text, int typed) {
    std::size_t at = static_cast<std::size_t>(std::clamp(typed, 0, static_cast<int>(text.size())));
    while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xC0U) == 0x80U)
        ++at;
    return static_cast<int>(at);
}

struct TextImage final {
    // Logical advance and line height, rounded outward for game layout.
    int w{};
    int h{};
    gui_forms::TextMaskLease mask{};
};

// One view owns this service, bank and session on its UI executor. A render
// attempt calls begin(), requests every needed mask, then checks ready().
// A pending attempt keeps its model snapshot; it must not publish its pixels.
class GameText final {
public:
    GameText(const std::filesystem::path& fonts, const std::string& dialogue_family);
    void begin();
    void cancel();
    [[nodiscard]] TextImage get(const std::string_view source, const bool bold,
        const double size, const double wrap, const double scale,
        const bool mono = false);
    [[nodiscard]] bool ready() const { return ready_; }
private:
    // Destruction is reverse declaration order: broker, session, bank, service.
    gui_forms::TextMaskService service_{};
    gui_forms::EncodedFontLease fonts_{};
    std::unique_ptr<gui_forms::TextMaskSession> session_{};
    std::unique_ptr<TextRequests> requests_{};
    bool ready_{false};
};

// Blend native coverage into a complete, opaque BGRA game surface. The span
// owns no pixels and must contain height rows of stride pixels. Signed ink
// bearings are relative to the supplied logical top-left, in device pixels.
void blit_game_text(std::span<std::uint32_t> destination, const int width,
    const int height, const std::size_t stride, const TextImage& text,
    const int x, const int y, const double red, const double green,
    const double blue, const double alpha, const double magnification = 1);
// A rectangle of window pixels [x0, x1) x [y0, y1).
struct GameTextClip {
    int x0{};
    int y0{};
    int x1{};
    int y1{};
};
// The same, drawn only inside `clip`, into BGRA or (when `rgba`) RGBA pixels.
void blit_game_text(std::span<std::uint32_t> destination, int width, int height, std::size_t stride,
    const TextImage& text, int x, int y, double red, double green, double blue, double alpha,
    double magnification, GameTextClip clip, bool rgba);
// The window pixels a text blitted at (x, y) can touch.
[[nodiscard]] GameTextClip game_text_bounds(const TextImage& text, int x, int y, double magnification = 1);
}
