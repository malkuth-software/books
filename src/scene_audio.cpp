#include "scene_audio.hpp"
#include <algorithm>
#include <cmath>

namespace games {
void SceneAudio::music(const std::string& name, bool enabled) {
    const bool was_on = music_on_;
    music_on_ = enabled && master_music_ && foreground_;
    if (!foreground_) { return; }
    if (wanted_ != name) {
        wanted_ = name;
        front_ = 1 - front_;
        player_.clear(front_);
        music_gain_[front_] = 0;
        front_started_ = false;
    }
    if (music_on_ && !name.empty() && !front_started_) {
        player_.start(front_, name, true, 0);
        front_started_ = true;
    } else if (music_on_ && !was_on) { player_.resume(front_); }
}
#ifdef GUI_FORMS_AUDIO_GENERATOR
void SceneAudio::live_music(const std::shared_ptr<gui_forms::AudioGenerator>& source, bool enabled) {
    const bool was_on = music_on_;
    music_on_ = enabled && source && master_music_ && foreground_;
    if (!foreground_) { return; }
    static const std::string live = "<live>";
    if (wanted_ != live) {
        wanted_ = live;
        front_ = 1 - front_;
        player_.clear(front_);
        music_gain_[front_] = 0;
        front_started_ = false;
    }
    if (music_on_ && !front_started_) {
        player_.generate(front_, source, 0);
        front_started_ = true;
    } else if (music_on_ && !was_on) { player_.resume(front_); }
}
#endif
void SceneAudio::effects(const std::string& name, double gain, double rate, bool enabled, double pan) {
    if (!enabled || !foreground_ || !master_sound_) { return; }
    std::size_t chosen = next_effect_;
    for (std::size_t offset = 0; offset < effect_names_.size(); ++offset) {
        const std::size_t slot = (next_effect_ + offset) % effect_names_.size();
        if (effect_names_[slot] == name && !player_.playing(7 + slot)) { chosen = slot; break; }
        if (!player_.playing(7 + slot)) { chosen = slot; }
    }
    effect_names_[chosen] = name;
    next_effect_ = (chosen + 1) % effect_names_.size();
    player_.start(7 + chosen, name, false, std::clamp(gain, 0.0, 1.0), std::clamp(rate, .5, 2.0), pan);
}
void SceneAudio::ambience(const std::array<double, 5>& values, bool enabled) {
    const std::array<const char*, 5> names{"eggy_amb_wind", "eggy_amb_brook", "eggy_amb_forest", "eggy_amb_rain", "eggy_amb_fire"};
    for (std::size_t i = 0; i < values.size(); ++i) {
        bed_target_[i] = enabled && foreground_ && master_sound_ ? std::clamp(values[i], 0.0, 1.0) : 0;
        if (bed_target_[i] > 0 && !bed_started_[i]) {
            player_.start(i + 2, names[i], true, 0);
            bed_started_[i] = true;
        }
    }
}
void SceneAudio::cabinet(bool foreground, bool music, bool sound) {
    foreground_ = foreground;
    master_music_ = music;
    master_sound_ = sound;
    if (!foreground || !music) {
        music_on_ = false;
        for (std::size_t i = 0; i < 2; ++i) { music_gain_[i] = 0; player_.gain(i, 0); player_.pause(i); }
    }
    if (!foreground || !sound) {
        for (std::size_t i = 0; i < 5; ++i) { bed_gain_[i] = bed_target_[i] = 0; player_.pause(i + 2); }
        for (std::size_t i = 7; i < PcmPlayer::slot_count; ++i) { player_.clear(i); }
    }
    if (!foreground) {
        // Hidden controls do not retain large music buffers or delayed effects.
        player_.shutdown();
        wanted_.clear();
        front_started_ = false;
        bed_started_.fill(false);
    }
}
void SceneAudio::duck(double amount) { if (std::isfinite(amount)) { duck_ = std::max(duck_, std::clamp(amount, 0.0, 1.0)); } }
void SceneAudio::tick(double dt) {
    player_.tick();
    if (!foreground_ || !std::isfinite(dt)) { return; }
    dt = std::clamp(dt, 0.0, .5);
    duck_ = std::max(0.0, duck_ - dt * .25);
    const double level = .42 * (1 - .75 * duck_);
    for (std::size_t i = 0; i < 2; ++i) {
        const double target = i == front_ && music_on_ ? level : 0;
        music_gain_[i] += std::clamp(target - music_gain_[i], -dt / 3.5, dt / 3.5);
        player_.gain(i, music_gain_[i]);
        if (i != front_ && music_gain_[i] == 0) { player_.clear(i); }
        if (i == front_ && !music_on_ && music_gain_[i] == 0) { player_.pause(i); }
    }
    for (std::size_t i = 0; i < 5; ++i) {
        if (!bed_started_[i]) { continue; }
        bed_gain_[i] += (bed_target_[i] - bed_gain_[i]) * std::min(1.0, dt * 1.2);
        player_.gain(i + 2, bed_gain_[i]);
        if (bed_gain_[i] < .005 && bed_target_[i] < .005) { player_.pause(i + 2); }
        else if (!player_.playing(i + 2)) { player_.resume(i + 2); }
    }
}
void SceneAudio::stop() {
    player_.shutdown(); wanted_.clear(); bed_started_.fill(false); music_gain_.fill(0);
    bed_gain_.fill(0); bed_target_.fill(0); front_started_ = false; music_on_ = false;
}
bool SceneAudio::needs_tick() const {
    if (player_.pending())
        return true;
    if (!foreground_)
        return false;
    if (duck_ > 0)
        return true;
    const double level = .42 * (1 - .75 * duck_);
    for (std::size_t i = 0; i < music_gain_.size(); ++i)
        if (music_gain_[i] != (i == front_ && music_on_ ? level : 0))
            return true;
    for (std::size_t i = 0; i < bed_gain_.size(); ++i)
        if (bed_started_[i] && std::abs(bed_gain_[i] - bed_target_[i]) > .0001)
            return true;
    return false;
}
bool SceneAudio::pending() const { const bool result = player_.pending(); return result; }
gui_forms::AudioStatus SceneAudio::status() const {
    const gui_forms::AudioStatus result = player_.status(); return result;
}
gui_forms::AudioStatus SceneAudio::render(std::span<float> samples) {
    const gui_forms::AudioStatus result = player_.render(samples); return result;
}
}
