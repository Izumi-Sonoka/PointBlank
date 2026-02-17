#include "pointblank/display/MonitorManager.hpp"
#include <iostream>
#include <algorithm>
#include <climits>

namespace pblank {

MonitorManager::MonitorManager() = default;

MonitorManager::~MonitorManager() = default;

bool MonitorManager::initialize(Display* display) {
    if (!display) {
        std::cerr << "MonitorManager: Null display" << std::endl;
        return false;
    }
    
    display_ = display;
    root_window_ = DefaultRootWindow(display_);
    
    // Check for XRandR extension
    int event_base, error_base;
    if (!XRRQueryExtension(display_, &event_base, &error_base)) {
        std::cerr << "MonitorManager: XRandR extension not available" << std::endl;
        xrandr_available_ = false;
        return false;
    }
    
    xrandr_event_base_ = event_base;
    xrandr_error_base_ = error_base;
    
    // Query XRandR version
    int major, minor;
    if (!XRRQueryVersion(display_, &major, &minor)) {
        std::cerr << "MonitorManager: Failed to query XRandR version" << std::endl;
        return false;
    }
    
    xrandr_major_ = major;
    xrandr_minor_ = minor;
    xrandr_available_ = true;
    
    std::cout << "MonitorManager: XRandR " << major << "." << minor << " available" << std::endl;
    
    // Initial monitor query
    queryMonitors();
    initializeCameras();
    selectEvents();
    
    return true;
}

void MonitorManager::queryMonitors() {
    if (!display_ || !xrandr_available_) return;
    
    monitors_.clear();
    
    XRRScreenResources* resources = XRRGetScreenResources(display_, root_window_);
    if (!resources) {
        std::cerr << "MonitorManager: Failed to get screen resources" << std::endl;
        return;
    }
    
    RROutput primary_output = XRRGetOutputPrimary(display_, root_window_);
    
    for (int i = 0; i < resources->noutput; ++i) {
        RROutput output = resources->outputs[i];
        XRROutputInfo* output_info = XRRGetOutputInfo(display_, resources, output);
        
        if (!output_info) continue;
        
        MonitorInfo monitor;
        monitor.id = static_cast<int>(monitors_.size());
        monitor.name = output_info->name ? output_info->name : "Unknown";
        monitor.connected = (output_info->connection == RR_Connected);
        monitor.primary = (output == primary_output);
        monitor.mm_width = output_info->mm_width;
        monitor.mm_height = output_info->mm_height;
        
        if (output_info->connection == RR_Connected && output_info->crtc != None) {
            XRRCrtcInfo* crtc_info = XRRGetCrtcInfo(display_, resources, output_info->crtc);
            
            if (crtc_info) {
                monitor.x = crtc_info->x;
                monitor.y = crtc_info->y;
                monitor.width = crtc_info->width;
                monitor.height = crtc_info->height;
                
                XRRFreeCrtcInfo(crtc_info);
            }
        }
        
        // Calculate scale for HiDPI
        double dpi = monitor.getDPI();
        if (dpi > 120.0) {
            monitor.scale = dpi / 96.0;
        }
        
        std::cout << "MonitorManager: Found monitor " << monitor.name 
                  << " (" << monitor.width << "x" << monitor.height 
                  << " at " << monitor.x << "," << monitor.y << ")"
                  << (monitor.primary ? " [PRIMARY]" : "")
                  << std::endl;
        
        monitors_.push_back(std::move(monitor));
        XRRFreeOutputInfo(output_info);
    }
    
    XRRFreeScreenResources(resources);
    
    // If no monitors found, create a default one
    if (monitors_.empty()) {
        std::cerr << "MonitorManager: No monitors found, using default" << std::endl;
        
        MonitorInfo default_monitor;
        default_monitor.id = 0;
        default_monitor.name = "Default";
        default_monitor.x = 0;
        default_monitor.y = 0;
        default_monitor.width = DisplayWidth(display_, DefaultScreen(display_));
        default_monitor.height = DisplayHeight(display_, DefaultScreen(display_));
        default_monitor.primary = true;
        default_monitor.connected = true;
        
        monitors_.push_back(std::move(default_monitor));
    }
}

void MonitorManager::initializeCameras() {
    for (auto& monitor : monitors_) {
        if (!monitor.camera) {
            monitor.camera = std::make_unique<Camera>(monitor.width, monitor.height);
        } else {
            monitor.camera->setScreenDimensions(monitor.width, monitor.height);
        }
    }
}

void MonitorManager::refresh() {
    if (!xrandr_available_) return;
    
    std::cout << "MonitorManager: Refreshing monitor configuration" << std::endl;
    
    // Store old monitor count
    size_t old_count = monitors_.size();
    
    // Re-query monitors
    queryMonitors();
    initializeCameras();
    
    // Notify of changes
    if (monitors_.size() != old_count) {
        notifyChange(MonitorEventType::Configuration, -1, nullptr);
    }
}

const MonitorInfo* MonitorManager::getPrimaryMonitor() const {
    for (const auto& monitor : monitors_) {
        if (monitor.primary) return &monitor;
    }
    
    // Return first monitor if no primary
    return monitors_.empty() ? nullptr : &monitors_[0];
}

const MonitorInfo* MonitorManager::getMonitorAt(int x, int y) const {
    for (const auto& monitor : monitors_) {
        if (monitor.contains(x, y)) return &monitor;
    }
    return nullptr;
}

const MonitorInfo* MonitorManager::getMonitor(int index) const {
    if (index < 0 || index >= static_cast<int>(monitors_.size())) {
        return nullptr;
    }
    return &monitors_[index];
}

Rect MonitorManager::getTotalBounds() const {
    if (monitors_.empty()) return {0, 0, 1920, 1080};
    
    int min_x = INT_MAX, min_y = INT_MAX;
    int max_x = INT_MIN, max_y = INT_MIN;
    
    for (const auto& monitor : monitors_) {
        min_x = std::min(min_x, monitor.x);
        min_y = std::min(min_y, monitor.y);
        max_x = std::max(max_x, static_cast<int>(monitor.x + monitor.width));
        max_y = std::max(max_y, static_cast<int>(monitor.y + monitor.height));
    }
    
    return {min_x, min_y, 
            static_cast<unsigned int>(max_x - min_x),
            static_cast<unsigned int>(max_y - min_y)};
}

std::pair<unsigned int, unsigned int> MonitorManager::getTotalDimensions() const {
    auto bounds = getTotalBounds();
    return {bounds.width, bounds.height};
}

void MonitorManager::selectEvents() {
    if (!display_ || !xrandr_available_ || root_window_ == None) return;
    
    // Select for screen change notifications
    XRRSelectInput(display_, root_window_, 
                   RRScreenChangeNotifyMask | 
                   RRCrtcChangeNotifyMask | 
                   RROutputChangeNotifyMask |
                   RROutputPropertyNotifyMask);
    
    XFlush(display_);
}

bool MonitorManager::handleEvent(XEvent& event) {
    if (!xrandr_available_ || event.type != xrandr_event_base_ + RRScreenChangeNotify) {
        return false;
    }
    
    std::cout << "MonitorManager: Received XRandR screen change event" << std::endl;
    
    // Process the screen change
    XRRScreenChangeNotifyEvent* scn_event = 
        reinterpret_cast<XRRScreenChangeNotifyEvent*>(&event);
    
    (void)scn_event;  // Unused for now
    
    // Refresh configuration
    refresh();
    
    return true;
}

Camera* MonitorManager::getMonitorCamera(int monitor_id) {
    if (monitor_id < 0 || monitor_id >= static_cast<int>(monitors_.size())) {
        return nullptr;
    }
    return monitors_[monitor_id].camera.get();
}

const Camera* MonitorManager::getMonitorCamera(int monitor_id) const {
    if (monitor_id < 0 || monitor_id >= static_cast<int>(monitors_.size())) {
        return nullptr;
    }
    return monitors_[monitor_id].camera.get();
}

void MonitorManager::syncCameras(int64_t virtual_x, int64_t virtual_y) {
    for (auto& monitor : monitors_) {
        if (monitor.camera) {
            monitor.camera->teleportTo(virtual_x, virtual_y);
        }
    }
}

const MonitorInfo* MonitorManager::getMonitorForWindow(Window window) const {
    if (!display_) return nullptr;
    
    // Get window geometry
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(display_, window, &attrs)) {
        return getPrimaryMonitor();
    }
    
    // Get window center
    int center_x = attrs.x + attrs.width / 2;
    int center_y = attrs.y + attrs.height / 2;
    
    // Translate to root coordinates
    Window child;
    XTranslateCoordinates(display_, window, root_window_,
                         center_x, center_y,
                         &center_x, &center_y, &child);
    
    return getMonitorAt(center_x, center_y);
}

void MonitorManager::notifyChange(MonitorEventType type, int monitor_id, MonitorInfo* monitor) {
    if (callback_) {
        MonitorEvent event;
        event.type = type;
        event.monitor_id = monitor_id;
        event.monitor = monitor;
        callback_(event);
    }
}

} // namespace pblank