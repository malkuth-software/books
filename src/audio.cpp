#include "audio.hpp"
#include "pcm_player.hpp"
#include "runtime_paths.hpp"
#include <map>

namespace games {
namespace {
PcmPlayer player{};
std::string current{};
bool music_enabled{};
std::map<std::string, unsigned> variants{};
std::array<std::string, 29> effects{};
// Live scores by music name, and the one of the game now playing. Its effects use the
// last slot.
std::map<std::string, std::shared_ptr<LiveScore>> scores{};
std::shared_ptr<LiveScore> live{};
constexpr std::size_t live_effects_slot = PcmPlayer::slot_count - 1;
std::size_t next_effect{};
// TriPeaks plays at the Klondike table's music until it has its own.
std::string track_name(const std::string& name) {
    return name == "solitaire" || name == "tripeaks" ? "klondike" : name;
}
std::string effect_name(const std::string& name) {
    if (name == "win") { return "stinger_win_" + track_name(current); }
    const std::map<std::string, std::pair<std::string, unsigned>> aliases{
        {"deal", {"card_shuffle", 2}}, {"flip", {"card_flip", 4}}, {"select", {"card_pickup", 4}},
        {"place", {"card_place", 4}}, {"foundation", {"card_foundation", 4}}, {"undo", {"ui_undo", 2}},
        {"hint", {"ui_hint", 2}}, {"invalid", {"ui_invalid", 2}},
        {"sudoku_digit_place", {"sudoku_digit_place", 3}}, {"sudoku_note_place", {"sudoku_note_place", 3}}};
    const std::map<std::string, std::pair<std::string, unsigned>>::const_iterator found = aliases.find(name);
    std::string base = name;
    unsigned count = 0;
    if (found != aliases.end()) { base = (*found).second.first; count = (*found).second.second; }
    else {
        const std::filesystem::path directory = std::filesystem::path(asset_directory()) / "audio";
        while (count < 9) {
            const std::string stem = base + "_0" + std::to_string(count + 1);
            const bool compressed = std::filesystem::exists(directory / (stem + ".ogg"));
            const bool pcm = std::filesystem::exists(directory / (stem + ".wav"));
            if (!compressed && !pcm) { break; }
            ++count;
        }
    }
    return count ? base + "_0" + std::to_string(1 + variants[base]++ % count) : base;
}
void attach_score(const std::string& game) {
    const std::map<std::string, std::shared_ptr<LiveScore>>::const_iterator found = scores.find(game);
    const std::shared_ptr<LiveScore> score = found == scores.end() ? nullptr : (*found).second;
    if (score == live) { return; }
    player.clear(live_effects_slot);
    live = score;
#ifdef GUI_FORMS_AUDIO_GENERATOR
    if (live) { player.generate(live_effects_slot, (*live).effects(), 1); }
#endif
}
}
void live_score(const std::string& game, std::shared_ptr<LiveScore> score) {
    scores[game] = std::move(score);
}
void music_play(const std::string& game, bool enabled) {
    music_enabled = enabled;
    if (game.empty()) { player.shutdown(); effects.fill({}); current.clear(); live.reset(); return; }
    attach_score(game);
    if (!enabled) { player.pause(0); return; }
    if (current != game) {
        player.clear(1);
        player.route(0, AudioBus::music);
#ifdef GUI_FORMS_AUDIO_GENERATOR
        if (live) { player.generate(0, (*live).music(), .4); current = game; return; }
#endif
        player.start(0, "music_" + track_name(game) + "_loop", true, .4);
        current = game;
    } else { player.resume(0); }
}
void sound_play(const std::string& name, bool enabled, double rate) {
    if (live && (*live).cue(name, enabled)) { return; }
    if (!enabled) { return; }
    const std::string mapped = effect_name(name);
    if (name == "win" || mapped.rfind("stinger_", 0) == 0) {
        player.pause(0);
        player.start(1, mapped, false, 1);
        return;
    }
    if (name == "deal") { player.clear(1); if (music_enabled) { player.resume(0); } }
    std::size_t slot = next_effect;
    for (std::size_t i = 0; i < effects.size(); ++i) {
        if (effects[i] == mapped) { slot = i; break; }
        if (!player.playing(i + 2)) { slot = i; }
    }
    effects[slot] = mapped;
    next_effect = (slot + 1) % effects.size();
    player.start(slot + 2, mapped, false, 1, rate);
}
void audio_poll() { player.tick(); }
bool audio_pending() { return player.pending(); }
}
