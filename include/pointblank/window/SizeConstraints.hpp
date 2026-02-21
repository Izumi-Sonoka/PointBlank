/**
 * @file SizeConstraints.hpp
 * @brief Window Size Constraints Manager
 * 
 * Manages window size constraints including:
 * - X11 16-bit coordinate limits (32767 pixel hard-cap)
 * - Application-specified size hints (WM_NORMAL_HINTS)
 * - User-defined minimum/maximum sizes
 * - Aspect ratio constraints
 * - Resize increment constraints (grid alignment)
 * 
 * Phase 8 of Enhanced TWM Features
 */

#ifndef SIZECONSTRAINTS_HPP
#define SIZECONSTRAINTS_HPP

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace pblank {

namespace X11Limits {
    constexpr int16_t MIN_COORD = -32768;
    constexpr int16_t MAX_COORD = 32767;
    constexpr uint16_t MAX_SIZE = 32767;
    constexpr int MIN_WINDOW_SIZE = 1;
}

struct SizeHintFlags {
    bool user_position{false};      
    bool user_size{false};          
    bool program_position{false};   
    bool program_size{false};       
    bool min_size{false};           
    bool max_size{false};           
    bool resize_inc{false};         
    bool aspect{false};             
    bool base_size{false};          
    bool gravity{false};            
};

struct WindowSizeHints {
    SizeHintFlags flags;
    
    int min_width{1};
    int min_height{1};
    int max_width{X11Limits::MAX_SIZE};
    int max_height{X11Limits::MAX_SIZE};
    
    int base_width{0};
    int base_height{0};
    
    int width_inc{1};
    int height_inc{1};
    
    double min_aspect_x{0};
    double min_aspect_y{0};
    double max_aspect_x{0};
    double max_aspect_y{0};
    
    int win_gravity{NorthWestGravity};
    
    int constrained_width{0};
    int constrained_height{0};
};

struct ConstraintResult {
    int width;
    int height;
    bool was_constrained{false};
    bool hit_min_limit{false};
    bool hit_max_limit{false};
    bool hit_x11_limit{false};
};

class SizeConstraints {
public:
    
    static SizeConstraints& instance();

    void initialize(Display* display);

    bool readHints(Window window);

    std::optional<WindowSizeHints> getHints(Window window) const;

    void removeWindow(Window window);

    ConstraintResult applyConstraints(Window window, int width, int height) const;

    struct PositionResult {
        int x, y, width, height;
        bool was_constrained;
    };
    PositionResult applyConstraintsWithGravity(
        Window window,
        int x, int y,
        int width, int height,
        int old_width, int old_height
    ) const;

    static std::pair<int, int> clampToX11Limits(int width, int height);

    static bool isValidPosition(int x, int y);

    static bool isValidSize(int width, int height);

    static std::pair<int, int> applyResizeIncrement(
        const WindowSizeHints& hints,
        int width, int height
    );

    static std::pair<int, int> applyAspectRatio(
        const WindowSizeHints& hints,
        int width, int height
    );

    std::pair<int, int> getBaseSize(Window window) const;

    std::pair<int, int> getCellCount(Window window, int width, int height) const;

    void setGlobalMinSize(int min_width, int min_height);

    void setGlobalMaxSize(int max_width, int max_height);

    std::pair<int, int> getGlobalMinSize() const;

    std::pair<int, int> getGlobalMaxSize() const;

private:
    SizeConstraints() = default;
    SizeConstraints(const SizeConstraints&) = delete;
    SizeConstraints& operator=(const SizeConstraints&) = delete;

    Display* display_{nullptr};
    
    int global_min_width_{100};
    int global_min_height_{50};
    int global_max_width_{X11Limits::MAX_SIZE};
    int global_max_height_{X11Limits::MAX_SIZE};
    
    std::unordered_map<Window, WindowSizeHints> hints_cache_;
};

} 

#endif 
