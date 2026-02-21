#pragma once

#include <X11/Xlib.h>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace pblank {

struct ScratchpadState {
    Window window;
    int original_workspace;
    int x, y;
    unsigned int width, height;
    bool was_floating;
    
    ScratchpadState(Window w, int ws, int X, int Y, unsigned int w_, unsigned int h_, bool f)
        : window(w), original_workspace(ws), x(X), y(Y), width(w_), height(h_), was_floating(f) {}
};

/**
 * ScratchpadManager - Manages hidden windows outside workspace
 * 
 * Similar to i3's scratchpad, allows hiding/showing windows with a keybind.
 */
class ScratchpadManager {
public:
    ScratchpadManager() = default;
    ~ScratchpadManager() = default;
    
    ScratchpadManager(const ScratchpadManager&) = delete;
    ScratchpadManager& operator=(const ScratchpadManager&) = delete;
    
    void hideToScratchpad(Window w, int current_workspace, int x, int y, 
                         unsigned int width, unsigned int height, bool was_floating);
    
    bool showScratchpad();
    
    bool showScratchpadIndex(size_t index);
    
    bool showScratchpadNext();
    
    bool showScratchpadPrevious();
    
    void removeFromScratchpad(Window w);
    
    size_t count() const { return scratchpad_windows_.size(); }
    
    bool isInScratchpad(Window w) const;
    
    const std::vector<ScratchpadState>& getWindows() const { return scratchpad_windows_; }
    
    int getOriginalWorkspace(Window w) const;
    
    bool getGeometry(Window w, int& x, int& y, unsigned int& width, unsigned int& height) const;
    
    void clear() { scratchpad_windows_.clear(); }
    
    using ShowCallback = std::function<void(Window, int, int, int, unsigned int, unsigned int, bool)>;
    void setShowCallback(ShowCallback cb) { show_callback_ = std::move(cb); }
    
    using HideCallback = std::function<void(Window)>;
    void setHideCallback(HideCallback cb) { hide_callback_ = std::move(cb); }

private:
    std::vector<ScratchpadState> scratchpad_windows_;
    size_t current_index_ = 0;
    
    ShowCallback show_callback_;
    HideCallback hide_callback_;
    
    size_t findIndex(Window w) const;
};

} 
