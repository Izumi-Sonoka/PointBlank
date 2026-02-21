#pragma once

#include <string>
#include <optional>
#include <X11/Xlib.h>

namespace pblank {

class XServerManager {
public:
    static Display* initializeDisplay(std::optional<std::string> display_name = std::nullopt);
    static bool isXServerRunning(const std::string& display_name = ":0");
    static bool startXServer(const std::string& display_name = ":0", const std::string& vt = "");
    static void shutdownXServer();
    static void setXEnvironment(const std::string& display_name);
    
private:
    static bool we_started_x_;
    static pid_t x_server_pid_;
    static std::string current_display_;
};

} 
