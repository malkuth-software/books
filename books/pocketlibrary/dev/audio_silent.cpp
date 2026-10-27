// Silent audio for this game's tests and previews. Not part of PlaySuite: the
// application links src/platform/audio.cpp instead. It keeps a log of what the
// game asked for, so a test can check that a move makes its sound.
#include "audio_log.hpp"
#include "platform/audio.hpp"

namespace pl {
namespace {
std::vector<std::string> requests;
}

const std::vector<std::string>& audio_log() {
    return requests;
}

void audio_log_clear() {
    requests.clear();
}

void audio_music(const std::string& track, bool enabled) {
    if (enabled && !track.empty()) {
        requests.push_back("music:" + track);
    }
}

void audio_sfx(const std::string& name, float, float, bool enabled) {
    if (enabled) {
        requests.push_back("sfx:" + name);
    }
}

void audio_duck_music(float) {}
void audio_tick(double) {}

bool audio_needs_tick() {
    return false;
}

void audio_cabinet(bool, bool, bool) {}
void audio_stop() {}

}  // namespace pl
