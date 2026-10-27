# Sound sources

Every sound in PlaySuite is made here, from code: no samples, no recordings, no
borrowed tunes. Keep the scripts that render this game's music and effects in this
folder so anyone can rebuild them. `make_sfx.py` is a small standard-library
starting point; for richer instruments, copy
`games/zenconstruction/audio_src/engine/synth.py` into this folder (it needs NumPy)
and write the score beside it. See the Craft section of `new-games/AGENTS.md`.

Render into this game's `assets/audio/` with the `pl_` file prefix. The generator
already uses that destination. The build discovers these files automatically;
there is no central audio count or wiring step. Keep music loop metadata in a JSON
file alongside the audio, with a `music` array containing each track's `id`,
`loop_start_sample` and `loop_end_sample_exclusive` in 48 kHz sample units.
The runtime merges prefixed audio into `audio/` and copies other module assets
below `pocketlibrary/`. Its verifier rejects filename collisions and missing loops.
