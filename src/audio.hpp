#pragma once
#include <memory>
#include <string>
#include <string_view>
namespace gui_forms {
class AudioGenerator;
}
namespace games {
// A game whose music and effects are synthesized as they play. Registered for its music
// name, it is used whenever music_play() names that game: its music takes the music slot
// (under the Music master, paused with it, replaced when the shelf or another game takes
// over), its effects get a slot of their own while the game is current, and every
// sound_play() is offered to cue() first.
class LiveScore {
  public:
    virtual ~LiveScore() = default;
    virtual std::shared_ptr<gui_forms::AudioGenerator> music() = 0;
    virtual std::shared_ptr<gui_forms::AudioGenerator> effects() = 0;
    // A named game event; `sound` is the Sound master. True when the score answered it,
    // so that no recorded sound plays.
    virtual bool cue(std::string_view name, bool sound) = 0;
};
void live_score(const std::string& game, std::shared_ptr<LiveScore> score);
void music_play(const std::string& game, bool enabled);
// `rate` above 1 plays the sound faster and higher (2: an octave up).
void sound_play(const std::string& name, bool enabled, double rate = 1);
void audio_poll();
[[nodiscard]] bool audio_pending();
} // namespace games
