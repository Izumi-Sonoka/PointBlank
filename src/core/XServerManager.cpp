#include "pointblank/core/XServerManager.hpp"
#include <cstdlib>
#include <X11/Xlib.h>

namespace pblank {

bool XServerManager::we_started_x_ = false;
pid_t XServerManager::x_server_pid_ = -1;
std::string XServerManager::current_display_ = "";

Display* XServerManager::initializeDisplay(std::optional<std::string> display_name) {
    const char* display_env = std::getenv("DISPLAY");
    std::string display_to_try;
    
    if (display_name.has_value()) {
        display_to_try = *display_name;
    } else if (display_env != nullptr && display_env[0] != '\0') {
        display_to_try = display_env;
    } else {
        display_to_try = ":0";
    }
    
    return XOpenDisplay(display_to_try.c_str());
}

bool XServerManager::isXServerRunning(const std::string& display_name) {
    Display* d = XOpenDisplay(display_name.c_str());
    if (d) { XCloseDisplay(d); return true; }
    return false;
}

bool XServerManager::startXServer(const std::string&, const std::string&) {
    return false;
}

void XServerManager::shutdownXServer() {}

void XServerManager::setXEnvironment(const std::string& display_name) {
    setenv("DISPLAY", display_name.c_str(), 1);
}

} // namespace pblank
