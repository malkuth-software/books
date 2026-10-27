#pragma once
#include "pcm_player.hpp"
#include <array>
#include <memory>

namespace games {
// Shared game policy for the two animated controls: two music slots, five
// ambience beds and a bounded 24-voice pool. All calls are on the UI thread.
class SceneAudio final {
public:
    // Slots 0 and 1 are the music crossfade (the Music volume); the rest are Sound.
    explicit SceneAudio(bool offline = false) : player_(offline) {
        player_.route(0, AudioBus::music);
        player_.route(1, AudioBus::music);
    }
    void music(const std::string& name, bool enabled);
#ifdef GUI_FORMS_AUDIO_GENERATOR
    // Music the scene composes as it plays, in the music slots as a track would be: it
    // crossfades in, follows the Music master, and pauses while it is not wanted.
    void live_music(const std::shared_ptr<gui_forms::AudioGenerator>& source, bool enabled);
#endif
    void effects(const std::string& name, double gain, double rate, bool enabled, double pan = 0);
    void ambience(const std::array<double, 5>& values, bool enabled);
    void cabinet(bool foreground, bool music, bool sound);
    void duck(double amount);
    void tick(double dt);
    void stop();
    bool pending() const;
    [[nodiscard]] bool needs_tick() const;
    gui_forms::AudioStatus status() const;
    gui_forms::AudioStatus render(std::span<float> samples);
private:
    PcmPlayer player_{};
    std::array<double, 2> music_gain_{};
    std::array<double, 5> bed_gain_{}, bed_target_{};
    std::array<bool, 5> bed_started_{};
    std::array<std::string, 24> effect_names_{};
    std::size_t next_effect_{}, front_{};
    std::string wanted_{};
    bool front_started_{};
    bool foreground_{true}, master_music_{true}, master_sound_{true}, music_on_{};
    double duck_{};
};
}
