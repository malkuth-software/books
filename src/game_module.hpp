#pragma once
#include "suite.hpp"
#include <map>
#include <memory>
#include <optional>
#include <type_traits>

namespace games {
// Shared engines can retain a single control (the four card games share one cabinet).
// New games need only their own factory; no collection changes are necessary.
struct ModuleContext {
    bool dev = false;
    bool hosted = true;
    std::map<std::string, std::shared_ptr<gf::Control>> shared;
};
class GameInstance : public CommandSource {
  public:
    virtual std::shared_ptr<gf::Control> control() const = 0;
    virtual void activate() = 0;
    virtual void preferences(bool active, bool music, bool sound, bool reduced) = 0;
    virtual bool scripted_action(std::string_view) { return false; }
    virtual bool editing_name() const { return false; }
};

template<class View> class HostedGameInstance : public GameInstance {
  public:
    explicit HostedGameInstance(std::shared_ptr<View> view) : view_(std::move(view)) {}
    std::shared_ptr<gf::Control> control() const override { return view_; }
    void activate() override { (*view_).activate(); }
    void preferences(bool active, bool music, bool sound, bool reduced) override {
        if constexpr (requires(View& v) { v.set_cabinet(active,music,sound,reduced); })
            (*view_).set_cabinet(active,music,sound,reduced);
        else if constexpr (requires(View& v) { v.set_cabinet_preferences(music,sound,reduced); })
            (*view_).set_cabinet_preferences(music,sound,reduced);
    }
    bool scripted_action(std::string_view action) override {
        if constexpr (requires(View& v) { v.scripted_action(action); })
            return (*view_).scripted_action(action);
        return false;
    }
    bool editing_name() const override {
        if constexpr (requires(const View& v) { v.editing_name(); })
            return (*view_).editing_name();
        return false;
    }
    std::vector<GameCommand> commands() const override {
        if constexpr (std::is_base_of_v<CommandSource, View>)
            return (*view_).commands();
        else if constexpr (requires(const View& v) { v.host_panel(); }) {
            const std::string panel=(*view_).host_panel();
            return {{"new","New game",true,false,true},{"help","Help",true,panel=="help"},
                    {"scores","Top scores",true,panel=="scores"}};
        }
        return {};
    }
    void run_command(std::string_view id) override {
        if constexpr (std::is_base_of_v<CommandSource,View>)
            (*view_).run_command(id);
        else if constexpr (requires(View& v) { v.host_command(std::string(id)); })
            (*view_).host_command(std::string(id));
    }
    std::vector<GameSetting> settings() const override {
        if constexpr (std::is_base_of_v<CommandSource, View>)
            return (*view_).settings();
        return {};
    }
    void change_setting(std::string_view id, double value) override {
        if constexpr (std::is_base_of_v<CommandSource, View>)
            (*view_).change_setting(id, value);
    }
    std::string key_command(std::uint32_t key) const override {
        if constexpr (std::is_base_of_v<CommandSource, View>)
            return (*view_).key_command(key);
        return {};
    }
    bool back() override {
        if constexpr (std::is_base_of_v<CommandSource, View>)
            return (*view_).back();
        return false;
    }
  protected:
    std::shared_ptr<View> view_;
};
struct HelpTopic { const char* id; const char* title; const char* text; };
struct GameDescriptor {
    Entry entry;
    const char* id;
    EntryInfo info;
    const char* help;
    std::vector<HelpTopic> help_topics;
    bool hosted; // draws its own live surface and plays its own music
    bool rail;   // the capsule sits on a rail above the game instead of floating over it
    std::unique_ptr<GameInstance> (*create)(ModuleContext&);
    void (*cover)(gf::Painter&,gf::Rect);
};
const GameDescriptor& game_descriptor(Entry entry);
std::optional<Entry> find_entry(std::string_view id);
} // namespace games
