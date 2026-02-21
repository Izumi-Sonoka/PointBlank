#include "pointblank/window/ScratchpadManager.hpp"
#include <algorithm>

namespace pblank {

void ScratchpadManager::hideToScratchpad(Window w, int current_workspace, int x, int y,
                                        unsigned int width, unsigned int height, bool was_floating) {
    
    if (isInScratchpad(w)) {
        return;
    }
    
    
    scratchpad_windows_.emplace_back(w, current_workspace, x, y, width, height, was_floating);
    
    
    if (hide_callback_) {
        hide_callback_(w);
    }
}

bool ScratchpadManager::showScratchpad() {
    if (scratchpad_windows_.empty()) {
        return false;
    }
    
    
    return showScratchpadIndex(scratchpad_windows_.size() - 1);
}

bool ScratchpadManager::showScratchpadIndex(size_t index) {
    if (index >= scratchpad_windows_.size()) {
        return false;
    }
    
    const auto& state = scratchpad_windows_[index];
    current_index_ = index;
    
    
    if (show_callback_) {
        show_callback_(state.window, state.original_workspace, 
                     state.x, state.y, state.width, state.height, state.was_floating);
    }
    
    return true;
}

bool ScratchpadManager::showScratchpadNext() {
    if (scratchpad_windows_.empty()) {
        return false;
    }
    
    current_index_ = (current_index_ + 1) % scratchpad_windows_.size();
    return showScratchpadIndex(current_index_);
}

bool ScratchpadManager::showScratchpadPrevious() {
    if (scratchpad_windows_.empty()) {
        return false;
    }
    
    if (current_index_ == 0) {
        current_index_ = scratchpad_windows_.size() - 1;
    } else {
        current_index_--;
    }
    
    return showScratchpadIndex(current_index_);
}

void ScratchpadManager::removeFromScratchpad(Window w) {
    auto it = std::remove_if(scratchpad_windows_.begin(), scratchpad_windows_.end(),
        [w](const ScratchpadState& state) { return state.window == w; });
    
    scratchpad_windows_.erase(it, scratchpad_windows_.end());
    
    
    if (current_index_ >= scratchpad_windows_.size() && !scratchpad_windows_.empty()) {
        current_index_ = scratchpad_windows_.size() - 1;
    }
}

bool ScratchpadManager::isInScratchpad(Window w) const {
    return findIndex(w) != static_cast<size_t>(-1);
}

int ScratchpadManager::getOriginalWorkspace(Window w) const {
    size_t idx = findIndex(w);
    if (idx == static_cast<size_t>(-1)) {
        return -1;
    }
    return scratchpad_windows_[idx].original_workspace;
}

bool ScratchpadManager::getGeometry(Window w, int& x, int& y, 
                                    unsigned int& width, unsigned int& height) const {
    size_t idx = findIndex(w);
    if (idx == static_cast<size_t>(-1)) {
        return false;
    }
    
    const auto& state = scratchpad_windows_[idx];
    x = state.x;
    y = state.y;
    width = state.width;
    height = state.height;
    return true;
}

size_t ScratchpadManager::findIndex(Window w) const {
    for (size_t i = 0; i < scratchpad_windows_.size(); i++) {
        if (scratchpad_windows_[i].window == w) {
            return i;
        }
    }
    return static_cast<size_t>(-1);
}

} 
