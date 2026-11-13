# Conformance preparation: Pocket Library

Branch `claude/conformance-prep`, on `main` 6c10fc0 (2026-10-27). That is the newest line: no other branch,
and ~/Developer/pocket-bible `work/library` (2026-10-15) is older and was not used. Written against GUI.Forms main
96d3696 (cp29-cp31, version 0.6.0). Code only: nothing here has been compiled or run. The integrator builds it.

Pocket Library was not in the I6 product inventory. It still pinned GUI.Forms v0.2.1308 (e077f9d5), from
before the 0.5 fold, so it needed the cp22-cp27 port as well as cp29-cp31. Its shell (src/, new-games/kit)
is a copy of PlaySuite's, so the shell part of PlaySuite's port was carried over as three-way cherry-picks.

## What was adapted (three commits)

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
   - **On_key and on_pointer are already gone at 96d3696** (cp29, GUI.Forms 56b6627ea), although the brief
     listed them as still to come:
     - the view derives from `games::RoutedInput<gf::Control>` (src/routed_input.hpp, shared with PlaySuite),
       which rebuilds the routed event from each raiser's arguments for `handle_pointer` / `handle_key`;
     - 1 `on_key` and 1 `on_pointer` override moved, and the `on_key_bubble` forwarder calls `handle_key`;
     - new test: tests/routed_input_tests.cpp (ctest `routed_input`).
   - **Removed `Control::set_child_index`** (cp28): `set_child_index_from_top(id, 0)`.
   - **Application-first main:**
     - `Application::run` is a member, so main calls `Application::create` (GUI.Forms' default theme for now;
       PlaySuite's theme is still to carry over), builds
       the window with the application, and runs it;
     - then it releases the run state, the root and the application, and calls `Application::shutdown()`.
   - **theme.hpp:** no file needs it (no complete Theme use outside headers that reach it).
   - **Exports:** every `gf::` name is exported or header-only, except Audio (a static library) and
     Worker/CancellationFlag (in GUIForms::Threading's list).

## What still waits on GUI.Forms

- **Pins (placeholders):** .github/workflows/applications.yml `ref:` and tools/package_common.py
  `TOOLKIT_REVISION` still name e077f9d5, with a PLACEHOLDER comment. AGENTS.md and docs/BUILDING.md also
  describe v0.2.1308. harness.lock pins template_harness 8868f87. They move once, at the conformance rebase: to
  the GUI.Forms release that carries cp32/cp33 (0.6 numbering, with its cache assets), and to the
  template_harness main commit GUI.Forms pins. AGENTS.md's release paragraph and BUILDING.md's list of what the
  pinned revision added should be rewritten then.
- **cp32/cp33 APIs:**
  - font family: `FontSpec` gains `family` / `stack`. The 18 positional `FontSpec{role, ...}` initializers
    name no family. The book pages are set by the page compositor through the FreeType and HarfBuzz
    components directly, not through FontSpec. Under the interface/content font split (2026-11-13), that is
    the product's own content engine; only the shell's interface text takes the bundled families.
  - grid engine, stock icons, final virtuals: not used.
- **Remaining routed-phase overrides:** if a later checkpoint retires the preview and bubble virtuals, these
  7 move:
  - pocketlibrary_view.hpp: `on_key_bubble`;
  - src/collection.hpp: `on_pointer_preview` and `on_key_preview`;
  - src/help_book.hpp: two;
  - src/settings_sheet.hpp: two.
- **STXlib boundary types:** none consumed yet. When public signatures move to STXlib's owned span, string or
  result types (A2), the LiveSurface lease (`pixels()`), `load_bgra32_premultiplied` and the text-mask call
  sites follow.
- **Shell parity with PlaySuite:** the shell is still a copy. PlaySuite's later shell work (the theme package,
  which Pocket Library should take (see Risks), .form window, held keys, business card, shelf glide) is not carried over. That is a product choice, not a
  conformance need.
- **Harness:** template_harness main must take `claude/csm-ccache-pch-main` first (owner prerequisite).

## How the integrator builds it

Pocket Library needs Application, Audio, AudioLoopTransport, TextMasks, FreeType and HarfBuzz, so
`--features all`:

```sh
# in a GUI.Forms checkout at 96d3696 (or the conformance head)
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
- routed_input;
- pocketlibrary_view_contract (the real view headless);
- pocketlibrary_library, audio_policy, dogfood;
- the native smoke `pocketlibrary --smoke` (the Application-first main);
- the full ctest.

## Risks (not checked without a build)

- **The port is larger than PlaySuite's.** It went v0.2.1308 → 0.6 in one step, by name sweeps (removed
  identifiers, property setters, getters of property cells) and by the PlaySuite cherry-picks. A cell read
  spelled as a call on a product-held control, and not caught by the sweep, shows only at compile time.
  The same goes for a signature that changed type but kept its name.
- **Theme: still to do.** Pocket Library should use PlaySuite's theme (the package in PlaySuite's
  themes/playsuite, `games::playsuite_theme()`), as PlaySuite does for its capsule and help. This branch does not
  have it yet: main uses `ApplicationConfig{}`, GUI.Forms' default look. Carrying it over means PlaySuite's
  themes/playsuite, src/playsuite_theme.*, tools/make_theme.py and the `gui_forms_add_theme` build step (PlaySuite
  82906c2), then passing the package in `ApplicationConfig::theme.packages` as PlaySuite's main does.
- The 51 accessors that delete their rvalue overload can refuse a call on a temporary; only a compile shows
  these.
