# Conformance preparation: Pocket Library

Branch `claude/conformance-prep`, on `main` 6c10fc0 (2026-10-27). That is the newest line: no other branch,
and ~/Developer/pocket-bible `work/library` (2026-10-15) is older and was not used. Written against GUI.Forms main
96d3696 (cp29-cp31, version 0.6.0), then brought to the integrator's `claude/batch-cp33` (cp32/cp33, still
0.6.0). Code only: nothing here has been compiled or run. The integrator builds it.

`claude/conformance-port` (on prep fecc076) carries it to GUI.Forms #483 (checkpoint/2026-11-15-cp32, read at
48a8560a6): section 6 below.

Two branches, following PlaySuite (owner ruling, option C, provisional):
- `claude/conformance-prep` (this one) keeps `games::RoutedInput` as the fallback input layer.
- `claude/conformance-raisers`, stacked on it, has the view on GUI.Forms' raisers directly (df3be11's port,
  with `gf::key_event_arguments`). The view has no drag, so nothing goes on DragTracker.

Pocket Library was not in the I6 product inventory. It still pinned GUI.Forms v0.2.1308 (e077f9d5), from
before the 0.5 fold, so it needed the cp22-cp27 port as well as cp29-cp31. Its shell (src/, new-games/kit)
is a copy of PlaySuite's, so the shell part of PlaySuite's port was carried over as three-way cherry-picks.

## What was adapted

1. **Shell: GUI.Forms 0.5 property cells** (from games 3d395eb):
   - `set_<property>(v)` is an assignment, and every read is `.read()`;
   - `on_detaching_from_window` takes the SurfaceRoot;
   - every framebuffer is BGRA8.

   Games-only files were dropped. Conflicts in capsule, collection, help book and settings sheet were resolved
   by hand, keeping Pocket Library's own lines and wording.
2. **Shell: stable IDs, Skia, track bar** (the GUI.Forms parts of games b034633):
   - runtime IDs go through `games::stable_id` (src/stable_ids.hpp), because StableId refuses anything but
     kebab segments; help's suite topics are "suite";
   - CPU Skia on every host, and the theme embed script variable set in our scope;
   - the settings slider uses `TrackBar::value_span_bounds()`.

   PlaySuite's theme-package painting and script verbs were left out.
3. **GUI.Forms 0.6 (96d3696):**
   - **Flat control headers:** 6 includes in 4 files. A sweep of every GUI.Forms include against 96d3696's
     `include/` found no other missing header.
   - **Version:** `find_package(GUIForms 0.6 ...)` (`SameMinorVersion`).
   - **The view** (books/pocketlibrary/src/pocketlibrary_view):
     - property cells: focusable, the search field's placeholder_text, maximum_length, font, accessible_name,
       visible and text, the cursor, and `visible.read()`;
     - `on_detaching_from_window(gf::SurfaceRoot&)`.
   - **On_key and on_pointer are already gone at 96d3696** (cp29, GUI.Forms 56b6627ea). On this branch the
     view keeps its `handle_pointer` / `handle_key` through `games::RoutedInput` (src/routed_input.hpp,
     PlaySuite's copy: a crossing carries the window position through cp33's `pointer_location()`; ctest
     `routed_input`). The raiser-direct view and its test (`pocketlibrary_view_input`) are on
     `claude/conformance-raisers`. `ControlStyles::opaque` is gone (audit A17) and is not set.
   - **Removed `Control::set_child_index`** (cp28): `set_child_index_from_top(id, 0)`.
   - **Application-first main:**
     - `Application::run` is a member, so main calls `Application::create` (with PlaySuite's theme package over
       Windows Professional), builds
       the window with the application, and runs it;
     - then it releases the run state, the root and the application, and calls `Application::shutdown()`.
   - **theme.hpp:** no file needs it (no complete Theme use outside headers that reach it).
   - **Exports:** every `gf::` name is exported or header-only, except Audio (a static library) and
     Worker/CancellationFlag (in GUIForms::Threading's list).

4. **cp33 (claude/batch-cp33):**
   - Linux: main calls `std::setlocale(LC_CTYPE, "")`; CMakeLists.txt checks for libc++ 22 on Linux
     (`GAMES_LINUX_LIBCXX_22`) when the application builds.
   - One module (owner rule: a shipped program has no native library dependency and is one module):
     libc++, libc++abi and libunwind link statically through gui_forms/cmake/llvm22.cmake (GUI.Forms
     claude/cp33-libcxx-static 39d2ea6b0); CMakeLists.txt refuses a Linux tree whose link line lacks the
     static archives; the native job installs libc++-22-dev, libc++abi-22-dev and libunwind-22-dev with
     clang-22. GUI.Forms links into the executable once its claude/one-module lands (still SHARED at
     cp33). tools/package_linux.py bundles no shared library and refuses a program whose readelf -d or ldd
     names the LLVM runtime or a libgui_forms library, or resolves anything outside the system's library
     folders (tools/test_package_linux.py, run before packaging). The LLVM licences still ship.
   - themes/playsuite and tools/make_theme.py stay byte-identical to PlaySuite's, now with its
     `business-card` rules.
   - Not needed: no form file; no relative pointer or release regions (nothing holds the pointer); WheelPhase,
     timestamps and FontSpec's family and stack are appended fields; no class derives from a class made
     final; the window band and WindowCapsule belong to the surface window manager. The std rules are not
     adopted yet (coordinator's choice).
   - Business card and diagnostic overlay: Pocket Library had neither, so there is nothing to replace. Showing
     GUI.Forms' card (as PlaySuite now does) would be a product choice; the theme already carries its rules.
5. **The capsule** is a GUI.Forms composite, as PlaySuite's (prep 1cf66ed), since 5214319:
   controls/command-capsule.control (the pill a themed part, the title a stock label) is generated into
   games::CommandCapsuleControl by `gui_forms_add_control`, and games::CommandCapsule derives from it, adding
   the commands, the master switches (Music hidden and unplaced: the library plays no music) and the opening
   layout. The title is Barlow Condensed loaded through GUI.Forms' app-font API (src/capsule_title_face.*,
   option GAMES_GUI_FORMS_APP_FONTS, ON): Collection owns the family, loads it before the capsule and
   releases it in ~Collection; the capsule copies its id, and no static holds it (the owner settled the
   gap on 2026-11-15: composites may load fonts). src/text_sprites.*, used only by the capsule, is removed.
   The source build forces GUI_FORMS_BUILD_FORM_COMPILER for the control.
   - themes/playsuite carries PlaySuite's card-parity rules (paper-fibre stock, copyright and remark inks),
     byte-identical; Pocket Library still shows no business card.

6. **GUI.Forms #483 (`claude/conformance-port`):**
   - **Configure:** `find_package(GUIForms)` no longer asks for the FreeType and HarfBuzz components (GUI.Forms
     has neither since 2026-11-16), and the source build no longer sets the removed
     GUI_FORMS_ENABLE_HARFBUZZ_TEXT.
   - **The page compositor on STXlib (owner, "Pocket Library clean-out"):** `pl::page::FontSet` keeps its
     interface but drives no engine of its own. Each face is an `stxlib::glyphs::Typeface` (open_typeface,
     shared), a `Face` (masks: Fit::light, the outline shifted by the 1/64-pixel phase,
     `DrawStyle::thicken` for synthesized small capitals) and a `Shaper` (runs tagged with script Latn/Grek/
     Hebr and language ENG/ELL/IWR, features c2sc/onum/lnum/mark/mkmk, the em in layout units as the size,
     so advances come back in layout units, unhinted). Metrics are the font's numbers (FreeType's rule, OS/2
     x and cap heights, else the x and H boxes); glyph boxes are `Typeface::ink_box`. `pl_page` links
     `stxlib::algorithms`, found by the installed package (`find_dependency(stxlib)`) or, for a source build,
     again from the prefix the toolkit chose (`GUI_FORMS_STX_PREFIX_DIR`, read from its directory scope,
     since imported targets do not leave it). Pocket Library now depends on no FreeType or HarfBuzz.
     `ShapeFeatures::cluster_per_character` is gone (no caller; STXlib's shaper has one cluster level, a
     base with its marks). New test `pocketlibrary_font_set` (books/pocketlibrary/tests/font_set_tests.cpp).
   - **D15 V1, stxlib::Optional:** Label fonts, `cursor` and `expanded_state` are written through
     `Optional<T>::of`; PartPaint's `measured_text` and `corner_radius` through `set()`; `skin_part_key`
     returns `stxlib::Optional<PartKey>`; ButtonContentInk's authored colour is `none()`; `next_wake` is read
     through `value()`; the theme test reads the title's font with `present()` and `value_or`; the presentation
     override's `colour_scheme` is `set()`. Button, CheckBox and TextBox fonts stay plain FontSpec cells.
   - **Renames:** `HorizontalAlignment::far` is `end`; `FontContext::interface` is `factory_bundle`. No
     Ribbon, DateTimePicker or STXlib status names are used.

## What still waits on GUI.Forms

- **Pins (placeholders):** .github/workflows/applications.yml `ref:` and tools/package_common.py
  `TOOLKIT_REVISION` still name e077f9d5, with a PLACEHOLDER comment. AGENTS.md and docs/BUILDING.md also
  describe v0.2.1308. harness.lock pins template_harness 8868f87. They move once, at the conformance rebase: to
  the GUI.Forms release that carries cp32/cp33 (0.6 numbering, with its cache assets), and to the
  template_harness main commit GUI.Forms pins. AGENTS.md's release paragraph and BUILDING.md's list of what the
  pinned revision added should be rewritten then.
- **GUI.Forms gaps:** W402 on a light-only package (PlaySuite's docs/TOOLKIT_REQUESTS.md). Closed: the
  capsule title's display face (app-font API, composites). Closed by cp33: the public `key_event_arguments` and the crossing position.
- **Fonts:**
  - font family: `FontSpec` gains `family` / `stack`. The 18 positional `FontSpec{role, ...}` initializers
    name no family. The book pages are set by the page compositor through STXlib's glyph engine directly
    (section 6), not through FontSpec. Under the interface/content font split (2026-11-13), that is
    the product's own content engine; only the shell's interface text takes the bundled families.
- **Remaining routed-phase overrides:** if a later checkpoint retires the preview and bubble virtuals, these
  7 move:
  - pocketlibrary_view.hpp: `on_key_bubble`;
  - src/collection.hpp: `on_pointer_preview` and `on_key_preview`;
  - src/help_book.hpp: two;
  - src/settings_sheet.hpp: two.
- **STXlib boundary types:** none consumed yet. When public signatures move to STXlib's owned span, string or
  result types (A2), the LiveSurface lease (`pixels()`), `load_bgra32_premultiplied` and the text-mask call
  sites follow.
- **Shell parity with PlaySuite:** the shell is still a copy. PlaySuite's later shell work (.form window, held keys, business card, shelf glide) is not carried over. That is a product choice, not a
  conformance need.
- **Harness:** template_harness main must take `claude/csm-ccache-pch-main` first (owner prerequisite).

## How the integrator builds it

Pocket Library needs Application, Audio, AudioLoopTransport and TextMasks (and STXlib's `stxlib::algorithms`,
which the package finds beside itself), so `--features all`:

```sh
# in a GUI.Forms checkout at claude/batch-cp33 (or the conformance head)
python3 -B tools/build_sdk.py --target macos-arm64 --prefix <sdk> --features all
# in this branch's checkout
cmake -S . -B .build/conformance -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=<template_harness>/harness/cmake/llvm22.cmake \
  -DGUIForms_DIR=<sdk>/lib/cmake/GUIForms -DGAMES_RUNTIME_ASSET_DIR=<prepared runtime assets>
cmake --build .build/conformance --parallel 2
ctest --test-dir .build/conformance --output-on-failure
```

Here `GUIForms_DIR`, not only `CMAKE_PREFIX_PATH`, selects the installed package (CMakeLists.txt tests
`GUIForms_DIR`). Alternatively pass `-DGAMES_TOOLKIT_SOURCE_DIR=<gui_forms checkout>` to build GUI.Forms in-tree.
Build every target before ctest (AGENTS.md: a `pocketlibrary`-only build leaves stale test executables). The
portable core (`-DGAMES_BUILD_APPLICATION=OFF`) is unaffected by this branch.

Tests that should cover it:
- pocketlibrary_font_set (the faces on STXlib), and by eye a spread of the Bible and of a Literata book against
  the FreeType rendering;
- routed_input (prep) or pocketlibrary_view_input (raisers);
- on Linux: the libc++ configure check and tools/package_linux.py;
- playsuite_theme (the package compiles with no finding but W201);
- pocketlibrary_view_contract (the real view headless);
- pocketlibrary_library, audio_policy, dogfood;
- the native smoke `pocketlibrary --smoke` (the Application-first main);
- the full ctest.

## Risks (not checked without a build)

- **STXlib page text:** metrics and advances now come from STXlib (its numbers follow FreeType and HarfBuzz,
  with its own rounding), so line breaks can move by a word and page counts and saved places can shift;
  thickening grows to the right and upward (FreeType's emboldening grew both ways), so small capitals sit
  about half a 26.6 step to the right. Light hinting is STXlib's y-only fit, not FreeType's autohinter.
- **stxlib::algorithms twice:** pl_page links the static archive that GUI.Forms links privately. With one
  module that is one copy; if any GUI.Forms library were shared again, check for a second stxlib::state.

- **The port is larger than PlaySuite's.** It went v0.2.1308 → 0.6 in one step, by name sweeps (removed
  identifiers, property setters, getters of property cells) and by the PlaySuite cherry-picks. A cell read
  spelled as a call on a product-held control, and not caught by the sweep, shows only at compile time.
  The same goes for a signature that changed type but kept its name.
- **Theme.** Pocket Library's look is PlaySuite's theme package, copied as the shell is: themes/playsuite and
  tools/make_theme.py (byte for byte; `make_theme.py --check` passes here), `gui_forms_add_theme` on the
  pocketlibrary target, and main creating the application with the package over Windows Professional (a
  refusal reported, then the base theme) and forcing the light scheme per window, as PlaySuite's main does.
  The shell's skinned parts take the sheet's rules as PlaySuite's do: PlaySuite's shell work from 82906c2
  is carried over (the shell, the help book and the settings controls carry skin-*, gloss, help-glyph,
  setting and help-text classes and paint their parts through the theme; games_theme and
  set_theme_override are gone; docs/TOOLKIT_REQUESTS.md lists the theme gaps). Not carried: the .form window
  and Collection::set_development (PlaySuite's form-made shell), the card-back choice and the business card
  (PlaySuite only). Check the look by eye against PlaySuite.
- The Linux package's one-module check and the static libc++ configure check pass only once the GUI.Forms
  pin carries one-module (folded as 66f82629b) and the static libc++ link.
- **Capsule composite:** targets that include capsule.hpp need the generated header first (game_ui exports
  the directory; ordering relies on CMake's dependency edges); the .control vocabulary is unchecked until
  the form compiler reads it; the title label may measure 0 before it is attached (the width is re-measured
  in arrange); the app-font API allows one load per name per process, so a second Collection in one process
  (the theme test makes one) finds the name taken unless the first released it. An installed GUI.Forms must
  export GUIForms::form_compiler.
- The libc++ check runs before `find_package`; a Linux tree configured without llvm22.cmake now stops at
  configure.
- The 51 accessors that delete their rvalue overload can refuse a call on a temporary; only a compile shows
  these.
