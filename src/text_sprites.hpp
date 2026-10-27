#pragma once
#include "game_text.hpp"
#include "gui_forms/basic_controls.hpp"
#include <functional>
#include <map>
#include <memory>
#include <string>
namespace games {
namespace gf = gui_forms;

// Display lettering in the game typefaces (Barlow Condensed, Libre Baskerville) for
// ordinary painted controls. Masks are shaped asynchronously by the shared text
// service and kept as premultiplied images. Painting never waits: until a sprite is
// ready, draw() paints the same words in the host face, and update() reports when
// a repaint will show the lettering.
enum class Face { condensed, serif };
struct SpriteSpec {
    std::string text;
    Face face = Face::condensed;
    bool bold = true;
    double size = 16;
    gf::Color color = gf::Color::rgba(255, 255, 255);
};
class TextSprites {
  public:
    TextSprites();
    ~TextSprites();
    TextSprites(const TextSprites&) = delete;
    TextSprites& operator=(const TextSprites&) = delete;
    // Paints at the logical line box whose top-left is origin. Returns the advance width.
    double draw(gf::Painter& p, const SpriteSpec& spec, gf::Point origin);
    // Width and height of the line box; estimated until the mask is shaped.
    gf::Size measure(const SpriteSpec& spec);
    // Call outside painting (from a timer). Returns true when new lettering became ready.
    bool update(gf::Window& window);
    [[nodiscard]] bool waiting() const;
    void release(gf::Window& window);
    std::function<void()> request_update;

  private:
    struct Sprite {
        gf::ImageId image{};
        double width = 0, height = 0;
        gf::Rect ink{};
    };
    std::string key(const SpriteSpec& spec) const;
    std::map<std::string, Sprite> ready_;
    std::map<std::string, SpriteSpec> wanted_;
    std::unique_ptr<GameText> condensed_, serif_;
    bool failed_ = false;
    double scale_ = 1;
};
} // namespace games
