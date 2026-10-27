#pragma once
// The shell's paper: the cream book paper the help document and the Settings screen
// are printed on, made once per window scale by the paper engine (shared/paper) as one
// seamless tile and kept as a window image, so a sheet of any size is one pattern
// fill. Before a window is attached, or if the toolkit refuses the image, the sheet
// is its flat colour, which is what it always was.
#include "presentation.hpp"
namespace games {
class PaperSheet {
  public:
    // The flat colour of the paper; what a plain pixel of the tile comes out as.
    [[nodiscard]] static gf::Color colour();
    // Makes the tile for `window` at its scale, or remakes it when the scale changed.
    void prepare(gf::Window& window);
    void release(gf::Window& window) noexcept;
    // Paints the paper over `bounds`.
    void paint(gf::Painter& painter, gf::Rect bounds) const;

  private:
    gf::ImageId image_{};
    double scale_ = 0;
    int pixels_ = 0;  // the tile's side in device pixels
};
} // namespace games
