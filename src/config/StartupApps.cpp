#include "pointblank/config/StartupApps.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <filesystem>
#include <pwd.h>

namespace pblank {

void StartupApps::loadFromConfig(const std::string& config_content) {
    
    
    std::istringstream iss(config_content);
    std::string line;
    
    while (std::getline(iss, line)) {
        
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);
        
        
        if (line[0] == '/' || line[0] == '#') continue;
        
        
        if (line.rfind("startup:", 0) == 0 || line.rfind("exec:", 0) == 0) {
            size_t colon = line.find(':');
            std::string cmd = line.substr(colon + 1);
            
            
            cmd.erase(std::remove(cmd.begin(), cmd.end(), '\"'), cmd.end());
            cmd.erase(std::remove(cmd.begin(), cmd.end(), '\''), cmd.end());
            
            
            int delay = 0;
            size_t delay_pos = cmd.find("delay:");
            if (delay_pos != std::string::npos) {
                std::string delay_str = cmd.substr(delay_pos + 6);
                delay = std::stoi(delay_str);
                cmd = cmd.substr(0, delay_pos);
            }
            
            
            start = cmd.find_first_not_of(" \t");
            if (start != std::string::npos) {
                cmd = cmd.substr(start);
            }
            
            if (!cmd.empty()) {
                addApp(cmd, delay);
            }
        }
    }
}

void StartupApps::loadXDGAutostart() {
    std::string autostart_dir = getAutostartDir();
    
    if (!std::filesystem::exists(autostart_dir)) {
        return;
    }
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(autostart_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".desktop") {
                std::string cmd = parseDesktopFile(entry.path());
                if (!cmd.empty()) {
                    addApp(cmd);
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "StartupApps: Error loading autostart: " << e.what() << std::endl;
    }
}

void StartupApps::launchAll() {
    if (!launcher_) {
        std::cerr << "StartupApps: No launcher callback set" << std::endl;
        return;
    }
    
    for (auto& app : apps_) {
        if (app.launched) continue;
        
        if (app.delay_ms > 0) {
            std::thread([this, &app]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(app.delay_ms));
                if (!app.launched) {
                    launcher_(app.command);
                    app.launched = true;
                }
            }).detach();
        } else {
            launcher_(app.command);
            app.launched = true;
        }
    }
}

void StartupApps::addApp(const std::string& command, int delay_ms, int workspace) {
    apps_.emplace_back(command, delay_ms, workspace);
}

std::string StartupApps::parseDesktopFile(const std::string& path) const {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    
    bool hidden = false;
    std::string exec_cmd;
    
    std::string line;
    while (std::getline(file, line)) {
        
        if (line.find("Hidden=true") == 0 || line.find("Hidden=true") != std::string::npos) {
            hidden = true;
            break;
        }
        
        
        if (line.find("OnlyShownIn=") == 0) {
            
            continue;
        }
        
        
        if (line.find("Exec=") == 0) {
            exec_cmd = line.substr(5);
            
            
            
            size_t pos;
            while ((pos = exec_cmd.find('%')) != std::string::npos) {
                size_t end = pos + 1;
                if (end < exec_cmd.length()) {
                    char c = exec_cmd[end];
                    
                    if (c == 'f' || c == 'F' || c == 'u' || c == 'U' ||
                        c == 'd' || c == 'D' || c == 'n' || c == 'N' ||
                        c == 'm' || c == 'M') {
                        exec_cmd.erase(pos, 2);
                    } else if (c == '%') {
                        exec_cmd.erase(pos, 1);
                    } else {
                        exec_cmd.erase(pos, 1);
                    }
                } else {
                    exec_cmd.erase(pos, 1);
                }
            }
        }
    }
    
    return hidden ? "" : exec_cmd;
}

std::string StartupApps::getAutostartDir() const {
    
    const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
    if (xdg_config) {
        return std::string(xdg_config) + "/autostart";
    }
    
    
    const char* home = std::getenv("HOME");
    if (home) {
        return std::string(home) + "/.config/autostart";
    }
    
    
    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir) {
        return std::string(pw->pw_dir) + "/.config/autostart";
    }
    
    return "";
}

} 
