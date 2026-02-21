#pragma once

/**
 * @file SessionManager.hpp
 * @brief Session Environment Manager for X11 Desktop Integration
 * 
 * This file implements a robust, init-system agnostic session setup that:
 * - Sets required XDG environment variables
 * - Updates D-Bus activation environment
 * - Launches xdg-desktop-portal services
 * 
 * The implementation is designed to work across all Linux distributions
 * regardless of the init system (systemd, OpenRC, runit, s6, etc.)
 * 
 * @author Point Blank Systems Engineering Team
 * @version 1.0.0
 */

#include <string>
#include <vector>

namespace pblank {

class SessionManager {
public:
    SessionManager() = default;
    ~SessionManager() = default;
    
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;
    SessionManager(SessionManager&&) = delete;
    SessionManager& operator=(SessionManager&&) = delete;
    
    static bool setupEnvironment();
    
    static bool updateDBusEnvironment();
    
    static bool launchPortals();
    
    static bool initializeSession();
    
    static bool isSystemd();
    
    static bool isOpenRC();
    
    static bool processExists(const std::string& name);
    
    static std::string getSessionType();
    
    static bool isSessionConfigured();

private:
    
    static bool setEnvIfUnset(const char* name, const char* value, 
                              bool overwrite = false);
    
    static bool runCommand(const std::string& cmd, bool background = false);
    
    static bool ensureRuntimeDir();
    
    static bool launchPortalBackend();
    
    static bool waitForProcess(const std::string& name, int timeout_ms);
};

} 