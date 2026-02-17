/**
 * @file FloatingWindowManager.cpp
 * @brief Floating Window Position Persistence Manager implementation
 * 
 * Phase 9 of Enhanced TWM Features
 */

#include "pointblank/window/FloatingWindowManager.hpp"
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <chrono>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace pblank {

// Helper to escape strings for file storage
static std::string escapeString(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

// Helper to unescape strings from file storage
static std::string unescapeString(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[i + 1]) {
                case '\\': result += '\\'; ++i; break;
                case '"': result += '"'; ++i; break;
                case 'n': result += '\n'; ++i; break;
                case 'r': result += '\r'; ++i; break;
                case 't': result += '\t'; ++i; break;
                default: result += s[i]; break;
            }
        } else {
            result += s[i];
        }
    }
    return result;
}

bool SavedPosition::matches(const std::string& cls, const std::string& instance) const {
    // Exact match
    if (window_class == cls && window_instance == instance) {
        return true;
    }
    
    // Wildcard match on class
    if (window_class == "*" && window_instance == instance) {
        return true;
    }
    
    // Wildcard match on instance
    if (window_class == cls && window_instance == "*") {
        return true;
    }
    
    // Match both wildcards
    if (window_class == "*" && window_instance == "*") {
        return true;
    }
    
    return false;
}

FloatingWindowManager& FloatingWindowManager::instance() {
    static FloatingWindowManager instance;
    return instance;
}

void FloatingWindowManager::initialize(Display* display, 
                                       const std::filesystem::path& config_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    display_ = display;
    config_path_ = config_path;
    
    // Create config directory if needed
    if (config_path_.has_parent_path()) {
        std::filesystem::create_directories(config_path_.parent_path());
    }
    
    // Load saved positions
    loadFromDisk();
}

void FloatingWindowManager::registerFloatingWindow(Window window, 
                                                  int x, int y,
                                                  int width, int height,
                                                  int workspace_id, 
                                                  int monitor_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    FloatingWindowState& state = floating_windows_[window];
    state.window = window;
    state.x = x;
    state.y = y;
    state.width = width;
    state.height = height;
    state.workspace_id = workspace_id;
    state.monitor_id = monitor_id;
    state.is_floating = true;
    state.last_seen = getCurrentTimeMs();
    state.created = state.last_seen;
    
    // Read window identity
    readWindowIdentity(window, state);
}

void FloatingWindowManager::updatePosition(Window window, int x, int y) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = floating_windows_.find(window);
    if (it != floating_windows_.end()) {
        it->second.x = x;
        it->second.y = y;
        it->second.last_seen = getCurrentTimeMs();
    }
}

void FloatingWindowManager::updateSize(Window window, int width, int height) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = floating_windows_.find(window);
    if (it != floating_windows_.end()) {
        it->second.width = width;
        it->second.height = height;
        it->second.last_seen = getCurrentTimeMs();
    }
}

void FloatingWindowManager::unregisterWindow(Window window) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = floating_windows_.find(window);
    if (it != floating_windows_.end()) {
        // Save position for this window class before removing
        if (!it->second.window_class.empty()) {
            saved_positions_.push_back({
                it->second.window_class,
                it->second.window_instance,
                it->second.x,
                it->second.y,
                it->second.width,
                it->second.height,
                it->second.workspace_id,
                it->second.centered
            });
            
            // Keep only last 100 saved positions
            if (saved_positions_.size() > 100) {
                saved_positions_.erase(saved_positions_.begin());
            }
        }
        
        floating_windows_.erase(it);
    }
}

std::optional<FloatingWindowState> FloatingWindowManager::getWindowState(Window window) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = floating_windows_.find(window);
    if (it != floating_windows_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool FloatingWindowManager::isFloating(Window window) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = floating_windows_.find(window);
    return it != floating_windows_.end() && it->second.is_floating;
}

void FloatingWindowManager::setFloating(Window window, bool floating) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = floating_windows_.find(window);
    if (it != floating_windows_.end()) {
        it->second.is_floating = floating;
    }
}

std::optional<std::tuple<int, int, int, int>> FloatingWindowManager::getRestoredPosition(
    const std::string& window_class,
    const std::string& window_instance,
    const std::string& title,
    int default_width,
    int default_height
) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Find matching saved position
    for (const auto& saved : saved_positions_) {
        if (saved.matches(window_class, window_instance)) {
            return std::make_tuple(
                saved.x,
                saved.y,
                saved.width > 0 ? saved.width : default_width,
                saved.height > 0 ? saved.height : default_height
            );
        }
    }
    
    return std::nullopt;
}

void FloatingWindowManager::savePositionForClass(Window window) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = floating_windows_.find(window);
    if (it != floating_windows_.end() && !it->second.window_class.empty()) {
        // Remove old entry for same class
        saved_positions_.erase(
            std::remove_if(saved_positions_.begin(), saved_positions_.end(),
                [&](const SavedPosition& p) {
                    return p.window_class == it->second.window_class &&
                           p.window_instance == it->second.window_instance;
                }),
            saved_positions_.end()
        );
        
        // Add new entry
        saved_positions_.push_back({
            it->second.window_class,
            it->second.window_instance,
            it->second.x,
            it->second.y,
            it->second.width,
            it->second.height,
            it->second.workspace_id,
            it->second.centered
        });
    }
}

std::pair<int, int> FloatingWindowManager::centerWindow(
    Window window,
    int monitor_width, int monitor_height,
    int monitor_x, int monitor_y
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = floating_windows_.find(window);
    if (it == floating_windows_.end()) {
        return {monitor_x + monitor_width / 2, monitor_y + monitor_height / 2};
    }
    
    // Calculate centered position
    int x = monitor_x + (monitor_width - it->second.width) / 2;
    int y = monitor_y + (monitor_height - it->second.height) / 2;
    
    it->second.x = x;
    it->second.y = y;
    it->second.centered = true;
    it->second.last_seen = getCurrentTimeMs();
    
    return {x, y};
}

std::pair<int, int> FloatingWindowManager::ensureVisible(
    Window window,
    int screen_width, int screen_height
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = floating_windows_.find(window);
    if (it == floating_windows_.end()) {
        return {0, 0};
    }
    
    int x = it->second.x;
    int y = it->second.y;
    bool adjusted = false;
    
    // Ensure at least 100 pixels visible on each side
    const int min_visible = 100;
    
    if (x + it->second.width < min_visible) {
        x = min_visible - it->second.width;
        adjusted = true;
    }
    if (x > screen_width - min_visible) {
        x = screen_width - min_visible;
        adjusted = true;
    }
    if (y + it->second.height < min_visible) {
        y = min_visible - it->second.height;
        adjusted = true;
    }
    if (y > screen_height - min_visible) {
        y = screen_height - min_visible;
        adjusted = true;
    }
    
    if (adjusted) {
        it->second.x = x;
        it->second.y = y;
        it->second.last_seen = getCurrentTimeMs();
    }
    
    return {x, y};
}

void FloatingWindowManager::moveToWorkspace(Window window, int workspace_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = floating_windows_.find(window);
    if (it != floating_windows_.end()) {
        it->second.workspace_id = workspace_id;
        it->second.last_seen = getCurrentTimeMs();
    }
}

std::vector<Window> FloatingWindowManager::getFloatingWindows(int workspace_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<Window> result;
    for (const auto& pair : floating_windows_) {
        if (pair.second.workspace_id == workspace_id || pair.second.sticky) {
            result.push_back(pair.first);
        }
    }
    return result;
}

std::vector<Window> FloatingWindowManager::getFloatingWindowsOnMonitor(int monitor_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<Window> result;
    for (const auto& pair : floating_windows_) {
        if (pair.second.monitor_id == monitor_id) {
            result.push_back(pair.first);
        }
    }
    return result;
}

void FloatingWindowManager::setSticky(Window window, bool sticky) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = floating_windows_.find(window);
    if (it != floating_windows_.end()) {
        it->second.sticky = sticky;
        it->second.last_seen = getCurrentTimeMs();
    }
}

bool FloatingWindowManager::isSticky(Window window) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = floating_windows_.find(window);
    return it != floating_windows_.end() && it->second.sticky;
}

void FloatingWindowManager::saveToDisk() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (config_path_.empty()) {
        return;
    }
    
    std::ofstream file(config_path_);
    if (!file.is_open()) {
        return;
    }
    
    // Write header
    file << "# Pointblank Floating Window Positions\n";
    file << "# Format: class|instance|x|y|width|height|workspace|centered\n";
    file << "# Lines starting with # are comments\n\n";
    
    // Write saved positions
    for (const auto& saved : saved_positions_) {
        file << escapeString(saved.window_class) << "|"
             << escapeString(saved.window_instance) << "|"
             << saved.x << "|"
             << saved.y << "|"
             << saved.width << "|"
             << saved.height << "|"
             << saved.workspace_id << "|"
             << (saved.centered ? "1" : "0") << "\n";
    }
    
    file.close();
}

void FloatingWindowManager::loadFromDisk() {
    // Note: Caller should hold mutex
    
    if (config_path_.empty() || !std::filesystem::exists(config_path_)) {
        return;
    }
    
    std::ifstream file(config_path_);
    if (!file.is_open()) {
        return;
    }
    
    saved_positions_.clear();
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Parse line
        std::istringstream iss(line);
        std::string token;
        std::vector<std::string> tokens;
        
        while (std::getline(iss, token, '|')) {
            tokens.push_back(unescapeString(token));
        }
        
        if (tokens.size() >= 7) {
            SavedPosition saved;
            saved.window_class = tokens[0];
            saved.window_instance = tokens[1];
            saved.x = std::stoi(tokens[2]);
            saved.y = std::stoi(tokens[3]);
            saved.width = std::stoi(tokens[4]);
            saved.height = std::stoi(tokens[5]);
            saved.workspace_id = std::stoi(tokens[6]);
            saved.centered = tokens.size() > 7 && tokens[7] == "1";
            
            saved_positions_.push_back(saved);
        }
    }
    
    file.close();
}

void FloatingWindowManager::clearSavedPositions() {
    std::lock_guard<std::mutex> lock(mutex_);
    saved_positions_.clear();
}

size_t FloatingWindowManager::getFloatingWindowCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return floating_windows_.size();
}

uint64_t FloatingWindowManager::getCurrentTimeMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

void FloatingWindowManager::readWindowIdentity(Window window, FloatingWindowState& state) {
    if (!display_) {
        return;
    }
    
    // Read WM_CLASS
    XClassHint* class_hint = XAllocClassHint();
    if (class_hint) {
        if (XGetClassHint(display_, window, class_hint)) {
            state.window_instance = class_hint->res_name ? class_hint->res_name : "";
            state.window_class = class_hint->res_class ? class_hint->res_class : "";
        }
        XFree(class_hint);
    }
    
    // Read window title
    XTextProperty text_prop;
    if (XGetWMName(display_, window, &text_prop)) {
        if (text_prop.value) {
            state.title = reinterpret_cast<char*>(text_prop.value);
            XFree(text_prop.value);
        }
    }
}

std::optional<SavedPosition> FloatingWindowManager::findSavedPosition(
    const std::string& window_class,
    const std::string& window_instance
) const {
    for (const auto& saved : saved_positions_) {
        if (saved.matches(window_class, window_instance)) {
            return saved;
        }
    }
    return std::nullopt;
}

} // namespace pblank
