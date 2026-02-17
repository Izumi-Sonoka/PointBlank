/**
 * @file SessionManager.cpp
 * @brief Implementation of Session Environment Manager
 * 
 * @author Point Blank Systems Engineering Team
 * @version 1.0.0
 */

#include "pointblank/core/SessionManager.hpp"
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>

namespace pblank {

// ============================================================================
// Environment Setup
// ============================================================================

bool SessionManager::setupEnvironment() {
    std::cout << "[SessionManager] Setting up XDG environment..." << std::endl;
    
    // Set required XDG environment variables for X11 session
    if (!setEnvIfUnset("XDG_SESSION_TYPE", "x11", false)) {
        std::cerr << "[SessionManager] Warning: Failed to set XDG_SESSION_TYPE" << std::endl;
    }
    
    // NOTE: Do NOT set XDG_CURRENT_DESKTOP or XDG_SESSION_DESKTOP for standalone
    // window managers. These should only be set by desktop environments (GNOME, KDE, etc.)
    // Setting these causes fetch scripts to incorrectly identify the window manager
    // as a desktop environment. The window manager is properly identified via EWMH
    // atoms (_NET_SUPPORTING_WM_CHECK, WM_NAME) which fetch scripts should check.
    
    // Ensure XDG_RUNTIME_DIR exists
    if (!ensureRuntimeDir()) {
        std::cerr << "[SessionManager] Warning: Could not ensure XDG_RUNTIME_DIR" << std::endl;
        // Non-fatal, continue
    }
    
    // Verify the environment is set correctly
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
    
    // Build the environment variables to export
    // Only export XDG_SESSION_TYPE for window manager (not CURRENT_DESKTOP or SESSION_DESKTOP)
    std::string env_vars = "XDG_SESSION_TYPE";
    
    // Try dbus-update-activation-environment first (most portable)
    // This works on all init systems
    std::string dbus_cmd = "dbus-update-activation-environment --all " + env_vars + " 2>/dev/null";
    
    bool dbus_success = runCommand(dbus_cmd, false);
    
    if (dbus_success) {
        std::cout << "[SessionManager] D-Bus environment updated via dbus-update-activation-environment" << std::endl;
    } else {
        std::cerr << "[SessionManager] Warning: dbus-update-activation-environment failed or not available" << std::endl;
    }
    
    // If systemd is running, also update systemd user environment
    // This is optional and will gracefully skip if not available
    if (isSystemd()) {
        std::string systemd_cmd = "systemctl --user import-environment " + env_vars + " 2>/dev/null";
        if (runCommand(systemd_cmd, false)) {
            std::cout << "[SessionManager] Systemd user environment updated" << std::endl;
        }
    }
    
    // The dbus update is considered successful if either method worked
    // or if we're on a system without dbus (still allow WM to start)
    return true;
}

bool SessionManager::launchPortals() {
    std::cout << "[SessionManager] Launching desktop portal services..." << std::endl;
    
    bool any_success = false;
    
    // Check if xdg-desktop-portal is already running
    if (processExists("xdg-desktop-portal")) {
        std::cout << "[SessionManager] xdg-desktop-portal already running" << std::endl;
        any_success = true;
    } else {
        // Launch xdg-desktop-portal in background
        if (runCommand("xdg-desktop-portal &", true)) {
            std::cout << "[SessionManager] Launched xdg-desktop-portal" << std::endl;
            
            // Wait for it to start
            if (waitForProcess("xdg-desktop-portal", 3000)) {
                any_success = true;
            }
        } else {
            std::cerr << "[SessionManager] Warning: Failed to launch xdg-desktop-portal" << std::endl;
        }
    }
    
    // Small delay to let the main portal initialize
    usleep(500000);  // 500ms
    
    // Launch the appropriate backend
    if (launchPortalBackend()) {
        any_success = true;
    }
    
    return any_success;
}

bool SessionManager::initializeSession() {
    std::cout << "[SessionManager] ========================================" << std::endl;
    std::cout << "[SessionManager] Initializing session environment..." << std::endl;
    std::cout << "[SessionManager] ========================================" << std::endl;
    
    // Step 1: Set up environment variables
    if (!setupEnvironment()) {
        std::cerr << "[SessionManager] Error: Failed to set up environment" << std::endl;
        return false;
    }
    
    // Step 2: Update D-Bus activation environment
    if (!updateDBusEnvironment()) {
        // Non-fatal, continue
        std::cerr << "[SessionManager] Warning: D-Bus environment update had issues" << std::endl;
    }
    
    // Step 3: Launch portal services
    if (!launchPortals()) {
        // Non-fatal, continue
        std::cerr << "[SessionManager] Warning: Portal launch had issues" << std::endl;
    }
    
    std::cout << "[SessionManager] ========================================" << std::endl;
    std::cout << "[SessionManager] Session initialization complete" << std::endl;
    std::cout << "[SessionManager] ========================================" << std::endl;
    
    return true;
}

// ============================================================================
// Utility Methods
// ============================================================================

bool SessionManager::isSystemd() {
    // Check for systemd by looking for its runtime directory
    return access("/run/systemd/system", F_OK) == 0;
}

bool SessionManager::isOpenRC() {
    // Check for OpenRC by looking for its runtime directory
    return access("/run/openrc", F_OK) == 0;
}

bool SessionManager::processExists(const std::string& name) {
    // Use pgrep to check if process exists
    std::string cmd = "pgrep -x " + name + " > /dev/null 2>&1";
    int result = system(cmd.c_str());
    return WIFEXITED(result) && WEXITSTATUS(result) == 0;
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

// ============================================================================
// Private Helper Methods
// ============================================================================

bool SessionManager::setEnvIfUnset(const char* name, const char* value, 
                                    bool overwrite) {
    const char* existing = std::getenv(name);
    
    if (existing != nullptr && existing[0] != '\0' && !overwrite) {
        // Already set and not overwriting
        return true;
    }
    
    // Set the environment variable
    // setenv returns 0 on success
    return setenv(name, value, overwrite ? 1 : 0) == 0;
}

bool SessionManager::runCommand(const std::string& cmd, bool background) {
    // Build the full command
    std::string full_cmd;
    
    if (background) {
        // Run in background, redirect output to /dev/null
        full_cmd = "(" + cmd + ") > /dev/null 2>&1 &";
    } else {
        // Run in foreground, suppress output
        full_cmd = cmd + " > /dev/null 2>&1";
    }
    
    int result = system(full_cmd.c_str());
    
    // For background commands, system() returns immediately
    // so we can't check the actual process exit status
    if (background) {
        return true;
    }
    
    return WIFEXITED(result) && WEXITSTATUS(result) == 0;
}

bool SessionManager::ensureRuntimeDir() {
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    
    if (runtime_dir != nullptr && runtime_dir[0] != '\0') {
        // Already set, verify it exists
        if (std::filesystem::exists(runtime_dir)) {
            return true;
        }
    }
    
    // Try to create/find a suitable runtime directory
    std::string uid = std::to_string(getuid());
    std::string runtime_path = "/run/user/" + uid;
    
    if (std::filesystem::exists(runtime_path)) {
        // Set XDG_RUNTIME_DIR
        setenv("XDG_RUNTIME_DIR", runtime_path.c_str(), 0);
        return true;
    }
    
    // Try /tmp as fallback
    runtime_path = "/tmp/runtime-" + uid;
    
    // Create if it doesn't exist
    if (!std::filesystem::exists(runtime_path)) {
        std::error_code ec;
        if (!std::filesystem::create_directory(runtime_path, ec)) {
            return false;
        }
        
        // Set permissions (0700)
        chmod(runtime_path.c_str(), S_IRWXU);
    }
    
    setenv("XDG_RUNTIME_DIR", runtime_path.c_str(), 0);
    return true;
}

bool SessionManager::launchPortalBackend() {
    // List of portal backends to try, in order of preference for X11
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
        
        // Check if the executable exists
        std::string check_cmd = "which " + executable + " > /dev/null 2>&1";
        if (system(check_cmd.c_str()) == 0) {
            // Try to launch it
            std::string launch_cmd = executable + " &";
            if (runCommand(launch_cmd, true)) {
                std::cout << "[SessionManager] Launched " << description << std::endl;
                
                // Wait briefly for it to start
                if (waitForProcess(executable, 2000)) {
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
    int check_interval = 100;  // 100ms between checks
    
    while (elapsed < timeout_ms) {
        if (processExists(name)) {
            return true;
        }
        
        usleep(check_interval * 1000);  // Convert to microseconds
        elapsed += check_interval;
    }
    
    return false;
}

} // namespace pblank