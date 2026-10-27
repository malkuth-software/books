#pragma once
#include "gui_forms/commands.hpp"
#include "gui_forms/value.hpp"
#include "suite.hpp"
#include "suite_settings.hpp"
#include <array>
#include <memory>
namespace games {

// The masters as GUI.Forms state, owned by the shell and borrowed by every control
// that shows one: the capsule's switches bind to the commands, the
// Settings screen's check boxes and sliders to the commands and values. A change
// made through any of them goes to the SettingsStore, and a change in the store
// (the M key, a game's own menu) comes back here, so every control stays in step.
// Invoking a command asks for the change; the shell decides (Collection::toggle).
class SuiteModel final : public gf::Component {
  public:
    SuiteModel();
    gf::Command music{"suite.music", "Music"};          // checked while music plays
    gf::Command sound{"suite.sound", "Sound"};          // checked while sound plays
    gf::Command reduced{"suite.reduced", "Reduce motion"}; // checked while motion is reduced
    gf::Command settings{"suite.settings", "Settings"}; // checked while the screen is open
    gf::Value<double> music_volume{100}, sound_volume{100}; // percent

  private:
    SettingsObservation store_{};
    void pull();
    void volume_moved();
};

// The four master switches as the capsule shows them: Music, Sound, Motion and
// Settings (`which` 0..3), named with `prefix` ("capsule.").
[[nodiscard]] std::shared_ptr<SuiteButton> make_master_switch(const std::string& prefix, int which);
// Binds switches made by make_master_switch to the model's commands.
void bind_master_switches(const std::array<std::shared_ptr<SuiteButton>, 4>& switches,
                          SuiteModel& model);
} // namespace games
