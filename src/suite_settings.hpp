#pragma once
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
namespace games {

// The Pocket Library's masters: one copy for the library and the shell. Each control
// that shows one of these observes the store, so a change from anywhere (the capsule's
// switches, the M key, the Settings screen) repaints every switch at once.
struct SuiteSettings {
    bool music = true, sound = true, reduced = false;
    // Slider positions, 0..1. audio_gain() turns a position into a voice gain.
    double music_volume = 1, sound_volume = 1;
    bool operator==(const SuiteSettings& other) const;
};
// The gain a volume slider position gives: squared, so equal steps sound even.
[[nodiscard]] double audio_gain(double volume);

// Text snapshot "POCKETLIBRARY_SETTINGS 1" then key=value lines. Unknown keys are
// ignored and missing keys keep their defaults, so later versions can add keys.
[[nodiscard]] std::string encode_suite_settings(const SuiteSettings& settings);
// Returns false, leaving `destination` unchanged, when the text is not settings.
[[nodiscard]] bool decode_suite_settings(const std::string& text, SuiteSettings& destination);
[[nodiscard]] std::filesystem::path suite_settings_path();
// Reads the settings file; the defaults without one.
[[nodiscard]] SuiteSettings load_suite_settings();
bool save_suite_settings(const SuiteSettings& settings);

class SettingsStore;
// Observation lasts as long as this token; dropping it stops the callback.
class SettingsObservation final {
  public:
    SettingsObservation() = default;
    ~SettingsObservation();
    SettingsObservation(SettingsObservation&& other) noexcept;
    SettingsObservation& operator=(SettingsObservation&& other) noexcept;
    SettingsObservation(const SettingsObservation&) = delete;
    SettingsObservation& operator=(const SettingsObservation&) = delete;

  private:
    friend class SettingsStore;
    explicit SettingsObservation(std::size_t id) : id_(id) {}
    std::size_t id_ = 0;
};

// The one in-memory copy of the masters. UI thread only. It follows the state
// directory: when GAMES_STATE_DIR changes (tests, --dev), it loads again.
class SettingsStore final {
  public:
    [[nodiscard]] static SettingsStore& shared();
    [[nodiscard]] const SuiteSettings& values();
    // Saves and notifies every observer when anything changed.
    void set(const SuiteSettings& next);
    void toggle_music();
    void toggle_sound();
    void toggle_reduced();
    [[nodiscard]] SettingsObservation observe(std::function<void()> changed);
    // Drops the loaded copy; the next read loads from disk again.
    void reload();

  private:
    friend class SettingsObservation;
    SettingsStore() = default;
    void forget(std::size_t id);
    [[nodiscard]] bool subscribed(std::size_t id) const;
    struct Observer {
        std::size_t id;
        std::function<void()> changed;
    };
    SuiteSettings values_{};
    std::filesystem::path loaded_from_{};
    bool loaded_ = false;
    std::vector<Observer> observers_{};
    std::size_t next_id_ = 1;
};
} // namespace games
