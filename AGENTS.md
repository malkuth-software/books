# Pocket Library development

The Pocket Library is one native C++20 application on GUI.Forms holding a library of
books. It is PlaySuite's shell (malkuth-software/games) with one volume on its shelf: the
library opens straight onto its bookcase, with the command capsule, Help and the
Settings screen floating over it. Internal identifiers keep PlaySuite's names where the
shell came from (`games::`, `GAMES_*` variables, `GAME.json`), so code moves between
the two repositories without renaming.

This file is the working agreement for anyone, person or model, changing it. Read it
first; then the document for the work in hand: [docs/README.md](docs/README.md)
indexes the rest.

## Where things live

- `books/pocketlibrary/`: the library, discovered from its `GAME.json`: the book model,
  the coder and the parts (`src/container.*`), the bookcase and the reader, the page
  compositor (`src/page/`), its sounds (`audio_src/`, `assets/audio/`), its fonts, its
  tests, and every tool that makes the books (`tools/`). Its
  [README](books/pocketlibrary/README.md) is the brief and
  [COMPRESSION.md](books/pocketlibrary/COMPRESSION.md) the study of the storage.
- What Git keeps of the books: the catalogue (`tools/catalogue.json`: shelf order,
  lettering, binding, licence and the block that codes each book), the lock of upstream
  sources (`tools/sources.lock.json`, each file by URL, size and SHA-256), the
  importers, the corrections, the shelf index (`assets/books/shelf.txt`), the list of
  parts (`assets/books/parts.txt`) and their pins (`pinned_assets.json`). The parts
  themselves (`assets/books/*.plb`) are published on the `library-data` release
  (`tools/publish_library.py`) and fetched and checked by the asset preparation
  (`tools/pinned_assets.py`): never commit one, and keep that release when old
  releases are removed. Sources are not archived; an import downloads them again
  (`tools/fetch_sources.py`) and the lock stops it if one has changed.
- `shared/paper/`: the paper engine the pages, Help and Settings are printed on.
- `src/`: the shell and its services: the capsule (`capsule.*`), Help (`help_book.*`),
  the Settings screen (`settings_sheet.*`), the masters (`suite_settings.*`, file
  `pocketlibrary-settings-v1.txt`, mirrored into GUI.Forms state by `SuiteModel`), the
  audio buses (`pcm_player.*`), the saves and assets API (`game_data.*`) and the walnut
  of the bookcase (`wood.*`). Saves live in `Rainstar/Pocket Library`; the first start
  copies the places PlaySuite's Pocket Library kept.
- `new-games/kit/`: the headless text and view-contract helpers the library's
  contract test uses.

## Rules that do not bend

- Every book is exactly its source's text: a hash-pinned file read by a deterministic
  importer, every correction of a source's own defects recorded. Never hand-edit a
  normalized text or a part.
- Adding books makes a new block; it never relearns the tables or rechooses the base.
  A new generation (new tables and base, every block coded again) is a deliberate
  decision, made with the owner, and is proved like the first: every book read back
  byte for byte, the parts' sizes and the time to prepare the base.
- Book ids, block numbers once published, save formats and the bookmarks file are
  permanent.
- Do not modify the GUI.Forms checkout to fix an application build. A reusable
  cross-platform capability becomes a GUI.Forms enhancement, coordinated with its owner
  and recorded in [docs/TOOLKIT_REQUESTS.md](docs/TOOLKIT_REQUESTS.md) until it lands.
- Threads use GUI.Forms' `Worker` and `AtomicThreadPool` and cancellation its
  `CancellationFlag`, never `std::thread` or `std::stop_token`. Link `GUIForms::Audio`
  or `GUIForms::Application`, never also static `GUIForms::Core`.
- Preserve demand-driven rendering: a settled bookcase or spread schedules no frames.
- Write new C++ in the house style ([docs/HOUSE_STYLE.md](docs/HOUSE_STYLE.md)) and run
  `python3 scripts/check-style.py` before publishing. Keep compile parallelism at two
  jobs when sharing a host with other work.
- Check every new layout at the window minimum, 600 × 420, and at scales 1 to 2.
- A new generated material, look or sound is shown to the owner as labelled candidates,
  with sample text, before it goes into the application.

## Tests

Build every target before running CTest; a build of `pocketlibrary` alone leaves stale
test executables that pass falsely. `pocketlibrary_library` checks the coder, the parts
and the bookmarks without a window; `pocketlibrary_view_contract` drives the real
control headless.

## Building

[docs/BUILDING.md](docs/BUILDING.md) and `.github/workflows/applications.yml` are the
recipe for the application and its packages. Release CI pins GUI.Forms
`e077f9d5a5117312e65e8c3b741c4edd24013d56` (v0.2.1308) from `malkuth-software/gui_forms` and follows
its LLVM 22.1.8 contract on every platform (macOS 14.0 Apple silicon with GUI.Forms'
bundled runtime, Windows 10, Ubuntu 24.04), restoring that revision's published
compiler cache. The main workflow is the release gate, and every green push to `main`
publishes a release.

Every change reaches `main` through a pull request whose checks pass; nothing is pushed
to `main` directly, from the M4 or anywhere else. Open the PR from a branch, set it to
merge automatically when its required checks pass, and do not wait on CI. A CI step
never pushes to `main`; work that once did runs as a check on the pull request. The one
required check is `Pocket Library CI summary`, the last job of
`.github/workflows/applications.yml`: it reports on every pull request and passes only
when each job the change needs passed (`changes`, `prepare`, the `core` matrix on pull
requests and the `native` release-gate matrix; a docs-only change skips the builds). Before
any push, `git merge-base --is-ancestor be3731f HEAD` must succeed: the history before
the 2026-10-27 reset is never published again.

The portable core configures without GUI.Forms and builds the library without pixels
and its tests:

```sh
cmake -S . -B .build/portable-core -G Ninja -DCMAKE_BUILD_TYPE=Release -DGAMES_BUILD_APPLICATION=OFF
cmake --build .build/portable-core --parallel 2
ctest --test-dir .build/portable-core --output-on-failure --timeout 120
```

### The M4 Mini

The authoritative checkout is `/Users/ultimussecundai/Developer/books`. `m4build`
syncs the checkout it is run from to its own mirror under `~/Developer/CodexBuilds/` on
the M4 and runs a command there; the mirror is disposable build input and takes no
source edits, and the checkout must not be edited while a sync runs. The native build
is `.build/app-dogfood`, against `~/Developer/PlaysuiteDependencies/toolkit-dogfood` and
the prepared runtime `.build/runtime-shell` (prepared here, where FFmpeg is, and copied
into the mirror). The development compiler is Homebrew LLVM 22.1.8.

```sh
/Users/ultimussecundai/.local/bin/m4build -- sh -c '
  D=$HOME/Developer/PlaysuiteDependencies/toolkit-dogfood
  export PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:$PATH
  export CC=clang CXX=clang++ OBJCXX=clang++ CCACHE_BASEDIR=$PWD CCACHE_COMPILERCHECK=content
  export GUI_FORMS_LLVM_RUNTIME=$D/toolchain/llvm-22.1.8-macos14
  cmake -S . -B .build/app-dogfood -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=$D/gui_forms/cmake/llvm22.cmake \
    -DGAMES_TOOLKIT_SOURCE_DIR=$D/gui_forms -DGAMES_RUNTIME_ASSET_DIR=$PWD/.build/runtime-shell \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_OBJCXX_COMPILER_LAUNCHER=ccache \
    -DGUI_FORMS_SKIA_PREBUILT=ON -DGUI_FORMS_SKIA_OUT=$D/skia-out &&
  cmake --build .build/app-dogfood --target all -j2 &&
  ctest --test-dir .build/app-dogfood --output-on-failure --timeout 300'
```

The native executable is `.build/app-dogfood/pocketlibrary.app/Contents/MacOS/pocketlibrary`.
Use `--dev --script <file>` for isolated native checks; a script is lines of
`<milliseconds> <verb>` ending in `quit`, with the verbs `help`, `command <id>`,
`action <code>`, `key <name>`, `click X Y`, `hover X Y`, `resize W H` and
`capture <file.ppm>`. `--profile-idle 24` measures the settled library. Never open a
window on the owner's Mac: run and capture on the M4.

Report what was observed. Passing tests is not native, audio or cross-platform
validation; audio is judged by listening, and the person is sent the files.
