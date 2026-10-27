#include "text_requests.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace games {
TextRequests::TextRequests(gui_forms::TextMaskSession& session, gui_forms::EncodedFontLease fonts)
    : session_(session), fonts_(std::move(fonts)) {}
TextRequests::~TextRequests() { static_cast<void>(cancel_all()); }

bool TextRequests::matches(const Pending& item, const gui_forms::TextMaskRequest& request) {
    const gui_forms::TextMaskRequest& options = item.options;
    const std::string_view text(item.text.data(), item.length);
    const bool equal = item.occupied && text == request.utf8 &&
        options.primary_face == request.primary_face && options.size == request.size &&
        options.wrap_width == request.wrap_width && options.device_scale == request.device_scale &&
        options.raster == request.raster;
    return equal;
}
void TextRequests::retire(Pending& item) {
    item.mask.reset();
    item.length = 0;
    item.options = {};
    item.id = {};
    item.result = {};
    item.occupied = false;
    item.completed = false;
}
gui_forms::TextMaskResult TextRequests::request(const gui_forms::TextMaskRequest& input,
    gui_forms::TextMaskLease& output) {
    using gui_forms::TextMaskStatus;
    using gui_forms::TextMaskLimit;
    if (std::this_thread::get_id() != executor_) { return {TextMaskStatus::wrong_executor}; }
    const gui_forms::TextMaskSessionSnapshot snapshot = session_.snapshot();
    if (snapshot.closing) {
        for (Pending& item : pending_) { retire(item); }
        return {TextMaskStatus::closing};
    }
    if (input.utf8.size() > input_limit) {
        return {TextMaskStatus::limit_exceeded, TextMaskLimit::input_bytes};
    }
    if (input.utf8.data() == nullptr && !input.utf8.empty()) { return {TextMaskStatus::invalid_input}; }
    for (std::size_t index = 0; index < input.utf8.size(); ++index) {
        const char byte = input.utf8[index];
        const bool isolated_cr = byte == '\r' &&
            (index + 1 == input.utf8.size() || input.utf8[index + 1] != '\n');
        normalized_[index] = isolated_cr ? '\n' : byte;
    }
    gui_forms::TextMaskRequest request = input;
    request.utf8 = std::string_view(normalized_.data(), input.utf8.size());
    for (Pending& item : pending_) {
        if (!matches(item, request)) { continue; }
        if (!item.completed) { return {TextMaskStatus::pending}; }
        const gui_forms::TextMaskResult result = item.result;
        if (result.status == TextMaskStatus::success) { output = item.mask; }
        retire(item);
        return result;
    }
    const gui_forms::TextMaskResult cached = session_.lookup(fonts_, request, output);
    if (cached.status != TextMaskStatus::cache_miss) { return cached; }
    Pending* available = nullptr;
    for (Pending& item : pending_) {
        if (!item.occupied) { available = &item; break; }
    }
    if (available == nullptr) { return {TextMaskStatus::busy, TextMaskLimit::request_slots}; }
    gui_forms::TextMaskRequestId id{};
    const gui_forms::TextMaskResult submitted = session_.submit(fonts_, request, id);
    if (submitted.status != TextMaskStatus::success) { return submitted; }
    Pending& item = *available;
    std::copy(request.utf8.begin(), request.utf8.end(), item.text.begin());
    item.length = request.utf8.size();
    item.options = request;
    item.options.utf8 = {};
    item.id = id;
    item.completed = false;
    item.occupied = true;
    return {TextMaskStatus::pending};
}
gui_forms::TextMaskResult TextRequests::poll() {
    using gui_forms::TextMaskStatus;
    if (std::this_thread::get_id() != executor_) { return {TextMaskStatus::wrong_executor}; }
    const gui_forms::TextMaskSessionSnapshot snapshot = session_.snapshot();
    if (snapshot.closing) {
        for (Pending& item : pending_) { retire(item); }
        return {TextMaskStatus::closing};
    }
    for (const gui_forms::TextMaskRequestSnapshot& slot : snapshot.requests) {
        if (slot.state != gui_forms::TextMaskSlot::completed) { continue; }
        for (Pending& item : pending_) {
            if (!item.occupied || item.completed || item.id.session != slot.id.session ||
                item.id.serial != slot.id.serial) { continue; }
            const gui_forms::TextMaskResult result = session_.take(item.id, item.mask);
            if (result.status == TextMaskStatus::pending) { continue; }
            item.result = result;
            item.completed = true;
        }
    }
    return {TextMaskStatus::success};
}
gui_forms::TextMaskResult TextRequests::cancel_all() {
    using gui_forms::TextMaskStatus;
    if (std::this_thread::get_id() != executor_) { return {TextMaskStatus::wrong_executor}; }
    for (Pending& item : pending_) {
        if (!item.occupied) { continue; }
        if (!item.completed) {
            const gui_forms::TextMaskCancellation result = session_.cancel(item.id);
            if (result.result.status != TextMaskStatus::success &&
                result.result.status != TextMaskStatus::stale) { return result.result; }
        }
        retire(item);
    }
    return {TextMaskStatus::success};
}
std::size_t TextRequests::outstanding() const {
    if (std::this_thread::get_id() != executor_) {
        throw std::logic_error("Games text requests belong to their opening executor");
    }
    std::size_t count = 0;
    for (const Pending& item : pending_) { if (item.occupied) { ++count; } }
    return count;
}
}
