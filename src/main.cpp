#include "pointblank/core/WindowManager.hpp"
#include "pointblank/core/SessionManager.hpp"
#include "pointblank/core/XServerManager.hpp"
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <sys/stat.h>

using namespace pblank;

// Global pointer for signal handling
WindowManager* g_wm = nullptr;

// Custom config path (optional)
static std::optional<std::filesystem::path> custom_config_path;

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nReceived shutdown signal, exiting gracefully..." << std::endl;
        
        // Shutdown X server if we started it
        XServerManager::shutdownXServer();
        
        std::exit(0);
    }
}

void printUsage(const char* program_name) {
    std::cout << "Point Blank - Tiling Window Manager\n"
              << "Usage: " << program_name << " [options]\n"
              << "\nOptions:\n"
              << "  -h, --help     Show this help message\n"
              << "  -v, --version  Show version information\n"
              << "  -c, --config   Specify config file path\n"
              << "  -d, --display  Specify X display (e.g., :0, :1)\n"
              << "  --no-startx    Don't attempt to start X server\n"
              << std::endl;
}

void printVersion() {
    std::cout << "Point Blank Window Manager v0.1.0\n"
              << "Built with C++20 for X11\n"
              << "Copyright (c) 2026\n"
              << std::endl;
}

/**
 * @brief Ensure required configuration directories exist
 * Creates ~/.config/pblank/, ~/.config/pblank/extensions/,
 * ~/.config/pblank/extensions/pb/, and ~/.config/pblank/extensions/user/
 */
void ensureConfigDirectories() {
    std::filesystem::path config_base;
    
    // Use XDG_CONFIG_HOME if set, otherwise default to ~/.config
    const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
    if (xdg_config != nullptr && xdg_config[0] != '\0') {
        config_base = std::filesystem::path(xdg_config);
    } else {
        const char* home = std::getenv("HOME");
        if (home == nullptr) {
            std::cerr << "Warning: HOME environment variable not set, using /tmp for config" << std::endl;
            config_base = std::filesystem::path("/tmp/.config");
        } else {
            config_base = std::filesystem::path(home) / ".config";
        }
    }
    
    // Define required directories
    std::filesystem::path pblank_dir = config_base / "pblank";
    std::filesystem::path extensions_dir = pblank_dir / "extensions";
    std::filesystem::path pb_ext_dir = extensions_dir / "pb";      // For #import
    std::filesystem::path user_ext_dir = extensions_dir / "user";  // For #include
    
    std::vector<std::filesystem::path> dirs = {
        pblank_dir,
        extensions_dir,
        pb_ext_dir,
        user_ext_dir
    };
    
    for (const auto& dir : dirs) {
        if (!std::filesystem::exists(dir)) {
            std::error_code ec;
            if (std::filesystem::create_directories(dir, ec)) {
                std::cout << "Created config directory: " << dir << std::endl;
            } else {
                std::cerr << "Warning: Failed to create directory " << dir 
                          << ": " << ec.message() << std::endl;
            }
        }
    }
}

int main(int argc, char* argv[]) {
    bool auto_start_x = true;
    std::optional<std::string> custom_display;
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        
        if (arg == "-v" || arg == "--version") {
            printVersion();
            return 0;
        }
        
        if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) {
                custom_config_path = argv[++i];
            } else {
                std::cerr << "Error: --config requires a path argument" << std::endl;
                return 1;
            }
        }
        
        if (arg == "-d" || arg == "--display") {
            if (i + 1 < argc) {
                custom_display = argv[++i];
            } else {
                std::cerr << "Error: --display requires a display argument" << std::endl;
                return 1;
            }
        }
        
        if (arg == "--no-startx") {
            auto_start_x = false;
        }
    }
    
    // Setup signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // Ensure required configuration directories exist
    ensureConfigDirectories();
    
    std::cout << R"(
    ____        _       __          ____  __            __
   / __ \____  (_)___  / /_   _    / __ )/ /___ _____  / /__
  / /_/ / __ \/ / __ \/ __/  (_)  / __  / / __ `/ __ \/ //_/
 / ____/ /_/ / / / / / /_   _    / /_/ / / /_/ / / / / ,<
/_/    \____/_/_/ /_/\__/  (_)  /_____/_/\__,_/_/ /_/_/|_|

By: N3ZT POSSIBLE G3N && Point:projects
    _   __ 
   / | / / _ _____  _  _  __ _____ _    _   __ _  
  /  |/ /  _) / |  |_)/ \(_ (_  | |_)| |_  /__ _)|\ |
 / /|  /   _)/_ |  |  \_/__)__)_|_|_)|_|_  \_| _)| \| 
/_/ |_/

Point:Blank Window Manager v0.1.0.0
    )" << std::endl;
    
    // Initialize X server or connect to existing one
    Display* display = nullptr;
    if (auto_start_x) {
        display = XServerManager::initializeDisplay(custom_display);
        if (display == nullptr) {
            std::cerr << "Failed to initialize X display" << std::endl;
            std::cerr << "\nTroubleshooting:" << std::endl;
            std::cerr << "  1. Make sure X server (Xorg) is installed" << std::endl;
            std::cerr << "  2. Try running with an existing X session" << std::endl;
            std::cerr << "  3. Use 'startx' with ~/.xinitrc instead" << std::endl;
            return 1;
        }
        XCloseDisplay(display); // WindowManager will open its own connection
    }
    
    // Initialize session environment (XDG variables, D-Bus, portals)
    // This is critical for desktop integration and screen recorder compatibility
    if (!SessionManager::initializeSession()) {
        std::cerr << "Warning: Session initialization had issues, continuing anyway..." << std::endl;
        // Non-fatal - continue without full session setup
    }
    
    try {
        // Create and initialize window manager
        WindowManager wm;
        g_wm = &wm;
        
        // Set custom config path if provided
        if (custom_config_path.has_value()) {
            wm.setConfigPath(*custom_config_path);
        }
        
        if (!wm.initialize()) {
            std::cerr << "Failed to initialize window manager" << std::endl;
            XServerManager::shutdownXServer();
            return 1;
        }
        
        std::cout << "Window manager initialized successfully" << std::endl;
        std::cout << "Press SUPER+SHIFT+Q to exit" << std::endl;
        
        // Run main event loop
        wm.run();
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        XServerManager::shutdownXServer();
        return 1;
    }
    
    // Clean shutdown
    XServerManager::shutdownXServer();
    
    return 0;
}