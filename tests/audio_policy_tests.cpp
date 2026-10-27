#include "pcm_player.hpp"
#include "scene_audio.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
void write_integer(std::ostream& stream, std::uint32_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) { stream.put(static_cast<char>(value >> (8 * i))); }
}
void write_tone(const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary);
    file.write("RIFF", 4); write_integer(file, 36 + 480 * 4, 4);
    file.write("WAVEfmt ", 8); write_integer(file, 16, 4);
    write_integer(file, 1, 2); write_integer(file, 2, 2);
    write_integer(file, 48000, 4); write_integer(file, 192000, 4);
    write_integer(file, 4, 2); write_integer(file, 16, 2);
    file.write("data", 4); write_integer(file, 480 * 4, 4);
    for (unsigned i = 0; i < 480; ++i) {
        write_integer(file, 8192, 2); write_integer(file, 8192, 2);
    }
    require(static_cast<bool>(file), "write tone fixture");
}
void settle(games::PcmPlayer& player) {
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (player.pending() && std::chrono::steady_clock::now() < deadline) {
        player.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(!player.pending(), "loader finishes within test deadline");
}
void settle(games::SceneAudio& scene) {
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (scene.pending() && std::chrono::steady_clock::now() < deadline) {
        scene.tick(.016);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(!scene.pending(), "scene loading finishes within test deadline");
}
void require_silence(std::span<const float> samples) {
    for (float sample : samples) { require(sample == 0, "muted scene is silent"); }
}
void player_controls(const std::filesystem::path& audio) {
    games::PcmPlayer player(true);
    player.start(0, "tone", true, 1);
    settle(player);
    std::array<float, 512> samples{};
    const gui_forms::AudioStatus rendered = player.render(samples);
    require(rendered == gui_forms::AudioStatus::ok && samples[0] == .25f, "real mixer receives loaded PCM");
    player.pause(0);
    const gui_forms::AudioStatus paused = player.render(samples);
    require(paused == gui_forms::AudioStatus::ok, "pause render");
    require_silence(samples);
    player.resume(0);
    const gui_forms::AudioStatus resumed = player.render(samples);
    require(resumed == gui_forms::AudioStatus::ok && samples[0] == .25f, "resume holds loaded clip");
    player.start(1, "retry", false, 1);
    settle(player);
    require(!player.playing(1), "missing file does not create a voice");
    write_tone(audio / "retry.wav");
    player.start(1, "retry", false, 1);
    settle(player);
    require(player.playing(1), "same name can be retried after missing file appears");
    player.pause(0);
    std::array<float, 4096> drain{};
    const gui_forms::AudioStatus ended = player.render(drain);
    require(ended == gui_forms::AudioStatus::ok && !player.playing(1), "effect reaches EOF");
    player.start(1, "retry", false, 1);
    require(player.playing(1), "finished cached effect restarts");
    player.start(2, "tone", true, 1);
    player.shutdown();
    require(!player.pending() && player.status() == gui_forms::AudioStatus::closed, "shutdown cancels pending publication");
    player.tick();
    require(!player.playing(2), "late completion cannot revive a cleared slot");
}
// The last sample of a block, after any gain smoothing has settled.
float settled(games::PcmPlayer& player) {
    std::array<float, 4096> samples{};
    for (int i = 0; i < 4; ++i)
        require(player.render(samples) == gui_forms::AudioStatus::ok, "volume render");
    return samples[samples.size() - 2];
}
#ifdef GUI_FORMS_AUDIO_GENERATOR
class Steady final : public gui_forms::AudioGenerator {
  public:
    void render(std::span<float> stereo) noexcept override {
        for (float& sample : stereo)
            sample = .5F;
    }
};
#endif
// The Music and Sound volumes scale every voice: clips and live generators, already
// sounding or started later, each following its own master.
void volume_masters() {
    games::PcmPlayer player(true);
    games::PcmPlayer second(true);
    player.route(0, games::AudioBus::music);
    player.start(0, "tone", true, 1);
    player.start(1, "tone", true, .5);
    settle(player);
    player.pause(1);
    require(std::abs(settled(player) - .25f) < 1e-4f, "full music volume plays the clip as recorded");
    games::set_bus_gain(games::AudioBus::music, .25);
    require(std::abs(settled(player) - .0625f) < 1e-4f, "music volume scales a voice already playing");
    games::set_bus_gain(games::AudioBus::sound, .5);
    require(std::abs(settled(player) - .0625f) < 1e-4f, "the sound volume leaves music alone");
    player.pause(0);
    player.resume(1);
    require(std::abs(settled(player) - .0625f) < 1e-4f, "sound volume times the voice's own gain");
    player.gain(1, 1);
    require(std::abs(settled(player) - .125f) < 1e-4f, "a later gain change keeps the master");
    second.start(3, "tone", true, 1);
    settle(second);
    require(std::abs(settled(second) - .125f) < 1e-4f, "a voice started later follows the master");
#ifdef GUI_FORMS_AUDIO_GENERATOR
    player.pause(1);
    player.route(5, games::AudioBus::music);
    player.generate(5, std::make_shared<Steady>(), 1);
    require(std::abs(settled(player) - .125f) < 1e-4f, "a live generator follows the music volume");
    games::set_bus_gain(games::AudioBus::music, 0);
    require(std::abs(settled(player)) < 1e-6f, "music volume zero silences live music");
#endif
    games::set_bus_gain(games::AudioBus::music, 1);
    games::set_bus_gain(games::AudioBus::sound, 1);
    require(games::bus_gain(games::AudioBus::music) == 1 && games::bus_gain(games::AudioBus::sound) == 1,
            "masters restored");
}
void scene_gates() {
    games::SceneAudio scene(true);
    require(!scene.needs_tick(), "silent scene needs no polling");
    scene.music("tone", false);
    scene.tick(.1);
    require(scene.status() == gui_forms::AudioStatus::closed && !scene.pending(), "disabled music does not open engine or load");
    scene.music("tone", true);
    require(scene.needs_tick(), "loading and fading music must keep the scheduler awake");
    settle(scene);
    for (unsigned i = 0; i < 12; ++i) { scene.tick(.25); }
    require(!scene.needs_tick(), "steady music plays without UI polling");
    std::array<float, 512> samples{};
    const gui_forms::AudioStatus audible = scene.render(samples);
    require(audible == gui_forms::AudioStatus::ok && samples[0] > .01f, "enabled music fades in");
    scene.music("tone", false);
    for (unsigned i = 0; i < 12; ++i) { scene.tick(.25); }
    scene.cabinet(true, true, true);
    const gui_forms::AudioStatus locally_muted = scene.render(samples);
    require(locally_muted == gui_forms::AudioStatus::ok, "local mute render");
    require_silence(samples);
    scene.music("tone", true);
    for (unsigned i = 0; i < 12; ++i) { scene.tick(.25); }
    const gui_forms::AudioStatus reenabled = scene.render(samples);
    require(reenabled == gui_forms::AudioStatus::ok && samples[0] > .01f, "music resumes after local enable");
    scene.cabinet(true, false, false);
    scene.effects("tone", 1, 1, true);
    const gui_forms::AudioStatus master_muted = scene.render(samples);
    require(master_muted == gui_forms::AudioStatus::ok, "master mute render");
    require_silence(samples);
    scene.cabinet(false, true, true);
    require(!scene.needs_tick(), "hidden audio needs no polling");
    require(scene.status() == gui_forms::AudioStatus::closed && !scene.pending(), "hidden scene releases audio");
    scene.effects("tone", 1, 1, true);
    scene.music("tone", true);
    require(scene.status() == gui_forms::AudioStatus::closed, "hidden scene cannot reopen engine");
}
}
int main() {
    try {
        const std::chrono::steady_clock::duration stamp = std::chrono::steady_clock::now().time_since_epoch();
        const std::filesystem::path root = std::filesystem::temp_directory_path() /
            ("games-audio-policy-" + std::to_string(stamp.count()));
        const std::filesystem::path audio = root / "audio";
        std::filesystem::create_directories(audio);
#ifdef _WIN32
        const int environment = _putenv_s("GAMES_ASSET_DIR", root.string().c_str());
#else
        const int environment = setenv("GAMES_ASSET_DIR", root.string().c_str(), 1);
#endif
        require(environment == 0, "isolated test asset path");
        write_tone(audio / "tone.wav");
        player_controls(audio);
        volume_masters();
        scene_gates();
        std::filesystem::remove(audio / "tone.wav");
        std::filesystem::remove(audio / "retry.wav");
        std::filesystem::remove(audio);
        std::filesystem::remove(root);
        std::cout << "Audio policy, volume masters, async retry, mute, EOF and shutdown pass.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
