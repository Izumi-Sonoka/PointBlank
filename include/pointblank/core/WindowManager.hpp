#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>
#include <filesystem>
#include <optional>
#include <set>
#include <string_view>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include "pointblank/display/EWMHManager.hpp"
#include "pointblank/display/MonitorManager.hpp"
#include "pointblank/ipc/IPCServer.hpp"
#include "pointblank/window/ScratchpadManager.hpp"
#include "pointblank/performance/RenderPipeline.hpp"
#include "pointblank/performance/PerformanceTuner.hpp"
#include "pointblank/window/WindowSwallower.hpp"

namespace pblank {

struct DisplayDeleter {
    void operator()(Display* display) const {
        if (display) XCloseDisplay(display);
    }
};

struct WindowDeleter {
    Display* display;
    void operator()(Window* window) const {
        if (window && display) {
            XDestroyWindow(display, *window);
            delete window;
        }
    }
};

struct GCDeleter {
    Display* display;
    void operator()(GC* gc) const {
        if (gc && display) {
            XFreeGC(display, *gc);
            delete gc;
        }
    }
};

using DisplayPtr = std::unique_ptr<Display, DisplayDeleter>;
using WindowPtr = std::unique_ptr<Window, WindowDeleter>;
using GCPtr = std::unique_ptr<GC, GCDeleter>;

class ConfigParser;
class LayoutEngine;
class Toaster;
class KeybindManager;
class LayoutConfigParser;
class ConfigWatcher;

namespace ewmh {
    class EWMHManager;
}

/**
 * @brief Represents a managed window client
 */
class ManagedWindow {
public:
    ManagedWindow(Window window, Display* display);
    
    Window getWindow() const { return window_; }
    int getWorkspace() const { return workspace_; }
    void setWorkspace(int ws) { workspace_ = ws; }
    
    std::string getClass() const;
    std::string getTitle() const;
    
    void setGeometry(int x, int y, unsigned int width, unsigned int height);
    void getGeometry(int& x, int& y, unsigned int& width, unsigned int& height) const;
    
    bool isFloating() const { return floating_; }
    void setFloating(bool floating) { floating_ = floating; }
    
    bool isFullscreen() const { return fullscreen_; }
    void setFullscreen(bool fullscreen);
    
    bool isHidden() const { return hidden_; }
    void setHidden(bool hidden) { hidden_ = hidden; }
    
    void setOpacity(double opacity);
    
    void storeTiledGeometry(int x, int y, unsigned int width, unsigned int height);
    void getTiledGeometry(int& x, int& y, unsigned int& width, unsigned int& height) const;
    
private:
    Window window_;
    Display* display_;
    int workspace_{0};
    bool floating_{false};
    bool fullscreen_{false};
    bool hidden_{false};
    
    int x_{0}, y_{0};
    unsigned int width_{0}, height_{0};
    
    int tiled_x_{0}, tiled_y_{0};
    unsigned int tiled_width_{0}, tiled_height_{0};
};

class WindowManager {
public:
    WindowManager();
    ~WindowManager();

    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;
    WindowManager(WindowManager&&) = delete;
    WindowManager& operator=(WindowManager&&) = delete;

    bool initialize();

    void setConfigPath(const std::filesystem::path& path);

    void run();

    void reloadConfig();

    Display* getDisplay() const { return display_.get(); }

    Window getRootWindow() const { return root_; }

    void killActiveWindow();

    Window getFocusedWindow() const;

    void switchWorkspace(int workspace);
    
    void moveWindowToWorkspace(int workspace, bool follow = false);
    
    int getCurrentWorkspace() const { return current_workspace_; }
    
    int getWorkspaceWindowCount(int workspace) const;

    void switchWorkspaceOnMonitor(int workspace, int monitor_id);
    
    int getCurrentMonitor() const { return current_monitor_; }
    
    void setCurrentMonitor(int monitor_id);
    
    bool isPerMonitorWorkspaces() const { return per_monitor_workspaces_; }
    
    void mapWorkspaceToMonitor(int workspace, int monitor_id);
    
    int getWorkspaceMonitor(int workspace) const;

    void moveFocus(const std::string& direction);
    
    void focusWindow(Window window);

    void swapFocusedWindow(const std::string& direction);

    void resizeFocusedWindow(const std::string& direction);
    
    void resizeFocusedWindow(double delta);

    void toggleFloating();
    
    void toggleFullscreen();
    
    void setFullscreen(Window window, bool fullscreen);
    
    void showScratchpad();
    
    void showScratchpadNext();
    
    void showScratchpadPrevious();
    
    void hideToScratchpad();

    void setLayout(const std::string& layout_name);
    
    void cycleLayoutNext();
    
    void cycleLayoutPrev();
    
    void toggleSplitDirection();

    void execCommand(const std::string& command);
    
    void exit() { running_ = false; }

private:
    
    DisplayPtr display_;
    Window root_;
    int screen_;

    std::unique_ptr<ConfigParser> config_parser_;
    std::unique_ptr<LayoutConfigParser> layout_config_parser_;
    std::unique_ptr<LayoutEngine> layout_engine_;
    std::unique_ptr<Toaster> toaster_;
    std::unique_ptr<KeybindManager> keybind_manager_;
    std::unique_ptr<ConfigWatcher> config_watcher_;
    std::unique_ptr<MonitorManager> monitor_manager_;
    
    std::unique_ptr<ewmh::EWMHManager> ewmh_manager_;

    std::unique_ptr<ScratchpadManager> scratchpad_manager_;
    
    std::unique_ptr<IPCServer> ipc_server_;
    
    std::unique_ptr<RenderPipeline> render_pipeline_;
    std::unique_ptr<PerformanceTuner> performance_tuner_;
    
    std::unique_ptr<WindowSwallower> window_swallower_;
    
    std::optional<std::filesystem::path> custom_config_path_;

    std::unordered_map<Window, std::unique_ptr<ManagedWindow>> clients_;
    
    inline void reserveClients(size_t size) { clients_.reserve(size); }
    
    inline ManagedWindow* findClient(Window window) {
        auto it = clients_.find(window);
        return (it != clients_.end()) ? it->second.get() : nullptr;
    }
    
    inline const ManagedWindow* findClient(Window window) const {
        auto it = clients_.find(window);
        return (it != clients_.end()) ? it->second.get() : nullptr;
    }
    
    int current_workspace_{0};
    int max_workspaces_{12};
    bool infinite_workspaces_{false};
    bool dynamic_workspace_creation_{true};
    bool auto_remove_empty_workspaces_{true};
    int min_persist_workspaces_{1};
    int highest_used_workspace_{0};  
    
    bool per_monitor_workspaces_{false};     
    bool virtual_workspace_mapping_{false};   
    std::unordered_map<int, int> workspace_to_monitor_;  
    std::vector<std::vector<Window>> per_monitor_last_focus_;  
    int current_monitor_{0};  
    
    bool running_{true};
    bool focus_follows_mouse_{false}; 
    bool monitor_focus_follows_mouse_{false};  
    int current_monitor_id_{-1};  
    
    std::vector<Window> workspace_last_focus_;
    
    std::set<Window> pending_unmaps_;

    bool dragging_{false};
    Window drag_window_{None};
    int drag_start_x_{0};
    int drag_start_y_{0};
    int drag_current_x_{0};  
    int drag_current_y_{0};  
    int drag_window_start_x_{0};
    int drag_window_start_y_{0};
    bool drag_was_floating_{false};  
    Window drag_last_swap_target_{None};  
    
    bool resizing_{false};
    Window resize_window_{None};
    int resize_start_x_{0};
    int resize_start_y_{0};
    int resize_start_width_{0};
    int resize_start_height_{0};
    int resize_start_window_x_{0};  
    int resize_start_window_y_{0};
    std::string resize_edge_{};  
    
    bool bidirectional_resize_{false};
    Window bidirectional_resize_window_{None};
    int bidirectional_resize_start_x_{0};
    int bidirectional_resize_start_y_{0};
    int bidirectional_resize_window_x_{0};
    int bidirectional_resize_window_y_{0};
    unsigned int bidirectional_resize_window_width_{0};
    unsigned int bidirectional_resize_window_height_{0};
    bool bidirectional_resize_was_floating_{false};  
    
    bool auto_resize_non_docks_{true};
    bool floating_resize_enabled_{true};
    int floating_resize_edge_size_{8};
    
    bool is_warping_{false};

    void handleMapRequest(const XMapRequestEvent& event);
    void handleConfigureRequest(const XConfigureRequestEvent& event);
    void handleKeyPress(const XKeyEvent& event);
    void handleButtonPress(const XButtonEvent& event);
    void handleButtonRelease(const XButtonEvent& event);
    void handleMotionNotify(const XMotionEvent& event);
    void handleDestroyNotify(const XDestroyWindowEvent& event);
    void handleUnmapNotify(const XUnmapEvent& event);
    void handleEnterNotify(const XCrossingEvent& event);
    void handleFocusIn(const XFocusChangeEvent& event);
    void handlePropertyNotify(const XPropertyEvent& event);

    void startDrag(Window window, int root_x, int root_y);
    void updateDrag(int root_x, int root_y);
    void endDrag();
    Window findWindowAtPosition(int root_x, int root_y);
    
    void startResize(Window window, int root_x, int root_y, const std::string& edge);
    void updateResize(int root_x, int root_y);
    void endResize();
    std::string getEdgeAtPosition(Window window, int root_x, int root_y);
    
    void startBidirectionalResize(Window window, int root_x, int root_y);
    void updateBidirectionalResize(int root_x, int root_y);
    void endBidirectionalResize();

    bool becomeWindowManager();
    void setupEventMask();
    void setupScratchpadManager();
    void setupIPCServer();
    void scanExistingWindows();
    void applyLayout();
    
    bool loadConfigSafe();
    void fallbackToDefaultConfig();
    
    void applyConfigToLayout();
    
    void setupConfigWatcher();
    
    void hideWorkspaceWindows(int workspace);
    void showWorkspaceWindows(int workspace);
    void updateWindowVisibility();
    
    void updateFocusAfterSwitch();
    void updateAllBorders();
    Window findWindowToFocus(int workspace);
    
    void unmanageWindow(Window window);
    void manageWindow(Window window);
    
    void updateEWMHClientList();
    void updateEWMHActiveWindow(Window window);
    void updateEWMHWorkspaceCount();
    void updateEWMHCurrentWorkspace();
    void updateEWMHWindowDesktop(Window window, int desktop);
    void updateEWMHWindowState(Window window);
    
    void updateExternalBarProperties();
    void updateExternalBarWorkspace();
    void updateExternalBarActiveWindow();
    void updateExternalBarLayoutMode();
    
    static int onXError(Display* display, XErrorEvent* error);
    static int onWMDetected(Display* display, XErrorEvent* error);
    static bool wm_detected_;
};

} 
