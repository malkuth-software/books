#pragma once
// Sound: one looping music track with crossfades, and pitch-varied effects.
// Names are file stems under assets/audio ("pl_press" plays pl_press.wav as
// prepared for the runtime). A missing file stays silent, so a game can be
// written before its sounds exist.
//
// Inside PlaySuite these forward to the shared scene audio (audio.cpp). The
// headless harness links dev/audio_silent.cpp, which records nothing and plays
// nothing.
#include <string>

namespace pl {

// Crossfades to `track`; "" fades the music out. `enabled` false silences it.
void audio_music(const std::string& track, bool enabled);
// gain 0..1, rate 0.5..2 (pitch follows rate).
void audio_sfx(const std::string& name, float gain = 1.f, float rate = 1.f, bool enabled = true);
// A temporary dip in the music (0..1) so a stinger can be heard.
void audio_duck_music(float amount);
// Advances fades. Call from the view's timer while audio_needs_tick() is true.
void audio_tick(double seconds);
// True while a load, fade or duck is still in progress. Steady music needs no ticks.
[[nodiscard]] bool audio_needs_tick();
// The shell's foreground state and master switches. Not in front means silence.
void audio_cabinet(bool foreground, bool music, bool sound);
void audio_stop();

}  // namespace pl
