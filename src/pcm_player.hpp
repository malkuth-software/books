#pragma once
#include "gui_forms/audio/audio.hpp"
#include "gui_forms/threading.hpp"
#include <array>
#include <filesystem>
#include <future>
#include <memory>
#include <string>

namespace games {
// Every voice follows one of the two PlaySuite volume masters (Settings).
enum class AudioBus { sound, music };
// Sets a master's gain (0..1) and applies it at once to every voice of every
// player. Voice gain = the slot's own gain × the master's gain. UI thread only.
void set_bus_gain(AudioBus bus, double gain);
[[nodiscard]] double bus_gain(AudioBus bus);

// Game-owned scheduling only; PCM validation, mixing and device output belong
// to GUI.Forms. File reads run on one bounded background loader.
class PcmPlayer final {
public:
    static constexpr std::size_t slot_count = 32;
    explicit PcmPlayer(bool offline = false);
    ~PcmPlayer();
    PcmPlayer(const PcmPlayer&) = delete;
    PcmPlayer& operator=(const PcmPlayer&) = delete;
    // Which master a slot follows: Sound unless routed to Music. Kept across clear().
    void route(std::size_t slot, AudioBus bus);
    // Reapplies the masters to every live voice (set_bus_gain calls this).
    void apply_bus_gains();
#ifdef GUI_FORMS_AUDIO_LOOP_TRANSPORT
    gui_forms::AudioLoopStatus loop_transport(gui_forms::AudioLoopTransport& output);
#endif
    void start(std::size_t slot, const std::string& name, bool loop, double gain, double rate = 1,
               double pan = 0);
#ifdef GUI_FORMS_AUDIO_GENERATOR
    // A live source in a slot, synthesized as it plays. It sounds until the slot is
    // cleared; gain(), pause() and resume() apply, and a paused source is frozen.
    void generate(std::size_t slot, std::shared_ptr<gui_forms::AudioGenerator> source, double gain);
#endif
    void rate(std::size_t slot, double value);
    void gain(std::size_t slot, double value);
    void pause(std::size_t slot);
    void resume(std::size_t slot);
    void clear(std::size_t slot);
    void tick();
    void shutdown();
    bool playing(std::size_t slot) const;
    bool pending() const;
    gui_forms::AudioStatus status() const;
    gui_forms::AudioStatus render(std::span<float> samples);
private:
    struct Slot final {
        std::string name{};
        std::shared_ptr<const gui_forms::AudioClip> clip{};
        std::future<gui_forms::AudioClipResult> pending{};
        gui_forms::AudioVoice voice{};
        std::shared_ptr<gui_forms::CancellationFlag> cancellation{};
        bool voice_ready{};
        bool loop{}, paused{};
        double gain{1}, rate{1}, pan{};
    };
    gui_forms::AudioEngine engine_{};
    std::array<Slot, slot_count> slots_{};
    std::array<AudioBus, slot_count> buses_{};
    bool attempted_{};
    bool offline_{};
    void open();
    void create_voice(Slot& slot);
    [[nodiscard]] double voice_gain(const Slot& slot) const; // slot.gain × its master
    static void report(gui_forms::AudioStatus status, const std::string& operation);
};
}
