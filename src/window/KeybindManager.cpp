#include "pointblank/window/KeybindManager.hpp"
#include "pointblank/core/WindowManager.hpp"
#include "pointblank/layout/LayoutEngine.hpp"
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

namespace pblank {

KeybindManager::KeybindManager() = default;

void KeybindManager::registerKeybind(const std::string& keybind_string, 
                                     const std::string& action) {
    // Parse keybind string format: "SUPER, SHIFT, Q" or "SUPER, Q"
    std::string modifiers_str;
    std::string key_str;
    
    auto last_comma = keybind_string.rfind(',');
    if (last_comma != std::string::npos) {
        modifiers_str = keybind_string.substr(0, last_comma);
        key_str = keybind_string.substr(last_comma + 1);
        
        // Trim whitespace
        modifiers_str.erase(0, modifiers_str.find_first_not_of(" \t"));
        modifiers_str.erase(modifiers_str.find_last_not_of(" \t") + 1);
        key_str.erase(0, key_str.find_first_not_of(" \t"));
        key_str.erase(key_str.find_last_not_of(" \t") + 1);
    } else {
        // No modifiers, just a key
        key_str = keybind_string;
        key_str.erase(0, key_str.find_first_not_of(" \t"));
        key_str.erase(key_str.find_last_not_of(" \t") + 1);
    }
    
    Keybind bind;
    bind.modifiers = parseModifiers(modifiers_str);
    bind.keysym = parseKey(key_str);
    bind.action = action;
    
    // Check if this is an exec: directive
    if (action.find("exec:") == 0) {
        bind.exec_command = action.substr(5);
        // Trim whitespace and quotes
        bind.exec_command.erase(0, bind.exec_command.find_first_not_of(" \t\""));
        bind.exec_command.erase(bind.exec_command.find_last_not_of(" \t\"") + 1);
    }
    
    // Check for duplicate keybinds - remove existing one if found
    // This ensures user-defined keybinds override any defaults
    auto it = std::remove_if(keybinds_.begin(), keybinds_.end(),
        [keysym = bind.keysym, mods = bind.modifiers](const Keybind& existing) {
            return existing.keysym == keysym && existing.modifiers == mods;
        });
    keybinds_.erase(it, keybinds_.end());
    
    // Add the new keybind (at the end so it takes priority)
    keybinds_.push_back(bind);
    
}

void KeybindManager::registerDefaultKeybind(const std::string& keybind_string, 
                                             const std::string& action) {
    // Parse keybind string format - same as registerKeybind
    std::string modifiers_str;
    std::string key_str;
    
    auto last_comma = keybind_string.rfind(',');
    if (last_comma != std::string::npos) {
        modifiers_str = keybind_string.substr(0, last_comma);
        key_str = keybind_string.substr(last_comma + 1);
        
        modifiers_str.erase(0, modifiers_str.find_first_not_of(" \t"));
        modifiers_str.erase(modifiers_str.find_last_not_of(" \t") + 1);
        key_str.erase(0, key_str.find_first_not_of(" \t"));
        key_str.erase(key_str.find_last_not_of(" \t") + 1);
    } else {
        key_str = keybind_string;
        key_str.erase(0, key_str.find_first_not_of(" \t"));
        key_str.erase(key_str.find_last_not_of(" \t") + 1);
    }
    
    Keybind bind;
    bind.modifiers = parseModifiers(modifiers_str);
    bind.keysym = parseKey(key_str);
    bind.action = action;
    
    if (action.find("exec:") == 0) {
        bind.exec_command = action.substr(5);
        bind.exec_command.erase(0, bind.exec_command.find_first_not_of(" \t\""));
        bind.exec_command.erase(bind.exec_command.find_last_not_of(" \t\"") + 1);
    }
    
    // Add default keybinds without deduplication
    // They have lower priority than user keybinds
    keybinds_.push_back(bind);
    
}

unsigned int KeybindManager::parseModifiers(const std::string& modifiers) {
    unsigned int mask = 0;
    
    if (modifiers.empty()) {
        return mask;
    }
    
    // Convert to uppercase for case-insensitive matching
    std::string mod_upper = modifiers;
    std::transform(mod_upper.begin(), mod_upper.end(), mod_upper.begin(), ::toupper);
    
    // Parse comma-separated modifiers
    std::istringstream ss(mod_upper);
    std::string token;
    
    while (std::getline(ss, token, ',')) {
        // Trim whitespace
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        
        if (token.empty()) continue;
        
        if (token == "SUPER" || token == "MOD4") {
            mask |= Mod4Mask;
        } else if (token == "ALT" || token == "MOD1") {
            mask |= Mod1Mask;
        } else if (token == "CTRL" || token == "CONTROL") {
            mask |= ControlMask;
        } else if (token == "SHIFT" || token == "L_SHIFT" || token == "R_SHIFT") {
            mask |= ShiftMask;
        } else if (token == "MOD2") {
            mask |= Mod2Mask;
        } else if (token == "MOD3") {
            mask |= Mod3Mask;
        } else if (token == "MOD5") {
            mask |= Mod5Mask;
        } else {
            std::cerr << "Warning: Unknown modifier '" << token << "'" << std::endl;
        }
    }
    
    return mask;
}

KeySym KeybindManager::parseKey(const std::string& key) {
    if (key.empty()) {
        std::cerr << "Error: Empty key string" << std::endl;
        return NoSymbol;
    }
    
    // Convert to uppercase for consistency
    std::string key_upper = key;
    std::transform(key_upper.begin(), key_upper.end(), key_upper.begin(), ::toupper);
    
    // Map common key names
    static const std::unordered_map<std::string, KeySym> key_map = {
        {"RETURN", XK_Return},
        {"ENTER", XK_Return},
        {"SPACE", XK_space},
        {"TAB", XK_Tab},
        {"ESC", XK_Escape},
        {"ESCAPE", XK_Escape},
        {"BACKSPACE", XK_BackSpace},
        {"DELETE", XK_Delete},
        {"INSERT", XK_Insert},
        {"HOME", XK_Home},
        {"END", XK_End},
        {"PAGEUP", XK_Page_Up},
        {"PAGEDOWN", XK_Page_Down},
        {"LEFT", XK_Left},
        {"RIGHT", XK_Right},
        {"UP", XK_Up},
        {"DOWN", XK_Down},
        
        // Function keys
        {"F1", XK_F1}, {"F2", XK_F2}, {"F3", XK_F3}, {"F4", XK_F4},
        {"F5", XK_F5}, {"F6", XK_F6}, {"F7", XK_F7}, {"F8", XK_F8},
        {"F9", XK_F9}, {"F10", XK_F10}, {"F11", XK_F11}, {"F12", XK_F12},
        
        // Number keys (for workspace switching)
        {"1", XK_1}, {"2", XK_2}, {"3", XK_3}, {"4", XK_4}, {"5", XK_5},
        {"6", XK_6}, {"7", XK_7}, {"8", XK_8}, {"9", XK_9}, {"0", XK_0},
        
        // Special keys
        {"GRAVE", XK_grave},
    };
    
    auto it = key_map.find(key_upper);
    if (it != key_map.end()) {
        return it->second;
    }
    
    // For single character keys, use XStringToKeysym
    if (key.length() == 1) {
        // Map special characters to their X11 keysym names
        static const std::unordered_map<char, const char*> char_to_keysym = {
            {'`', "grave"},
            {'~', "asciitilde"},
            {'!', "exclam"},
            {'@', "at"},
            {'#', "numbersign"},
            {'$', "dollar"},
            {'%', "percent"},
            {'^', "asciicircum"},
            {'&', "ampersand"},
            {'*', "asterisk"},
            {'(', "parenleft"},
            {')', "parenright"},
            {'-', "minus"},
            {'_', "underscore"},
            {'=', "equal"},
            {'+', "plus"},
            {'[', "bracketleft"},
            {']', "bracketright"},
            {'{', "braceleft"},
            {'}', "braceright"},
            {'|', "bar"},
            {'\\', "backslash"},
            {';', "semicolon"},
            {':', "colon"},
            {'\'', "quoteright"},
            {'"', "quotedbl"},
            {',', "comma"},
            {'<', "less"},
            {'.', "period"},
            {'>', "greater"},
            {'/', "slash"},
            {'?', "question"},
        };
        
        auto it = char_to_keysym.find(key[0]);
        if (it != char_to_keysym.end()) {
            KeySym ks = XStringToKeysym(it->second);
            if (ks != NoSymbol) {
                return ks;
            }
        }
        
        // Only convert to lowercase for letter keys (a-z), not symbols
        std::string lower_key = key;
        if (key[0] >= 'A' && key[0] <= 'Z') {
            std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
        }
        KeySym ks = XStringToKeysym(lower_key.c_str());
        if (ks != NoSymbol) {
            return ks;
        }
    }
    
    // Try direct XStringToKeysym lookup
    KeySym ks = XStringToKeysym(key.c_str());
    if (ks != NoSymbol) {
        return ks;
    }
    
    std::cerr << "Warning: Could not parse key '" << key << "'" << std::endl;
    return NoSymbol;
}

void KeybindManager::grabKeys(Display* display, Window root) {
    // Ungrab all keys first to avoid conflicts
    XUngrabKey(display, AnyKey, AnyModifier, root);
    
    for (const auto& bind : keybinds_) {
        if (bind.keysym == NoSymbol) {
            std::cerr << "Skipping keybind with invalid keysym" << std::endl;
            continue;
        }
        
        KeyCode keycode = XKeysymToKeycode(display, bind.keysym);
        if (keycode == 0) {
            std::cerr << "Warning: No keycode for keysym " << bind.keysym << std::endl;
            continue;
        }
        
        // Grab with lock key combinations
        grabKeyWithLocks(display, keycode, bind.modifiers, root);
    }
    
    XSync(display, False);
}

void KeybindManager::grabKeyWithLocks(Display* display, KeyCode keycode, 
                                      unsigned int modifiers, Window root) {
    // Grab with different combinations to handle Num Lock and Caps Lock
    unsigned int lock_modifiers[] = {
        0,
        Mod2Mask,           // Num Lock
        LockMask,           // Caps Lock
        Mod2Mask | LockMask // Both
    };
    
    for (unsigned int lock_mod : lock_modifiers) {
        XGrabKey(display, keycode, modifiers | lock_mod, root, True,
                GrabModeAsync, GrabModeAsync);
    }
}

void KeybindManager::handleKeyPress(const XKeyEvent& event, WindowManager* wm) {
    KeySym keysym = XkbKeycodeToKeysym(event.display, event.keycode, 0, 0);
    
    // Mask out lock keys (Num Lock, Caps Lock, Scroll Lock)
    unsigned int modifiers = event.state & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask);
    
    for (const auto& bind : keybinds_) {
        if (bind.keysym == keysym && bind.modifiers == modifiers) {
            
            if (!bind.exec_command.empty()) {
                executeCommand(bind.exec_command);
            } else {
                executeAction(bind.action, wm);
            }
            return;
        }
    }
    
}

void KeybindManager::executeAction(const std::string& action, WindowManager* wm) {
    
    // Parse action and parameters
    std::istringstream iss(action);
    std::string command;
    iss >> command;
    
    if (command == "killactive") {
        wm->killActiveWindow();
        
    } else if (command == "fullscreen") {
        wm->toggleFullscreen();
        
    } else if (command == "togglefloating") {
        wm->toggleFloating();
        
    } else if (command == "reload") {
        wm->reloadConfig();
        
    } else if (command == "exit") {
        wm->exit();
        
    } else if (command == "workspace") {
        // Switch to workspace
        int ws;
        iss >> ws;
        wm->switchWorkspace(ws);
        
    } else if (command == "movetoworkspace") {
        int ws;
        iss >> ws;
        wm->moveWindowToWorkspace(ws, true);  // Follow by default
    
    } else if (command == "movetoworkspacesilent") {
        int ws;
        iss >> ws;
        wm->moveWindowToWorkspace(ws, false);  // Don't follow
        
    } else if (command == "workspacenext") {
        // Switch to next workspace (for infinite workspaces)
        wm->switchWorkspace(wm->getCurrentWorkspace() + 2);  // +2 because it's 1-indexed in switchWorkspace
        
    } else if (command == "workspaceprev") {
        // Switch to previous workspace
        int current = wm->getCurrentWorkspace();
        if (current > 0) {
            wm->switchWorkspace(current);  // current is 0-indexed, switchWorkspace expects 1-indexed
        }
        
    } else if (command == "layout") {
        // Change layout mode
        std::string layout_type;
        iss >> layout_type;
        wm->setLayout(layout_type);
        
    } else if (command == "cyclenext") {
        // Cycle to next layout
        wm->cycleLayoutNext();
        
    } else if (command == "cycleprev") {
        // Cycle to previous layout
        wm->cycleLayoutPrev();
        
    } else if (command == "focusleft") {
        wm->moveFocus("left");
        
    } else if (command == "focusright") {
        wm->moveFocus("right");
        
    } else if (command == "focusup") {
        wm->moveFocus("up");
        
    } else if (command == "focusdown") {
        wm->moveFocus("down");
        
    } else if (command == "swapleft") {
        wm->swapFocusedWindow("left");
        
    } else if (command == "swapright") {
        wm->swapFocusedWindow("right");
        
    } else if (command == "swapup") {
        wm->swapFocusedWindow("up");
        
    } else if (command == "swapdown") {
        wm->swapFocusedWindow("down");
        
    } else if (command == "resizeleft") {
        wm->resizeFocusedWindow("left");
        
    } else if (command == "resizeright") {
        wm->resizeFocusedWindow("right");
        
    } else if (command == "resizeup") {
        wm->resizeFocusedWindow("up");
        
    } else if (command == "resizedown") {
        wm->resizeFocusedWindow("down");
        
    } else if (command == "togglesplit") {
        wm->toggleSplitDirection();
        
    } else {
        std::cerr << "Unknown action: " << action << std::endl;
    }
}

void KeybindManager::executeCommand(const std::string& command) {
    
    pid_t pid = fork();
    
    if (pid == -1) {
        std::cerr << "Failed to fork process for command: " << command << std::endl;
        perror("fork");
        return;
    }
    
    if (pid == 0) {
        // Child process
        
        // Create a new session so the child is not killed when WM exits
        if (setsid() == -1) {
            perror("setsid");
            std::exit(1);
        }
        
        // Close all file descriptors except stdin/stdout/stderr
        for (int fd = 3; fd < 1024; ++fd) {
            close(fd);
        }
        
        // Redirect stdin/stdout/stderr to /dev/null to prevent blocking
        int devnull = open("/dev/null", O_RDWR);
        if (devnull != -1) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2) {
                close(devnull);
            }
        }
        
        // Execute the command through shell
        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        
        // If execl fails
        perror("execl");
        std::exit(1);
    }
    
    // Parent process - don't wait for child
}

} // namespace pblank
