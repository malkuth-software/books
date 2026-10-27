#include "collection.hpp"
#include "dogfood.hpp"
#include "gui_forms/application.hpp"
#include "gui_forms/paint_framebuffer.hpp"
#include <fstream>
#include "runtime_paths.hpp"
#include <iostream>
#include <map>
#include <stdexcept>

namespace gf = gui_forms;
struct RunState final {
    games::dogfood::Options options;
    std::vector<games::dogfood::Action> actions;
    std::size_t next = 0;
    gf::FrameRequestToken timer{};
    gf::FrameTime started{}, sample_start{}, resize_until{};
    bool completed = false, sampling = false, resizing = false;
    gf::Size resize_from{};
    std::weak_ptr<gf::Control> root;
    std::weak_ptr<games::Collection> collection;
    std::shared_ptr<games::GameInstance> standalone;
};
static std::uint32_t key_code(const std::string& name) {
    const std::map<std::string,std::uint32_t> keys{
        {"up",gf::PhysicalKey::up},{"down",gf::PhysicalKey::down},
        {"left",gf::PhysicalKey::left},{"right",gf::PhysicalKey::right},
        {"enter",gf::PhysicalKey::enter},{"space",gf::PhysicalKey::space},
        {"escape",gf::PhysicalKey::escape},{"tab",gf::PhysicalKey::tab},
        {"home",gf::PhysicalKey::home},{"end",gf::PhysicalKey::end},
        {"page_up",gf::PhysicalKey::page_up},{"page_down",gf::PhysicalKey::page_down},
        {"f1",gf::PhysicalKey::f1},{"f2",gf::PhysicalKey::f2},
        {"w",gf::PhysicalKey::w},{"a",gf::PhysicalKey::a},{"s",gf::PhysicalKey::s},
        {"d",gf::PhysicalKey::d},{"h",gf::PhysicalKey::h},{"m",gf::PhysicalKey::m},
        {"t",gf::PhysicalKey::t},{"z",gf::PhysicalKey::z},{"n",gf::PhysicalKey::n},
        {"backspace",gf::PhysicalKey::backspace}};
    const std::map<std::string,std::uint32_t>::const_iterator found = keys.find(name);
    if (found == keys.end()) throw std::runtime_error("Unsupported script key: " + name);
    return (*found).second;
}
static void invalidate_capture(gf::Control& control) {
    if(!control.visible()) return;
    control.invalidate(gf::Dirty::paint);
    for(const std::shared_ptr<gf::Control>& child:control.children()) invalidate_capture(*child);
}
// Re-record visible controls so direct-surface games are included even when their
// normal host presentation bypasses the retained background display chunk.
// Capture through the same native renderer, including hosted help.
static void capture_frame(gf::Window& window,const std::filesystem::path& path) {
    const gf::Size size=window.client_size();
    const gf::Rect bounds{0,0,size.width,size.height};
    std::unique_ptr<gf::PaintFramebuffer> frame=window.create_framebuffer(size,window.scale());
    if(!frame || !(*frame).begin(window.image_resources(),bounds))
        throw std::runtime_error("Native renderer could not capture the window");
    invalidate_capture(*window.root());
    static_cast<void>(window.paint((*frame).painter(),bounds));
    (*frame).end();
    if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path,std::ios::binary|std::ios::trunc);
    output<<"P6\n"<<(*frame).width()<<' '<<(*frame).height()<<"\n255\n";
    const std::span<std::byte> pixels=(*frame).pixels();
    const bool bgra=(*frame).channel_order()==gf::FramebufferChannelOrder::bgra;
    for(std::uint32_t y=0;y<(*frame).height();++y) {
        const unsigned char* row=reinterpret_cast<const unsigned char*>(pixels.data()+y*(*frame).row_bytes());
        for(std::uint32_t x=0;x<(*frame).width();++x) {
            const char rgb[]={static_cast<char>(row[x*4+(bgra?2:0)]),
                              static_cast<char>(row[x*4+1]),static_cast<char>(row[x*4+(bgra?0:2)])};
            output.write(rgb,3);
        }
    }
    output.close();
    if(!output) throw std::runtime_error("Cannot write native capture: "+path.string());
}
struct RunTick final {
    std::weak_ptr<RunState> state;
    gf::Window* window = nullptr;
    gf::ApplicationWindowHandle handle;
    void perform(RunState& live,const games::dogfood::Action& action) const {
        const std::shared_ptr<games::Collection> collection = live.collection.lock();
        if (action.verb == "quit") {
            live.completed = true;
            static_cast<void>(handle.request_close());
        } else if (action.verb == "help") {
            if (collection) (*collection).show_help();
            else (*live.standalone).run_command("help");
        } else if (action.verb == "shelf" || action.verb == "open") {
            throw std::runtime_error("The Pocket Library has no shelf of games: " + action.verb);
        } else if (action.verb == "command") {
            if (collection) (*collection).dispatch_command(action.argument);
            else (*live.standalone).run_command(action.argument);
        } else if (action.verb == "action") {
            const bool accepted = collection ? (*collection).dispatch_action(action.argument)
                : (*live.standalone).scripted_action(action.argument);
            if (!accepted) throw std::runtime_error("Game rejected development action: " + action.argument);
        } else if (action.verb == "key") {
            gf::KeyEvent key{};
            key.action = gf::KeyAction::down; key.physical_key = key_code(action.argument);
            static_cast<void>((*window).dispatch_key(key));
            key.action = gf::KeyAction::up;
            static_cast<void>((*window).dispatch_key(key));
        } else if (action.verb == "click") {
            gf::PointerEvent pointer{};
            pointer.position = {action.x,action.y}; pointer.button = gf::PointerButton::primary;
            pointer.action = gf::PointerAction::down;
            static_cast<void>((*window).dispatch_pointer(pointer));
            pointer.action = gf::PointerAction::up;
            static_cast<void>((*window).dispatch_pointer(pointer));
        } else if (action.verb == "down" || action.verb == "up") {
            // a press held across script lines, for controls that act when held
            gf::PointerEvent pointer{};
            pointer.position = {action.x,action.y}; pointer.button = gf::PointerButton::primary;
            pointer.action = action.verb == "down" ? gf::PointerAction::down : gf::PointerAction::up;
            static_cast<void>((*window).dispatch_pointer(pointer));
        } else if (action.verb == "hover") {
            gf::PointerEvent pointer{};
            pointer.position = {action.x,action.y}; pointer.action = gf::PointerAction::move;
            static_cast<void>((*window).dispatch_pointer(pointer));
        } else if (action.verb == "capture") {
            capture_frame(*window,action.argument);
        } else if (action.verb == "resize") {
            // A request to the native window: the model follows on the host's resize event,
            // and the window manager may choose another size, so the script waits for a change.
            const gf::HostServiceStatus status = handle.request_client_size({action.x,action.y});
            if (!status.accepted())
                throw std::runtime_error(std::string("Native window refused resize: ") +
                                         gf::host_service_error_name(status.error));
            live.resize_from = (*window).client_size();
            live.resizing = live.resize_from != gf::Size{action.x,action.y};
            live.resize_until = gf::FrameClock::now() + std::chrono::seconds(2);
        }
    }
    void operator()(gf::FrameTime now) const {
        const std::shared_ptr<RunState> live = state.lock();
        if (!live || (*live).completed) return;
        const double seconds = std::chrono::duration<double>(now - (*live).started).count();
        if ((*live).options.profile) {
            if (!(*live).sampling && seconds >= 5) {
                (*live).sampling = true; (*live).sample_start = now;
                (*window).reset_activity_metrics();
                std::cout << "PROFILE_BEGIN" << std::endl;
            } else if ((*live).sampling && seconds >= 15) {
                const double sampled = std::chrono::duration<double>(now - (*live).sample_start).count();
                std::cout << "PROFILE_METRICS {\"seconds\":" << sampled << ",\"window\":"
                          << (*window).metrics_snapshot().to_json() << "}\nPROFILE_END" << std::endl;
                (*live).sampling = false;
                (*live).options.profile = false;
                (*live).options.smoke = true;
                (*live).started = now - std::chrono::seconds(4);
            }
        } else if ((*live).options.smoke && seconds >= 5) {
            (*live).completed = true;
            static_cast<void>(handle.request_close());
        }
        if ((*live).resizing) {
            const bool unchanged = (*window).client_size() == (*live).resize_from;
            if (unchanged && now < (*live).resize_until) return;
            if (unchanged) std::cerr << "Script resize: the native window kept its size\n";
            (*live).resizing = false;
        }
        while ((*live).next < (*live).actions.size() &&
               (*live).actions[(*live).next].milliseconds <= seconds * 1000) {
            perform(*live,(*live).actions[(*live).next++]);
            if ((*live).completed || (*live).resizing) break;
        }
    }
};
struct Ready final {
    std::shared_ptr<RunState> state;
    void operator()(gf::Window& window,gf::ApplicationWindowHandle handle) const {
        const std::shared_ptr<gf::Control> root = (*state).root.lock();
        if (!root) return;
        const std::shared_ptr<games::Collection> collection = (*state).collection.lock();
        if (collection) {
            (*collection).activate();
        } else {
            (*(*state).standalone).preferences(true,!(*state).options.smoke,!(*state).options.smoke,false);
            (*(*state).standalone).activate();
        }
        static_cast<void>(window.request_focus(root));
        (*state).started = gf::FrameClock::now();
        if ((*state).options.smoke || (*state).options.profile || !(*state).actions.empty()) {
            const std::chrono::milliseconds interval((*state).options.profile ? 1000 : 10);
            (*state).timer = window.schedule_ui_timer(*root,interval,(*state).started+interval,
                                                       RunTick{state,&window,handle});
        }
    }
};
int main(int argc,char** argv) {
    try {
        const games::dogfood::Options arguments = games::dogfood::parse_options(argc,argv);
        if (arguments.help) { std::cout << games::dogfood::usage(); return 0; }
        if (arguments.list) {
            for (games::Entry entry : games::entries) {
                const games::GameDescriptor& game = games::game_descriptor(entry);
                std::cout << static_cast<int>(entry) << '\t' << game.id << '\t' << game.info.title << '\n';
            }
            return 0;
        }
        if (!arguments.game.empty() && !games::find_entry(arguments.game))
            throw std::runtime_error("Unknown game: " + arguments.game);
        if (arguments.profile && arguments.profile_entry >= 0 &&
            !games::valid_entry(static_cast<games::Entry>(arguments.profile_entry)))
            throw std::runtime_error("Unknown permanent game ID; use --list-games");
        games::dogfood::DevelopmentState saves(arguments.dev || arguments.smoke || arguments.profile);
        games::initialize_assets(argv[0]);
        if (!(arguments.dev || arguments.smoke || arguments.profile)) games::carry_over_playsuite_places();
        std::shared_ptr<RunState> state = std::make_shared<RunState>();
        (*state).options = arguments;
        if (!arguments.script.empty()) (*state).actions = games::dogfood::read_script(arguments.script);
        std::shared_ptr<gf::Control> root;
        games::ModuleContext context;
        context.dev = arguments.dev; context.hosted = !arguments.standalone;
        if (arguments.standalone) {
            (*state).standalone = games::game_descriptor(*games::find_entry(arguments.game)).create(context);
            root = (*(*state).standalone).control();
        } else {
            std::shared_ptr<games::Collection> collection = gf::make_control<games::Collection>(gf::StableId("games.table"),arguments.dev);
            (*state).collection = collection; root = collection;
        }
        (*state).root = root;
        std::unique_ptr<gf::Window> window = std::make_unique<gf::Window>(root,gf::Size{1060,680});
        gf::ApplicationWindowOptions options;
        options.title = games::suite_name();
        options.initial_size = {1060,680}; options.minimum_size = {600,420};
        options.print_metrics_on_close = true; options.ready = Ready{state};
        const gf::ApplicationResult result = gf::Application::run(std::move(window),std::move(options));
        if (result.callback_exception) std::rethrow_exception(result.callback_exception);
        if ((arguments.smoke || arguments.profile || !arguments.script.empty()) && !(*state).completed) return 1;
        if ((*state).completed && result.accepted()) std::cout << "Native Pocket Library window smoke passed\n";
        return result.accepted() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
