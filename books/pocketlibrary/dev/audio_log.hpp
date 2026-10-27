#pragma once
// What the silent audio adapter was asked to play, for tests.
#include <string>
#include <vector>

namespace pl {
const std::vector<std::string>& audio_log();
void audio_log_clear();
}  // namespace pl
