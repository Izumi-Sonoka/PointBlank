#pragma once

/**
 * @file Camera.hpp
 * @brief Camera model for infinite canvas coordinate transformation
 * 
 * The camera remains at origin (0,0) in screen space. When the user "pans",
 * we apply the inverse transformation to all window coordinates, keeping
 * rendered positions within X11's 16-bit safe zone (-32768 to 32767).
 * 
 * @author Point Blank Systems Engineering Team
 * @version 1.0.0
 */

#include <cstdint>
#include <utility>
#include <algorithm>
#include <cmath>

namespace pblank {

struct VirtualRect {
    int64_t x{0};
    int64_t y{0};
    unsigned int width{0};
    unsigned int height{0};
    
    bool contains(int64_t px, int64_t py) const {
        return px >= x && px < x + width &&
               py >= y && py < y + height;
    }
    
    bool overlaps(const VirtualRect& other) const {
        return x < other.x + other.width &&
               x + width > other.x &&
               y < other.y + other.height &&
               y + height > other.y;
    }
    
    std::pair<int64_t, int64_t> center() const {
        return { x + width / 2, y + height / 2 };
    }
};

struct ScreenRect {
    int x{0};
    int y{0};
    unsigned int width{0};
    unsigned int height{0};
    
    bool isValid() const {
        return x >= X11_MIN && x <= X11_MAX &&
               y >= X11_MIN && y <= X11_MAX &&
               width <= MAX_WINDOW_DIMENSION &&
               height <= MAX_WINDOW_DIMENSION;
    }
    
    static constexpr int X11_MIN = -32768;
    static constexpr int X11_MAX = 32767;
    static constexpr unsigned int MAX_WINDOW_DIMENSION = 32767;
};

class Camera {
public:
    Camera() = default;
    
    Camera(unsigned int screen_width, unsigned int screen_height)
        : screen_width_(screen_width)
        , screen_height_(screen_height)
    {}
    
    std::pair<int64_t, int64_t> pan(int64_t dx, int64_t dy) {
        offset_x_ += dx;
        offset_y_ += dy;
        return { dx, dy };
    }
    
    std::pair<int64_t, int64_t> getOffset() const {
        return { offset_x_, offset_y_ };
    }
    
    void setOffset(int64_t x, int64_t y) {
        offset_x_ = x;
        offset_y_ = y;
    }
    
    void teleportTo(int64_t virtual_x, int64_t virtual_y) {
        
        offset_x_ = virtual_x - screen_width_ / 2;
        offset_y_ = virtual_y - screen_height_ / 2;
    }
    
    std::pair<int, int> toScreen(int64_t virtual_x, int64_t virtual_y) const {
        
        int64_t screen_x = virtual_x - offset_x_;
        int64_t screen_y = virtual_y - offset_y_;
        
        screen_x = std::clamp(screen_x, 
                              static_cast<int64_t>(ScreenRect::X11_MIN),
                              static_cast<int64_t>(ScreenRect::X11_MAX));
        screen_y = std::clamp(screen_y,
                              static_cast<int64_t>(ScreenRect::X11_MIN),
                              static_cast<int64_t>(ScreenRect::X11_MAX));
        
        return { static_cast<int>(screen_x), static_cast<int>(screen_y) };
    }
    
    ScreenRect toScreenRect(const VirtualRect& vrect) const {
        auto [sx, sy] = toScreen(vrect.x, vrect.y);
        
        unsigned int w = std::min(vrect.width, ScreenRect::MAX_WINDOW_DIMENSION);
        unsigned int h = std::min(vrect.height, ScreenRect::MAX_WINDOW_DIMENSION);
        
        return { sx, sy, w, h };
    }
    
    std::pair<int64_t, int64_t> toVirtual(int screen_x, int screen_y) const {
        return { offset_x_ + screen_x, offset_y_ + screen_y };
    }
    
    bool isVisible(const VirtualRect& vrect) const {
        
        VirtualRect visible = getVisibleBounds();
        return visible.overlaps(vrect);
    }
    
    bool isVisible(int64_t virtual_x, int64_t virtual_y) const {
        VirtualRect visible = getVisibleBounds();
        return visible.contains(virtual_x, virtual_y);
    }
    
    bool isFullyVisible(const VirtualRect& vrect) const {
        VirtualRect visible = getVisibleBounds();
        return vrect.x >= visible.x &&
               vrect.y >= visible.y &&
               vrect.x + vrect.width <= visible.x + visible.width &&
               vrect.y + vrect.height <= visible.y + visible.height;
    }
    
    VirtualRect getVisibleBounds() const {
        return { offset_x_, offset_y_, screen_width_, screen_height_ };
    }
    
    std::pair<unsigned int, unsigned int> getScreenDimensions() const {
        return { screen_width_, screen_height_ };
    }
    
    void setScreenDimensions(unsigned int width, unsigned int height) {
        screen_width_ = width;
        screen_height_ = height;
    }
    
    void centerOn(int64_t virtual_x, int64_t virtual_y) {
        offset_x_ = virtual_x - screen_width_ / 2;
        offset_y_ = virtual_y - screen_height_ / 2;
    }
    
    void centerOn(const VirtualRect& vrect) {
        auto [cx, cy] = vrect.center();
        centerOn(cx, cy);
    }
    
    int64_t distanceTo(int64_t virtual_x, int64_t virtual_y) const {
        int64_t cx = offset_x_ + screen_width_ / 2;
        int64_t cy = offset_y_ + screen_height_ / 2;
        int64_t dx = virtual_x - cx;
        int64_t dy = virtual_y - cy;
        
        return std::abs(dx) + std::abs(dy);
    }
    
    double euclideanDistanceTo(int64_t virtual_x, int64_t virtual_y) const {
        int64_t cx = offset_x_ + screen_width_ / 2;
        int64_t cy = offset_y_ + screen_height_ / 2;
        int64_t dx = virtual_x - cx;
        int64_t dy = virtual_y - cy;
        return std::sqrt(static_cast<double>(dx * dx + dy * dy));
    }
    
    std::pair<int64_t, int64_t> getVirtualCenter() const {
        return { offset_x_ + screen_width_ / 2, offset_y_ + screen_height_ / 2 };
    }
    
    std::pair<int64_t, int64_t> getVirtualTopLeft() const {
        return { offset_x_, offset_y_ };
    }
    
    std::pair<int64_t, int64_t> getVirtualBottomRight() const {
        return { offset_x_ + screen_width_, offset_y_ + screen_height_ };
    }

private:
    
    int64_t offset_x_{0};
    int64_t offset_y_{0};
    
    unsigned int screen_width_{1920};
    unsigned int screen_height_{1080};
};

} 
