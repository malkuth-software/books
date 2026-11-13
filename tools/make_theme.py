#!/usr/bin/env python3
"""Write PlaySuite's GUI.Forms theme package from the shell's colours.

    python3 tools/make_theme.py          rewrite themes/playsuite/
    python3 tools/make_theme.py --check  exit 1 if themes/playsuite/ is out of date

PlaySuite's look (the paymenottowork gloss, the game panels' skins, the gold settings
controls, the help sheet's headings and links, and the editors of each skin) used to be
painted by each control in C++. It is now a theme package: the controls paint their parts
through GUI.Forms (paint_part), and this sheet gives every part its colours, gradients,
borders and shadows. The geometry stays in the controls (where a face sits inside its
button, a round button's circle, the pressed offset), as the theme language leaves layout
to the program.

The colours below are the values the C++ painters used, and the state colours are
computed here exactly as they were computed there: the gloss's hover, pressed and
disabled stops (`paint_gloss`, src/suite.cpp) and the game buttons' (`GameButton`,
src/presentation.cpp), including each one's rounding. Every text and edge colour is a
palette role, so that forced high contrast can replace it by its meaning.

Where the theme language cannot say exactly what the painter drew, the sheet carries
the nearest form and docs/TOOLKIT_REQUESTS.md ("Theme gaps") names the missing feature.
"""
from __future__ import annotations

import argparse
import hashlib
import itertools
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PACKAGE = ROOT / "themes" / "playsuite"
SHEET = "playsuite.theme.css"
VERSION = "1.0.0"

Colour = tuple[int, int, int, int]


def rgb(r: int, g: int, b: int, a: int = 255) -> Colour:
    return (r, g, b, a)


def hex_colour(c: Colour) -> str:
    if c[3] == 255:
        return "#%02x%02x%02x" % c[:3]
    return "#%02x%02x%02x%02x" % c


def mix_rounded(a: Colour, b: Colour, t: float) -> Colour:
    """mix_color in src/suite.cpp: each channel rounded to nearest (std::lround)."""
    t = min(max(t, 0.0), 1.0)
    return tuple(int(x * (1 - t) + y * t + 0.5) for x, y in zip(a, b))  # type: ignore[return-value]


def mix_truncated(a: Colour, b: Colour, t: float) -> Colour:
    """mix in src/presentation.cpp: each channel truncated by static_cast."""
    return tuple(int(x * (1 - t) + y * t) for x, y in zip(a, b))  # type: ignore[return-value]


class Sheet:
    """The palette and the rules, in the order they are written. PlaySuite's named colours
    are custom properties defined in :root (GUI.Forms' palette roles are a fixed set)."""

    def __init__(self) -> None:
        self.roles: dict[str, Colour] = {}
        self.blocks: list[str] = []

    def role(self, name: str, colour: Colour) -> str:
        known = self.roles.get(name)
        if known is not None and known != colour:
            raise ValueError(f"role {name} given two colours")
        self.roles[name] = colour
        return f"var(--{name})"

    def comment(self, text: str) -> None:
        self.blocks.append("\n/* " + text + " */")

    def rule(self, selector: str, declarations: list[str]) -> None:
        body = "; ".join(declarations)
        self.blocks.append(f"{selector} {{ {body}; }}")

    def text(self) -> str:
        # A rule holds at most 64 declarations, so the palette is several :root blocks
        # (the engine reads them as one palette); the first also names the scheme.
        lines = [f"  --{name}: {hex_colour(colour)};" for name, colour in self.roles.items()]
        palette = ""
        for start in range(0, max(len(lines), 1), PALETTE_BLOCK):
            scheme = "  color-scheme: light;\n" if start == 0 else ""
            palette += ":root {\n" + scheme + "\n".join(lines[start:start + PALETTE_BLOCK]) + "\n}\n"
        header = (
            "/* PlaySuite's look as a GUI.Forms theme package. Written by tools/make_theme.py\n"
            "   from the shell's colours; edit the script, not this file. */\n" + palette)
        return header + "\n".join(self.blocks) + "\n"


# Role declarations in one :root block (GUI.Forms admits 64 declarations a rule).
PALETTE_BLOCK = 60


def pseudo(states: tuple[str, ...]) -> str:
    return "".join(":" + state for state in states)


def subsets(names: list[str]) -> list[tuple[str, ...]]:
    result: list[tuple[str, ...]] = []
    for size in range(len(names) + 1):
        result.extend(itertools.combinations(names, size))
    return result


def combo_name(states: tuple[str, ...]) -> str:
    return "-".join(states) if states else "rest"


# The gloss: paint_gloss in src/suite.cpp -----------------------------------------------

GLOSS_TONES: dict[str, dict[str, object]] = {
    "chrome": {
        "rest": [rgb(255, 255, 255), rgb(219, 227, 239), rgb(189, 201, 220), rgb(230, 235, 243)],
        "hot": [rgb(255, 255, 255), rgb(238, 243, 255), rgb(215, 227, 255), rgb(255, 255, 255)],
        "edge": rgb(100, 114, 139),
        "ink": rgb(23, 43, 83),
    },
    "gold": {
        "rest": [rgb(255, 244, 214), rgb(255, 226, 160), rgb(242, 199, 102), rgb(255, 233, 180)],
        "hot": [rgb(255, 250, 230), rgb(255, 234, 178), rgb(248, 210, 120), rgb(255, 240, 200)],
        "edge": rgb(138, 106, 34),
        "ink": rgb(59, 42, 7),
    },
    "navy": {
        "rest": [rgb(90, 127, 214), rgb(42, 78, 168), rgb(23, 52, 127), rgb(58, 95, 189)],
        "hot": [rgb(112, 148, 232), rgb(58, 96, 190), rgb(34, 66, 150), rgb(76, 114, 208)],
        "edge": rgb(10, 24, 64),
        "ink": rgb(255, 255, 255),
    },
    "smoke": {
        "rest": [rgb(88, 100, 128, 235), rgb(58, 68, 92, 235), rgb(40, 48, 68, 235), rgb(52, 62, 84, 235)],
        "hot": [rgb(112, 126, 158, 245), rgb(76, 88, 116, 245), rgb(54, 64, 88, 245), rgb(70, 82, 108, 245)],
        "edge": rgb(255, 255, 255, 80),
        "ink": rgb(242, 244, 248),
    },
}
GLOSS_SHADOW = "0 2px 2px " + hex_colour(rgb(10, 18, 40, 70))
GLOSS_RING = rgb(255, 210, 122)
GLOSS_DISABLED_TOWARD = (150, 156, 168)


def gloss_stops(tone: str, hot: bool, down: bool, disabled: bool) -> list[Colour]:
    data = GLOSS_TONES[tone]
    stops: list[Colour] = list(data["hot" if hot else "rest"])  # type: ignore[arg-type]
    if down:
        stops = [stops[2], stops[2], stops[1], stops[0]]
    if disabled:
        stops = [mix_rounded(c, (*GLOSS_DISABLED_TOWARD, c[3]), 0.45) for c in stops]
    return stops


def gloss_gradient(stops: list[Colour]) -> str:
    return ("linear-gradient(to bottom, %s 0%%, %s 48%%, %s 52%%, %s 100%%)"
            % tuple(hex_colour(c) for c in stops))


CLEAR = hex_colour(rgb(255, 255, 255, 0))


def shine_keylines(alpha: int) -> str:
    """The bright line a little under a flat top (paint_gloss draws it at the face's
    y + 2): the second keyline ring's top, the first ring clear. paint_gloss also kept
    it radius * .6 + 2 in from each end and faded it in over min(10, a quarter of its
    length); keylines run the face's full width (gap: TOOLKIT_REQUESTS "Theme gaps")."""
    lit = hex_colour(rgb(255, 255, 255, alpha))
    return f"keylines: {CLEAR}, {lit} {CLEAR} {CLEAR}"


def gloss_face(sheet: Sheet, tone: str, hot: bool, down: bool, disabled: bool, shine: bool) -> list[str]:
    stops = gloss_stops(tone, hot, down, disabled)
    edge = sheet.role(f"gloss-{tone}-edge", GLOSS_TONES[tone]["edge"])  # type: ignore[arg-type]
    shadow = GLOSS_SHADOW if not down and not disabled else "none"
    declarations = [f"background: {gloss_gradient(stops)}", f"border: 1px solid {edge}", f"box-shadow: {shadow}"]
    if shine:
        alpha = 40 if tone == "smoke" else 30 if down else 150
        declarations.append(shine_keylines(alpha))
    else:
        declarations.append("keylines: none")
    return declarations


def write_gloss(sheet: Sheet) -> None:
    sheet.comment("The gloss (SuiteButton: the capsule, the shelf's switches, the settings chips, "
                  "Rock Stack's buttons). A button.gloss carries its tone as a class; lit (checked) "
                  "is gold whatever the tone; a pill or a circle (rounded-end) has no shine line.")
    states = ["hover", "active", "disabled"]
    for lit in (False, True):
        tones = ["gold"] if lit else list(GLOSS_TONES)
        for tone in tones:
            for rounded in (False, True):
                ends = ".rounded-end" if rounded else ":not(.rounded-end)"
                base = f"button.gloss:checked{ends}" if lit else f"button.gloss.{tone}{ends}"
                for combo in subsets(states):
                    hot = "hover" in combo
                    down = "active" in combo
                    disabled = "disabled" in combo
                    sheet.rule(f"{base}{pseudo(combo)}::part(face)",
                               gloss_face(sheet, tone, hot, down, disabled, not rounded))
            ink = sheet.role(f"gloss-{tone}-ink", GLOSS_TONES[tone]["ink"])  # type: ignore[arg-type]
            faded = GLOSS_TONES[tone]["ink"]
            ink_disabled = sheet.role(f"gloss-{tone}-ink-disabled", (*faded[:3], 110))  # type: ignore[index]
            label_base = "button.gloss:checked" if lit else f"button.gloss.{tone}"
            sheet.rule(f"{label_base}::part(label)", [f"color: {ink}"])
            sheet.rule(f"{label_base}:disabled::part(label)", [f"color: {ink_disabled}"])
    ring = sheet.role("gloss-ring", GLOSS_RING)
    sheet.rule("button.gloss::part(focus)",
               [f"border: 2px solid {ring}", "background: transparent", "box-shadow: none"])


# The game panels' buttons: GameButton in src/presentation.cpp --------------------------

GAME_SKINS = ["slate", "ivory", "blue", "mint"]


def game_button_colours(skin: str, chosen: bool, hot: bool, down: bool, disabled: bool) -> dict[str, Colour]:
    light = skin in ("ivory", "blue")
    top = rgb(247, 246, 235) if light else rgb(56, 79, 99)
    bottom = rgb(212, 218, 202) if light else rgb(30, 49, 69)
    border = rgb(119, 145, 136) if light else rgb(82, 107, 126)
    ink = rgb(39, 65, 60) if light else rgb(226, 236, 242)
    if skin == "blue":
        top, bottom, border, ink = rgb(244, 251, 255), rgb(197, 223, 241), rgb(113, 157, 191), rgb(31, 81, 120)
    if skin == "mint":
        top, bottom, border = rgb(77, 109, 102), rgb(39, 68, 66), rgb(125, 161, 137)
    if chosen:
        top, bottom, border, ink = rgb(235, 212, 153), rgb(189, 150, 82), rgb(130, 100, 49), rgb(45, 49, 49)
    if hot:
        top = mix_truncated(top, rgb(245, 246, 229), 0.12)
        border = mix_truncated(border, rgb(181, 220, 225), 0.55)
    if down:
        top, bottom = bottom, top
    if disabled:
        top = mix_truncated(top, bottom, 0.5)
        ink = mix_truncated(ink, bottom, 0.55)
        border = mix_truncated(border, bottom, 0.4)
    highlight = rgb(255, 255, 255, 20 if down else 195 if light else 53)
    lowlight = rgb(6, 20, 35, 30 if light else 90)
    focus = rgb(45, 103, 131) if light else rgb(170, 216, 233)
    return {"top": top, "bottom": bottom, "border": border, "ink": ink, "highlight": highlight,
            "lowlight": lowlight, "focus": focus}


def write_game_buttons(sheet: Sheet) -> None:
    sheet.comment("The game panels' buttons (GameButton: the card table, the puzzles). A "
                  "button.game-button carries its skin as a class; chosen is :checked.")
    states = ["checked", "hover", "active", "disabled"]
    for skin in GAME_SKINS:
        for combo in subsets(states):
            c = game_button_colours(skin, "checked" in combo, "hover" in combo, "active" in combo,
                                    "disabled" in combo)
            name = f"game-{skin}-{combo_name(combo)}"
            border = sheet.role(name + "-edge", c["border"])
            ink = sheet.role(name + "-ink", c["ink"])
            down = "active" in combo
            body = f"linear-gradient(to bottom, {hex_colour(c['top'])}, {hex_colour(c['bottom'])})"
            shadow = "none" if down else "0 2px 2px " + hex_colour(rgb(0, 8, 17, 70))
            # The top highlight (face y + 1) and the bottom lowlight (face bottom - 1) are
            # the first keyline ring's top and bottom; GameButton kept them 5 and 4 points
            # in from the ends, where keylines run the full width.
            keylines = (f"keylines: {hex_colour(c['highlight'])} {CLEAR} {hex_colour(c['lowlight'])} {CLEAR}")
            selector = f"button.game-button.{skin}{pseudo(combo)}"
            sheet.rule(selector + "::part(face)",
                       [f"background: {body}", f"border: 1px solid {border}", f"box-shadow: {shadow}", keylines])
            sheet.rule(selector + "::part(label)", [f"color: {ink}"])
        focus = sheet.role(f"game-{skin}-focus", game_button_colours(skin, False, False, False, False)["focus"])
        sheet.rule(f"button.game-button.{skin}::part(focus)",
                   [f"border: 1px solid {focus}", "background: transparent", "box-shadow: none"])


# The Settings screen: SettingSlider, SettingCheck, CardBackChoice (src/settings_sheet.cpp)

SETTINGS_INK = rgb(42, 38, 24)


def write_settings(sheet: Sheet) -> None:
    sheet.comment("The Settings screen's gold slider (track-bar.setting), gold box "
                  "(check-box.setting) and card backs (button.card-back).")
    groove_edge = sheet.role("setting-groove-edge", rgb(173, 151, 88))
    sheet.rule("track-bar.setting::part(track)",
               [f"background: {hex_colour(rgb(226, 214, 166))}", f"border: 1px solid {groove_edge}",
                "box-shadow: none"])
    sheet.rule("track-bar.setting::part(fill)",
               [f"background: {hex_colour(rgb(201, 146, 40))}", "border: none", "box-shadow: none"])
    sheet.rule("track-bar.setting:disabled::part(fill)", [f"background: {hex_colour(rgb(190, 180, 150))}"])
    knob_shadow = "0 1px 3px " + hex_colour(rgb(0, 0, 0, 80))
    for disabled in (False, True):
        face = gloss_face(sheet, "gold", False, False, disabled, False)
        shadows = knob_shadow if disabled else f"{GLOSS_SHADOW}, {knob_shadow}"
        face = [d for d in face if not d.startswith("box-shadow")] + [f"box-shadow: {shadows}"]
        sheet.rule("track-bar.setting" + (":disabled" if disabled else "") + "::part(thumb)", face)
    slider_focus = sheet.role("setting-slider-focus", rgb(184, 137, 42))
    sheet.rule("track-bar.setting::part(focus)",
               [f"border: 2px solid {slider_focus}", "background: transparent", "box-shadow: none"])

    box_edge = sheet.role("setting-box-edge", rgb(166, 140, 74))
    sheet.rule("check-box.setting::part(indicator)",
               [f"background: {hex_colour(rgb(255, 253, 240))}", f"border: 1.5px solid {box_edge}",
                "box-shadow: none"])
    sheet.rule("check-box.setting:hover::part(indicator)", [f"background: {hex_colour(rgb(255, 246, 214))}"])
    # A checked box is the gold gloss at radius 4, its shine line included.
    for combo in subsets(["hover", "active", "disabled"]):
        sheet.rule(f"check-box.setting:checked{pseudo(combo)}::part(indicator)",
                   gloss_face(sheet, "gold", "hover" in combo, "active" in combo, "disabled" in combo, True))
    ink = sheet.role("settings-ink", SETTINGS_INK)
    ink_disabled = sheet.role("settings-ink-disabled", rgb(150, 140, 110))
    sheet.rule("check-box.setting::part(label)", [f"color: {ink}"])
    sheet.rule("check-box.setting:disabled::part(label)", [f"color: {ink_disabled}"])
    box_focus = sheet.role("setting-box-focus", rgb(214, 160, 40, 200))
    sheet.rule("check-box.setting::part(focus)",
               [f"border: 2px solid {box_focus}", "background: transparent", "box-shadow: none"])

    hover_ring = sheet.role("card-back-hover", rgb(184, 137, 42, 150))
    chosen_ring = sheet.role("card-back-chosen", rgb(214, 160, 40))
    sheet.rule("button.card-back::part(face)",
               [f"border: 2px solid {hover_ring}", "background: transparent", "box-shadow: none"])
    sheet.rule("button.card-back:checked::part(face)", [f"border: 3px solid {chosen_ring}"])
    card_focus = sheet.role("card-back-focus", rgb(214, 160, 40, 200))
    sheet.rule("button.card-back::part(focus)",
               [f"border: 1px solid {card_focus}", "background: transparent", "box-shadow: none"])


# Help: HelpGlyph and TopicHeading (src/help_book.cpp) ----------------------------------

def write_help(sheet: Sheet) -> None:
    sheet.comment("Help's links (button.help-glyph: the ? on the shelf, the close marks) and "
                  "topic headings (button.topic-heading).")
    sheet.rule("button.help-glyph", ["text-shadow: 1px 1.5px 0 " + hex_colour(rgb(0, 0, 0, 110))])
    inks = {"": rgb(184, 137, 42), ".help-link": rgb(240, 192, 84), ".card-mark": rgb(228, 220, 196)}
    names = {"": "help-glyph-ink", ".help-link": "help-link-ink", ".card-mark": "card-mark-ink"}
    for variant, colour in inks.items():
        role = sheet.role(names[variant], colour)
        sheet.rule(f"button.help-glyph{variant}", [f"glyph-color: {role}"])
        sheet.rule(f"button.help-glyph{variant}::part(label)", [f"color: {role}"])
    # Hovered, the glyph lights; its underline keeps the ink (glyph-color).
    hot = sheet.role("help-glyph-hot", rgb(255, 224, 140))
    sheet.rule("button.help-glyph:hover::part(label)", [f"color: {hot}"])
    sheet.rule("button.help-glyph.help-link:hover::part(label)", [f"color: {hot}"])
    sheet.rule("button.help-glyph.card-mark:hover::part(label)", [f"color: {hot}"])

    sheet.rule("button.topic-heading::part(face)",
               [f"background: {hex_colour(rgb(244, 235, 175))}", "border: none", "box-shadow: none"])
    sheet.rule("button.topic-heading:hover::part(face)", [f"background: {hex_colour(rgb(236, 226, 156))}"])
    title = sheet.role("help-ink", SETTINGS_INK)
    mark = sheet.role("help-mark", rgb(66, 58, 33))
    sheet.rule("button.topic-heading::part(label)", [f"color: {title}", f"muted-color: {mark}"])
    edge = sheet.role("help-heading-focus", rgb(110, 91, 37))
    sheet.rule("button.topic-heading::part(focus)",
               [f"border: 1px solid {edge}", "background: transparent", "box-shadow: none"])
    # The help and settings sheets' labels, on paper: after the skins, which they beat.
    sheet.rule("label.help-text", [f"color: {title}"])
    sheet.rule("label.settings-text", [f"color: {sheet.role('settings-ink', SETTINGS_INK)}"])
    sheet.rule("label.settings-heading", [f"color: {sheet.role('settings-heading', rgb(140, 101, 34))}"])


# The skins: games_theme(ButtonSkin) in src/presentation.cpp ----------------------------

def write_skins(sheet: Sheet) -> None:
    sheet.comment("The skins a subtree takes by class (.skin-ivory for the shell, .skin-blue and "
                  ".skin-slate for Sudoku): its editors, its default text and its fonts. A game's "
                  "skin follows the shell's, so the nearer skin wins where both match.")
    for skin in ("ivory", "mint", "blue", "slate"):
        light = skin in ("blue", "ivory")
        text = sheet.role(f"skin-{skin}-text", rgb(35, 63, 82) if light else rgb(229, 235, 241))
        field = rgb(252, 253, 250) if light else rgb(12, 27, 43)
        edge = sheet.role(f"skin-{skin}-field-edge", rgb(115, 153, 184) if light else rgb(104, 147, 171))
        field_text = sheet.role(f"skin-{skin}-field-text", rgb(26, 59, 83) if light else rgb(239, 240, 222))
        focus = sheet.role(f"skin-{skin}-focus", rgb(140, 192, 222))
        sheet.rule(f".skin-{skin} label",
                   [f"color: {text}", "font-family: content", "font-size: 14px", "font-weight: 400"])
        sheet.rule(f".skin-{skin} text-box",
                   [f"color: {field_text}", "font-family: content", "font-size: 16px", "font-weight: 400"])
        sheet.rule(f".skin-{skin} text-box::part(face)",
                   [f"background: {hex_colour(field)}", f"border: 1px solid {edge}", "border-radius: 4px"])
        sheet.rule(f".skin-{skin} text-box:focus-visible", [f"outline: 1px solid {focus}"])


def build() -> dict[str, str]:
    sheet = Sheet()
    write_gloss(sheet)
    write_game_buttons(sheet)
    write_skins(sheet)
    write_settings(sheet)
    write_help(sheet)
    css = sheet.text()
    digest = hashlib.sha256(css.encode("utf-8")).hexdigest()
    manifest = {
        "format": "gui-forms-theme/1",
        "name": "playsuite",
        "version": VERSION,
        "engine": ">=0.3",
        "sheet": {"path": SHEET, "sha256": digest},
    }
    return {SHEET: css, "theme.json": json.dumps(manifest, indent=2) + "\n"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail if the package is out of date")
    arguments = parser.parse_args()
    files = build()
    stale = []
    for name, text in files.items():
        path = PACKAGE / name
        current = path.read_text(encoding="utf-8") if path.exists() else None
        if current != text:
            stale.append(name)
            if not arguments.check:
                PACKAGE.mkdir(parents=True, exist_ok=True)
                path.write_bytes(text.encode("utf-8"))
    if arguments.check and stale:
        print("themes/playsuite is out of date: run python3 tools/make_theme.py (" + ", ".join(stale) + ")")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
