#pragma once

/**
 * @file EWMHManager.hpp
 * @brief Extended Window Manager Hints (EWMH) Manager
 * 
 * This file implements full EWMH compliance for the Pointblank window manager.
 * EWMH is defined by freedesktop.org and is essential for proper desktop
 * integration with panels, taskbars, pagers, and other desktop components.
 * 
 * @author Point Blank Systems Engineering Team
 * @version 1.0.0
 */

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace pblank {
namespace ewmh {

struct Atoms {
    
    Atom NET_SUPPORTED;
    Atom NET_SUPPORTING_WM_CHECK;
    Atom NET_NUMBER_OF_DESKTOPS;
    Atom NET_CURRENT_DESKTOP;
    Atom NET_DESKTOP_NAMES;
    Atom NET_DESKTOP_GEOMETRY;
    Atom NET_DESKTOP_VIEWPORT;
    Atom NET_WORKAREA;
    Atom NET_ACTIVE_WINDOW;
    Atom NET_CLIENT_LIST;
    Atom NET_CLIENT_LIST_STACKING;
    Atom NET_SHOWING_DESKTOP;
    
    Atom NET_WM_NAME;
    Atom NET_WM_VISIBLE_NAME;
    Atom NET_WM_ICON_NAME;
    Atom NET_WM_DESKTOP;
    Atom NET_WM_WINDOW_TYPE;
    Atom NET_WM_STATE;
    Atom NET_WM_ALLOWED_ACTIONS;
    Atom NET_WM_STRUT;
    Atom NET_WM_STRUT_PARTIAL;
    Atom NET_WM_ICON_GEOMETRY;
    Atom NET_WM_ICON;
    Atom NET_WM_PID;
    Atom NET_WM_HANDLED_ICONS;
    Atom NET_WM_USER_TIME;
    Atom NET_WM_USER_TIME_WINDOW;
    Atom NET_WM_OPAQUE_REGION;
    Atom NET_WM_BYPASS_COMPOSITOR;
    
    Atom NET_WM_WINDOW_TYPE_NORMAL;
    Atom NET_WM_WINDOW_TYPE_DIALOG;
    Atom NET_WM_WINDOW_TYPE_UTILITY;
    Atom NET_WM_WINDOW_TYPE_TOOLBAR;
    Atom NET_WM_WINDOW_TYPE_SPLASH;
    Atom NET_WM_WINDOW_TYPE_MENU;
    Atom NET_WM_WINDOW_TYPE_DROPDOWN_MENU;
    Atom NET_WM_WINDOW_TYPE_POPUP_MENU;
    Atom NET_WM_WINDOW_TYPE_TOOLTIP;
    Atom NET_WM_WINDOW_TYPE_NOTIFICATION;
    Atom NET_WM_WINDOW_TYPE_DOCK;
    Atom NET_WM_WINDOW_TYPE_DESKTOP;
    
    Atom NET_WM_STATE_MODAL;
    Atom NET_WM_STATE_STICKY;
    Atom NET_WM_STATE_MAXIMIZED_VERT;
    Atom NET_WM_STATE_MAXIMIZED_HORZ;
    Atom NET_WM_STATE_SHADED;
    Atom NET_WM_STATE_SKIP_TASKBAR;
    Atom NET_WM_STATE_SKIP_PAGER;
    Atom NET_WM_STATE_HIDDEN;
    Atom NET_WM_STATE_FULLSCREEN;
    Atom NET_WM_STATE_ABOVE;
    Atom NET_WM_STATE_BELOW;
    Atom NET_WM_STATE_DEMANDS_ATTENTION;
    Atom NET_WM_STATE_FOCUSED;
    
    Atom NET_WM_ACTION_MOVE;
    Atom NET_WM_ACTION_RESIZE;
    Atom NET_WM_ACTION_MINIMIZE;
    Atom NET_WM_ACTION_SHADE;
    Atom NET_WM_ACTION_STICK;
    Atom NET_WM_ACTION_MAXIMIZE_HORZ;
    Atom NET_WM_ACTION_MAXIMIZE_VERT;
    Atom NET_WM_ACTION_FULLSCREEN;
    Atom NET_WM_ACTION_CHANGE_DESKTOP;
    Atom NET_WM_ACTION_CLOSE;
    Atom NET_WM_ACTION_ABOVE;
    Atom NET_WM_ACTION_BELOW;
    
    Atom NET_CLOSE_WINDOW;
    Atom NET_MOVERESIZE_WINDOW;
    Atom NET_WM_MOVERESIZE;
    Atom NET_RESTACK_WINDOW;
    Atom NET_REQUEST_FRAME_EXTENTS;
    Atom NET_WM_FULLSCREEN_MONITORS;
    
    Atom UTF8_STRING;
    Atom WM_PROTOCOLS;
    Atom WM_DELETE_WINDOW;
    Atom WM_STATE;
    Atom WM_TAKE_FOCUS;
    
    Atom PB_CURRENT_WORKSPACE;
    Atom PB_WORKSPACE_NAMES;
    Atom PB_OCCUPIED_WORKSPACES;
    Atom PB_ACTIVE_WINDOW_TITLE;
    Atom PB_ACTIVE_WINDOW_CLASS;
    Atom PB_LAYOUT_MODE;
    Atom PB_WORKSPACE_WINDOW_COUNTS;
};

enum class WindowType {
    Normal,
    Dialog,
    Utility,
    Toolbar,
    Splash,
    Menu,
    DropdownMenu,
    PopupMenu,
    Tooltip,
    Notification,
    Dock,
    Desktop,
    Unknown
};

enum class WindowState : uint32_t {
    NoState         = 0,
    Modal           = 1 << 0,
    Sticky          = 1 << 1,
    MaximizedVert   = 1 << 2,
    MaximizedHorz   = 1 << 3,
    Shaded          = 1 << 4,
    SkipTaskbar     = 1 << 5,
    SkipPager       = 1 << 6,
    Hidden          = 1 << 7,
    Fullscreen      = 1 << 8,
    AboveLayer      = 1 << 9,
    BelowLayer      = 1 << 10,
    DemandsAttention = 1 << 11,
    Focused         = 1 << 12
};

class EWMHManager {
public:
    
    EWMHManager(Display* display, Window root);
    
    ~EWMHManager();
    
    EWMHManager(const EWMHManager&) = delete;
    EWMHManager& operator=(const EWMHManager&) = delete;
    EWMHManager(EWMHManager&&) = delete;
    EWMHManager& operator=(EWMHManager&&) = delete;
    
    bool initialize(const char* wm_name);
    
    void setNumberOfDesktops(int count);
    
    void setCurrentDesktop(int index);
    
    void setDesktopNames(const std::vector<std::string>& names);
    
    void updateWorkarea(int screen_width, int screen_height,
                        unsigned long left = 0, unsigned long right = 0,
                        unsigned long top = 0, unsigned long bottom = 0);
    
    void setShowingDesktop(bool showing);
    
    void setClientList(const std::vector<Window>& windows);
    
    void setClientListStacking(const std::vector<Window>& windows);
    
    void setActiveWindow(Window window);
    
    void setWindowDesktop(Window window, unsigned long desktop);
    
    unsigned long getWindowDesktop(Window window);
    
    void setWindowState(Window window, const std::vector<Atom>& states);
    
    void addWindowState(Window window, Atom state);
    
    void removeWindowState(Window window, Atom state);
    
    std::vector<Atom> getWindowState(Window window);
    
    bool hasWindowState(Window window, Atom state);
    
    void setWindowType(Window window, Atom type);
    
    WindowType getWindowType(Window window);
    
    void setWindowAllowedActions(Window window, const std::vector<Atom>& actions);
    
    void setWindowPID(Window window, pid_t pid);
    
    pid_t getWindowPID(Window window);
    
    std::string getWindowTitle(Window window);
    
    bool handleClientMessage(const XClientMessageEvent& event);
    
    bool handleRequestFrameExtents(const XClientMessageEvent& event);
    
    using DesktopSwitchCallback = std::function<void(int)>;
    using WindowActionCallback = std::function<void(Window, Atom)>;
    using WindowMoveCallback = std::function<void(Window, int, int, int, int)>;
    
    void setDesktopSwitchCallback(DesktopSwitchCallback callback);
    void setWindowActionCallback(WindowActionCallback callback);
    void setWindowMoveCallback(WindowMoveCallback callback);
    
    inline const Atoms& getAtoms() const { return atoms_; }
    
    inline int getNumberOfDesktops() const { return num_desktops_; }
    inline int getCurrentDesktop() const { return current_desktop_; }
    inline Window getRootWindow() const { return root_; }
    
    static bool isFloatingType(WindowType type);
    
    static bool isTiledType(WindowType type);
    
    struct StrutPartial {
        unsigned long left{0};
        unsigned long right{0};
        unsigned long top{0};
        unsigned long bottom{0};
        unsigned long left_start_y{0};
        unsigned long left_end_y{0};
        unsigned long right_start_y{0};
        unsigned long right_end_y{0};
        unsigned long top_start_x{0};
        unsigned long top_end_x{0};
        unsigned long bottom_start_x{0};
        unsigned long bottom_end_x{0};
    };
    
    StrutPartial getStrutPartial(Window window);
    
    StrutPartial getCombinedStruts(int screen_width, int screen_height);
    
    void registerDockWindow(Window window);
    
    void unregisterDockWindow(Window window);
    
    inline const std::vector<Window>& getDockWindows() const { return dock_windows_; }

    void setCurrentWorkspacePB(int workspace);
    
    void setWorkspaceNamesPB(const std::vector<std::string>& names);
    
    void setOccupiedWorkspacesPB(const std::vector<int>& workspaces);
    
    void setActiveWindowTitlePB(const std::string& title);
    
    void setActiveWindowClassPB(const std::string& window_class);
    
    void setLayoutModePB(const std::string& mode);
    
    void setWorkspaceWindowCountsPB(const std::vector<int>& counts);

private:
    Display* display_;
    Window root_;
    Window wm_check_window_;  
    Atoms atoms_;
    
    int num_desktops_;
    int current_desktop_;
    bool showing_desktop_;
    
    DesktopSwitchCallback desktop_switch_callback_;
    WindowActionCallback window_action_callback_;
    WindowMoveCallback window_move_callback_;
    
    std::vector<Window> client_list_;
    std::vector<std::string> desktop_names_;
    
    std::vector<Window> dock_windows_;
    
    void initAtoms();
    
    void setSupportedHints();
    
    void createWMCheckWindow(const char* wm_name);
    
    bool handleCurrentDesktop(const XClientMessageEvent& event);
    
    bool handleActiveWindow(const XClientMessageEvent& event);
    
    bool handleCloseWindow(const XClientMessageEvent& event);
    
    bool handleWmDesktop(const XClientMessageEvent& event);
    
    bool handleWmState(const XClientMessageEvent& event);
    
    bool handleWmMoveResize(const XClientMessageEvent& event);
    
    bool handleRestackWindow(const XClientMessageEvent& event);
    
    std::string getTextProperty(Window window, Atom property);
    
    void setTextProperty(Window window, Atom property, const std::string& value);
    
    unsigned long getCardinalProperty(Window window, Atom property);
    
    void setCardinalProperty(Window window, Atom property, unsigned long value);
    
    std::vector<Atom> getAtomVectorProperty(Window window, Atom property);
    
    void setAtomVectorProperty(Window window, Atom property, 
                               const std::vector<Atom>& values);
};

} 
} 