/**
 * @file SizeConstraints.cpp
 * @brief Window Size Constraints Manager implementation
 * 
 * Phase 8 of Enhanced TWM Features
 */

#include "pointblank/window/SizeConstraints.hpp"
#include <algorithm>
#include <cmath>

namespace pblank {

SizeConstraints& SizeConstraints::instance() {
    static SizeConstraints instance;
    return instance;
}

void SizeConstraints::initialize(Display* display) {
    display_ = display;
}

bool SizeConstraints::readHints(Window window) {
    if (!display_ || !window) {
        return false;
    }
    
    XSizeHints* hints = XAllocSizeHints();
    if (!hints) {
        return false;
    }
    
    long supplied_return = 0;
    WindowSizeHints& cached = hints_cache_[window];
    
    
    cached = WindowSizeHints{};
    
    if (XGetWMNormalHints(display_, window, hints, &supplied_return)) {
        
        if (supplied_return & USPosition) {
            cached.flags.user_position = true;
        }
        if (supplied_return & USSize) {
            cached.flags.user_size = true;
        }
        if (supplied_return & PPosition) {
            cached.flags.program_position = true;
        }
        if (supplied_return & PSize) {
            cached.flags.program_size = true;
        }
        if (supplied_return & PMinSize) {
            cached.flags.min_size = true;
            cached.min_width = hints->min_width;
            cached.min_height = hints->min_height;
        }
        if (supplied_return & PMaxSize) {
            cached.flags.max_size = true;
            cached.max_width = std::min(hints->max_width, 
                                       static_cast<int>(X11Limits::MAX_SIZE));
            cached.max_height = std::min(hints->max_height,
                                        static_cast<int>(X11Limits::MAX_SIZE));
        }
        if (supplied_return & PResizeInc) {
            cached.flags.resize_inc = true;
            cached.width_inc = std::max(1, hints->width_inc);
            cached.height_inc = std::max(1, hints->height_inc);
        }
        if (supplied_return & PAspect) {
            cached.flags.aspect = true;
            cached.min_aspect_x = hints->min_aspect.x;
            cached.min_aspect_y = hints->min_aspect.y;
            cached.max_aspect_x = hints->max_aspect.x;
            cached.max_aspect_y = hints->max_aspect.y;
        }
        if (supplied_return & PBaseSize) {
            cached.flags.base_size = true;
            cached.base_width = hints->base_width;
            cached.base_height = hints->base_height;
        }
        if (supplied_return & PWinGravity) {
            cached.flags.gravity = true;
            cached.win_gravity = hints->win_gravity;
        }
    }
    
    XFree(hints);
    return true;
}

std::optional<WindowSizeHints> SizeConstraints::getHints(Window window) const {
    auto it = hints_cache_.find(window);
    if (it != hints_cache_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void SizeConstraints::removeWindow(Window window) {
    hints_cache_.erase(window);
}

ConstraintResult SizeConstraints::applyConstraints(Window window, 
                                                    int width, int height) const {
    ConstraintResult result;
    result.width = width;
    result.height = height;
    
    
    auto [clamped_w, clamped_h] = clampToX11Limits(width, height);
    if (clamped_w != width || clamped_h != height) {
        result.hit_x11_limit = true;
        result.was_constrained = true;
        result.width = clamped_w;
        result.height = clamped_h;
    }
    
    
    if (result.width < global_min_width_) {
        result.width = global_min_width_;
        result.hit_min_limit = true;
        result.was_constrained = true;
    }
    if (result.height < global_min_height_) {
        result.height = global_min_height_;
        result.hit_min_limit = true;
        result.was_constrained = true;
    }
    if (global_max_width_ > 0 && result.width > global_max_width_) {
        result.width = global_max_width_;
        result.hit_max_limit = true;
        result.was_constrained = true;
    }
    if (global_max_height_ > 0 && result.height > global_max_height_) {
        result.height = global_max_height_;
        result.hit_max_limit = true;
        result.was_constrained = true;
    }
    
    
    auto hints = getHints(window);
    if (hints) {
        
        if (hints->flags.min_size) {
            if (result.width < hints->min_width) {
                result.width = hints->min_width;
                result.hit_min_limit = true;
                result.was_constrained = true;
            }
            if (result.height < hints->min_height) {
                result.height = hints->min_height;
                result.hit_min_limit = true;
                result.was_constrained = true;
            }
        }
        
        
        if (hints->flags.max_size) {
            if (result.width > hints->max_width) {
                result.width = hints->max_width;
                result.hit_max_limit = true;
                result.was_constrained = true;
            }
            if (result.height > hints->max_height) {
                result.height = hints->max_height;
                result.hit_max_limit = true;
                result.was_constrained = true;
            }
        }
        
        
        if (hints->flags.resize_inc) {
            auto [inc_w, inc_h] = applyResizeIncrement(*hints, 
                                                       result.width, result.height);
            if (inc_w != result.width || inc_h != result.height) {
                result.width = inc_w;
                result.height = inc_h;
                result.was_constrained = true;
            }
        }
        
        
        if (hints->flags.aspect) {
            auto [aspect_w, aspect_h] = applyAspectRatio(*hints,
                                                       result.width, result.height);
            if (aspect_w != result.width || aspect_h != result.height) {
                result.width = aspect_w;
                result.height = aspect_h;
                result.was_constrained = true;
            }
        }
    }
    
    
    std::tie(result.width, result.height) = clampToX11Limits(result.width, result.height);
    
    return result;
}

SizeConstraints::PositionResult SizeConstraints::applyConstraintsWithGravity(
    Window window,
    int x, int y,
    int width, int height,
    int old_width, int old_height
) const {
    PositionResult result;
    result.x = x;
    result.y = y;
    result.width = width;
    result.height = height;
    result.was_constrained = false;
    
    
    ConstraintResult size_result = applyConstraints(window, width, height);
    result.width = size_result.width;
    result.height = size_result.height;
    result.was_constrained = size_result.was_constrained;
    
    
    auto hints = getHints(window);
    int gravity = NorthWestGravity;
    
    if (hints && hints->flags.gravity) {
        gravity = hints->win_gravity;
    }
    
    
    int delta_w = result.width - old_width;
    int delta_h = result.height - old_height;
    
    
    switch (gravity) {
        case NorthWestGravity:
            
            break;
            
        case NorthGravity:
            
            result.x -= delta_w / 2;
            break;
            
        case NorthEastGravity:
            
            result.x -= delta_w;
            break;
            
        case WestGravity:
            
            result.y -= delta_h / 2;
            break;
            
        case CenterGravity:
            
            result.x -= delta_w / 2;
            result.y -= delta_h / 2;
            break;
            
        case EastGravity:
            
            result.x -= delta_w;
            result.y -= delta_h / 2;
            break;
            
        case SouthWestGravity:
            
            result.y -= delta_h;
            break;
            
        case SouthGravity:
            
            result.x -= delta_w / 2;
            result.y -= delta_h;
            break;
            
        case SouthEastGravity:
            
            result.x -= delta_w;
            result.y -= delta_h;
            break;
            
        case StaticGravity:
            
            
            break;
            
        default:
            break;
    }
    
    return result;
}

std::pair<int, int> SizeConstraints::clampToX11Limits(int width, int height) {
    return {
        std::clamp(width, X11Limits::MIN_WINDOW_SIZE, 
                   static_cast<int>(X11Limits::MAX_SIZE)),
        std::clamp(height, X11Limits::MIN_WINDOW_SIZE,
                   static_cast<int>(X11Limits::MAX_SIZE))
    };
}

bool SizeConstraints::isValidPosition(int x, int y) {
    return x >= X11Limits::MIN_COORD && x <= X11Limits::MAX_COORD &&
           y >= X11Limits::MIN_COORD && y <= X11Limits::MAX_COORD;
}

bool SizeConstraints::isValidSize(int width, int height) {
    return width >= X11Limits::MIN_WINDOW_SIZE && 
           width <= X11Limits::MAX_SIZE &&
           height >= X11Limits::MIN_WINDOW_SIZE && 
           height <= X11Limits::MAX_SIZE;
}

std::pair<int, int> SizeConstraints::applyResizeIncrement(
    const WindowSizeHints& hints,
    int width, int height
) {
    if (!hints.flags.resize_inc) {
        return {width, height};
    }
    
    int base_w = hints.flags.base_size ? hints.base_width : 0;
    int base_h = hints.flags.base_size ? hints.base_height : 0;
    
    
    int cells_w = (width - base_w) / hints.width_inc;
    int cells_h = (height - base_h) / hints.height_inc;
    
    
    return {
        base_w + cells_w * hints.width_inc,
        base_h + cells_h * hints.height_inc
    };
}

std::pair<int, int> SizeConstraints::applyAspectRatio(
    const WindowSizeHints& hints,
    int width, int height
) {
    if (!hints.flags.aspect) {
        return {width, height};
    }
    
    
    double aspect = static_cast<double>(width) / static_cast<double>(height);
    
    
    double min_aspect = hints.min_aspect_x / hints.min_aspect_y;
    double max_aspect = hints.max_aspect_x / hints.max_aspect_y;
    
    
    if (aspect < min_aspect) {
        
        width = static_cast<int>(height * min_aspect);
    } else if (aspect > max_aspect) {
        
        height = static_cast<int>(width / max_aspect);
    }
    
    return {width, height};
}

std::pair<int, int> SizeConstraints::getBaseSize(Window window) const {
    auto hints = getHints(window);
    if (hints && hints->flags.base_size) {
        return {hints->base_width, hints->base_height};
    }
    return {0, 0};
}

std::pair<int, int> SizeConstraints::getCellCount(Window window, 
                                                  int width, int height) const {
    auto hints = getHints(window);
    if (!hints || !hints->flags.resize_inc) {
        return {width, height};
    }
    
    auto [base_w, base_h] = getBaseSize(window);
    
    return {
        (width - base_w) / hints->width_inc,
        (height - base_h) / hints->height_inc
    };
}

void SizeConstraints::setGlobalMinSize(int min_width, int min_height) {
    global_min_width_ = std::max(1, min_width);
    global_min_height_ = std::max(1, min_height);
}

void SizeConstraints::setGlobalMaxSize(int max_width, int max_height) {
    global_max_width_ = max_width > 0 ? 
        std::min(max_width, static_cast<int>(X11Limits::MAX_SIZE)) : 
        X11Limits::MAX_SIZE;
    global_max_height_ = max_height > 0 ?
        std::min(max_height, static_cast<int>(X11Limits::MAX_SIZE)) :
        X11Limits::MAX_SIZE;
}

std::pair<int, int> SizeConstraints::getGlobalMinSize() const {
    return {global_min_width_, global_min_height_};
}

std::pair<int, int> SizeConstraints::getGlobalMaxSize() const {
    return {global_max_width_, global_max_height_};
}

} 
