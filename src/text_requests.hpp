#pragma once
#include "gui_forms/text_mask.hpp"
#include <array>
#include <thread>

namespace games {
// One queue per view and font bank, used only on its opening executor. The
// borrowed session must outlive this queue; this queue exclusively submits and
// consumes its requests. The owner may close the session. Wake delivery belongs to the
// view/session owner; poll never waits for rendering. No local layout or raster.
class TextRequests final {
public:
    TextRequests(gui_forms::TextMaskSession& session, gui_forms::EncodedFontLease fonts);
    ~TextRequests();
    TextRequests(const TextRequests&) = delete;
    TextRequests& operator=(const TextRequests&) = delete;
    // Failure/pending preserves output. Call poll on a UI wake or before a frame.
    [[nodiscard]] gui_forms::TextMaskResult request(
        const gui_forms::TextMaskRequest& input, gui_forms::TextMaskLease& output);
    [[nodiscard]] gui_forms::TextMaskResult poll();
    [[nodiscard]] gui_forms::TextMaskResult cancel_all();
    [[nodiscard]] std::size_t outstanding() const;
private:
    static constexpr std::size_t input_limit = 16'384;
    struct Pending final {
        std::array<char, input_limit> text{};
        std::size_t length{};
        gui_forms::TextMaskRequest options{}; // utf8 stays empty; text owns bytes.
        gui_forms::TextMaskRequestId id{};
        gui_forms::TextMaskResult result{};
        gui_forms::TextMaskLease mask{};
        bool occupied{};
        bool completed{};
    };
    [[nodiscard]] static bool matches(const Pending& item,
        const gui_forms::TextMaskRequest& request);
    static void retire(Pending& item);
    gui_forms::TextMaskSession& session_;
    gui_forms::EncodedFontLease fonts_{};
    const std::thread::id executor_{std::this_thread::get_id()};
    // Exactly 144 KiB of text capacity. Completed results share these nine
    // records with pending work; there is no extra completion queue or LRU.
    std::array<Pending, 9> pending_{};
    // Reused normalization workspace; only the admitted input length is read.
    std::array<char, input_limit> normalized_{};
};
}
