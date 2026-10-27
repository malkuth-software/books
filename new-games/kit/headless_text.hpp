#pragma once
// Headless text for the new-games harness: rasterises the PlaySuite fonts
// (assets/fonts) to alpha masks without GUI.Forms, so a game's frames can be
// rendered to PNG files and looked at on any machine with only a C++ compiler.
//
// This is a development aid. It is never linked into PlaySuite, where text comes
// from the GUI.Forms text service. Glyph outlines are the same font files; line
// breaking and kerning are simpler here, so widths can differ from the
// application by a pixel or two. Leave that much slack in a layout.
#include <cstdint>
#include <string>
#include <vector>

namespace kit {

struct TextMask {
    int w = 0;
    int h = 0;
    std::vector<std::uint8_t> a;  // w * h coverage, row-major
};

// The four faces PlaySuite's game text service offers, in its order.
enum class Face { serif, serif_bold, mono, mono_bold };

// Renders UTF-8 text. `size` is the em height in pixels. `wrap_width` 0 keeps one
// line (explicit '\n' still breaks). `aliased` thresholds coverage, as the
// application does for its pixel face. Throws std::runtime_error when the font
// files cannot be read.
[[nodiscard]] TextMask render_text(const std::string& utf8, Face face, double size,
                                   double wrap_width, bool aliased);

// Where the fonts are read from: the PLAYSUITE_FONT_DIR environment variable if
// set, else the directory compiled in as KIT_FONT_DIR.
[[nodiscard]] std::string font_directory();

}  // namespace kit
