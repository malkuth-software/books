#include "dogfood.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace games::dogfood;
static bool rejected(const std::vector<const char*>& arguments) {
    try { static_cast<void>(parse_options(static_cast<int>(arguments.size()),arguments.data())); }
    catch(const std::runtime_error&) { return true; }
    return false;
}
int main() {
    assert(rejected({"games","--script","test.txt"}));
    assert(rejected({"games","--standalone"}));
    assert(rejected({"games","--game"}));
    assert(rejected({"games","--smoke-test","--profile-idle","0"}));
    assert(rejected({"games","--unknown"}));
    const std::vector<const char*> argv{"games","--game","maze","--standalone","--dev","--smoke-test"};
    const Options options=parse_options(static_cast<int>(argv.size()),argv.data());
    assert(options.game=="maze" && options.standalone && options.dev && options.smoke);
    const std::filesystem::path script=std::filesystem::temp_directory_path()/"playsuite-script-test.txt";
    const char* bad[]={"0 help\n", "0 quit\n1 help\n", "1 help\n0 quit\n", "-1 quit\n", "300001 quit\n", "0 click nan 2\n1 quit\n", "0 execute command\n1 quit\n"};
    for(const char* text:bad) {
        { std::ofstream output(script); output<<text; }
        bool failed=false;
        try { static_cast<void>(read_script(script)); } catch(const std::runtime_error&) { failed=true; }
        assert(failed);
    }
    { std::ofstream output(script); output<<"# native smoke\n0 help\n40 key escape\n80 click 100 200\n100 resize 600 420\n150 action auto12\n200 capture output frame.ppm\n1000 quit\n"; }
    const std::vector<Action> actions=read_script(script);
    assert(actions.size()==7 && actions.back().verb=="quit" && actions[2].x==100 && actions[4].argument=="auto12" && actions[5].argument=="output frame.ppm");
    std::filesystem::remove(script);
    std::cout<<"Native script parsing, bounds and development-mode requirements pass.\n";
}
