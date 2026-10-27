# Games mask adapter requirements

The animated games compose their scene into GUI.Forms LiveSurface pixels. They need text layout and alpha masks from the shared text service; they do not require a separate native presenter or platform font API. This document proposes the remaining service behavior. It does not claim that wrapping or monochrome rasterization is available in the current prepared-text development profile.

## Inputs and coordinates

The existing `text_mask` adapters receive UTF-8, a game font role, a floating font size, and a floating wrap width. Both size and width are in the mask's logical pixel grid, before window DPI scaling. A positive wrap width is a layout constraint; zero means no soft wrapping. The game converts its scene width to this grid before calling the adapter: Atom Probe, Four Pegs and Switchbox multiply by their scene pixel size, normally 2; Eggy uses its chosen pixel size.

Keep layout in double precision. Propose quantizing font size and wrap width once to 1/64 logical pixel, nearest with ties away from zero, and using those same values for layout and cache identity. The service must expose its actual quantization if different. Device scale affects rasterization, not the logical wrap width or the selected line breaks. Test scale 1, 1.25, 1.5 and 2. Pixel-style text uses an integer-scale monochrome mask; ordinary dialogue uses grayscale coverage at the actual device scale.

## Lines and overflow

- Preserve explicit LF breaks. Treat CRLF as one break; normalize an isolated CR to LF at the adapter boundary. Preserve empty and trailing explicit lines in layout height. Empty input has no ink and zero logical advance.
- Use word wrapping at the requested width without automatic hyphenation or ellipsis. Preserve leading spaces and the spacing inside a line; spaces consumed by a soft break need not become ink at the next line's start. The score tables use repeated spaces and must retain their alignment.
- If a word cannot fit an otherwise empty line, break at an extended grapheme boundary using shared layout knowledge. Never split UTF-8 bytes, a combining sequence, or a shaping cluster. If one indivisible cluster exceeds the width, retain it and report horizontal overflow; clipping belongs to the canvas compositor.
- Tabs are not used by these game interfaces. A first profile may refuse them explicitly. A refusal must not be presented as a successfully rendered empty string.
- Left alignment and a requested additional line gap of 0.05 times font size preserve the current dialogue intent. Quantize that extra gap once to 1/64 logical pixel and reuse it for every line. Expose line baselines, logical extents and ink bounds separately. A face change is allowed to change exact line breaks; source, face identity, size and width held fixed must be deterministic.

The provider owns shaping, bidi resolution and contextual line breaking. The Games adapter will not measure and split independently shaped substrings.

## Masks, clipping and ownership

Grayscale output is an 8-bit linear coverage mask with no RGB subpixel channels. Monochrome means a glyph rasterization mode producing only 0 or 255 coverage, with its matching glyph loading/hinting policy; it does not mean thresholding a completed grayscale mask. Expand packed mono coverage to the same 8-bit mask storage at the shared raster boundary. This is primarily required by Eggy's pixel text.

Return the mask's signed origin relative to the layout anchor, row stride, dimensions and immutable owned storage. Ink bounds must contain overhangs and accents. The compatibility adapter may add a one-pixel transparent border, but must not clip an italic bearing or descender to a logical advance rectangle. Canvas clipping remains in the game's bounded mask blit.

Proposed session limits: at most 700 cache records, 2 MiB of retained UTF-8 key storage, and 32 MiB of all distinct live mask allocations per active game, including candidates and evicted masks still held by a frame; key by UTF-8 bytes, registered face identity/revision, size, wrap width, spacing, raster profile and scale. Shared immutable lease accounting must charge an allocation until its last owner releases it; LRU eviction alone does not establish the memory bound. Charge owned key capacity, with a separate bound on actual metadata allocation bytes as described below. The adapter returns owning handles or holds a frame's owners until publication completes. Eviction drops only cache ownership and cannot invalidate a mask being composed. Hiding/detaching a game drops its cache and cancels pending work; stale results must not revive the old view. A failed preparation preserves the last complete frame and reports its typed failure.

Accepted initial candidate request limits are 16 KiB UTF-8, 4096 device pixels per axis, 256 lines, and 4 MiB of coverage storage. A scale-2 request therefore cannot allocate an 8192-pixel device axis. Return an explicit limit result rather than truncate. These are integration proposals, not changes to the provider's existing budget contract.

## Consumer acceptance of the bounded development profile

Games accepts the proposed `logical_wrapped_mask_v1` behavior and the following tightened limits for implementation. This is consumer contract acceptance; it does not establish implemented headers, a stable ABI, successful rendering, or an installed SDK.

- Nine total request slots include dispatched, running, queued, completed and retiring work. At most one request executes and eight wait. Completed work holds its slot until consumed or discarded; the adapter must drain slots while preparing a frame.
- The lifetime ledger permits at most 709 distinct live mask objects, including zero-ink results and evicted masks retained by a frame, and 718 distinct exact-source owners. Retained source capacity remains charged against the 2 MiB UTF-8 budget until its last owner releases it.
- Actual requested first-party metadata allocations, including shared-owner control blocks and retained container capacity, have an 8 MiB bound. Cache eviction alone cannot release charges held by leases. The 32 MiB live coverage, 4 MiB individual coverage, and 4096-device-pixel axis limits still apply.
- One job has bounded shaping work: at most 2048 native shaping calls and 4 MiB of aggregate submitted context. Exceeding these bounds produces a typed failure. Native call latency and vendor allocator behavior are not asserted to be hard process limits.
- Queue admission can succeed before the eventual ink extent is known. A later coverage or metadata refusal is an ordinary typed completion and preserves the caller's previous mask and last complete frame.
- Cancelling queued or completed work retires its slot immediately. Running work retains its slot and allocation charges until the worker acknowledges retirement; it cannot publish after cancellation. Closing and replacing a session does not reset charges held by older leases.

The adapter will batch and drain requests without blocking the UI, retain owners for every borrowed mask used by a frame, and publish only a complete frame. It will normalize isolated CR before submission, preserve admitted LF/CRLF semantics, and perform no independent shaping or line breaking.

The fixture file separates grayscale scales (1, 1.25, 1.5 and 2) from monochrome scales (integer 1-4). Fractional monochrome requests are refusal fixtures, not successful rendering cases. Source bytes, logical line breaks and source offsets remain invariant across supported device scales for a fixed face and input.

All ten positive fixtures fit the scalar input limits: their UTF-8 lengths are 0-129 bytes, sizes are 11.5-16 logical units, wrap widths are 0-704, and they contain at most four explicit logical lines. Actual wrapped line counts, ink extents and shaping-work consumption must still be measured by the implementation tests.

Font registration will use per-game banks instead of registering all ten candidate faces together. A dialogue regular/bold pair plus the Cousine and Carlito pairs requires six faces. The exact candidate bank sizes are 2,208,324 bytes for Four Pegs, 2,117,796 for Atom Probe, and 2,018,368 for Switchbox. The largest individual face is 682,468 bytes. These fit the proposed eight-face, 4 MiB-per-face and 8 MiB aggregate encoded-font bounds; old banks retained by leases still count toward the two-live-bank limit.

## Font fixtures

`assets/fonts/manifest.json` records exact candidate font bytes and licenses. No Apple or Windows system font is redistributed. All bundled candidates retain their SIL OFL files. The candidates are:

| Game role | Encoded face candidates |
| --- | --- |
| Four Pegs dialogue and headings | Libre Baskerville Regular / Bold |
| Atom Probe dialogue and headings | Barlow Condensed Regular / Bold |
| Switchbox handwritten dialogue | Comic Neue Regular / Bold |
| Eggy pixel text and generic pixel roles | Cousine Regular / Bold |
| Neutral UI role | Carlito Regular / Bold |

These choices need inspection in the actual shared renderer before release. They are independent registered encoded faces; they must not silently replace the toolkit's application-wide font roles.

`tests/fixtures/portable_text.json` contains actual game strings plus edge cases. Numeric mask goldens must be produced and reviewed with the pinned shared raster implementation; they are deliberately not guessed from CoreText or another font renderer. Initial assertions concern complete text coverage, line semantics, wrap bounds, mono coverage and scale-independent logical layout. The full help panels must then be inspected at ordinary and fractional DPI to verify that the last paragraph and buttons remain visible.


## Request adapter development subset

`TextRequests` is the Games-side bounded request broker for a single view and encoded font bank. It borrows a session that must outlive it, retains the font owner, deduplicates identical pending inputs after isolated-CR normalization, and drains completed slots without waiting for the worker. Only successful completion replaces the caller's mask lease. Pending work and typed failure preserve it. Closing the session prevents old completions from being returned.

The adapter uses nine fixed records shared by pending and completed work. Its exact pending-source buffers occupy 144 KiB, plus one reused 16 KiB normalization workspace, of application-owned storage, separate from the provider's lifetime ledger. It does not allocate coverage copies or maintain a second mask cache. Callers retain returned leases throughout frame composition and publish only complete frames. Four Pegs, Atom Probe, Switchbox and Eggy now use this broker through `GameText`.

The independent consumer test currently targets the reviewed Stage 2 toolkit source revision `7b260cfb9f3267392e1470b0fcf4cd2497437819`. Fetch the toolkit's pinned text dependencies before configuring:

```sh
cmake -S tests/portable_text -B build/text-requests -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DGAMES_TEXT_SOURCE_DIR=<GUIForms-source-root>
cmake --build build/text-requests --target text_request_tests text_native_consumer_tests --parallel 2
ctest --test-dir build/text-requests --output-on-failure --timeout 30
```

These two tests use the real public service and its approved Carlito and Cousine font fixtures. The lifecycle test checks normalization, deduplication, backpressure, retained completions, executor affinity, cancellation and close. The native consumer checks actual wrapped dialogue, scale-independent logical lines, grayscale rendering from 0.5× through 4×, binary monochrome rendering at integer scales, combining-cluster integrity, CRLF/trailing lines, empty text, retained ownership after cache eviction and service closure, and preservation of prior coverage on admission or late dimension failure.

Both tests pass on Windows with the pinned source. The optional second argument to `text_native_consumer_tests` writes native PGM previews; the wrapped dialogue at 150% and Eggy lettering at 300% have been visually inspected. These are text-adapter checks, not complete game-frame or application validation. The toolkit feature remains default-off and source-only; its headers are not promoted into the installed SDK by this consumer.

## Vendor game frame integration

Each vendor view uses an owned render snapshot while its simulation and audio continue. A text request that is pending or backpressured prevents publication of the candidate frame. The next timer callback resumes the same snapshot, including its typewriter prefix, until all labels and wrapping metrics are ready. The previous complete surface remains readable. After successful publication, the next attempt captures the current simulation. Resizing or changing panels cancels the obsolete candidate and clears obsolete hit regions. Button hit regions are installed with their completed frame.

`GameText` registers the bundled dialogue pair, Cousine pair and Carlito pair through the public encoded-font API. The provider owns shaping, wrapping, hinting, caching and coverage. Games retains immutable mask leases only as needed for composition; it has no second text cache. Logical advances control layout, and signed native ink bearings control clipped BGRA placement. Grayscale text uses the actual display scale. Four Pegs uses the previously inventoried Libre Baskerville pair. The complete game and help frames for all four views have been inspected at 100% and 150%. Atom Probe uses Barlow Condensed and Switchbox uses Comic Neue. Eggy retains true binary Cousine masks rasterized at scale one, magnifying the finished pixel artwork with the same fractional display mapping as the scene; it does not request an unsupported fractional mono raster from the provider. Help now suppresses the underlying speech text so it cannot overprint the panel title.

Enable `GAMES_TEST_VENDOR_FRAMES=ON` in the source consumer profile to build `vendor_game_frame_tests`. It drives the real view through public Window scheduling and keyboard input with isolated development saves and muted audio, checks successive completed frames while dialogue changes, immutable retained frames, help and fractional DPI. `game_text_frame_tests` separately exercises a 25-label frame through the nine-slot queue and verifies signed bearings, clipping, stride padding and failure preservation using real coverage. The repository's native text workflow runs these seven tests and retains native previews. These tests do not establish a packaged native-window launch or a complete Games release.

Sponsor: Rainstar
