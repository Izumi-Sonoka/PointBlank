#pragma once

#include <X11/Xlib.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>

namespace pblank {

/**
 * @brief Window Swallowing Manager
 * 
 * Implements X11 window swallowing - when a terminal launches a child window,
 * it can "swallow" the child so the child appears embedded in the terminal
 * instead of as a separate tiled window.
 * 
 * This follows the model used by dwm, bspwm, and similar WMs:
 * - Terminal sets _NET_WM_PID or uses a specific window property to indicate swallow eligibility
 * - When a new window is created, check if its parent is a terminal
 * - If eligible, embed the new window in the terminal instead of tiling it
 */
class WindowSwallower {
public:
    WindowSwallower();
    ~WindowSwallower() = default;
    
    WindowSwallower(const WindowSwallower&) = delete;
    WindowSwallower& operator=(const WindowSwallower&) = delete;
    
    bool shouldSwallow(Window parent, Window child) const;
    
    void registerSwallower(Window terminal);
    
    void unregisterSwallower(Window terminal);
    
    bool isSwallower(Window window) const;
    
    Window getSwallowerForWindow(Window child) const;
    
    bool wasSwallowed(Window window) const;
    
    Window getSwallower(Window window) const;
    
    void onWindowClose(Window window);
    
    void setTerminalClasses(std::vector<std::string> classes);
    
    void setEnabled(bool enabled) { enabled_ = enabled; }
    
    bool isEnabled() const { return enabled_; }

private:
    
    std::unordered_map<Window, Window> swallowed_windows_;
    
    std::vector<Window> registered_swallower_terminals_;
    
    std::vector<std::string> terminal_classes_;
    
    bool enabled_ = true;
    
    bool isTerminal(Window window) const;
    
    pid_t getWindowPID(Window window) const;
};

} 
