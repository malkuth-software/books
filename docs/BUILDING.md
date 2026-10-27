# Build the Pocket Library

Use CMake 3.25 or later, Ninja and LLVM 22.1.8. The repository's `Build installable Pocket Library` workflow is the executable build recipe for each released platform. GUI.Forms has its own repository, `malkuth-software/gui_forms` (split from `file_manager`'s `gui_forms/` subtree on 2026-10-07). The workflow checks it out at `e077f9d5a5117312e65e8c3b741c4edd24013d56` (release v0.2.1308) into `gui_forms/`. On Windows it first restores the MSYS2 CLANG64 tree GUI.Forms publishes, to `D:\a\_temp\msys64`, with the pinned template_harness's `harness/windows_toolchain.py restore`, so no job depends on the MSYS2 mirrors and the compiler matches the cache byte for byte. It then fetches GUI.Forms' pinned text dependencies, builds its CPU renderer where required, and prepares the resources, the library's parts among them. The GUI.Forms revisions books has built against since 6fd0071 (2026-10-13) added owner-held event handlers (`gf::on`), application-owned `Command` and `Value` state with control binding, the collection controls the shell uses, the `AtomicThreadPool`/`Worker` threading utilities (the shared `GUIForms::Threading` library, which `GUIForms::Audio` and `GUIForms::Application` both carry: never also link static Core), a view-bound display link on macOS, and stx_vorbis, GUI.Forms' own Vorbis decoder with bounded BFFT synthesis, which replaces stb_vorbis (`GUI_FORMS_AUDIO_VORBIS_BACKEND=stx`, forced in our CMake). Audio loads are cancelled with `gui_forms::CancellationFlag`. It requires a full rebuild of every consumer. It also presents a live surface under floating controls without repainting the window around them on every frame, copies only the damage a live surface publishes (`LiveSurfaceWriteLease::publish(damage)` promises every pixel outside it equals the previous publication), and keeps shaped text and blurred shadows with each control's recorded paint.

Every platform follows GUI.Forms' LLVM 22 contract (`gui_forms/docs/COMPILER_CACHE.md`): one compiler, LLVM 22.1.8, named `clang`/`clang++` (`CC`, `CXX` and, on macOS, `OBJCXX`), and `-DCMAKE_TOOLCHAIN_FILE=<workspace>/gui_forms/cmake/llvm22.cmake`.

| Platform | Compiler | Minimum |
|---|---|---|
| macOS | Homebrew `llvm@22` | macOS 14.0, Apple silicon only |
| Linux x64 and arm64 | apt.llvm.org `llvm-toolchain-noble-22` | Ubuntu 24.04 |
| Windows | MSYS2 CLANG64 clang 22.1.8 (with winpthreads) | Windows 10 |

On macOS the compiler is Homebrew's, but the C++ runtime is not: Homebrew's libc++ requires macOS 26. GUI.Forms builds libc++, libc++abi and libunwind from pinned LLVM 22.1.8 source for macOS 14 (`template_harness/harness/build_macos_runtimes.py`, which moved there from GUI.Forms); set `GUI_FORMS_LLVM_RUNTIME` to that folder. The toolchain file adds its headers, libraries and the 14.0 minimum, and `tools/package_macos.py --runtime "$GUI_FORMS_LLVM_RUNTIME"` ships those three libraries (about 1.6 MB) with their notices and audits the app's minimum.

GUI.Forms publishes a compiler cache from each tested main build as the prerelease `build-<revision>`, for macOS arm64, Linux x64, Linux arm64 and Windows x64; the macOS archive also carries the runtime. `tools/restore_toolkit_cache.py <platform>` downloads the one for the pin, checks it against the digest recorded there and the revision in its manifest, and unpacks it; on macOS it then verifies the runtime against the local compiler and SDK and rebuilds it from source if they differ. The workflow builds in `.build/native/app` with `CCACHE_BASEDIR` at the workspace, matching GUI.Forms' layout so cached toolkit objects match. A miss only compiles from source. Update the pin, the digests and the package revision in `tools/package_common.py` together.

The source integration uses public GUI.Forms APIs for native windows, input, presentation, text masks and audio. An installed GUI.Forms can replace the source when it was built with every feature (`tools/build_sdk.py --features all`, GUI.Forms ec23f9d8 or later, as the local lockstep install is): configure with `-DGUIForms_DIR=<prefix>/lib/cmake/GUIForms` instead of `GAMES_TOOLKIT_SOURCE_DIR`. It then supplies the text masks, the loop transport, and the pinned FreeType and HarfBuzz that the page compositor uses. On Windows, put `<prefix>/bin` on `PATH` for the tests. CI still builds from source. The Pocket Library does not install or export an SDK. Apple framework adaptation is confined to GUI.Forms; the application and its adapters compile as C++.

The quickest check that a workspace builds is `python3 scripts/smoke-build.py`, the template_harness bootstrap's smoke command. It configures `.build/native-<platform>/books-smoke` with `GAMES_BUILD_APPLICATION=OFF`: no GUI.Forms, no runtime assets and no library data. It builds the library core with the harness toolchain file and runs the dogfood, paper_engine and pocketlibrary_library tests.

Prepare resources once with Python and FFmpeg (with libvorbis):

```sh
python3 tools/prepare_portable_assets.py --output build/runtime --ffmpeg ffmpeg
python3 tools/verify_portable_assets.py --runtime build/runtime
```

The preparation first fetches the library's parts, each pinned by
`books/pocketlibrary/pinned_assets.json` (`tools/pinned_assets.py`), from this
repository's `library-data` release, and checks their size and SHA-256. A part already
in place and matching its pin is not fetched again. They are then gathered and packaged
like any other asset: the application ships them and downloads nothing itself.
`python3 tools/pinned_assets.py` fetches them alone, and `--verify` checks them without
the network. The `library-data` release is build input, not an app release: keep it
whenever old releases are removed. Making and publishing parts is in
[`books/pocketlibrary/COMPRESSION.md`](../books/pocketlibrary/COMPRESSION.md).

Fetch the toolkit text dependencies:

```sh
sh gui_forms/third_party/fetch_text_stack.sh
sh gui_forms/third_party/fetch_linebreak.sh
```

On Windows, MSYS2 CLANG64 supplies the compiler, CMake and Ninja. No Skia build is required. With absolute paths for the two resource directories:

```sh
cmake -S . -B build/app -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DGAMES_TOOLKIT_SOURCE_DIR=<absolute-gui_forms-checkout> \
  -DGAMES_RUNTIME_ASSET_DIR=<absolute-prepared-runtime-directory>
cmake --build build/app --parallel 2
ctest --test-dir build/app --output-on-failure --timeout 180
python3 scripts/check-style.py
```

On Linux, also install GN, Ninja, X11/Xcursor and ATK development packages. macOS and Linux build the toolkit's pinned CPU Skia archive (`gui_forms/third_party/build_skia_cpu.sh` or `build_skia_cpu_linux.sh`, which apply the same compiler, runtime and minimum) and pass `GUI_FORMS_SKIA_PREBUILT=ON` and `GUI_FORMS_SKIA_OUT` to the complete application configure. See the workflow for the exact commands.

Packaging scripts stage into a new output directory and refuse to overwrite an existing application folder:

```sh
python tools/package_windows.py --build build/app --toolkit gui_forms --runtime-dir <mingw64-bin> --installer
python3 tools/package_macos.py --build build/app --toolkit gui_forms --runtime <macos-14-llvm-runtime>
python3 tools/package_linux.py --build build/app --toolkit gui_forms
```

Windows installer creation requires NSIS. Linux packaging requires patchelf and Debian packaging tools; its DEB records the actual shared-library dependencies. The Windows and macOS scripts copy and validate the native dependency closure. All packages include the library's parts, toolkit fonts, the library's fonts, dependency notices and a resource manifest.

`tools/smoke_package.py <packaged-executable>` creates temporary isolated saves, removes asset/library overrides, launches the real native window, checks a presented frame and closes through the UI scheduler. Linux CI runs it inside Xvfb. It has a 60-second timeout. This complements CTest and manual visual/gameplay checks.

Every push to `main` that passes all four native application jobs publishes a release, `0.2.<run number>`; a `v<version>` tag in the `0.2` series publishes that version. The series moved from 0.1 to 0.2 at the 2026-10-27 history reset, so every new release is above anything published before it. The workflow publishes installers, archives and SHA-256 checksums only after all four native application jobs pass.

Sponsor: Rainstar
