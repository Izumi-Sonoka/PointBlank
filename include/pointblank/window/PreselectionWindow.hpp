#pragma once

/**
 * @file PreselectionWindow.hpp
 * @brief Pre-selection preview window for split direction visualization
 * 
 * Shows a preview overlay indicating where the next split will occur
 * when using togglesplit. Uses a semi-transparent rounded rectangle
 * to indicate the split direction and resulting window areas.
 * 
 * @author Point Blank Systems Engineering Team
 * @version 1.0.0
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>
#include <memory>
#include <chrono>
#include "pointblank/layout/LayoutEngine.hpp"

namespace pblank {

enum class PreselectionMode {
    NoPreview,      
    Vertical,       
    Horizontal,     
    Auto            
};

struct PreselectionConfig {
    unsigned long border_color{0x89B4FA};       
    unsigned long fill_color{0x002200};        
    int border_width{3};                       
    int corner_radius{8};                      
    double opacity{0.3};                       
    int animation_ms{150};                     
    bool show_label{true};                     
};

class PreselectionWindow {
public:
    PreselectionWindow();
    ~PreselectionWindow();
    
    PreselectionWindow(const PreselectionWindow&) = delete;
    PreselectionWindow& operator=(const PreselectionWindow&) = delete;
    
    bool initialize(Display* display);
    
    void showPreview(const Rect& bounds, PreselectionMode mode, double ratio = 0.5);
    
    void hidePreview();
    
    bool isVisible() const { return visible_; }
    
    void updateBounds(const Rect& bounds);
    
    void setConfig(const PreselectionConfig& config) { config_ = config; }
    
    const PreselectionConfig& getConfig() const { return config_; }
    
    void setRatio(double ratio) { ratio_ = std::clamp(ratio, 0.1, 0.9); }
    
    PreselectionMode cycleMode();
    
    PreselectionMode getMode() const { return current_mode_; }
    
    void setMode(PreselectionMode mode);
    
    void render();
    
    Window getWindow() const { return preview_window_; }
    
private:
    Display* display_{nullptr};
    Window root_window_{None};
    Window preview_window_{None};
    
    bool visible_{false};
    Rect current_bounds_;
    PreselectionMode current_mode_{PreselectionMode::NoPreview};
    double ratio_{0.5};
    PreselectionConfig config_;
    
    Picture window_picture_{None};
    Picture fill_picture_{None};
    XRenderPictFormat* pict_format_{nullptr};
    
    std::chrono::steady_clock::time_point show_time_;
    double current_opacity_{0.0};
    
    bool createOverlayWindow();
    
    bool createPictures();
    
    void drawRoundedRect(int x, int y, int width, int height, 
                         int radius, unsigned long color, double opacity);
    
    std::pair<Rect, Rect> calculatePreviewRects() const;
    
    void updateAnimation();
};

} 