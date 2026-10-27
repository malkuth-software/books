#pragma once
#include "gui_forms/control.hpp"
#include <string_view>
namespace games {
class HelpHost {
  public:
    virtual ~HelpHost() = default;
    virtual void show_help(std::string_view topic = {}) = 0;
};
// Hosted games share the collection document; standalone consumers keep their own help.
inline bool route_help(gui_forms::Control& game, std::string_view topic = {}) {
    for (std::shared_ptr<gui_forms::Control> parent = game.parent(); parent;
         parent = (*parent).parent())
        if (HelpHost* host = dynamic_cast<HelpHost*>(parent.get())) {
            (*host).show_help(topic);
            return true;
        }
    return false;
}
} // namespace games
