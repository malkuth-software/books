#include "pcm_player.hpp"
#include "audio_loader.hpp"
#include <chrono>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <vector>

namespace games {
namespace {
std::array<double, 2> bus_gains{1, 1};
// Live players, so a master change reaches voices that are already sounding.
std::vector<PcmPlayer*>& players() {
    static std::vector<PcmPlayer*> live;
    return live;
}
}
void set_bus_gain(AudioBus bus, double gain) {
    const double value = std::isfinite(gain) ? std::clamp(gain, 0.0, 1.0) : 1.0;
    double& current = bus_gains[static_cast<std::size_t>(bus)];
    if (current == value) { return; }
    current = value;
    for (PcmPlayer* player : players()) { (*player).apply_bus_gains(); }
}
double bus_gain(AudioBus bus) { return bus_gains[static_cast<std::size_t>(bus)]; }
PcmPlayer::PcmPlayer(bool offline) : offline_(offline) {
    buses_.fill(AudioBus::sound);
    players().push_back(this);
}
void PcmPlayer::route(std::size_t index, AudioBus bus) {
    buses_.at(index) = bus;
    Slot& slot = slots_[index];
    if (slot.voice_ready && engine_.status() == gui_forms::AudioStatus::ok) { report(slot.voice.set_gain(voice_gain(slot)), "gain"); }
}
double PcmPlayer::voice_gain(const Slot& slot) const {
    const std::size_t index = static_cast<std::size_t>(&slot - slots_.data());
    return std::clamp(slot.gain, 0.0, 1.0) * bus_gain(buses_[index]);
}
void PcmPlayer::apply_bus_gains() {
    if (engine_.status() != gui_forms::AudioStatus::ok) { return; }
    for (Slot& slot : slots_) {
        if (slot.voice_ready) { report(slot.voice.set_gain(voice_gain(slot)), "gain"); }
    }
}
void PcmPlayer::report(gui_forms::AudioStatus status, const std::string& operation) {
    if (status != gui_forms::AudioStatus::ok) {
        std::cerr << "Audio " << operation << ": status " << static_cast<int>(status) << '\n';
    }
}
void PcmPlayer::create_voice(Slot& slot) {
    if (!slot.clip || engine_.status() != gui_forms::AudioStatus::ok) { return; }
    std::shared_ptr<const gui_forms::AudioClip> playback = slot.clip;
    if (slot.pan != 0) {
        const std::span<const float> source = (*slot.clip).samples();
        std::vector<float> stereo(source.begin(), source.end());
        const float left = static_cast<float>(1 - std::max(0.0, slot.pan));
        const float right = static_cast<float>(1 + std::min(0.0, slot.pan));
        for (std::size_t sample = 0; sample + 1 < stereo.size(); sample += 2) {
            stereo[sample] *= left;
            stereo[sample + 1] *= right;
        }
        const gui_forms::AudioClipResult balanced = gui_forms::AudioClip::copy(stereo);
        if (!balanced.clip) { report(balanced.status, "pan"); return; }
        playback = balanced.clip;
    }
    const gui_forms::AudioStatus admitted = engine_.voice(playback, slot.loop, slot.voice, !slot.loop);
    report(admitted, slot.name);
    slot.voice_ready = admitted == gui_forms::AudioStatus::ok;
    if (!slot.voice_ready) { return; }
    report(slot.voice.set_gain(voice_gain(slot)), "gain");
    report(slot.voice.set_rate(slot.rate), "rate");
    if (!slot.paused) { report(slot.voice.play(), "play"); }
}
void PcmPlayer::open() {
    if (attempted_) { return; }
    attempted_ = true;
    const gui_forms::AudioStatus opened = engine_.open(offline_);
    report(opened, "device open");
}
#ifdef GUI_FORMS_AUDIO_LOOP_TRANSPORT
gui_forms::AudioLoopStatus PcmPlayer::loop_transport(gui_forms::AudioLoopTransport& output) {
    open();
    const gui_forms::AudioLoopStatus result = engine_.loop_transport(output);
    return result;
}
#endif
void PcmPlayer::start(std::size_t index, const std::string& name, bool loop, double gain, double rate, double pan) {
    if (index >= slots_.size() || name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos) { return; }
    open();
    if (engine_.status() != gui_forms::AudioStatus::ok) { return; }
    Slot& slot = slots_[index];
    if (slot.name != name || (!slot.clip && !slot.pending.valid())) {
        clear(index);
        slot.name = name;
        slot.cancellation = std::make_shared<gui_forms::CancellationFlag>();
        slot.pending = load_audio_clip(name, slot.cancellation);
    }
    slot.loop = loop;
    slot.gain = gain;
    slot.rate = rate;
    slot.pan = std::clamp(pan, -1.0, 1.0);
    slot.paused = false;
    if (slot.clip) {
        slot.voice = gui_forms::AudioVoice{};
        create_voice(slot);
    }
}
#ifdef GUI_FORMS_AUDIO_GENERATOR
void PcmPlayer::generate(std::size_t index, std::shared_ptr<gui_forms::AudioGenerator> source, double gain) {
    if (index >= slots_.size() || !source) { return; }
    open();
    if (engine_.status() != gui_forms::AudioStatus::ok) { return; }
    clear(index);
    Slot& slot = slots_[index];
    slot.name = "generator";
    slot.gain = gain;
    const gui_forms::AudioStatus admitted = engine_.generator(std::move(source), slot.voice);
    report(admitted, slot.name);
    slot.voice_ready = admitted == gui_forms::AudioStatus::ok;
    if (!slot.voice_ready) { return; }
    report(slot.voice.set_gain(voice_gain(slot)), "gain");
    report(slot.voice.play(), "play");
}
#endif
void PcmPlayer::gain(std::size_t index, double value) {
    Slot& slot = slots_.at(index);
    slot.gain = value;
    if (slot.voice_ready && engine_.status() == gui_forms::AudioStatus::ok) { report(slot.voice.set_gain(voice_gain(slot)), "gain"); }
}
void PcmPlayer::rate(std::size_t index, double value) {
    Slot& slot = slots_.at(index);
    slot.rate = value;
    if (slot.voice_ready && engine_.status() == gui_forms::AudioStatus::ok)
        report(slot.voice.set_rate(value), "rate");
}
void PcmPlayer::pause(std::size_t index) {
    Slot& slot = slots_.at(index);
    slot.paused = true;
    if (slot.voice_ready && engine_.status() == gui_forms::AudioStatus::ok) { report(slot.voice.pause(), "pause"); }
}
void PcmPlayer::resume(std::size_t index) {
    Slot& slot = slots_.at(index);
    slot.paused = false;
    if (slot.voice_ready && engine_.status() == gui_forms::AudioStatus::ok) { report(slot.voice.play(), "resume"); }
}
void PcmPlayer::clear(std::size_t index) {
    Slot& slot = slots_.at(index);
    if (slot.cancellation) { (*slot.cancellation).request(); }
    slot = Slot{};
}
bool PcmPlayer::playing(std::size_t index) const {
    const Slot& slot = slots_.at(index);
    const bool active = !slot.paused && (slot.pending.valid() || slot.voice.playing());
    return active;
}
void PcmPlayer::tick() {
    for (Slot& slot : slots_) {
        if (!slot.pending.valid()) { continue; }
        const std::future_status readiness = slot.pending.wait_for(std::chrono::seconds(0));
        if (readiness != std::future_status::ready) { continue; }
        try {
            gui_forms::AudioClipResult result = slot.pending.get();
            report(result.status, slot.name);
            slot.clip = std::move(result.clip);
            create_voice(slot);
        } catch (const std::exception& error) {
            std::cerr << "Audio " << slot.name << ": " << error.what() << '\n';
        }
    }
}
void PcmPlayer::shutdown() {
    for (std::size_t i = 0; i < slots_.size(); ++i) { clear(i); }
    engine_.shutdown();
    attempted_ = false;
}
PcmPlayer::~PcmPlayer() {
    shutdown();
    std::vector<PcmPlayer*>& live = players();
    live.erase(std::remove(live.begin(), live.end(), this), live.end());
}
bool PcmPlayer::pending() const {
    for (const Slot& slot : slots_) { if (slot.pending.valid()) { return true; } }
    return false;
}
gui_forms::AudioStatus PcmPlayer::status() const {
    const gui_forms::AudioStatus result = engine_.status();
    return result;
}
gui_forms::AudioStatus PcmPlayer::render(std::span<float> samples) {
    const gui_forms::AudioStatus result = engine_.render(samples);
    return result;
}
}
