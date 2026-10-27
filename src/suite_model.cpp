#include "suite_model.hpp"
#include <cmath>
namespace games {
SuiteModel::SuiteModel() {
    settings.set_checked(false);
    pull();
    gf::on(music_volume.changed(), *this, &SuiteModel::volume_moved);
    gf::on(sound_volume.changed(), *this, &SuiteModel::volume_moved);
    store_ = SettingsStore::shared().observe(std::bind_front(&SuiteModel::pull, this));
}
namespace {
// The store holds fractions and the sliders percents; a round trip may differ in the
// last bit, and a value must not be rewritten while it is still announcing a change.
void follow(gf::Value<double>& value, double percent) {
    if (std::abs(value.get() - percent) > 1e-9)
        value.set(percent);
}
} // namespace
void SuiteModel::pull() {
    const SuiteSettings masters = SettingsStore::shared().values();
    music.set_checked(masters.music);
    sound.set_checked(masters.sound);
    reduced.set_checked(masters.reduced);
    follow(music_volume, masters.music_volume * 100);
    follow(sound_volume, masters.sound_volume * 100);
}
void SuiteModel::volume_moved() {
    SuiteSettings next = SettingsStore::shared().values();
    next.music_volume = music_volume.get() / 100;
    next.sound_volume = sound_volume.get() / 100;
    SettingsStore::shared().set(next);
}
std::shared_ptr<SuiteButton> make_master_switch(const std::string& prefix, int which) {
    struct Look {
        const char* name;
        Glyph glyph;
        SwitchLook look;
        const char* on;
        const char* off;
    };
    static const Look looks[4] = {
        {"Music", Glyph::music, SwitchLook::crossed_when_off, "Music on", "Music off"},
        {"Sound", Glyph::sound, SwitchLook::crossed_when_off, "Sound on", "Sound off"},
        {"Motion", Glyph::motion, SwitchLook::crossed_when_on, "Reduced motion", "Full motion"},
        {"Settings", Glyph::settings, SwitchLook::lit_when_on, "Settings", "Settings"}};
    const Look& look = looks[which];
    std::shared_ptr<SuiteButton> button =
        gf::make_control<SuiteButton>(gf::StableId(prefix + look.name), "", GlossTone::smoke);
    (*button).set_glyph(look.glyph);
    (*button).set_radius(SuiteButton::round);
    (*button).set_switch(look.look, look.on, look.off);
    return button;
}
void bind_master_switches(const std::array<std::shared_ptr<SuiteButton>, 4>& switches,
                          SuiteModel& model) {
    (*switches[0]).bind(model.music);
    (*switches[1]).bind(model.sound);
    (*switches[2]).bind(model.reduced);
    (*switches[3]).bind(model.settings);
}
} // namespace games
