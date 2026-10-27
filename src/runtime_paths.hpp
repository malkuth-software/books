#pragma once
#include <filesystem>
#include <string>
namespace games {
void initialize_assets(const char* executable);
std::string asset_directory();
// The per-user save folder: Rainstar/Pocket Library (GAMES_STATE_DIR, when set, for
// tests and development).
std::filesystem::path state_directory();
// The first time the Pocket Library runs, the places kept by PlaySuite's Pocket Library
// (before the library had its own application) are copied into its own folder.
void carry_over_playsuite_places();
}
