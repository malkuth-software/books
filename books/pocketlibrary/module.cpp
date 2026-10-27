#include "game_module.hpp"
#include "pocketlibrary_view.hpp"

namespace games::modules {
std::unique_ptr<GameInstance> pocketlibrary_create(ModuleContext& context) {
    const pl::Options options{.hosted = context.hosted, .dev = context.dev};
    return std::make_unique<HostedGameInstance<pl::PocketLibraryView>>(
        gf::make_control<pl::PocketLibraryView>(gf::StableId("pocketlibrary.view"), options));
}
} // namespace games::modules
