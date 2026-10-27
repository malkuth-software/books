// Hosted audio: PlaySuite's shared scene audio. Compiled only into the PlaySuite
// application (cmake/Application.cmake, game_audio_adapters).
#include "audio.hpp"

#include "scene_audio.hpp"

namespace pl {
namespace {
games::SceneAudio audio{};
}

void audio_music(const std::string& track, bool enabled) {
    audio.music(track, enabled);
}

void audio_sfx(const std::string& name, float gain, float rate, bool enabled) {
    audio.effects(name, gain, rate, enabled);
}

void audio_duck_music(float amount) {
    audio.duck(amount);
}

void audio_tick(double seconds) {
    audio.tick(seconds);
}

bool audio_needs_tick() {
    const bool needed = audio.needs_tick();
    return needed;
}

void audio_cabinet(bool foreground, bool music, bool sound) {
    audio.cabinet(foreground, music, sound);
}

void audio_stop() {
    audio.stop();
}

}  // namespace pl
