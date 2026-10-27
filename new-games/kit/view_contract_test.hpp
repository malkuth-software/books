#pragma once
// The contract every hosted PlaySuite game view must keep, as one reusable test.
// It drives the real control through a real (headless) GUI.Forms window: it
// publishes frames at the sizes and scales the shell uses, opens Help from the
// keyboard and from the capsule's command list, goes idle when nothing changes,
// and stops completely when the shelf hides it.
//
// A game adds a three-line test (see the template's tests/view_contract_tests.cpp)
// and registers it in cmake/Application.cmake. It builds with the application, so
// the repository's CI runs it on every platform.
#include "suite.hpp"

#include "gui_forms/live_surface.hpp"
#include "gui_forms/window.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace kit {
namespace gf = gui_forms;

struct ContractOptions {
    // False for a game with deliberate ambient motion (a breathing character, moving
    // water). Such a game must still stop when hidden; say why in its HANDOFF.md.
    bool settles_when_untouched = true;
    // Seconds allowed for opening animations to finish before the idle checks.
    double settle_seconds = 8;
    // Where to write frame previews (binary PPM). Empty writes none.
    std::string preview_directory;
};

inline void contract_require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// The save directories a test made, removed when it exits, so repeated runs do not
// fill the temporary folder.
inline std::vector<std::filesystem::path>& contract_save_directories() {
    static std::vector<std::filesystem::path> made;
    return made;
}

inline void contract_remove_saves() {
    for (const std::filesystem::path& directory : contract_save_directories()) {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
}

inline void contract_isolate_saves(const char* name) {
    const std::chrono::steady_clock::duration stamp = std::chrono::steady_clock::now().time_since_epoch();
    const std::filesystem::path directory = std::filesystem::temp_directory_path() /
        (std::string("playsuite-contract-") + name + "-" + std::to_string(stamp.count()));
    std::filesystem::create_directories(directory);
    if (contract_save_directories().empty()) {
        std::atexit(contract_remove_saves);
    }
    contract_save_directories().push_back(directory);
#ifdef _WIN32
    const int status = _wputenv_s(L"GAMES_STATE_DIR", directory.c_str());
#else
    const int status = setenv("GAMES_STATE_DIR", directory.c_str(), 1);
#endif
    contract_require(status == 0, "isolate the test's saves with GAMES_STATE_DIR");
}

inline void contract_write_frame(const gf::LiveSurfaceFrame& frame, const std::string& directory,
                                 const std::string& file) {
    if (directory.empty()) {
        return;
    }
    std::filesystem::create_directories(directory);
    std::ofstream output(std::filesystem::path(directory) / file, std::ios::binary);
    output << "P6\n" << frame.width() << ' ' << frame.height() << "\n255\n";
    const std::span<const std::byte> bytes = frame.pixels();
    for (std::uint32_t y = 0; y < frame.height(); ++y) {
        const std::size_t row = static_cast<std::size_t>(y) * frame.row_bytes();
        for (std::uint32_t x = 0; x < frame.width(); ++x) {
            const std::size_t offset = row + static_cast<std::size_t>(x) * 4;
            const char rgb[3] = {static_cast<char>(bytes[offset + 2]), static_cast<char>(bytes[offset + 1]),
                                 static_cast<char>(bytes[offset])};
            output.write(rgb, 3);
        }
    }
}

// Pumps the window until the view publishes a frame newer than `after`.
inline gf::LiveSurfaceFrame contract_next_frame(gf::Window& window, std::shared_ptr<gf::LiveSurface>& surface,
                                                std::uint64_t after, const char* waiting_for) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while (std::chrono::steady_clock::now() < deadline) {
        static_cast<void>(window.poll_frame_schedule(gf::FrameClock::now()));
        const std::vector<gf::LiveSurfacePresentation> presentations = window.take_live_surface_presentations();
        for (std::size_t index = 0; index < presentations.size(); ++index) {
            surface = presentations[index].surface;
        }
        // A resized surface shows its previous frame until the game publishes at the new
        // size; that carried frame is not the game's answer.
        if (surface && !(*surface).snapshot().carries_previous_frame) {
            gf::LiveSurfaceFrame frame = (*surface).acquire_latest();
            if (frame && frame.generation() > after) {
                return frame;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error(std::string("no frame was published in time: ") + waiting_for);
}

inline std::uint64_t contract_generation(const std::shared_ptr<gf::LiveSurface>& surface) {
    const std::uint64_t generation = (*surface).snapshot().published_generation;
    return generation;
}

// Quiet: nothing is scheduled, or the only wake is a second or more away. A game may
// hold one slow wake for a timed event (a character's remark after a long pause); it
// may not keep ticking.
inline bool contract_quiet(const gf::Window& window) {
    const std::optional<gf::FrameTime> wake = window.next_wake();
    if (!wake) {
        return true;
    }
    const bool far = *wake - gf::FrameClock::now() >= std::chrono::milliseconds(900);
    return far;
}

// Lets opening animations finish. Returns true once the window is quiet.
inline bool contract_settle(gf::Window& window, double seconds) {
    const gf::FrameTime deadline =
        gf::FrameClock::now() + std::chrono::milliseconds(static_cast<long long>(seconds * 1000));
    while (!contract_quiet(window) && gf::FrameClock::now() < deadline) {
        static_cast<void>(window.poll_frame_schedule(gf::FrameClock::now()));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const bool settled = contract_quiet(window);
    return settled;
}

inline const games::GameCommand* contract_find(const std::vector<games::GameCommand>& commands,
                                               const std::string& id) {
    for (std::size_t index = 0; index < commands.size(); ++index) {
        if (commands[index].id == id) {
            return &commands[index];
        }
    }
    return nullptr;
}

// View: the game's control. ViewOptions: its options record, constructed by the caller
// with hosted and dev both true.
template <class View, class ViewOptions>
int run_view_contract(const char* name, const ViewOptions& view_options, const ContractOptions& options) {
    try {
        contract_isolate_saves(name);
        std::shared_ptr<View> view = gf::make_control<View>(gf::StableId(name), view_options);
        (*view).set_cabinet(true, false, false, true);
        gf::Window window(view, {1180, 800});
        window.set_active(true);
        window.perform_layout();
        static_cast<void>(window.request_focus(view));
        std::shared_ptr<gf::LiveSurface> surface{};

        // 1. A complete first frame at the window's size.
        gf::LiveSurfaceFrame first = contract_next_frame(window, surface, 0, "the first frame");
        contract_require(first.width() == 1180 && first.height() == 800,
                         "the first frame fills the 1180 x 800 surface");
        contract_write_frame(first, options.preview_directory, std::string(name) + "-board.ppm");
        first = {};

        // 2. The capsule's commands: something primary, and Help.
        const std::vector<games::GameCommand> commands = (*view).commands();
        contract_require(!commands.empty(), "the game offers commands to the capsule");
        bool has_primary = false;
        for (std::size_t index = 0; index < commands.size(); ++index) {
            contract_require(!commands[index].id.empty() && !commands[index].label.empty(),
                             "every command has an id and a label");
            has_primary = has_primary || commands[index].primary;
        }
        contract_require(has_primary, "one command (usually New game) is primary, shown on the folded capsule");
        contract_require(contract_find(commands, "help") != nullptr, "the game has a command with the id \"help\"");

        // 3. Help opens from the keyboard (F1), shows in the command list, and closes with Escape.
        gf::KeyEvent key{};
        key.action = gf::KeyAction::down;
        key.physical_key = gf::PhysicalKey::f1;
        std::uint64_t before = contract_generation(surface);
        contract_require(window.dispatch_key(key), "F1 is handled");
        gf::LiveSurfaceFrame help = contract_next_frame(window, surface, before, "the help panel after F1");
        contract_write_frame(help, options.preview_directory, std::string(name) + "-help.ppm");
        help = {};
        const std::vector<games::GameCommand> with_help = (*view).commands();
        const games::GameCommand* help_command = contract_find(with_help, "help");
        contract_require(help_command != nullptr && (*help_command).checked,
                         "the help command is checked while help is open");

        // 4. A display scale of 150 percent gives a native-resolution frame.
        window.set_scale(1.5);
        window.perform_layout();
        before = contract_generation(surface);
        gf::LiveSurfaceFrame scaled = contract_next_frame(window, surface, before, "the frame at 150 percent");
        contract_require(scaled.width() == 1770 && scaled.height() == 1200,
                         "at 150 percent the frame is 1770 x 1200 device pixels");
        scaled = {};

        // 5. The smallest surfaces: 600 x 370 under the rail, 600 x 320 under an open, wrapped capsule.
        window.set_scale(1);
        window.resize({600, 370});
        window.perform_layout();
        before = contract_generation(surface);
        gf::LiveSurfaceFrame small_help = contract_next_frame(window, surface, before, "help at 600 x 370");
        contract_require(small_help.width() == 600 && small_help.height() == 370, "the frame follows a resize to 600 x 370");
        contract_write_frame(small_help, options.preview_directory, std::string(name) + "-help-600x370.ppm");
        small_help = {};
        key.physical_key = gf::PhysicalKey::escape;
        before = contract_generation(surface);
        static_cast<void>(window.dispatch_key(key));
        gf::LiveSurfaceFrame small_board = contract_next_frame(window, surface, before, "the board after Escape");
        contract_write_frame(small_board, options.preview_directory, std::string(name) + "-board-600x370.ppm");
        small_board = {};
        const std::vector<games::GameCommand> closed = (*view).commands();
        const games::GameCommand* closed_help = contract_find(closed, "help");
        contract_require(closed_help != nullptr && !(*closed_help).checked, "Escape closes help");
        window.resize({600, 320});
        window.perform_layout();
        before = contract_generation(surface);
        gf::LiveSurfaceFrame smallest = contract_next_frame(window, surface, before, "the board at 600 x 320");
        contract_require(smallest.width() == 600 && smallest.height() == 320, "the frame follows a resize to 600 x 320");
        contract_write_frame(smallest, options.preview_directory, std::string(name) + "-board-600x320.ppm");
        smallest = {};

        // 6. Idle: an untouched, settled game schedules nothing and republishes nothing.
        if (options.settles_when_untouched) {
            contract_require(contract_settle(window, options.settle_seconds),
                             "an untouched game stops ticking once its picture has settled");
            const std::uint64_t settled = contract_generation(surface);
            static_cast<void>(window.poll_frame_schedule(gf::FrameClock::now() + std::chrono::milliseconds(800)));
            contract_require(contract_generation(surface) == settled,
                             "a settled game does not republish an unchanged frame");
            // ... and a command wakes it again.
            (*view).run_command("help");
            gf::LiveSurfaceFrame woken = contract_next_frame(window, surface, settled, "a frame after a capsule command");
            woken = {};
            (*view).run_command("help");
        }

        // 7. Hidden behind the shelf: no timer callbacks, no frames. Then it resumes.
        (*view).set_cabinet(false, false, false, true);
        (*view).set_visible(false);
        const std::uint64_t hidden = contract_generation(surface);
        const std::uint64_t callbacks = window.metrics_snapshot().callbacks_emitted;
        static_cast<void>(window.poll_frame_schedule(gf::FrameClock::now() + std::chrono::seconds(2)));
        contract_require(window.metrics_snapshot().callbacks_emitted == callbacks,
                         "a hidden game has no timer callbacks");
        contract_require(contract_generation(surface) == hidden, "a hidden game publishes nothing");
        (*view).set_visible(true);
        (*view).set_cabinet(true, false, false, true);
        gf::LiveSurfaceFrame resumed = contract_next_frame(window, surface, hidden, "a frame after returning from the shelf");
        contract_require(resumed.width() > 0, "the game resumes with its surface");
        resumed = {};

        std::cout << name << ": frames, help, scale, small sizes, idle and hidden contract passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << name << ": " << error.what() << '\n';
        return 1;
    }
}

}  // namespace kit
