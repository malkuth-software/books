#pragma once
#include <string>
namespace games {
// Opens a web address in the system browser. GUI.Forms has no such service yet
// (docs/TOOLKIT_REQUESTS.md); only the shell's own fixed addresses come here.
void open_link(const std::string& address);
} // namespace games
