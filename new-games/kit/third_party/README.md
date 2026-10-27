# Third-party code in the kit

`stb_truetype.h` is version 1.26 of Sean Barrett's single-file TrueType
rasteriser, from https://github.com/nothings/stb, unmodified. It is offered by its
author under either the MIT license or as public domain; the full text of both is
at the end of the file.

It is used only by `../headless_text.cpp`, so that a game's frames can be rendered
to PNG files with the real fonts on a machine without GUI.Forms. It is a
development aid. It is not compiled into PlaySuite and does not ship in any
package.
