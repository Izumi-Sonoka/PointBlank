#pragma once

#include <X11/Xlib.h>
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace pblank {

/**
 * @brief Startup Application Manager
 * 
 * Manages applications that should be launched when the window manager starts.
 * Supports:
 * - Launching applications after a delay
 * - Waiting for the window manager to fully initialize
 * - Launching on specific workspaces
 * - Autostart desktop file support (XDG autostart)
 */
class StartupApps {
public:
    StartupApps();
    ~StartupApps() = default;
    
    StartupApps(const StartupApps&) = delete;
    StartupApps& operator=(const StartupApps&) = delete;
    
    void loadFromConfig(const std::string& config_content);
    
    void loadXDGAutostart();
    
    void launchAll();
    
    void addApp(const std::string& command, int delay_ms = 0, int workspace = -1);
    
    void setLauncher(std::function<void(const std::string&)> launcher);

private:
    struct StartupApp {
        std::string command;
        int delay_ms;
        int workspace;
        bool launched;
        
        StartupApp(const std::string& cmd, int delay, int ws)
            : command(cmd), delay_ms(delay), workspace(ws), launched(false) {}
    };
    
    std::vector<StartupApp> apps_;
    std::function<void(const std::string&)> launcher_;
    
    std::string parseDesktopFile(const std::string& path) const;
    
    std::string getAutostartDir() const;
};

inline StartupApps::StartupApps() = default;

inline void StartupApps::setLauncher(std::function<void(const std::string&)> launcher) {
    launcher_ = std::move(launcher);
}

} 
