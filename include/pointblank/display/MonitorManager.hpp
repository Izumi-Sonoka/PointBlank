#pragma once

/**
 * @file MonitorManager.hpp
 * @brief Multi-monitor management using XRandR extension
 * 
 * Provides automatic detection and management of multiple monitors,
 * with per-monitor camera support for the infinite canvas system.
 * 
 * @author Point Blank Systems Engineering Team
 * @version 1.0.0
 */

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include "pointblank/utils/Camera.hpp"
#include "pointblank/layout/LayoutEngine.hpp"  

namespace pblank {

struct MonitorInfo {
    int id{0};                          
    std::string name;                   
    int x{0};                           
    int y{0};                           
    unsigned int width{0};              
    unsigned int height{0};             
    unsigned int mm_width{0};           
    unsigned int mm_height{0};          
    bool primary{false};                
    bool connected{false};              
    double scale{1.0};                  
    
    std::unique_ptr<Camera> camera;
    
    Rect getBounds() const { return {x, y, width, height}; }
    
    double getDPI() const {
        if (mm_width == 0) return 96.0;  
        return (width * 25.4) / mm_width;
    }
    
    bool contains(int px, int py) const {
        return px >= x && px < x + static_cast<int>(width) &&
               py >= y && py < y + static_cast<int>(height);
    }
    
    std::pair<int, int> getCenter() const {
        return { x + static_cast<int>(width) / 2, 
                 y + static_cast<int>(height) / 2 };
    }
};

enum class MonitorEventType {
    Connected,       
    Disconnected,    
    Configuration,   
    PrimaryChanged   
};

struct MonitorEvent {
    MonitorEventType type;
    int monitor_id;
    MonitorInfo* monitor;  
};

class MonitorManager {
public:
    using MonitorCallback = std::function<void(const MonitorEvent&)>;
    
    MonitorManager();
    ~MonitorManager();
    
    MonitorManager(const MonitorManager&) = delete;
    MonitorManager& operator=(const MonitorManager&) = delete;
    
    bool initialize(Display* display);
    
    bool isAvailable() const { return xrandr_available_; }
    
    void refresh();
    
    const std::vector<MonitorInfo>& getMonitors() const { return monitors_; }
    
    size_t getMonitorCount() const { return monitors_.size(); }
    
    const MonitorInfo* getPrimaryMonitor() const;
    
    const MonitorInfo* getMonitorAt(int x, int y) const;
    
    const MonitorInfo* getMonitor(int index) const;
    
    Rect getTotalBounds() const;
    
    std::pair<unsigned int, unsigned int> getTotalDimensions() const;
    
    void setMonitorCallback(MonitorCallback callback) { callback_ = std::move(callback); }
    
    int getEventBase() const { return xrandr_event_base_; }
    
    bool handleEvent(XEvent& event);
    
    void selectEvents();
    
    Camera* getMonitorCamera(int monitor_id);
    const Camera* getMonitorCamera(int monitor_id) const;
    
    void syncCameras(int64_t virtual_x, int64_t virtual_y);
    
    const MonitorInfo* getMonitorForWindow(Window window) const;
    
private:
    Display* display_{nullptr};
    Window root_window_{None};
    bool xrandr_available_{false};
    int xrandr_event_base_{-1};
    int xrandr_error_base_{-1};
    int xrandr_major_{0};
    int xrandr_minor_{0};
    
    std::vector<MonitorInfo> monitors_;
    MonitorCallback callback_;
    
    void queryMonitors();
    
    void initializeCameras();
    
    void notifyChange(MonitorEventType type, int monitor_id, MonitorInfo* monitor);
};

} 