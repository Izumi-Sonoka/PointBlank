/**
 * @file FloatingWindowManager.hpp
 * @brief Floating Window Position Persistence Manager
 * 
 * Manages floating window positions with:
 * - Position persistence across sessions
 * - Per-workspace floating window state
 * - Smart position restoration (avoids off-screen placement)
 * - Integration with infinite canvas coordinate system
 * 
 * Phase 9 of Enhanced TWM Features
 */

#ifndef FLOATINGWINDOWMANAGER_HPP
#define FLOATINGWINDOWMANAGER_HPP

#include <X11/Xlib.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <filesystem>
#include <mutex>

namespace pblank {

static constexpr size_t MAX_FLOATING_WINDOWS = 256;

struct FloatingWindowState {
    Window window{0};               
    std::string window_class;       
    std::string window_instance;    
    std::string title;              
    
    int x{0};                       
    int y{0};                       
    int width{0};                   
    int height{0};                  
    
    int workspace_id{0};            
    int monitor_id{0};              
    
    bool is_floating{true};         
    bool above_tile{false};         
    bool sticky{false};             
    bool centered{false};           
    
    uint64_t last_seen{0};          
    uint64_t created{0};            
};

struct WindowIdentity {
    std::string window_class;
    std::string window_instance;
    std::string title_pattern;
    
    bool operator==(const WindowIdentity& other) const {
        return window_class == other.window_class &&
               window_instance == other.window_instance &&
               title_pattern == other.title_pattern;
    }
};

struct WindowIdentityHash {
    size_t operator()(const WindowIdentity& id) const {
        size_t h1 = std::hash<std::string>{}(id.window_class);
        size_t h2 = std::hash<std::string>{}(id.window_instance);
        size_t h3 = std::hash<std::string>{}(id.title_pattern);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

struct SavedPosition {
    std::string window_class;
    std::string window_instance;
    int x, y;
    int width, height;
    int workspace_id;
    bool centered;
    
    bool matches(const std::string& cls, const std::string& instance) const;
};

class FloatingWindowManager {
public:
    
    static FloatingWindowManager& instance();

    void initialize(Display* display, const std::filesystem::path& config_path);

    void registerFloatingWindow(Window window, int x, int y, 
                                int width, int height,
                                int workspace_id, int monitor_id);

    void updatePosition(Window window, int x, int y);

    void updateSize(Window window, int width, int height);

    void unregisterWindow(Window window);

    std::optional<FloatingWindowState> getWindowState(Window window) const;

    bool isFloating(Window window) const;

    void setFloating(Window window, bool floating);

    std::optional<std::tuple<int, int, int, int>> getRestoredPosition(
        const std::string& window_class,
        const std::string& window_instance,
        const std::string& title,
        int default_width, int default_height
    ) const;

    void savePositionForClass(Window window);

    std::pair<int, int> centerWindow(Window window,
                                     int monitor_width, int monitor_height,
                                     int monitor_x = 0, int monitor_y = 0);

    std::pair<int, int> ensureVisible(Window window,
                                      int screen_width, int screen_height);

    void moveToWorkspace(Window window, int workspace_id);

    std::vector<Window> getFloatingWindows(int workspace_id) const;

    std::vector<Window> getFloatingWindowsOnMonitor(int monitor_id) const;

    void setSticky(Window window, bool sticky);

    bool isSticky(Window window) const;

    void saveToDisk();

    void loadFromDisk();

    void clearSavedPositions();

    size_t getFloatingWindowCount() const;

    static uint64_t getCurrentTimeMs();

private:
    FloatingWindowManager() = default;
    FloatingWindowManager(const FloatingWindowManager&) = delete;
    FloatingWindowManager& operator=(const FloatingWindowManager&) = delete;

    void readWindowIdentity(Window window, FloatingWindowState& state);

    std::optional<SavedPosition> findSavedPosition(
        const std::string& window_class,
        const std::string& window_instance
    ) const;

    Display* display_{nullptr};
    std::filesystem::path config_path_;
    
    std::unordered_map<Window, FloatingWindowState> floating_windows_;
    
    std::vector<SavedPosition> saved_positions_;
    
    mutable std::mutex mutex_;
};

} 

#endif 
