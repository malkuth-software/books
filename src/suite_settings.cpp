#include "suite_settings.hpp"
#include "runtime_paths.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
namespace games {
namespace {
bool read_flag(const std::string& value, bool& destination) {
    if (value != "0" && value != "1")
        return false;
    destination = value == "1";
    return true;
}
bool read_percent(const std::string& value, double& destination) {
    std::istringstream in(value);
    int percent = -1;
    if (!(in >> percent) || percent < 0 || percent > 100)
        return false;
    destination = percent / 100.0;
    return true;
}
int percent(double volume) {
    return static_cast<int>(std::lround(std::clamp(volume, 0.0, 1.0) * 100));
}
} // namespace
bool SuiteSettings::operator==(const SuiteSettings& other) const {
    return music == other.music && sound == other.sound && reduced == other.reduced &&
           percent(music_volume) == percent(other.music_volume) &&
           percent(sound_volume) == percent(other.sound_volume);
}
double audio_gain(double volume) {
    const double position = std::clamp(volume, 0.0, 1.0);
    return position * position;
}
std::string encode_suite_settings(const SuiteSettings& s) {
    std::ostringstream out;
    out << "POCKETLIBRARY_SETTINGS 1\n"
        << "music=" << (s.music ? 1 : 0) << "\nsound=" << (s.sound ? 1 : 0)
        << "\nreduced_motion=" << (s.reduced ? 1 : 0) << "\nmusic_volume=" << percent(s.music_volume)
        << "\nsound_volume=" << percent(s.sound_volume) << '\n';
    return out.str();
}
bool decode_suite_settings(const std::string& text, SuiteSettings& destination) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.rfind("POCKETLIBRARY_SETTINGS ", 0) != 0)
        return false;
    SuiteSettings next;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key = line.substr(0, equals), value = line.substr(equals + 1);
        bool good = true;
        if (key == "music")
            good = read_flag(value, next.music);
        else if (key == "sound")
            good = read_flag(value, next.sound);
        else if (key == "reduced_motion")
            good = read_flag(value, next.reduced);
        else if (key == "music_volume")
            good = read_percent(value, next.music_volume);
        else if (key == "sound_volume")
            good = read_percent(value, next.sound_volume);
        if (!good)
            return false;
    }
    destination = next;
    return true;
}
std::filesystem::path suite_settings_path() {
    return state_directory() / "pocketlibrary-settings-v1.txt";
}
SuiteSettings load_suite_settings() {
    SuiteSettings result;
    std::error_code error;
    const std::filesystem::path path = suite_settings_path();
    if (std::filesystem::file_size(path, error) <= 4096 && !error) {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        static_cast<void>(decode_suite_settings(text.str(), result));
    }
    return result;
}
bool save_suite_settings(const SuiteSettings& settings) {
    const std::filesystem::path path = suite_settings_path();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << encode_suite_settings(settings);
        out.close();
        if (!out)
            return false;
    }
    std::filesystem::rename(temporary, path, error);
    return !error;
}

SettingsObservation::~SettingsObservation() {
    if (id_)
        SettingsStore::shared().forget(id_);
}
SettingsObservation::SettingsObservation(SettingsObservation&& other) noexcept : id_(other.id_) {
    other.id_ = 0;
}
SettingsObservation& SettingsObservation::operator=(SettingsObservation&& other) noexcept {
    if (this != &other) {
        if (id_)
            SettingsStore::shared().forget(id_);
        id_ = other.id_;
        other.id_ = 0;
    }
    return *this;
}
SettingsStore& SettingsStore::shared() {
    static SettingsStore store;
    return store;
}
const SuiteSettings& SettingsStore::values() {
    const std::filesystem::path path = suite_settings_path();
    if (!loaded_ || path != loaded_from_) {
        values_ = load_suite_settings();
        loaded_from_ = path;
        loaded_ = true;
    }
    return values_;
}
void SettingsStore::set(const SuiteSettings& requested) {
    SuiteSettings next = requested;
    next.music_volume = std::clamp(next.music_volume, 0.0, 1.0);
    next.sound_volume = std::clamp(next.sound_volume, 0.0, 1.0);
    if (next == values())
        return;
    values_ = next;
    static_cast<void>(save_suite_settings(values_));
    // Observers may observe or forget during the call; iterate over a copy and
    // skip any that a previous observer dropped.
    const std::vector<Observer> current = observers_;
    for (const Observer& observer : current)
        if (subscribed(observer.id))
            observer.changed();
}
bool SettingsStore::subscribed(std::size_t id) const {
    for (const Observer& observer : observers_)
        if (observer.id == id)
            return true;
    return false;
}
void SettingsStore::toggle_music() {
    SuiteSettings next = values();
    next.music = !next.music;
    set(next);
}
void SettingsStore::toggle_sound() {
    SuiteSettings next = values();
    next.sound = !next.sound;
    set(next);
}
void SettingsStore::toggle_reduced() {
    SuiteSettings next = values();
    next.reduced = !next.reduced;
    set(next);
}
SettingsObservation SettingsStore::observe(std::function<void()> changed) {
    const std::size_t id = next_id_++;
    observers_.push_back({id, std::move(changed)});
    return SettingsObservation(id);
}
void SettingsStore::forget(std::size_t id) {
    for (std::size_t i = 0; i < observers_.size(); ++i)
        if (observers_[i].id == id) {
            observers_.erase(observers_.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
}
void SettingsStore::reload() {
    loaded_ = false;
}
} // namespace games
