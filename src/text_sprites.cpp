#include "text_sprites.hpp"
#include "gui_forms/window.hpp"
#include "runtime_paths.hpp"
#include <cmath>
#include <cstdio>
#include <vector>
namespace games {
TextSprites::TextSprites() = default;
TextSprites::~TextSprites() = default;
std::string TextSprites::key(const SpriteSpec& s) const {
    char buffer[96];
    std::snprintf(buffer, sizeof buffer, "|%d|%d|%.2f|%02x%02x%02x%02x|%.3f",
                  static_cast<int>(s.face), s.bold ? 1 : 0, s.size, s.color.red, s.color.green,
                  s.color.blue, s.color.alpha, scale_);
    return s.text + buffer;
}
double TextSprites::draw(gf::Painter& p, const SpriteSpec& spec, gf::Point origin) {
    const std::string k = key(spec);
    std::map<std::string, Sprite>::const_iterator found = ready_.find(k);
    if (found != ready_.end()) {
        const Sprite& s = (*found).second;
        if (s.image.value)
            p.draw_image(s.image,
                         {origin.x + s.ink.x, origin.y + s.ink.y, s.ink.width, s.ink.height});
        return s.width;
    }
    if (!failed_ && wanted_.emplace(k, spec).second && request_update)
        request_update();
    // The host face stands in until the lettering is shaped.
    gf::FontSpec f{gf::FontRole::content, spec.size * (spec.face == Face::condensed ? .86 : .92),
                   static_cast<std::uint16_t>(spec.bold ? 700 : 400), false};
    p.draw_text_utf8({origin.x, origin.y + spec.size * .92}, spec.text, f, spec.color);
    return p.measure_text_utf8(spec.text, f).width;
}
gf::Size TextSprites::measure(const SpriteSpec& spec) {
    const std::string k = key(spec);
    std::map<std::string, Sprite>::const_iterator found = ready_.find(k);
    if (found != ready_.end())
        return {(*found).second.width, (*found).second.height};
    if (!failed_ && wanted_.emplace(k, spec).second && request_update)
        request_update();
    const double per_char = spec.face == Face::condensed ? .43 : .58;
    return {static_cast<double>(spec.text.size()) * spec.size * per_char, spec.size * 1.2};
}
bool TextSprites::waiting() const {
    return !wanted_.empty();
}
bool TextSprites::update(gf::Window& window) {
    if (failed_ || wanted_.empty())
        return false;
    if (window.scale() != scale_) {
        release(window);
        scale_ = window.scale();
        return true; // keys changed; painting will ask again at the new scale
    }
    try {
        const std::filesystem::path fonts = std::filesystem::path(asset_directory()) / "fonts";
        if (!condensed_)
            condensed_ = std::make_unique<GameText>(fonts, "BarlowCondensed");
        if (!serif_)
            serif_ = std::make_unique<GameText>(fonts, "LibreBaskerville");
        (*condensed_).begin();
        (*serif_).begin();
    } catch (...) {
        failed_ = true;
        wanted_.clear();
        return false;
    }
    bool loaded = false;
    for (std::map<std::string, SpriteSpec>::iterator it = wanted_.begin(); it != wanted_.end();) {
        const SpriteSpec& spec = (*it).second;
        GameText& text = spec.face == Face::condensed ? *condensed_ : *serif_;
        TextImage image{};
        try {
            image = text.get(spec.text, spec.bold, spec.size, 0, scale_);
        } catch (...) {
            it = wanted_.erase(it);
            continue;
        }
        if (!image.mask.has_value()) {
            ++it;
            continue;
        }
        const gui_forms::TextMaskMetrics m = image.mask.metrics();
        Sprite sprite;
        sprite.width = m.logical_width;
        sprite.height = m.logical_height;
        const std::span<const std::uint8_t> coverage = image.mask.coverage();
        if (m.width_px > 0 && m.height_px > 0 && m.width_px <= 4096 && m.height_px <= 4096) {
            std::vector<std::byte> pixels(static_cast<std::size_t>(m.width_px) * m.height_px * 4);
            for (std::uint32_t y = 0; y < m.height_px; ++y)
                for (std::uint32_t x = 0; x < m.width_px; ++x) {
                    const double a =
                        coverage[y * m.stride_bytes + x] / 255.0 * spec.color.alpha / 255.0;
                    std::byte* px = &pixels[(static_cast<std::size_t>(y) * m.width_px + x) * 4];
                    px[0] = static_cast<std::byte>(std::lround(spec.color.blue * a));
                    px[1] = static_cast<std::byte>(std::lround(spec.color.green * a));
                    px[2] = static_cast<std::byte>(std::lround(spec.color.red * a));
                    px[3] = static_cast<std::byte>(std::lround(255 * a));
                }
            gf::ImageLoadResult result =
                window.load_bgra32_premultiplied(m.width_px, m.height_px, m.width_px * 4, pixels);
            if (result) {
                sprite.image = result.image;
                sprite.ink = {m.ink_left_px / scale_, m.ink_top_px / scale_, m.width_px / scale_,
                              m.height_px / scale_};
            }
        }
        ready_.emplace((*it).first, sprite);
        it = wanted_.erase(it);
        loaded = true;
    }
    return loaded;
}
void TextSprites::release(gf::Window& window) {
    for (const std::pair<const std::string, Sprite>& entry : ready_)
        if (entry.second.image.value)
            static_cast<void>(window.remove_image(entry.second.image));
    ready_.clear();
    wanted_.clear();
}
} // namespace games
