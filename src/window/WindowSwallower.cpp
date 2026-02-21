#include "pointblank/window/WindowSwallower.hpp"
#include "pointblank/core/WindowManager.hpp"

#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <unistd.h>
#include <sys/types.h>
#include <cstring>

namespace pblank {

WindowSwallower::WindowSwallower() {
    
    terminal_classes_ = {"Alacritty", "alacritty", "kitty", "foot", "wezterm", 
                        "URxvt", "rxvt", "xterm", "gnome-terminal", "konsole",
                        "Terminator", "Tilix", "sakura", "lxterminal"};
}

bool WindowSwallower::shouldSwallow(Window parent, Window child) const {
    if (!enabled_) {
        return false;
    }
    
    
    if (!isSwallower(parent)) {
        return false;
    }
    
    
    for (const auto& [swallowed, terminal] : swallowed_windows_) {
        if (swallowed == child || terminal == parent) {
            return false;
        }
    }
    
    return true;
}

void WindowSwallower::registerSwallower(Window terminal) {
    
    for (Window w : registered_swallower_terminals_) {
        if (w == terminal) return;
    }
    registered_swallower_terminals_.push_back(terminal);
}

void WindowSwallower::unregisterSwallower(Window terminal) {
    registered_swallower_terminals_.erase(
        std::remove(registered_swallower_terminals_.begin(), 
                   registered_swallower_terminals_.end(), 
                   terminal),
        registered_swallower_terminals_.end()
    );
}

bool WindowSwallower::isSwallower(Window window) const {
    for (Window w : registered_swallower_terminals_) {
        if (w == window) return true;
    }
    return false;
}

Window WindowSwallower::getSwallowerForWindow(Window child) const {
    auto it = swallowed_windows_.find(child);
    if (it != swallowed_windows_.end()) {
        return it->second;
    }
    return None;
}

bool WindowSwallower::wasSwallowed(Window window) const {
    return swallowed_windows_.find(window) != swallowed_windows_.end();
}

Window WindowSwallower::getSwallower(Window window) const {
    auto it = swallowed_windows_.find(window);
    if (it != swallowed_windows_.end()) {
        return it->second;
    }
    return None;
}

void WindowSwallower::onWindowClose(Window window) {
    
    unregisterSwallower(window);
    
    
    swallowed_windows_.erase(window);
    
    
    for (auto it = swallowed_windows_.begin(); it != swallowed_windows_.end(); ) {
        if (it->second == window) {
            it = swallowed_windows_.erase(it);
        } else {
            ++it;
        }
    }
}

void WindowSwallower::setTerminalClasses(std::vector<std::string> classes) {
    terminal_classes_ = std::move(classes);
}

bool WindowSwallower::isTerminal(Window window) const {
    
    
    
    return false;
}

pid_t WindowSwallower::getWindowPID(Window window) const {
    
    
    return -1;
}

} 
