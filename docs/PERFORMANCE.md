# Performance and resource lifecycle

A settled view reuses its last output. Animation, input, layout changes,
asynchronous results and timed game actions are reasons to do work; the passage of
another display interval alone is not. PlaySuite sits open on a work machine all
day, and the standard for an open, untouched game is nearly no processor use.

Nothing is traded for a number: animation, image fidelity, audio quality, game
behaviour and saved state stay as they are. A metric improves by doing less work,
not by doing less of the game.

## How the collection keeps the rule

- **Games are created lazily.** The shelf creates a game the first time its box is
  opened, so an unvisited game costs nothing; a visited game keeps its position and
  its render caches.
- **Hidden means stopped.** Behind the shelf or another game, a game stops its
  timers, workers and sound (`set_cabinet(false, ...)`) and publishes nothing. Native
  frame tests check that hidden games have no timer callbacks or publications and
  that they resume.
- **Settled means still.** A game whose state is not changing schedules no frame; a
  frame is drawn because something moved, not because time passed. Ambient scenes
  (`shared/ambient`) hold a measured budget with a governor and stop when paused,
  hidden or occluded.
- **Frames repair damage.** The renderer (`shared/render`) draws straight into the
  window's live buffers in their own byte order, keeps still layers as pixels and
  redraws only the rectangle that changed plus what the buffer in hand missed; the
  overlays over a live frame are repainted only where it reached. Opaque surfaces are
  declared opaque so the host copies instead of blends.
- **Materials are made once.** Felt, grass, soil and paper are generated when a
  scene or sheet is first shown, at the window's scale, and kept; nothing is
  generated per frame.
- **Audio does not poll.** The shared player runs on its own callback; the UI thread
  touches it only on a change. Audio policy tests cover steady playback without UI
  polling.

The kit's [performance guide](../new-games/guide/06-performance.md) says how a game
is written to these rules; [RENDER_ENGINE.md](RENDER_ENGINE.md) says why the
renderer is shaped as it is.

## Measuring in the application

`games --profile-idle -1` profiles the shelf and `--profile-idle N` persisted game
entry `N`. The run waits five seconds, emits `PROFILE_BEGIN`, resets the window's
activity counters, waits ten more seconds, emits `PROFILE_END`, then closes a second
later with the native host's metrics, so teardown stays outside the sample. Always
set `GAMES_STATE_DIR` to an isolated directory.

- `tools/profile_presentation.py` runs that interval on any platform and saves frame
  counts, total and average presentation cost and the slowest presentation, from the
  metrics snapshot at `PROFILE_END`. These are presentation measurements, not process
  CPU or input latency. The macOS application workflow records Solitaire, Gems and
  Untangle in its check artifact; the values are diagnostic, with no machine-speed
  threshold.
- `tools/profile_windows.py` collects Windows process CPU time, working set and
  private memory with standard-library counters, with isolated saves, muted masters
  and the same saved position copied into each run.
- `GAMES_SCENE_TRACE` makes an ambient scene print its frame pacing, drawing and
  presentation times and missed surface leases when it closes.
- `r3d_bench [frames]` (`shared/render/tools`) times the 3D renderer on a game-like
  scene stage by stage with a checksum, so a change is shown to draw the same pixels
  in less time. `rockstack_shadow_tests --bench` times Rock Stack's shadow stage.

Keep the window uncovered and the pointer still; do not compile or capture during
the sample. Compare the same window size, display scale, preferences and saved
position; snapshot the isolated saves first, since timed game events change them.
CPU is a percentage of one logical core. A new deal or a celebration is active work,
not an idle benchmark, and a settled sample says nothing about dragging, dealing,
rotating or celebrating: measure those separately. Counters for unchanged paints,
layouts, callbacks and surface publications explain more than one CPU reading, and
a zero retained-paint count during direct animation is not zero CPU. Source
inspection on one platform does not replace a native measurement on another.

## Known costs still outside the games

- The host clears the window under an opaque live surface and copies the whole
  surface on some paths; both grow with device pixels, so a Retina window pays about
  four times as much while the scene's own work stays the same. The requests for a
  smaller presented surface and for skipping the clear are in
  [TOOLKIT_REQUESTS.md](TOOLKIT_REQUESTS.md).
- Visited games keep their render caches with their state. Releasing those caches
  needs explicit lifecycle handling so a return preserves transient state, speech,
  positions and input; it has not been done.
