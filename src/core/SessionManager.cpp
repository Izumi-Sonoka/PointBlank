/**
 * @file SessionManager.cpp
 * @brief Implementation of Session Environment Manager
 * 
 * @author Point Blank Systems Engineering Team
 * @version 1.0.0
 */

#include "pointblank/core/SessionManager.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>

namespace pblank {





bool SessionManager::setupEnvironment() {
    std::cout << "[SessionManager] Setting up XDG environment..." << std::endl;
    
    
    if (!setEnvIfUnset("XDG_SESSION_TYPE", "x11", false)) {
        std::cerr << "[SessionManager] Warning: Failed to set XDG_SESSION_TYPE" << std::endl;
    }
    
    
    
    
    
    
    
    
    if (!ensureRuntimeDir()) {
        std::cerr << "[SessionManager] Warning: Could not ensure XDG_RUNTIME_DIR" << std::endl;
        
    }
    
    
    const char* session_type = std::getenv("XDG_SESSION_TYPE");
    const char* current_desktop = std::getenv("XDG_CURRENT_DESKTOP");
    const char* session_desktop = std::getenv("XDG_SESSION_DESKTOP");
    
    std::cout << "[SessionManager] Environment configured:" << std::endl;
    std::cout << "[SessionManager]   XDG_SESSION_TYPE=" << (session_type ? session_type : "(not set)") << std::endl;
    std::cout << "[SessionManager]   XDG_CURRENT_DESKTOP=" << (current_desktop ? current_desktop : "(not set)") << std::endl;
    std::cout << "[SessionManager]   XDG_SESSION_DESKTOP=" << (session_desktop ? session_desktop : "(not set)") << std::endl;
    
    return true;
}

bool SessionManager::updateDBusEnvironment() {
    std::cout << "[SessionManager] Updating D-Bus activation environment..." << std::endl;
    
    
    
    std::string env_vars = "XDG_SESSION_TYPE";
    
    
    
    std::string dbus_cmd = "dbus-update-activation-environment --all " + env_vars + " 2>/dev/null";
    
    bool dbus_success = runCommand(dbus_cmd, false);
    
    if (dbus_success) {
        std::cout << "[SessionManager] D-Bus environment updated via dbus-update-activation-environment" << std::endl;
    } else {
        std::cerr << "[SessionManager] Warning: dbus-update-activation-environment failed or not available" << std::endl;
    }
    
    
    
    if (isSystemd()) {
        std::string systemd_cmd = "systemctl --user import-environment " + env_vars + " 2>/dev/null";
        if (runCommand(systemd_cmd, false)) {
            std::cout << "[SessionManager] Systemd user environment updated" << std::endl;
        }
    }
    
    
    
    return true;
}

bool SessionManager::launchPortals() {
    std::cout << "[SessionManager] Launching desktop portal services..." << std::endl;
    
    
    
    const char* xdg_session_type = getenv("XDG_SESSION_TYPE");
    bool is_wayland = xdg_session_type && strcmp(xdg_session_type, "wayland") == 0;
    
    
    
    if (!is_wayland) {
        std::cout << "[SessionManager] X11 session detected - skipping portal services" << std::endl;
        std::cout << "[SessionManager] Desktop integration features may be limited" << std::endl;
        return true;  
    }
    
    bool any_success = false;
    
    
    if (processExists("xdg-desktop-portal")) {
        std::cout << "[SessionManager] xdg-desktop-portal already running" << std::endl;
        any_success = true;
    } else {
        
        if (runCommand("xdg-desktop-portal &", true)) {
            std::cout << "[SessionManager] Launched xdg-desktop-portal" << std::endl;
            
            
            if (waitForProcess("xdg-desktop-portal", 500)) {
                any_success = true;
            }
        } else {
            std::cerr << "[SessionManager] Warning: Failed to launch xdg-desktop-portal" << std::endl;
        }
    }
    
    
    usleep(100000);  
    
    
    if (launchPortalBackend()) {
        any_success = true;
    }
    
    return any_success;
}

bool SessionManager::initializeSession() {
    std::cout << "[SessionManager] ========================================" << std::endl;
    std::cout << "[SessionManager] Initializing session environment..." << std::endl;
    std::cout << "[SessionManager] ========================================" << std::endl;
    
    
    if (!setupEnvironment()) {
        std::cerr << "[SessionManager] Error: Failed to set up environment" << std::endl;
        return false;
    }
    
    
    if (!updateDBusEnvironment()) {
        
        std::cerr << "[SessionManager] Warning: D-Bus environment update had issues" << std::endl;
    }
    
    
    if (!launchPortals()) {
        
        std::cerr << "[SessionManager] Warning: Portal launch had issues" << std::endl;
    }
    
    std::cout << "[SessionManager] ========================================" << std::endl;
    std::cout << "[SessionManager] Session initialization complete" << std::endl;
    std::cout << "[SessionManager] ========================================" << std::endl;
    
    return true;
}





bool SessionManager::isSystemd() {
    
    return access("/run/systemd/system", F_OK) == 0;
}

bool SessionManager::isOpenRC() {
    
    return access("/run/openrc", F_OK) == 0;
}

bool SessionManager::processExists(const std::string& name) {
    
    std::error_code ec;
    const std::filesystem::path proc_path("/proc");
    
    if (!std::filesystem::exists(proc_path, ec)) {
        return false;
    }
    
    
    for (const auto& entry : std::filesystem::directory_iterator(proc_path, ec)) {
        if (ec) continue;
        
        const std::string& entry_name = entry.path().filename();
        
        
        bool is_pid = !entry_name.empty() && 
                      std::all_of(entry_name.begin(), entry_name.end(), ::isdigit);
        if (!is_pid) continue;
        
        
        std::filesystem::path cmdline_path = entry.path() / "cmdline";
        std::ifstream cmdline_file(cmdline_path, std::ios::binary);
        if (!cmdline_file.is_open()) continue;
        
        
        std::string cmdline((std::istreambuf_iterator<char>(cmdline_file)),
                           std::istreambuf_iterator<char>());
        cmdline_file.close();
        
        
        std::replace(cmdline.begin(), cmdline.end(), '\0', ' ');
        
        
        
        if (cmdline.find(name) != std::string::npos) {
            
            
            size_t pos = cmdline.find(name);
            while (pos != std::string::npos) {
                bool valid_start = (pos == 0 || cmdline[pos-1] == ' ' || cmdline[pos-1] == '\0');
                bool valid_end = (pos + name.length() >= cmdline.length() || 
                                 cmdline[pos + name.length()] == ' ' || 
                                 cmdline[pos + name.length()] == '\0');
                if (valid_start && valid_end) {
                    return true;
                }
                pos = cmdline.find(name, pos + 1);
            }
        }
    }
    
    return false;
}

std::string SessionManager::getSessionType() {
    const char* session_type = std::getenv("XDG_SESSION_TYPE");
    return session_type ? std::string(session_type) : "";
}

bool SessionManager::isSessionConfigured() {
    const char* session_type = std::getenv("XDG_SESSION_TYPE");
    const char* current_desktop = std::getenv("XDG_CURRENT_DESKTOP");
    const char* session_desktop = std::getenv("XDG_SESSION_DESKTOP");
    
    return (session_type != nullptr && 
            current_desktop != nullptr && 
            session_desktop != nullptr);
}





bool SessionManager::setEnvIfUnset(const char* name, const char* value, 
                                    bool overwrite) {
    const char* existing = std::getenv(name);
    
    if (existing != nullptr && existing[0] != '\0' && !overwrite) {
        
        return true;
    }
    
    
    
    return setenv(name, value, overwrite ? 1 : 0) == 0;
}

bool SessionManager::runCommand(const std::string& cmd, bool background) {
    
    std::string full_cmd;
    
    if (background) {
        
        full_cmd = "(" + cmd + ") > /dev/null 2>&1 &";
    } else {
        
        full_cmd = cmd + " > /dev/null 2>&1";
    }
    
    int result = system(full_cmd.c_str());
    
    
    
    if (background) {
        return true;
    }
    
    return WIFEXITED(result) && WEXITSTATUS(result) == 0;
}

bool SessionManager::ensureRuntimeDir() {
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    
    if (runtime_dir != nullptr && runtime_dir[0] != '\0') {
        
        if (std::filesystem::exists(runtime_dir)) {
            return true;
        }
    }
    
    
    std::string uid = std::to_string(getuid());
    std::string runtime_path = "/run/user/" + uid;
    
    if (std::filesystem::exists(runtime_path)) {
        
        setenv("XDG_RUNTIME_DIR", runtime_path.c_str(), 0);
        return true;
    }
    
    
    runtime_path = "/tmp/runtime-" + uid;
    
    
    if (!std::filesystem::exists(runtime_path)) {
        std::error_code ec;
        if (!std::filesystem::create_directory(runtime_path, ec)) {
            return false;
        }
        
        
        chmod(runtime_path.c_str(), S_IRWXU);
    }
    
    setenv("XDG_RUNTIME_DIR", runtime_path.c_str(), 0);
    return true;
}

bool SessionManager::launchPortalBackend() {
    
    std::vector<std::pair<std::string, std::string>> backends = {
        {"xdg-desktop-portal-gtk", "GTK portal backend"},
        {"xdg-desktop-portal-wlr", "wlroots portal backend"},
        {"xdg-desktop-portal-kde", "KDE portal backend"},
        {"xdg-desktop-portal-gnome", "GNOME portal backend"}
    };
    
    for (const auto& [executable, description] : backends) {
        if (processExists(executable)) {
            std::cout << "[SessionManager] " << description << " already running" << std::endl;
            return true;
        }
        
        
        std::string check_cmd = "which " + executable + " > /dev/null 2>&1";
        if (system(check_cmd.c_str()) == 0) {
            
            std::string launch_cmd = executable + " &";
            if (runCommand(launch_cmd, true)) {
                std::cout << "[SessionManager] Launched " << description << std::endl;
                
                
                if (waitForProcess(executable, 300)) {
                    return true;
                }
            }
        }
    }
    
    std::cerr << "[SessionManager] Warning: No portal backend could be launched" << std::endl;
    std::cerr << "[SessionManager] Desktop integration features may be limited" << std::endl;
    
    return false;
}

bool SessionManager::waitForProcess(const std::string& name, int timeout_ms) {
    int elapsed = 0;
    int check_interval = 100;  
    
    while (elapsed < timeout_ms) {
        if (processExists(name)) {
            return true;
        }
        
        usleep(check_interval * 1000);  
        elapsed += check_interval;
    }
    
    return false;
}

} 