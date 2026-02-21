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
    
    
    
    auto it = std::remove_if(keybinds_.begin(), keybinds_.end(),
        [keysym = bind.keysym, mods = bind.modifiers](const Keybind& existing) {
            return existing.keysym == keysym && existing.modifiers == mods;
        });
    keybinds_.erase(it, keybinds_.end());
    
    
    keybinds_.emplace_back(std::move(bind));
    
}

void KeybindManager::registerDefaultKeybind(const std::string& keybind_string, 
                                             const std::string& action) {
    
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
    
    
    
    keybinds_.emplace_back(std::move(bind));
    
}

unsigned int KeybindManager::parseModifiers(const std::string& modifiers) {
    unsigned int mask = 0;
    
    if (modifiers.empty()) {
        return mask;
    }
    
    
    std::string mod_upper = modifiers;
    std::transform(mod_upper.begin(), mod_upper.end(), mod_upper.begin(), ::toupper);
    
    
    std::istringstream ss(mod_upper);
    std::string token;
    
    while (std::getline(ss, token, ',')) {
        
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
    
    
    std::string key_upper = key;
    std::transform(key_upper.begin(), key_upper.end(), key_upper.begin(), ::toupper);
    
    
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
        
        
        {"F1", XK_F1}, {"F2", XK_F2}, {"F3", XK_F3}, {"F4", XK_F4},
        {"F5", XK_F5}, {"F6", XK_F6}, {"F7", XK_F7}, {"F8", XK_F8},
        {"F9", XK_F9}, {"F10", XK_F10}, {"F11", XK_F11}, {"F12", XK_F12},
        
        
        {"1", XK_1}, {"2", XK_2}, {"3", XK_3}, {"4", XK_4}, {"5", XK_5},
        {"6", XK_6}, {"7", XK_7}, {"8", XK_8}, {"9", XK_9}, {"0", XK_0},
        
        
        {"MINUS", XK_minus},
        {"EQUAL", XK_equal},
        {"-", XK_minus},
        {"=", XK_equal},
        
        
        {"GRAVE", XK_grave},
        {"COMMA", XK_comma},
        {"PERIOD", XK_period},
        {"BRACKETLEFT", XK_bracketleft},
        {"BRACKETRIGHT", XK_bracketright},
        {"PRINT", XK_Print},
        {"SCROLLLOCK", XK_Scroll_Lock},
        {"PAUSE", XK_Pause},
        {"INSERT", XK_Insert},
        {"DELETE", XK_Delete},
        {"SUPER", XK_Super_L},
        {"LSuper", XK_Super_L},
        {"RSuper", XK_Super_R},
        {"ALT", XK_Alt_L},
        {"LAlt", XK_Alt_L},
        {"RAlt", XK_Alt_R},
        {"CTRL", XK_Control_L},
        {"LCtrl", XK_Control_L},
        {"RCtrl", XK_Control_R},
        {"SHIFT", XK_Shift_L},
        {"LShift", XK_Shift_L},
        {"RShift", XK_Shift_R},
        {"TAB", XK_Tab},
        {"SPACE", XK_space},
        {"RETURN", XK_Return},
        {"ESCAPE", XK_Escape},
        {"ESC", XK_Escape},
        {"BACKSPACE", XK_BackSpace},
        {"CAPS", XK_Caps_Lock},
    };
    
    auto it = key_map.find(key_upper);
    if (it != key_map.end()) {
        return it->second;
    }
    
    
    if (key.length() == 1) {
        
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
        
        
        std::string lower_key = key;
        if (key[0] >= 'A' && key[0] <= 'Z') {
            std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
        }
        KeySym ks = XStringToKeysym(lower_key.c_str());
        if (ks != NoSymbol) {
            return ks;
        }
    }
    
    
    KeySym ks = XStringToKeysym(key.c_str());
    if (ks != NoSymbol) {
        return ks;
    }
    
    std::cerr << "Warning: Could not parse key '" << key << "'" << std::endl;
    return NoSymbol;
}

void KeybindManager::grabKeys(Display* display, Window root) {
    
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
        
        
        grabKeyWithLocks(display, keycode, bind.modifiers, root);
    }
    
    XSync(display, False);
}

void KeybindManager::grabKeyWithLocks(Display* display, KeyCode keycode, 
                                      unsigned int modifiers, Window root) {
    
    unsigned int lock_modifiers[] = {
        0,
        Mod2Mask,           
        LockMask,           
        Mod2Mask | LockMask 
    };
    
    for (unsigned int lock_mod : lock_modifiers) {
        XGrabKey(display, keycode, modifiers | lock_mod, root, True,
                GrabModeAsync, GrabModeAsync);
    }
}

void KeybindManager::handleKeyPress(const XKeyEvent& event, WindowManager* wm) {
    KeySym keysym = XkbKeycodeToKeysym(event.display, event.keycode, 0, 0);
    
    
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
        
        int ws;
        iss >> ws;
        wm->switchWorkspace(ws);
        
    } else if (command == "movetoworkspace") {
        int ws;
        iss >> ws;
        wm->moveWindowToWorkspace(ws, true);  
    
    } else if (command == "movetoworkspacesilent") {
        int ws;
        iss >> ws;
        wm->moveWindowToWorkspace(ws, false);  
        
    } else if (command == "workspacenext") {
        
        
        wm->switchWorkspace(wm->getCurrentWorkspace() + 1);
        
    } else if (command == "workspaceprev") {
        
        
        
        int current = wm->getCurrentWorkspace();
        if (current >= 0) {
            wm->switchWorkspace(current);
        }
        
    } else if (command == "layout") {
        
        std::string layout_type;
        iss >> layout_type;
        wm->setLayout(layout_type);
        
    } else if (command == "cyclenext") {
        
        wm->cycleLayoutNext();
        
    } else if (command == "cycleprev") {
        
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
        
    } else if (command == "scratchpad_show") {
        
        wm->showScratchpad();
        
    } else if (command == "scratchpad_show_next") {
        
        wm->showScratchpadNext();
        
    } else if (command == "scratchpad_show_prev") {
        
        wm->showScratchpadPrevious();
        
    } else if (command == "scratchpad_hide") {
        
        wm->hideToScratchpad();
        
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
        
        
        
        if (setsid() == -1) {
            perror("setsid");
            std::exit(1);
        }
        
        
        for (int fd = 3; fd < 1024; ++fd) {
            close(fd);
        }
        
        
        int devnull = open("/dev/null", O_RDWR);
        if (devnull != -1) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2) {
                close(devnull);
            }
        }
        
        
        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        
        
        perror("execl");
        std::exit(1);
    }
    
    
}

} 
