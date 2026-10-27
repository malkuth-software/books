#include "open_link.hpp"
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <utility>
#include <vector>
extern char** environ;
#endif
namespace games {
#ifdef _WIN32
void open_link(const std::string& address) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, address.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length > 0 ? length : 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, address.c_str(), -1, wide.data(), length);
    ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
#else
namespace {
std::vector<pid_t> launched;
}
void open_link(const std::string& address) {
    // The opener exits as soon as it has handed the address on; collect earlier ones.
    std::vector<pid_t> running;
    for (pid_t pid : launched)
        if (waitpid(pid, nullptr, WNOHANG) == 0)
            running.push_back(pid);
    launched = std::move(running);
#ifdef __APPLE__
    const char* opener = "/usr/bin/open";
#else
    const char* opener = "xdg-open";
#endif
    std::string program(opener), target(address);
    char* arguments[] = {program.data(), target.data(), nullptr};
    pid_t pid = 0;
    if (posix_spawnp(&pid, opener, nullptr, nullptr, arguments, environ) == 0)
        launched.push_back(pid);
}
#endif
} // namespace games
