# GUI.Forms requests from the PlaySuite rework

AGENTS.md asks for reusable capabilities to become GUI.Forms enhancements, coordinated with the toolkit owner. The PlaySuite shell needed several things the pinned toolkit (`7b260cf`) does not offer. Games carries small local versions so the application works today. Each entry below names the local stand-in that a toolkit feature would replace. None of these changes were made to the shared toolkit checkout.

## Proposed toolkit features

1. **Application display faces in `Painter` text.** `FontRole` has only `control`, `content` and `monospace`, mapped by each host to bundled faces. Games paints its sign and box titles in Barlow Condensed and Libre Baskerville through `src/text_sprites.*`. That code shapes masks with the text-mask service, converts the coverage to premultiplied images, and draws placeholder text in the host face until the masks are ready. What would replace it: a way to register an application font family, or a `display` role, usable from `draw_text_utf8` and `measure_text_utf8`.

2. **Floating command bar control.** `src/capsule.*` is a generic overlay pill. It shows primary commands when folded, opens on hover with an eased width and height, wraps commands onto extra rows when narrow, can be pinned open, and leaves a shadow margin that is not hit-tested. A command model (id, label, enabled, checked, primary) drives it. Other applications could use it for full-window canvases.

3. **Device scale on `Painter`.** Polygon filling (`paint_polygon` in `src/presentation.cpp`) steps one device row at a time, so translucent fills never overlap. The shell has to publish the window scale through `set_polygon_scale` because `Painter` does not expose it.

4. **Anti-aliased polygon and path fill on `Painter`.** Only `gui_drawing::GraphicsRecorder` has paths, and it is not drawable through `Painter`. Games fills polygons with device-row spans and blends the partly covered pixel at each span end. Tiled shapes need a hard-edged mode, or seams appear between them.

5. **Scaled presentation of a small live surface, and no clear under an opaque one.**
   Measured with Stillwater (`shared/ambient`) on the macOS Skia CPU host at
   1060 x 680 points, scale 1, 20 frames a second: of 18 % of one core, about 6 %
   is host work per presented frame (Skia clearing the window under the opaque
   surface with `rect_memset32`, retained paint of the surface, Core Animation
   submission), and the game spends a further share enlarging its 530 x 340 scene
   to device pixels (`ambient::present_nearest`) only for the host to copy it again.
   Both grow with device pixels, so a Retina window costs about four times as much
   while the scene's own work stays the same. Two features would remove most of
   it: (a) a live surface that may be smaller than its destination, presented
   with nearest-pixel (or optionally linear) scaling by the host or compositor,
   with a way to blend crisp device-resolution overlay pixels (or a second,
   sparse overlay surface) for text; (b) skipping the background clear beneath a
   control that declares an opaque live surface covering its bounds. The kit's
   view contract currently requires device-sized frames, so (a) also needs a
   contract revision.

6. **Overlays over a moving live surface repainted each frame on macOS.** Measured
   with Gems and Untangle at rest (2026-10-11, M4, GUI.Forms c91afd9, 1060 x 680 at
   scale 1, 60 frames a second): of the main thread's time, about a fifth is host
   work per published frame, `-[GUIFormsView drawRetainedRect:]` replaying the
   floating capsule's retained chunks (its shadow, gradient and text) and
   `SkConvertPixels` copying the published region, even when the publication's
   damage does not touch the capsule. Both games now publish only what changed;
   the host still repaints the overlay every frame. The request: repaint an
   overlay over a live view only when the published damage reaches it, and copy
   the published region without a pixel conversion when the surface already has
   the window's byte order. *Done in GUI.Forms 6e9a817 (pinned at 2d7e64f): the
   macOS host now redraws only a publication's damage. Most of what remained was
   ours: `render::refresh_overlays` invalidated every overlay a whole-frame
   publication reached (the capsule in one corner, help in another), and the union
   painted the whole window each frame; it now does nothing on macOS. Painting
   far-apart damage as separate rectangles is GUI.Forms' open work W9.*

## Host behavior found on Windows

**Idle live-surface clock and registration lifecycle.** In pinned `7b260cf`,
   the Windows host arms the periodic presentation clock
   whenever `Window::has_live_surface_presentations()` is true. Registrations
   retain the surface until the control is detached; the public API offers no
   suspend/unregister token. Consequently a settled or hidden producer can stop
   publishing but still leave host wakeups and framebuffer storage behind.
   A publication-triggered wake, plus an explicit lifetime/suspension mechanism
   that preserves the latest frame for exposure, would let Games retire idle
   presentation work and hidden buffers without detaching game state. Games
   currently stops producer timers and avoids repeated publication; it does not
   modify this shared host policy.

1. **Retained repaints hide directly presented live views until their next frame.** A full-window repaint (an expose, or `PrintWindow` during automated captures) redraws a direct-presented view from its retained `on_paint`, which is the game's fallback fill. The live pixels return only when the game publishes a new generation, so a game that publishes rarely can look blank after an expose. Automated screenshots must therefore copy from the screen. A fix would be for the retained repaint to composite each registered surface's latest published frame. In the transactional DIB mode, `present_live_surface_updates` also skips presenting while other damage is pending, which makes needless invalidation costly. The PlaySuite shell avoids it: for example, button setters return early when nothing changed. Each integrated vendor view now also records `draw_live_surface` during ordinary painting so an expose or the help document can restore the latest complete frame without waiting for another publication.

2. **Ordinary controls over a live view.** Only `PaintPlane::overlay` controls are subtracted from direct presentation. The repaint of an ordinary control that overlaps a live view overwrites live pixels until the next frame. Overlay controls are correct over a live view, but on macOS they are expensive: with the PlaySuite capsule and help floating over a hosted game (2026-10-08, M4, 1100 x 720 at scale 2), the process used about half a core for as long as the game was open, every game alike, whether or not its picture moved (Koi-Koi 3.2 s against 15.5 s of CPU over 30 s; Maze 1.0 s against 14.9 s; Stillwater's 28 frames a second each became a full-window paint). The capsule therefore stays within a rail above the live-surface games; the card, Sudoku and puzzle games have the whole window. The request: when overlays cover only part of a live view, present the live frame directly everywhere else and repaint just the overlays' rectangles, and do no work at all for a wake that changes nothing. Then the rail can go. *Done in GUI.Forms 0d6c397 (pinned at 5bb7a8b): the capsule now floats over live-surface games; a game keeps the rail only while its own controls sit where the capsule floats (`"rail": true` in `GAME.json`).*

3. **Letter spacing drops characters.** With `FontSpec::letter_spacing` above zero, the Win32 DIB text path dropped or overlapped spaces, periods and digits. Examples: "Welcome back. Your…" rendered as "Welcome backYour", and "MISTAKES 0" overlapped. Games now uses letter spacing only on static uppercase captions.

## Animated rendering boundaries in the current pin

The Windows adapter backport at `c3b9d10d0ed708da2527fcb2697b29b8b2cd9007`
resolves input starvation during continuous rendering. Both native host loops
alternate bounded input-first and ordinary message turns before presentation.
The input turn includes posted keyboard/mouse messages and translated characters,
which are not covered by `PM_QS_INPUT` alone. A native queue fixture checks their
ordering through a posted render backlog, ordinary FIFO work and quit handling.

The same backport includes the complete `BeginPaint` device-pixel update bounds
when logical damage already exists. This prevents fractional damage edges from
leaving stale lines on an opaque help document over animated content, and keeps
native exposure damage from being omitted. The public toolkit interface is
unchanged. These fixes do not implement retained pixels or faster filtered image
composition; those requests below remain open.

The following were verified in pinned revision
`3f75e379213de972f78729a591594e70fe65a585` while auditing frequent updates
across the eighteen-game collection. The per-game paths are recorded in
`PERFORMANCE.md`.

1. **Damage propagation for direct live surfaces.** Producers can publish a
   damage rectangle, but `Window::take_live_surface_presentations` uses the
   control's visible bounds for changed generations. Carrying conservative
   damage through to native presentation needs to cover skipped generations,
   rotating buffer contents, scale changes, exposure and overlay removal.
   Simply applying the newest frame's rectangle would lose updates when a
   consumer skips an intermediate frame.

2. **Fast premultiplied image/surface composition.** Gems and Nature Cube use
   transparent, supersampled CPU rasters with ordinary image drawing. The
   Windows direct surface path uses copy semantics and cannot preserve that
   composition by substitution. A shared fast sampling/blending path, or an
   offscreen `Painter` that can produce an opaque composed board, would avoid
   application copies of toolkit rendering code. Preserve filtering, alpha,
   clipping, fractional translation and display scale.

3. **Retained pixels or command bounds during replay.** Separating static and
   animated controls retains recording, but intersecting chunks still replay
   every command through the painter. Untangle's stationary yarn contains many
   short line segments; even a smaller cat damage rectangle incurs their replay
   overhead. Reusable pixel retention or conservative command culling must
   preserve painter save/restore, transforms, clips, stroke extents and shadows.

4. **Comparable presentation metrics.** Window `frames_presented` and its
   duration counter cover retained painting; direct surface copies have separate
   host `live_presentations` counters. The current application sampling boundary
   resets only the former. Exposing/resetting both at the same boundary would
   permit comparable native measurements without counting startup and teardown
   in one path. Zero retained paints does not mean a live-surface game is idle.

## Native offscreen adapter

Games now pins `d58f530ad5ceaa8f4ab1e5afdc12027aff90d16d`, adding
`Window::create_framebuffer(Size, double)`, the native painter factory, and
`PaintFramebuffer`. Hosts explicitly register a borrowed framebuffer painter and
clear it before teardown; consumers do not discover it through cross-library RTTI.
Native DIB and
Skia implementations preserve their renderer's drawing and registered fonts.
The UI-thread-owned target retains writable premultiplied pixels, reports its
BGRA/RGBA order and stride explicitly, and clips each begin/end pass. Unsupported
recording/headless painters return no target. Requested dimensions are bounded.

Gems and Untangle use this facility to cache scenery pixels and publish complete
opaque direct surfaces. This resolves their need for offscreen composition and
static pixel retention without copying platform rendering code into Games.
Faster image filtering and propagation of bounded damage through the native
direct drain remain independent improvements. The adapter was developed in an
isolated toolkit branch and raised with the toolkit owner for upstream adoption.

## Live audio sources

Done: GUI.Forms `63e7128` adds `AudioGenerator`, `AudioEngine::generator` and the
feature macro `GUI_FORMS_AUDIO_GENERATOR` (falseywinchnet/file_manager#54). Games
uses it behind that macro: `PcmPlayer::generate`, `ambient::SoundDesk::live` and
`live_music`, Mowings mower, garden and band, and Stillwaters tank and shanty.
Builds against an older toolkit fall back to rendered clips.

## macOS live-surface painting (found with Stillwater, 2026-10-06)

Largely done: GUI.Forms #52 added `LiveSurfaceDescription::opaque` (no retained paint
under an opaque surface) and #55 (`2ac5dcd`) native-order surfaces
(`native_live_surface_pixel_format()`, RGBA on macOS and Linux, BGRA on Windows) with
pixel-exact copies at 1:1. The scene view and the puzzle views write native order and
declare themselves opaque. Stillwater, 1060 x 618, per frame on the M4: retained paint
1.84 -> 0.56 ms, live draw 1.55 -> 1.35 ms. The full-window CoreGraphics hand-off
remains. The original finding follows.

On the M4 (pinned Skia CPU adapter), a `LiveSurface` drawn through
`Painter::draw_live_surface` is not directly presented: every published frame
becomes a retained paint pass over the control's whole area. A 10-second
`sample` of Stillwater at 1060 x 618 (scale 1, 12 frames a second) puts about 20 %
of the process's active samples in that paint: `neon::rect_memset32` (the
destination is cleared before an opaque surface covers it), `srcover` blending
of a surface the producer marks opaque, `swap_rb` (a BGRA/RGBA conversion of every
pixel), loads and stores, and the IOKit flush. Requests: present opaque live
surfaces directly on macOS as on Windows, or at least skip the clear, use `src`
for opaque surfaces, and accept the surface in the destination's byte order. A
scene-sized surface enlarged by the presenter would also remove the producer's
enlarging copy.

## Shared settings and volume (found with the Settings screen, 2026-10-07)

1. **Mix-bus or engine gain in the audio engine.** `gui_forms::AudioVoice::set_gain`
   is the only gain; there is no engine master and no bus. PlaySuite's Music and
   Sound volumes are applied on the application side: `games::PcmPlayer` routes
   each slot to a bus (`AudioBus::music` or `AudioBus::sound`), multiplies the
   slot's own gain by the bus gain, keeps a list of live players and re-sets the
   gain of every live voice when a volume moves (`src/pcm_player.*`). The Four
   Pegs loop transport multiplies its own `set_gain`. What would replace it:
   named buses (or at least an engine master gain) on `AudioEngine`, applied in the
   mixer after voice gain, so a volume change is one call and reaches generators,
   clips and loop transports alike. A smoothed gain change would also avoid the
   step a voice hears when a slider jumps.
2. **A themeable TrackBar.** `TrackBar` paints from `BasicControlStyle` (one
   accent, square thumb) and maps the pointer with a private ten-point
   `track_inset` (`range_control_rendering.hpp`). The Settings screen overrides
   `on_paint` in `games::SettingSlider` for a gold groove and gloss knob and has to
   repeat that inset. A public track rectangle (or thumb/track recipes in the
   theme) would let applications restyle it safely. TrackBar also lacks a
   hovered-visual accessor such as `ButtonBase::hovered_visual()`. `CheckBox` likewise
   draws its focus as a fixed blue rectangle from the theme; `games::SettingCheck`
   overrides `on_paint` for a gold box and a soft gold focus outline (shown only
   while `focus_cue_visible()`, that is after keyboard input). A focus-ring recipe in
   the theme (colour, radius, inset) would make such subclasses unnecessary.
3. **A settings form control.** `src/settings_sheet.*` builds a scrolling page of
   labelled rows (check box with slider, choice chips, image choices, notes) from
   a declarative list, re-reads values in place and rebuilds only when the list's
   shape changes. A property-sheet control fed by such a model (with
   toggle, choice and slider rows and keyboard order) would serve other
   applications' preference screens.

## Opening a web address (found with the help credits, 2026-10-08)

1. **A platform "open this URL" service.** `LinkLabel` draws and activates a link, but
   GUI.Forms has no way to hand an address to the system browser. `src/open_link.*`
   does it for the shell's own fixed address (`/usr/bin/open` on macOS, `xdg-open` on
   Linux, `ShellExecuteW` on Windows). A host service beside the clipboard and dialogs
   would let any application do this without platform code. `LinkLabel` also sizes its
   underline from an estimated text width; the help page draws its own measured one.

## Live surface presentation (found cutting per-frame work, 2026-10-09)

1. **Present only a frame's damage.** `LiveSurfaceWriteLease::publish(damage)` records
   the changed rectangle, and `LiveSurfaceFrame::damage()` returns it, but the macOS
   host copies the whole control area from the surface on every new generation
   (`take_live_surface_presentations` clips to the control's bounds only). The games
   now repair and publish only what changed (`shared/render/ui/pixel_surface.*`),
   so on an animated scene the host's full copy (`memmove`, about 200 samples in 6 s
   for Pen the Sheep at 2200 x 1440) is the largest cost left outside the game.
   Intersecting the presentation clip with the frame's damage, when the window's
   retained raster already holds the previous generation, would remove most of it.

## Hover when the pointer leaves (found with the shelf, 2026-10-10)

1. **Leave the hovered control when the pointer leaves the window.** No host clears
   hover on window exit: the macOS host's `mouseExited:` (`macos_host.mm`) resets the
   cursor only, the Windows host's `WM_MOUSELEAVE` only revokes cursor interaction, and
   the Linux host sends `PointerAction::leave` on `LeaveNotify`, which the core does not
   treat as leaving hover. `Window::dispatch_pointer` only changes hover on a `move`
   inside the window. Every native toolkit leaves hover here, so this changes no
   intended behaviour (pointer capture keeps its exception, as macOS already does). So a
   control the pointer was over when it left the window stays `hovered_visual()`
   until the pointer comes back: the shelf's boxes, which lift while hovered, stay
   lifted. Likewise a control hidden while hovered is skipped (`eligible`) and keeps
   its hovered state. Wanted: on window exit, and when the hovered control becomes
   hidden or disabled, send it `leave` and clear the window's hover (the next `move`
   then sends `enter` as usual). PlaySuite works around the hidden case by giving
   each box a synthetic `leave` when the shelf hides for a game
   (`ShelfBox::forget_pointer`); it cannot see the window exit.

## An overlay's share of a live frame (found with the capsule, 2026-10-10)

1. **Optional: repaint an overlay's share when the live frame under it changes.**
   `take_live_surface_presentations` subtracts overlay rectangles from each live clip and
   leaves those pixels to retained painting, which repaints them only when the overlay is
   invalidated, though its own comment says retained painting "also draws the live frame
   beneath". PlaySuite now does this itself: every publish calls
   `render::refresh_overlays` (`shared/render/ui/overlays.hpp`), which invalidates the
   overlay-plane controls over the damage. Doing it in the toolkit, for generations whose
   damage meets an overlay rectangle, would serve translucent overlays in other
   applications too; opaque ones would pay a repaint they do not need.
   *Answered by GUI.Forms #69 (3e4812b, pinned): when a new frame's changed area reaches
   an overlay, the core marks that overlap for repaint, a first or skipped frame the whole
   area under it, and the macOS host redraws it in the same screen update. Checked on a
   Retina screen with the capsule open (Stillwater's Next tank, Catching Thieves' New garden
   and first opening, the capsule folding over a settled game): no stale pixels.
   PlaySuite's own workarounds are gone (the scene view's 30-frame repaint, the first-frame
   repaint, the whole-frame publish when overlays change, and presenting live views again
   after the capsule moves). `render::refresh_overlays` went too: on Windows a trace
   showed it repainting the capsule and help button ahead of Stillwater's water, which
   looked like the overlays catching up; without it the lag is gone.*

## Text over paper (found with the paper engine, 2026-10-11)

The help document and the Settings screen are now printed on a generated paper tile
(`shared/paper`, drawn with `fill_image_pattern`). One finding for the painter:

1. **Linear-light glyph blending with a fixed correction.** Dark text blended in sRGB
   over a light, textured ground comes out thinner than the same text on a flat fill,
   because the coverage ramp is applied to gamma-encoded values. The Pocket Bible
   interview (its typography section, F009) settled on blending glyph coverage in
   linear light with a fixed, measured correction so that dark-on-light text keeps its
   weight; greyscale antialiasing (F007) composes correctly over paper where subpixel
   rendering would fringe. A `Painter` or text-mask option for linear-light compositing
   would let both applications share it. PlaySuite carries no stand-in.
