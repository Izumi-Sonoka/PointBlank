#pragma once

#include <X11/Xlib.h>
#include <unordered_map>
#include <string>
#include <functional>
#include <vector>

namespace pblank {

class WindowManager;

/**
 * @brief Keybind manager for handling keyboard shortcuts
 * 
 * Parses keybind strings from .wmi config files, converts them to X11 
 * KeySyms and modifier masks, grabs keys globally, and executes actions
 * or external commands via fork/exec.
 */
class KeybindManager {
public:
    KeybindManager();
    
    void registerKeybind(const std::string& keybind_string, const std::string& action);
    
    void registerDefaultKeybind(const std::string& keybind_string, const std::string& action);
    
    void handleKeyPress(const XKeyEvent& event, WindowManager* wm);
    
    void grabKeys(Display* display, Window root);
    
    void clearKeybinds() { keybinds_.clear(); }
    
private:
    
    struct Keybind {
        unsigned int modifiers;  
        KeySym keysym;          
        std::string action;      
        std::string exec_command; 
    };
    
    std::vector<Keybind> keybinds_;
    
    inline void reserveKeybinds(size_t size) { keybinds_.reserve(size); }
    
    unsigned int parseModifiers(const std::string& modifiers);
    
    KeySym parseKey(const std::string& key);
    
    void executeAction(const std::string& action, WindowManager* wm);
    
    void executeCommand(const std::string& command);
    
    void grabKeyWithLocks(Display* display, KeyCode keycode, 
                          unsigned int modifiers, Window root);
};

} 
