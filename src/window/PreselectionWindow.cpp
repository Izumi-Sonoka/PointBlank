#include "pointblank/window/PreselectionWindow.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace pblank {

PreselectionWindow::PreselectionWindow() = default;

PreselectionWindow::~PreselectionWindow() {
    if (fill_picture_ != None) {
        XRenderFreePicture(display_, fill_picture_);
    }
    if (window_picture_ != None) {
        XRenderFreePicture(display_, window_picture_);
    }
    if (preview_window_ != None && display_) {
        XDestroyWindow(display_, preview_window_);
    }
}

bool PreselectionWindow::initialize(Display* display) {
    if (!display) {
        std::cerr << "PreselectionWindow: Null display" << std::endl;
        return false;
    }
    
    display_ = display;
    root_window_ = DefaultRootWindow(display_);
    
    
    int event_base, error_base;
    if (!XRenderQueryExtension(display_, &event_base, &error_base)) {
        std::cerr << "PreselectionWindow: XRender extension not available" << std::endl;
        
    }
    
    
    int screen = DefaultScreen(display_);
    pict_format_ = XRenderFindVisualFormat(display_, 
        DefaultVisual(display_, screen));
    
    if (!createOverlayWindow()) {
        std::cerr << "PreselectionWindow: Failed to create overlay window" << std::endl;
        return false;
    }
    
    if (!createPictures()) {
        std::cerr << "PreselectionWindow: Failed to create pictures" << std::endl;
        
    }
    
    std::cout << "PreselectionWindow: Initialized successfully" << std::endl;
    return true;
}

bool PreselectionWindow::createOverlayWindow() {
    
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.background_pixel = 0;
    attrs.border_pixel = 0;
    attrs.event_mask = ExposureMask;
    
    
    Visual* visual = DefaultVisual(display_, DefaultScreen(display_));
    int depth = DefaultDepth(display_, DefaultScreen(display_));
    
    
    int num_visuals;
    XVisualInfo vinfo_template;
    vinfo_template.screen = DefaultScreen(display_);
    vinfo_template.depth = 32;
    vinfo_template.c_class = TrueColor;
    
    XVisualInfo* vinfo_list = XGetVisualInfo(display_, 
        VisualScreenMask | VisualDepthMask | VisualClassMask,
        &vinfo_template, &num_visuals);
    
    if (vinfo_list && num_visuals > 0) {
        visual = vinfo_list[0].visual;
        depth = 32;
        XFree(vinfo_list);
        std::cout << "PreselectionWindow: Using ARGB visual for transparency" << std::endl;
    }
    
    
    preview_window_ = XCreateWindow(display_, root_window_,
        0, 0, 1, 1, 0,
        depth, InputOutput, visual,
        CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask,
        &attrs);
    
    if (preview_window_ == None) {
        return false;
    }
    
    
    XStoreName(display_, preview_window_, "Pointblank-Preselection");
    
    
    Atom net_wm_window_type = XInternAtom(display_, "_NET_WM_WINDOW_TYPE", False);
    Atom net_wm_window_type_notification = XInternAtom(display_, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
    if (net_wm_window_type != None && net_wm_window_type_notification != None) {
        XChangeProperty(display_, preview_window_, net_wm_window_type,
            XA_ATOM, 32, PropModeReplace,
            reinterpret_cast<unsigned char*>(&net_wm_window_type_notification), 1);
    }
    
    
    Atom net_wm_window_opacity = XInternAtom(display_, "_NET_WM_WINDOW_OPACITY", False);
    if (net_wm_window_opacity != None) {
        unsigned long opacity = static_cast<unsigned long>(config_.opacity * 0xFFFFFFFF);
        XChangeProperty(display_, preview_window_, net_wm_window_opacity,
            XA_CARDINAL, 32, PropModeReplace,
            reinterpret_cast<unsigned char*>(&opacity), 1);
    }
    
    return true;
}

bool PreselectionWindow::createPictures() {
    if (!pict_format_) {
        return false;
    }
    
    
    window_picture_ = XRenderCreatePicture(display_, preview_window_, 
        pict_format_, 0, nullptr);
    
    if (window_picture_ == None) {
        return false;
    }
    
    
    XRenderColor color;
    color.red = 0;
    color.green = 0;
    color.blue = 0;
    color.alpha = 0;
    
    fill_picture_ = XRenderCreateSolidFill(display_, &color);
    
    return fill_picture_ != None;
}

void PreselectionWindow::showPreview(const Rect& bounds, PreselectionMode mode, double ratio) {
    if (preview_window_ == None) return;
    
    current_bounds_ = bounds;
    current_mode_ = mode;
    ratio_ = std::clamp(ratio, 0.1, 0.9);
    
    
    XMoveResizeWindow(display_, preview_window_,
        bounds.x, bounds.y, bounds.width, bounds.height);
    
    
    XMapWindow(display_, preview_window_);
    XRaiseWindow(display_, preview_window_);
    
    visible_ = true;
    show_time_ = std::chrono::steady_clock::now();
    current_opacity_ = 0.0;
    
    
    render();
    
    std::cout << "PreselectionWindow: Showing preview for " 
              << (mode == PreselectionMode::Vertical ? "vertical" : 
                  mode == PreselectionMode::Horizontal ? "horizontal" : "auto")
              << " split at ratio " << ratio_ << std::endl;
}

void PreselectionWindow::hidePreview() {
    if (preview_window_ == None) return;
    
    XUnmapWindow(display_, preview_window_);
    visible_ = false;
    current_mode_ = PreselectionMode::NoPreview;
    
    std::cout << "PreselectionWindow: Preview hidden" << std::endl;
}

void PreselectionWindow::updateBounds(const Rect& bounds) {
    if (!visible_) return;
    
    current_bounds_ = bounds;
    XMoveResizeWindow(display_, preview_window_,
        bounds.x, bounds.y, bounds.width, bounds.height);
    render();
}

void PreselectionWindow::setMode(PreselectionMode mode) {
    if (mode == current_mode_) return;
    
    current_mode_ = mode;
    if (visible_) {
        render();
    }
}

PreselectionMode PreselectionWindow::cycleMode() {
    switch (current_mode_) {
        case PreselectionMode::NoPreview:
            current_mode_ = PreselectionMode::Vertical;
            break;
        case PreselectionMode::Vertical:
            current_mode_ = PreselectionMode::Horizontal;
            break;
        case PreselectionMode::Horizontal:
            current_mode_ = PreselectionMode::Auto;
            break;
        case PreselectionMode::Auto:
            current_mode_ = PreselectionMode::NoPreview;
            break;
    }
    
    if (visible_) {
        render();
    }
    
    return current_mode_;
}

void PreselectionWindow::render() {
    if (!visible_ || preview_window_ == None) return;
    
    updateAnimation();
    
    
    XClearWindow(display_, preview_window_);
    
    
    auto [left_rect, right_rect] = calculatePreviewRects();
    
    
    drawRoundedRect(left_rect.x - current_bounds_.x, 
                    left_rect.y - current_bounds_.y,
                    left_rect.width, left_rect.height,
                    config_.corner_radius, config_.fill_color, current_opacity_);
    
    drawRoundedRect(right_rect.x - current_bounds_.x,
                    right_rect.y - current_bounds_.y,
                    right_rect.width, right_rect.height,
                    config_.corner_radius, config_.fill_color, current_opacity_);
    
    
    drawRoundedRect(left_rect.x - current_bounds_.x,
                    left_rect.y - current_bounds_.y,
                    left_rect.width, left_rect.height,
                    config_.corner_radius, config_.border_color, current_opacity_ * 2);
    
    drawRoundedRect(right_rect.x - current_bounds_.x,
                    right_rect.y - current_bounds_.y,
                    right_rect.width, right_rect.height,
                    config_.corner_radius, config_.border_color, current_opacity_ * 2);
    
    
    GC gc = DefaultGC(display_, DefaultScreen(display_));
    XSetForeground(display_, gc, config_.border_color);
    
    if (current_mode_ == PreselectionMode::Vertical) {
        int split_x = static_cast<int>(current_bounds_.width * ratio_);
        XDrawLine(display_, preview_window_, gc, split_x, 0, split_x, current_bounds_.height);
    } else if (current_mode_ == PreselectionMode::Horizontal) {
        int split_y = static_cast<int>(current_bounds_.height * ratio_);
        XDrawLine(display_, preview_window_, gc, 0, split_y, current_bounds_.width, split_y);
    }
    
    XFlush(display_);
}

void PreselectionWindow::drawRoundedRect(int x, int y, int width, int height,
                                          int radius, unsigned long color, double opacity) {
    if (width <= 0 || height <= 0) return;
    
    
    GC gc = DefaultGC(display_, DefaultScreen(display_));
    
    
    XColor xcolor;
    xcolor.pixel = color;
    XQueryColor(display_, DefaultColormap(display_, DefaultScreen(display_)), &xcolor);
    
    
    unsigned short red = static_cast<unsigned short>(xcolor.red * opacity);
    unsigned short green = static_cast<unsigned short>(xcolor.green * opacity);
    unsigned short blue = static_cast<unsigned short>(xcolor.blue * opacity);
    
    XColor draw_color;
    draw_color.red = red;
    draw_color.green = green;
    draw_color.blue = blue;
    draw_color.flags = DoRed | DoGreen | DoBlue;
    
    Colormap cmap = DefaultColormap(display_, DefaultScreen(display_));
    XAllocColor(display_, cmap, &draw_color);
    
    XSetForeground(display_, gc, draw_color.pixel);
    
    
    
    if (radius > 0 && fill_picture_ != None) {
        
        XRenderColor render_color;
        render_color.red = xcolor.red;
        render_color.green = xcolor.green;
        render_color.blue = xcolor.blue;
        render_color.alpha = static_cast<unsigned short>(opacity * 0xFFFF);
        
        
        
        
        
        
        XRectangle rects[3];
        rects[0].x = x + radius;
        rects[0].y = y;
        rects[0].width = width - 2 * radius;
        rects[0].height = height;
        
        rects[1].x = x;
        rects[1].y = y + radius;
        rects[1].width = radius;
        rects[1].height = height - 2 * radius;
        
        rects[2].x = x + width - radius;
        rects[2].y = y + radius;
        rects[2].width = radius;
        rects[2].height = height - 2 * radius;
        
        XRenderFillRectangles(display_, PictOpOver, window_picture_,
            &render_color, rects, 3);
    } else {
        
        XFillRectangle(display_, preview_window_, gc, x, y, width, height);
    }
    
    XFreeColors(display_, cmap, &draw_color.pixel, 1, 0);
}

std::pair<Rect, Rect> PreselectionWindow::calculatePreviewRects() const {
    Rect left_rect, right_rect;
    
    if (current_mode_ == PreselectionMode::Vertical) {
        
        int split_w = static_cast<int>(current_bounds_.width * ratio_);
        
        left_rect.x = current_bounds_.x;
        left_rect.y = current_bounds_.y;
        left_rect.width = split_w;
        left_rect.height = current_bounds_.height;
        
        right_rect.x = current_bounds_.x + split_w;
        right_rect.y = current_bounds_.y;
        right_rect.width = current_bounds_.width - split_w;
        right_rect.height = current_bounds_.height;
    } else if (current_mode_ == PreselectionMode::Horizontal) {
        
        int split_h = static_cast<int>(current_bounds_.height * ratio_);
        
        left_rect.x = current_bounds_.x;
        left_rect.y = current_bounds_.y;
        left_rect.width = current_bounds_.width;
        left_rect.height = split_h;
        
        right_rect.x = current_bounds_.x;
        right_rect.y = current_bounds_.y + split_h;
        right_rect.width = current_bounds_.width;
        right_rect.height = current_bounds_.height - split_h;
    } else {
        
        if (current_bounds_.width > current_bounds_.height) {
            
            int split_w = static_cast<int>(current_bounds_.width * ratio_);
            
            left_rect.x = current_bounds_.x;
            left_rect.y = current_bounds_.y;
            left_rect.width = split_w;
            left_rect.height = current_bounds_.height;
            
            right_rect.x = current_bounds_.x + split_w;
            right_rect.y = current_bounds_.y;
            right_rect.width = current_bounds_.width - split_w;
            right_rect.height = current_bounds_.height;
        } else {
            
            int split_h = static_cast<int>(current_bounds_.height * ratio_);
            
            left_rect.x = current_bounds_.x;
            left_rect.y = current_bounds_.y;
            left_rect.width = current_bounds_.width;
            left_rect.height = split_h;
            
            right_rect.x = current_bounds_.x;
            right_rect.y = current_bounds_.y + split_h;
            right_rect.width = current_bounds_.width;
            right_rect.height = current_bounds_.height - split_h;
        }
    }
    
    return {left_rect, right_rect};
}

void PreselectionWindow::updateAnimation() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - show_time_).count();
    
    if (elapsed < config_.animation_ms) {
        
        current_opacity_ = static_cast<double>(elapsed) / config_.animation_ms * config_.opacity;
    } else {
        current_opacity_ = config_.opacity;
    }
}

} 