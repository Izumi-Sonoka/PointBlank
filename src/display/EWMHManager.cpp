/**
 * @file EWMHManager.cpp
 * @brief Implementation of Extended Window Manager Hints (EWMH) Manager
 * 
 * @author Point Blank Systems Engineering Team
 * @version 1.0.0
 */

#include "pointblank/display/EWMHManager.hpp"
#include <X11/Xutil.h>
#include <cstring>
#include <algorithm>
#include <iostream>

namespace pblank {
namespace ewmh {





EWMHManager::EWMHManager(Display* display, Window root)
    : display_(display)
    , root_(root)
    , wm_check_window_(None)
    , num_desktops_(1)
    , current_desktop_(0)
    , showing_desktop_(false)
{
    initAtoms();
}

EWMHManager::~EWMHManager() {
    if (wm_check_window_ != None) {
        XDestroyWindow(display_, wm_check_window_);
    }
}





bool EWMHManager::initialize(const char* wm_name) {
    
    createWMCheckWindow(wm_name);
    
    
    setSupportedHints();
    
    
    setNumberOfDesktops(num_desktops_);
    
    
    setCurrentDesktop(0);
    
    
    int screen = DefaultScreen(display_);
    unsigned long geometry[2] = {
        static_cast<unsigned long>(DisplayWidth(display_, screen)),
        static_cast<unsigned long>(DisplayHeight(display_, screen))
    };
    XChangeProperty(display_, root_, atoms_.NET_DESKTOP_GEOMETRY,
                   XA_CARDINAL, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(geometry), 2);
    
    
    unsigned long viewport[2] = {0, 0};
    XChangeProperty(display_, root_, atoms_.NET_DESKTOP_VIEWPORT,
                   XA_CARDINAL, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(viewport), 2);
    
    
    setShowingDesktop(false);
    
    std::cout << "[EWMH] Initialized with WM name: " << wm_name << std::endl;
    return true;
}

void EWMHManager::initAtoms() {
    
    atoms_.NET_SUPPORTED = XInternAtom(display_, "_NET_SUPPORTED", False);
    atoms_.NET_SUPPORTING_WM_CHECK = XInternAtom(display_, "_NET_SUPPORTING_WM_CHECK", False);
    atoms_.NET_NUMBER_OF_DESKTOPS = XInternAtom(display_, "_NET_NUMBER_OF_DESKTOPS", False);
    atoms_.NET_CURRENT_DESKTOP = XInternAtom(display_, "_NET_CURRENT_DESKTOP", False);
    atoms_.NET_DESKTOP_NAMES = XInternAtom(display_, "_NET_DESKTOP_NAMES", False);
    atoms_.NET_DESKTOP_GEOMETRY = XInternAtom(display_, "_NET_DESKTOP_GEOMETRY", False);
    atoms_.NET_DESKTOP_VIEWPORT = XInternAtom(display_, "_NET_DESKTOP_VIEWPORT", False);
    atoms_.NET_WORKAREA = XInternAtom(display_, "_NET_WORKAREA", False);
    atoms_.NET_ACTIVE_WINDOW = XInternAtom(display_, "_NET_ACTIVE_WINDOW", False);
    atoms_.NET_CLIENT_LIST = XInternAtom(display_, "_NET_CLIENT_LIST", False);
    atoms_.NET_CLIENT_LIST_STACKING = XInternAtom(display_, "_NET_CLIENT_LIST_STACKING", False);
    atoms_.NET_SHOWING_DESKTOP = XInternAtom(display_, "_NET_SHOWING_DESKTOP", False);
    
    
    atoms_.NET_WM_NAME = XInternAtom(display_, "_NET_WM_NAME", False);
    atoms_.NET_WM_VISIBLE_NAME = XInternAtom(display_, "_NET_WM_VISIBLE_NAME", False);
    atoms_.NET_WM_ICON_NAME = XInternAtom(display_, "_NET_WM_ICON_NAME", False);
    atoms_.NET_WM_DESKTOP = XInternAtom(display_, "_NET_WM_DESKTOP", False);
    atoms_.NET_WM_WINDOW_TYPE = XInternAtom(display_, "_NET_WM_WINDOW_TYPE", False);
    atoms_.NET_WM_STATE = XInternAtom(display_, "_NET_WM_STATE", False);
    atoms_.NET_WM_ALLOWED_ACTIONS = XInternAtom(display_, "_NET_WM_ALLOWED_ACTIONS", False);
    atoms_.NET_WM_STRUT = XInternAtom(display_, "_NET_WM_STRUT", False);
    atoms_.NET_WM_STRUT_PARTIAL = XInternAtom(display_, "_NET_WM_STRUT_PARTIAL", False);
    atoms_.NET_WM_ICON_GEOMETRY = XInternAtom(display_, "_NET_WM_ICON_GEOMETRY", False);
    atoms_.NET_WM_ICON = XInternAtom(display_, "_NET_WM_ICON", False);
    atoms_.NET_WM_PID = XInternAtom(display_, "_NET_WM_PID", False);
    atoms_.NET_WM_HANDLED_ICONS = XInternAtom(display_, "_NET_WM_HANDLED_ICONS", False);
    atoms_.NET_WM_USER_TIME = XInternAtom(display_, "_NET_WM_USER_TIME", False);
    atoms_.NET_WM_USER_TIME_WINDOW = XInternAtom(display_, "_NET_WM_USER_TIME_WINDOW", False);
    atoms_.NET_WM_OPAQUE_REGION = XInternAtom(display_, "_NET_WM_OPAQUE_REGION", False);
    atoms_.NET_WM_BYPASS_COMPOSITOR = XInternAtom(display_, "_NET_WM_BYPASS_COMPOSITOR", False);
    
    
    atoms_.NET_WM_WINDOW_TYPE_NORMAL = XInternAtom(display_, "_NET_WM_WINDOW_TYPE_NORMAL", False);
    atoms_.NET_WM_WINDOW_TYPE_DIALOG = XInternAtom(display_, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    atoms_.NET_WM_WINDOW_TYPE_UTILITY = XInternAtom(display_, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    atoms_.NET_WM_WINDOW_TYPE_TOOLBAR = XInternAtom(display_, "_NET_WM_WINDOW_TYPE_TOOLBAR", False);
    atoms_.NET_WM_WINDOW_TYPE_SPLASH = XInternAtom(display_, "_NET_WM_WINDOW_TYPE_SPLASH", False);
    atoms_.NET_WM_WINDOW_TYPE_MENU = XInternAtom(display_, "_NET_WM_WINDOW_TYPE_MENU", False);
    atoms_.NET_WM_WINDOW_TYPE_DROPDOWN_MENU = XInternAtom(display_, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", False);
    atoms_.NET_WM_WINDOW_TYPE_POPUP_MENU = XInternAtom(display_, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
    atoms_.NET_WM_WINDOW_TYPE_TOOLTIP = XInternAtom(display_, "_NET_WM_WINDOW_TYPE_TOOLTIP", False);
    atoms_.NET_WM_WINDOW_TYPE_NOTIFICATION = XInternAtom(display_, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
    atoms_.NET_WM_WINDOW_TYPE_DOCK = XInternAtom(display_, "_NET_WM_WINDOW_TYPE_DOCK", False);
    atoms_.NET_WM_WINDOW_TYPE_DESKTOP = XInternAtom(display_, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    
    
    atoms_.NET_WM_STATE_MODAL = XInternAtom(display_, "_NET_WM_STATE_MODAL", False);
    atoms_.NET_WM_STATE_STICKY = XInternAtom(display_, "_NET_WM_STATE_STICKY", False);
    atoms_.NET_WM_STATE_MAXIMIZED_VERT = XInternAtom(display_, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    atoms_.NET_WM_STATE_MAXIMIZED_HORZ = XInternAtom(display_, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    atoms_.NET_WM_STATE_SHADED = XInternAtom(display_, "_NET_WM_STATE_SHADED", False);
    atoms_.NET_WM_STATE_SKIP_TASKBAR = XInternAtom(display_, "_NET_WM_STATE_SKIP_TASKBAR", False);
    atoms_.NET_WM_STATE_SKIP_PAGER = XInternAtom(display_, "_NET_WM_STATE_SKIP_PAGER", False);
    atoms_.NET_WM_STATE_HIDDEN = XInternAtom(display_, "_NET_WM_STATE_HIDDEN", False);
    atoms_.NET_WM_STATE_FULLSCREEN = XInternAtom(display_, "_NET_WM_STATE_FULLSCREEN", False);
    atoms_.NET_WM_STATE_ABOVE = XInternAtom(display_, "_NET_WM_STATE_ABOVE", False);
    atoms_.NET_WM_STATE_BELOW = XInternAtom(display_, "_NET_WM_STATE_BELOW", False);
    atoms_.NET_WM_STATE_DEMANDS_ATTENTION = XInternAtom(display_, "_NET_WM_STATE_DEMANDS_ATTENTION", False);
    atoms_.NET_WM_STATE_FOCUSED = XInternAtom(display_, "_NET_WM_STATE_FOCUSED", False);
    
    
    atoms_.NET_WM_ACTION_MOVE = XInternAtom(display_, "_NET_WM_ACTION_MOVE", False);
    atoms_.NET_WM_ACTION_RESIZE = XInternAtom(display_, "_NET_WM_ACTION_RESIZE", False);
    atoms_.NET_WM_ACTION_MINIMIZE = XInternAtom(display_, "_NET_WM_ACTION_MINIMIZE", False);
    atoms_.NET_WM_ACTION_SHADE = XInternAtom(display_, "_NET_WM_ACTION_SHADE", False);
    atoms_.NET_WM_ACTION_STICK = XInternAtom(display_, "_NET_WM_ACTION_STICK", False);
    atoms_.NET_WM_ACTION_MAXIMIZE_HORZ = XInternAtom(display_, "_NET_WM_ACTION_MAXIMIZE_HORZ", False);
    atoms_.NET_WM_ACTION_MAXIMIZE_VERT = XInternAtom(display_, "_NET_WM_ACTION_MAXIMIZE_VERT", False);
    atoms_.NET_WM_ACTION_FULLSCREEN = XInternAtom(display_, "_NET_WM_ACTION_FULLSCREEN", False);
    atoms_.NET_WM_ACTION_CHANGE_DESKTOP = XInternAtom(display_, "_NET_WM_ACTION_CHANGE_DESKTOP", False);
    atoms_.NET_WM_ACTION_CLOSE = XInternAtom(display_, "_NET_WM_ACTION_CLOSE", False);
    atoms_.NET_WM_ACTION_ABOVE = XInternAtom(display_, "_NET_WM_ACTION_ABOVE", False);
    atoms_.NET_WM_ACTION_BELOW = XInternAtom(display_, "_NET_WM_ACTION_BELOW", False);
    
    
    atoms_.NET_CLOSE_WINDOW = XInternAtom(display_, "_NET_CLOSE_WINDOW", False);
    atoms_.NET_MOVERESIZE_WINDOW = XInternAtom(display_, "_NET_MOVERESIZE_WINDOW", False);
    atoms_.NET_WM_MOVERESIZE = XInternAtom(display_, "_NET_WM_MOVERESIZE", False);
    atoms_.NET_RESTACK_WINDOW = XInternAtom(display_, "_NET_RESTACK_WINDOW", False);
    atoms_.NET_REQUEST_FRAME_EXTENTS = XInternAtom(display_, "_NET_REQUEST_FRAME_EXTENTS", False);
    atoms_.NET_WM_FULLSCREEN_MONITORS = XInternAtom(display_, "_NET_WM_FULLSCREEN_MONITORS", False);
    
    
    atoms_.UTF8_STRING = XInternAtom(display_, "UTF8_STRING", False);
    atoms_.WM_PROTOCOLS = XInternAtom(display_, "WM_PROTOCOLS", False);
    atoms_.WM_DELETE_WINDOW = XInternAtom(display_, "WM_DELETE_WINDOW", False);
    atoms_.WM_STATE = XInternAtom(display_, "WM_STATE", False);
    atoms_.WM_TAKE_FOCUS = XInternAtom(display_, "WM_TAKE_FOCUS", False);
    
    
    atoms_.PB_CURRENT_WORKSPACE = XInternAtom(display_, "_PB_CURRENT_WORKSPACE", False);
    atoms_.PB_WORKSPACE_NAMES = XInternAtom(display_, "_PB_WORKSPACE_NAMES", False);
    atoms_.PB_OCCUPIED_WORKSPACES = XInternAtom(display_, "_PB_OCCUPIED_WORKSPACES", False);
    atoms_.PB_ACTIVE_WINDOW_TITLE = XInternAtom(display_, "_PB_ACTIVE_WINDOW_TITLE", False);
    atoms_.PB_ACTIVE_WINDOW_CLASS = XInternAtom(display_, "_PB_ACTIVE_WINDOW_CLASS", False);
    atoms_.PB_LAYOUT_MODE = XInternAtom(display_, "_PB_LAYOUT_MODE", False);
    atoms_.PB_WORKSPACE_WINDOW_COUNTS = XInternAtom(display_, "_PB_WORKSPACE_WINDOW_COUNTS", False);
}

void EWMHManager::createWMCheckWindow(const char* wm_name) {
    
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.event_mask = 0;
    
    wm_check_window_ = XCreateWindow(
        display_,
        root_,
        -1, -1, 1, 1,  
        0,             
        CopyFromParent, 
        InputOnly,     
        CopyFromParent, 
        CWOverrideRedirect | CWEventMask,
        &attrs
    );
    
    
    XChangeProperty(display_, wm_check_window_, atoms_.NET_WM_NAME,
                   atoms_.UTF8_STRING, 8, PropModeReplace,
                   reinterpret_cast<const unsigned char*>(wm_name),
                   static_cast<int>(strlen(wm_name)));
    
    
    XChangeProperty(display_, wm_check_window_, atoms_.NET_SUPPORTING_WM_CHECK,
                   XA_WINDOW, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(&wm_check_window_), 1);
    
    
    XChangeProperty(display_, root_, atoms_.NET_SUPPORTING_WM_CHECK,
                   XA_WINDOW, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(&wm_check_window_), 1);
}

void EWMHManager::setSupportedHints() {
    
    std::vector<Atom> supported;
    supported.reserve(64);  
    
    
    supported.push_back(atoms_.NET_SUPPORTED);
    supported.push_back(atoms_.NET_SUPPORTING_WM_CHECK);
    supported.push_back(atoms_.NET_NUMBER_OF_DESKTOPS);
    supported.push_back(atoms_.NET_CURRENT_DESKTOP);
    supported.push_back(atoms_.NET_DESKTOP_NAMES);
    supported.push_back(atoms_.NET_DESKTOP_GEOMETRY);
    supported.push_back(atoms_.NET_DESKTOP_VIEWPORT);
    supported.push_back(atoms_.NET_WORKAREA);
    supported.push_back(atoms_.NET_ACTIVE_WINDOW);
    supported.push_back(atoms_.NET_CLIENT_LIST);
    supported.push_back(atoms_.NET_CLIENT_LIST_STACKING);
    supported.push_back(atoms_.NET_SHOWING_DESKTOP);
    
    
    supported.push_back(atoms_.NET_WM_NAME);
    supported.push_back(atoms_.NET_WM_DESKTOP);
    supported.push_back(atoms_.NET_WM_WINDOW_TYPE);
    supported.push_back(atoms_.NET_WM_STATE);
    supported.push_back(atoms_.NET_WM_ALLOWED_ACTIONS);
    supported.push_back(atoms_.NET_WM_STRUT);
    supported.push_back(atoms_.NET_WM_STRUT_PARTIAL);
    supported.push_back(atoms_.NET_WM_PID);
    
    
    supported.push_back(atoms_.NET_WM_WINDOW_TYPE_NORMAL);
    supported.push_back(atoms_.NET_WM_WINDOW_TYPE_DIALOG);
    supported.push_back(atoms_.NET_WM_WINDOW_TYPE_UTILITY);
    supported.push_back(atoms_.NET_WM_WINDOW_TYPE_TOOLBAR);
    supported.push_back(atoms_.NET_WM_WINDOW_TYPE_SPLASH);
    supported.push_back(atoms_.NET_WM_WINDOW_TYPE_MENU);
    supported.push_back(atoms_.NET_WM_WINDOW_TYPE_DROPDOWN_MENU);
    supported.push_back(atoms_.NET_WM_WINDOW_TYPE_POPUP_MENU);
    supported.push_back(atoms_.NET_WM_WINDOW_TYPE_TOOLTIP);
    supported.push_back(atoms_.NET_WM_WINDOW_TYPE_NOTIFICATION);
    supported.push_back(atoms_.NET_WM_WINDOW_TYPE_DOCK);
    supported.push_back(atoms_.NET_WM_WINDOW_TYPE_DESKTOP);
    
    
    supported.push_back(atoms_.NET_WM_STATE_FULLSCREEN);
    supported.push_back(atoms_.NET_WM_STATE_MAXIMIZED_VERT);
    supported.push_back(atoms_.NET_WM_STATE_MAXIMIZED_HORZ);
    supported.push_back(atoms_.NET_WM_STATE_STICKY);
    supported.push_back(atoms_.NET_WM_STATE_HIDDEN);
    supported.push_back(atoms_.NET_WM_STATE_ABOVE);
    supported.push_back(atoms_.NET_WM_STATE_BELOW);
    supported.push_back(atoms_.NET_WM_STATE_DEMANDS_ATTENTION);
    supported.push_back(atoms_.NET_WM_STATE_FOCUSED);
    
    
    supported.push_back(atoms_.NET_WM_ACTION_MOVE);
    supported.push_back(atoms_.NET_WM_ACTION_RESIZE);
    supported.push_back(atoms_.NET_WM_ACTION_FULLSCREEN);
    supported.push_back(atoms_.NET_WM_ACTION_CLOSE);
    supported.push_back(atoms_.NET_WM_ACTION_CHANGE_DESKTOP);
    
    
    supported.push_back(atoms_.NET_CLOSE_WINDOW);
    supported.push_back(atoms_.NET_MOVERESIZE_WINDOW);
    supported.push_back(atoms_.NET_WM_MOVERESIZE);
    supported.push_back(atoms_.NET_RESTACK_WINDOW);
    supported.push_back(atoms_.NET_REQUEST_FRAME_EXTENTS);
    supported.push_back(atoms_.NET_WM_FULLSCREEN_MONITORS);
    
    XChangeProperty(display_, root_, atoms_.NET_SUPPORTED,
                   XA_ATOM, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(supported.data()),
                   static_cast<int>(supported.size()));
}





void EWMHManager::setNumberOfDesktops(int count) {
    num_desktops_ = count;
    
    unsigned long value = static_cast<unsigned long>(count);
    XChangeProperty(display_, root_, atoms_.NET_NUMBER_OF_DESKTOPS,
                   XA_CARDINAL, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(&value), 1);
}

void EWMHManager::setCurrentDesktop(int index) {
    current_desktop_ = index;
    
    unsigned long value = static_cast<unsigned long>(index);
    XChangeProperty(display_, root_, atoms_.NET_CURRENT_DESKTOP,
                   XA_CARDINAL, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(&value), 1);
}

void EWMHManager::setDesktopNames(const std::vector<std::string>& names) {
    desktop_names_ = names;
    
    
    std::string combined;
    for (size_t i = 0; i < names.size(); ++i) {
        combined += names[i];
        combined += '\0';
    }
    
    XChangeProperty(display_, root_, atoms_.NET_DESKTOP_NAMES,
                   atoms_.UTF8_STRING, 8, PropModeReplace,
                   reinterpret_cast<unsigned char*>(const_cast<char*>(combined.c_str())),
                   static_cast<int>(combined.size()));
}

void EWMHManager::updateWorkarea(int screen_width, int screen_height,
                                  unsigned long left, unsigned long right,
                                  unsigned long top, unsigned long bottom) {
    
    std::vector<unsigned long> workarea;
    workarea.reserve(num_desktops_ * 4);
    
    for (int i = 0; i < num_desktops_; ++i) {
        workarea.push_back(left);                              
        workarea.push_back(top);                               
        workarea.push_back(screen_width - left - right);       
        workarea.push_back(screen_height - top - bottom);      
    }
    
    XChangeProperty(display_, root_, atoms_.NET_WORKAREA,
                   XA_CARDINAL, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(workarea.data()),
                   static_cast<int>(workarea.size()));
}

void EWMHManager::setShowingDesktop(bool showing) {
    showing_desktop_ = showing;
    
    unsigned long value = showing ? 1 : 0;
    XChangeProperty(display_, root_, atoms_.NET_SHOWING_DESKTOP,
                   XA_CARDINAL, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(&value), 1);
}





void EWMHManager::setClientList(const std::vector<Window>& windows) {
    client_list_ = windows;
    
    if (windows.empty()) {
        XDeleteProperty(display_, root_, atoms_.NET_CLIENT_LIST);
        return;
    }
    
    XChangeProperty(display_, root_, atoms_.NET_CLIENT_LIST,
                   XA_WINDOW, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(const_cast<Window*>(windows.data())),
                   static_cast<int>(windows.size()));
}

void EWMHManager::setClientListStacking(const std::vector<Window>& windows) {
    if (windows.empty()) {
        XDeleteProperty(display_, root_, atoms_.NET_CLIENT_LIST_STACKING);
        return;
    }
    
    XChangeProperty(display_, root_, atoms_.NET_CLIENT_LIST_STACKING,
                   XA_WINDOW, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(const_cast<Window*>(windows.data())),
                   static_cast<int>(windows.size()));
}

void EWMHManager::setActiveWindow(Window window) {
    if (window == None) {
        XDeleteProperty(display_, root_, atoms_.NET_ACTIVE_WINDOW);
        return;
    }
    
    XChangeProperty(display_, root_, atoms_.NET_ACTIVE_WINDOW,
                   XA_WINDOW, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(&window), 1);
}





void EWMHManager::setWindowDesktop(Window window, unsigned long desktop) {
    XChangeProperty(display_, window, atoms_.NET_WM_DESKTOP,
                   XA_CARDINAL, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(&desktop), 1);
}

unsigned long EWMHManager::getWindowDesktop(Window window) {
    return getCardinalProperty(window, atoms_.NET_WM_DESKTOP);
}

void EWMHManager::setWindowState(Window window, const std::vector<Atom>& states) {
    if (states.empty()) {
        XDeleteProperty(display_, window, atoms_.NET_WM_STATE);
        return;
    }
    
    XChangeProperty(display_, window, atoms_.NET_WM_STATE,
                   XA_ATOM, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(const_cast<Atom*>(states.data())),
                   static_cast<int>(states.size()));
}

void EWMHManager::addWindowState(Window window, Atom state) {
    auto states = getWindowState(window);
    
    
    if (std::find(states.begin(), states.end(), state) != states.end()) {
        return;
    }
    
    states.push_back(state);
    setWindowState(window, states);
}

void EWMHManager::removeWindowState(Window window, Atom state) {
    auto states = getWindowState(window);
    
    auto it = std::find(states.begin(), states.end(), state);
    if (it != states.end()) {
        states.erase(it);
        setWindowState(window, states);
    }
}

std::vector<Atom> EWMHManager::getWindowState(Window window) {
    return getAtomVectorProperty(window, atoms_.NET_WM_STATE);
}

bool EWMHManager::hasWindowState(Window window, Atom state) {
    auto states = getWindowState(window);
    return std::find(states.begin(), states.end(), state) != states.end();
}

void EWMHManager::setWindowType(Window window, Atom type) {
    XChangeProperty(display_, window, atoms_.NET_WM_WINDOW_TYPE,
                   XA_ATOM, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(&type), 1);
}

WindowType EWMHManager::getWindowType(Window window) {
    auto types = getAtomVectorProperty(window, atoms_.NET_WM_WINDOW_TYPE);
    
    if (types.empty()) {
        return WindowType::Normal;
    }
    
    Atom type = types[0];
    
    if (type == atoms_.NET_WM_WINDOW_TYPE_NORMAL) return WindowType::Normal;
    if (type == atoms_.NET_WM_WINDOW_TYPE_DIALOG) return WindowType::Dialog;
    if (type == atoms_.NET_WM_WINDOW_TYPE_UTILITY) return WindowType::Utility;
    if (type == atoms_.NET_WM_WINDOW_TYPE_TOOLBAR) return WindowType::Toolbar;
    if (type == atoms_.NET_WM_WINDOW_TYPE_SPLASH) return WindowType::Splash;
    if (type == atoms_.NET_WM_WINDOW_TYPE_MENU) return WindowType::Menu;
    if (type == atoms_.NET_WM_WINDOW_TYPE_DROPDOWN_MENU) return WindowType::DropdownMenu;
    if (type == atoms_.NET_WM_WINDOW_TYPE_POPUP_MENU) return WindowType::PopupMenu;
    if (type == atoms_.NET_WM_WINDOW_TYPE_TOOLTIP) return WindowType::Tooltip;
    if (type == atoms_.NET_WM_WINDOW_TYPE_NOTIFICATION) return WindowType::Notification;
    if (type == atoms_.NET_WM_WINDOW_TYPE_DOCK) return WindowType::Dock;
    if (type == atoms_.NET_WM_WINDOW_TYPE_DESKTOP) return WindowType::Desktop;
    
    return WindowType::Unknown;
}

void EWMHManager::setWindowAllowedActions(Window window, const std::vector<Atom>& actions) {
    if (actions.empty()) {
        XDeleteProperty(display_, window, atoms_.NET_WM_ALLOWED_ACTIONS);
        return;
    }
    
    XChangeProperty(display_, window, atoms_.NET_WM_ALLOWED_ACTIONS,
                   XA_ATOM, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(const_cast<Atom*>(actions.data())),
                   static_cast<int>(actions.size()));
}

void EWMHManager::setWindowPID(Window window, pid_t pid) {
    unsigned long value = static_cast<unsigned long>(pid);
    XChangeProperty(display_, window, atoms_.NET_WM_PID,
                   XA_CARDINAL, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(&value), 1);
}

pid_t EWMHManager::getWindowPID(Window window) {
    return static_cast<pid_t>(getCardinalProperty(window, atoms_.NET_WM_PID));
}

std::string EWMHManager::getWindowTitle(Window window) {
    return getTextProperty(window, atoms_.NET_WM_NAME);
}





bool EWMHManager::handleClientMessage(const XClientMessageEvent& event) {
    
    if (event.message_type == atoms_.NET_CURRENT_DESKTOP) {
        return handleCurrentDesktop(event);
    }
    
    
    if (event.message_type == atoms_.NET_ACTIVE_WINDOW) {
        return handleActiveWindow(event);
    }
    
    
    if (event.message_type == atoms_.NET_CLOSE_WINDOW) {
        return handleCloseWindow(event);
    }
    
    
    if (event.message_type == atoms_.NET_WM_DESKTOP) {
        return handleWmDesktop(event);
    }
    
    
    if (event.message_type == atoms_.NET_WM_STATE) {
        return handleWmState(event);
    }
    
    
    if (event.message_type == atoms_.NET_WM_MOVERESIZE) {
        return handleWmMoveResize(event);
    }
    
    
    if (event.message_type == atoms_.NET_RESTACK_WINDOW) {
        return handleRestackWindow(event);
    }
    
    
    if (event.message_type == atoms_.NET_SHOWING_DESKTOP) {
        bool show = event.data.l[0] != 0;
        setShowingDesktop(show);
        return true;
    }
    
    
    if (event.message_type == atoms_.NET_REQUEST_FRAME_EXTENTS) {
        return handleRequestFrameExtents(event);
    }
    
    return false;  
}

bool EWMHManager::handleCurrentDesktop(const XClientMessageEvent& event) {
    int desktop = static_cast<int>(event.data.l[0]);
    
    
    if (desktop < 0 || desktop >= num_desktops_) {
        return false;
    }
    
    
    if (desktop_switch_callback_) {
        desktop_switch_callback_(desktop);
        return true;
    }
    
    return false;
}

bool EWMHManager::handleActiveWindow(const XClientMessageEvent& event) {
    Window window = event.window;
    
    
    
    
    if (window_action_callback_) {
        window_action_callback_(window, atoms_.NET_ACTIVE_WINDOW);
        return true;
    }
    
    return false;
}

bool EWMHManager::handleCloseWindow(const XClientMessageEvent& event) {
    Window window = event.window;
    
    
    XEvent msg;
    memset(&msg, 0, sizeof(msg));
    msg.xclient.type = ClientMessage;
    msg.xclient.window = window;
    msg.xclient.message_type = atoms_.WM_PROTOCOLS;
    msg.xclient.format = 32;
    msg.xclient.data.l[0] = static_cast<long>(atoms_.WM_DELETE_WINDOW);
    msg.xclient.data.l[1] = CurrentTime;
    
    XSendEvent(display_, window, False, 0, &msg);
    
    return true;
}

bool EWMHManager::handleRequestFrameExtents(const XClientMessageEvent& event) {
    
    
    
    
    
    
    
    
    
    
    
    
    (void)event;  
    return true;
}

bool EWMHManager::handleWmDesktop(const XClientMessageEvent& event) {
    Window window = event.window;
    unsigned long desktop = static_cast<unsigned long>(event.data.l[0]);
    
    
    if (desktop != 0xFFFFFFFF && 
        static_cast<int>(desktop) >= num_desktops_) {
        return false;
    }
    
    setWindowDesktop(window, desktop);
    
    if (window_action_callback_) {
        window_action_callback_(window, atoms_.NET_WM_DESKTOP);
    }
    
    return true;
}

bool EWMHManager::handleWmState(const XClientMessageEvent& event) {
    Window window = event.window;
    long action = event.data.l[0];
    Atom state1 = static_cast<Atom>(event.data.l[1]);
    Atom state2 = static_cast<Atom>(event.data.l[2]);
    
    
    auto processState = [this, window, action](Atom state) {
        if (state == 0) return;
        
        bool has_state = hasWindowState(window, state);
        
        switch (action) {
            case 0:  
                if (has_state) {
                    removeWindowState(window, state);
                    if (window_action_callback_) {
                        window_action_callback_(window, state);
                    }
                }
                break;
                
            case 1:  
                if (!has_state) {
                    addWindowState(window, state);
                    if (window_action_callback_) {
                        window_action_callback_(window, state);
                    }
                }
                break;
                
            case 2:  
                if (has_state) {
                    removeWindowState(window, state);
                } else {
                    addWindowState(window, state);
                }
                if (window_action_callback_) {
                    window_action_callback_(window, state);
                }
                break;
        }
    };
    
    processState(state1);
    processState(state2);
    
    return true;
}

bool EWMHManager::handleWmMoveResize(const XClientMessageEvent& event) {
    Window window = event.window;
    int x_root = static_cast<int>(event.data.l[0]);
    int y_root = static_cast<int>(event.data.l[1]);
    int direction = static_cast<int>(event.data.l[2]);
    int button = static_cast<int>(event.data.l[3]);
    
    
    if (window_move_callback_) {
        window_move_callback_(window, x_root, y_root, direction, button);
        return true;
    }
    
    return false;
}

bool EWMHManager::handleRestackWindow(const XClientMessageEvent& event) {
    Window window = event.window;
    
    Window sibling = static_cast<Window>(event.data.l[1]);
    int detail = static_cast<int>(event.data.l[2]);
    
    
    XWindowChanges changes;
    changes.sibling = sibling;
    changes.stack_mode = detail;
    
    XConfigureWindow(display_, window, CWSibling | CWStackMode, &changes);
    
    return true;
}





void EWMHManager::setDesktopSwitchCallback(DesktopSwitchCallback callback) {
    desktop_switch_callback_ = std::move(callback);
}

void EWMHManager::setWindowActionCallback(WindowActionCallback callback) {
    window_action_callback_ = std::move(callback);
}

void EWMHManager::setWindowMoveCallback(WindowMoveCallback callback) {
    window_move_callback_ = std::move(callback);
}





bool EWMHManager::isFloatingType(WindowType type) {
    switch (type) {
        case WindowType::Dialog:
        case WindowType::Utility:
        case WindowType::Toolbar:
        case WindowType::Splash:
        case WindowType::Menu:
        case WindowType::DropdownMenu:
        case WindowType::PopupMenu:
        case WindowType::Tooltip:
        case WindowType::Notification:
        case WindowType::Dock:
        case WindowType::Desktop:
            return true;
        default:
            return false;
    }
}

bool EWMHManager::isTiledType(WindowType type) {
    return type == WindowType::Normal;
}





std::string EWMHManager::getTextProperty(Window window, Atom property) {
    XTextProperty prop;
    if (!XGetTextProperty(display_, window, &prop, property)) {
        return "";
    }
    
    std::string result;
    if (prop.value) {
        result = reinterpret_cast<char*>(prop.value);
        XFree(prop.value);
    }
    
    return result;
}

void EWMHManager::setTextProperty(Window window, Atom property, 
                                   const std::string& value) {
    XChangeProperty(display_, window, property,
                   atoms_.UTF8_STRING, 8, PropModeReplace,
                   reinterpret_cast<const unsigned char*>(value.c_str()),
                   static_cast<int>(value.size()));
}

unsigned long EWMHManager::getCardinalProperty(Window window, Atom property) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char* prop = nullptr;
    
    if (XGetWindowProperty(display_, window, property, 0, 1, False,
                          XA_CARDINAL, &actual_type, &actual_format,
                          &nitems, &bytes_after, &prop) != Success) {
        return 0;
    }
    
    unsigned long value = 0;
    if (prop && nitems > 0) {
        value = *reinterpret_cast<unsigned long*>(prop);
        XFree(prop);
    }
    
    return value;
}

void EWMHManager::setCardinalProperty(Window window, Atom property, 
                                       unsigned long value) {
    XChangeProperty(display_, window, property,
                   XA_CARDINAL, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(&value), 1);
}

std::vector<Atom> EWMHManager::getAtomVectorProperty(Window window, Atom property) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char* prop = nullptr;
    
    std::vector<Atom> result;
    
    if (XGetWindowProperty(display_, window, property, 0, 1024, False,
                          XA_ATOM, &actual_type, &actual_format,
                          &nitems, &bytes_after, &prop) != Success) {
        return result;
    }
    
    if (prop && nitems > 0) {
        Atom* atoms = reinterpret_cast<Atom*>(prop);
        result.assign(atoms, atoms + nitems);
        XFree(prop);
    }
    
    return result;
}

void EWMHManager::setAtomVectorProperty(Window window, Atom property,
                                         const std::vector<Atom>& values) {
    if (values.empty()) {
        XDeleteProperty(display_, window, property);
        return;
    }
    
    XChangeProperty(display_, window, property,
                   XA_ATOM, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(const_cast<Atom*>(values.data())),
                   static_cast<int>(values.size()));
}





EWMHManager::StrutPartial EWMHManager::getStrutPartial(Window window) {
    StrutPartial strut;
    
    
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char* prop = nullptr;
    
    if (XGetWindowProperty(display_, window, atoms_.NET_WM_STRUT_PARTIAL, 0, 12, False,
                          XA_CARDINAL, &actual_type, &actual_format,
                          &nitems, &bytes_after, &prop) == Success && prop && nitems >= 12) {
        unsigned long* values = reinterpret_cast<unsigned long*>(prop);
        strut.left = values[0];
        strut.right = values[1];
        strut.top = values[2];
        strut.bottom = values[3];
        strut.left_start_y = values[4];
        strut.left_end_y = values[5];
        strut.right_start_y = values[6];
        strut.right_end_y = values[7];
        strut.top_start_x = values[8];
        strut.top_end_x = values[9];
        strut.bottom_start_x = values[10];
        strut.bottom_end_x = values[11];
        XFree(prop);
        return strut;
    }
    
    if (prop) XFree(prop);
    
    
    if (XGetWindowProperty(display_, window, atoms_.NET_WM_STRUT, 0, 4, False,
                          XA_CARDINAL, &actual_type, &actual_format,
                          &nitems, &bytes_after, &prop) == Success && prop && nitems >= 4) {
        unsigned long* values = reinterpret_cast<unsigned long*>(prop);
        strut.left = values[0];
        strut.right = values[1];
        strut.top = values[2];
        strut.bottom = values[3];
        XFree(prop);
    }
    
    if (prop) XFree(prop);
    
    return strut;
}

EWMHManager::StrutPartial EWMHManager::getCombinedStruts(int screen_width, int screen_height) {
    StrutPartial combined;
    
    
    for (Window dock : dock_windows_) {
        StrutPartial strut = getStrutPartial(dock);
        
        
        
        
        
        if (strut.left > 0) {
            if (strut.left_start_y == 0 && strut.left_end_y >= static_cast<unsigned long>(screen_height - 1)) {
                combined.left = std::max(combined.left, strut.left);
            } else if (strut.left_start_y == 0 && strut.left_end_y == 0) {
                
                combined.left = std::max(combined.left, strut.left);
            }
        }
        
        
        if (strut.right > 0) {
            if (strut.right_start_y == 0 && strut.right_end_y >= static_cast<unsigned long>(screen_height - 1)) {
                combined.right = std::max(combined.right, strut.right);
            } else if (strut.right_start_y == 0 && strut.right_end_y == 0) {
                combined.right = std::max(combined.right, strut.right);
            }
        }
        
        
        if (strut.top > 0) {
            if (strut.top_start_x == 0 && strut.top_end_x >= static_cast<unsigned long>(screen_width - 1)) {
                combined.top = std::max(combined.top, strut.top);
            } else if (strut.top_start_x == 0 && strut.top_end_x == 0) {
                
                combined.top = std::max(combined.top, strut.top);
            }
        }
        
        
        if (strut.bottom > 0) {
            if (strut.bottom_start_x == 0 && strut.bottom_end_x >= static_cast<unsigned long>(screen_width - 1)) {
                combined.bottom = std::max(combined.bottom, strut.bottom);
            } else if (strut.bottom_start_x == 0 && strut.bottom_end_x == 0) {
                combined.bottom = std::max(combined.bottom, strut.bottom);
            }
        }
    }
    
    std::cout << "[EWMH] Combined struts: left=" << combined.left 
              << " right=" << combined.right 
              << " top=" << combined.top 
              << " bottom=" << combined.bottom << std::endl;
    
    return combined;
}

void EWMHManager::registerDockWindow(Window window) {
    
    if (std::find(dock_windows_.begin(), dock_windows_.end(), window) != dock_windows_.end()) {
        return;
    }
    
    dock_windows_.push_back(window);
    std::cout << "[EWMH] Registered dock window: " << window << std::endl;
}

void EWMHManager::unregisterDockWindow(Window window) {
    auto it = std::find(dock_windows_.begin(), dock_windows_.end(), window);
    if (it != dock_windows_.end()) {
        dock_windows_.erase(it);
        std::cout << "[EWMH] Unregistered dock window: " << window << std::endl;
    }
}





void EWMHManager::setCurrentWorkspacePB(int workspace) {
    XChangeProperty(display_, root_, atoms_.PB_CURRENT_WORKSPACE,
                   XA_CARDINAL, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(&workspace), 1);
}

void EWMHManager::setWorkspaceNamesPB(const std::vector<std::string>& names) {
    
    std::string data;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) data += '\0';
        data += names[i];
    }
    
    XChangeProperty(display_, root_, atoms_.PB_WORKSPACE_NAMES,
                   atoms_.UTF8_STRING, 8, PropModeReplace,
                   reinterpret_cast<unsigned char*>(const_cast<char*>(data.c_str())),
                   static_cast<int>(data.size()));
}

void EWMHManager::setOccupiedWorkspacesPB(const std::vector<int>& workspaces) {
    if (workspaces.empty()) {
        XDeleteProperty(display_, root_, atoms_.PB_OCCUPIED_WORKSPACES);
        return;
    }
    
    std::vector<unsigned long> data(workspaces.size());
    for (size_t i = 0; i < workspaces.size(); ++i) {
        data[i] = static_cast<unsigned long>(workspaces[i]);
    }
    
    XChangeProperty(display_, root_, atoms_.PB_OCCUPIED_WORKSPACES,
                   XA_CARDINAL, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(data.data()),
                   static_cast<int>(data.size()));
}

void EWMHManager::setActiveWindowTitlePB(const std::string& title) {
    XChangeProperty(display_, root_, atoms_.PB_ACTIVE_WINDOW_TITLE,
                   atoms_.UTF8_STRING, 8, PropModeReplace,
                   reinterpret_cast<unsigned char*>(const_cast<char*>(title.c_str())),
                   static_cast<int>(title.size()));
}

void EWMHManager::setActiveWindowClassPB(const std::string& window_class) {
    XChangeProperty(display_, root_, atoms_.PB_ACTIVE_WINDOW_CLASS,
                   atoms_.UTF8_STRING, 8, PropModeReplace,
                   reinterpret_cast<unsigned char*>(const_cast<char*>(window_class.c_str())),
                   static_cast<int>(window_class.size()));
}

void EWMHManager::setLayoutModePB(const std::string& mode) {
    XChangeProperty(display_, root_, atoms_.PB_LAYOUT_MODE,
                   atoms_.UTF8_STRING, 8, PropModeReplace,
                   reinterpret_cast<unsigned char*>(const_cast<char*>(mode.c_str())),
                   static_cast<int>(mode.size()));
}

void EWMHManager::setWorkspaceWindowCountsPB(const std::vector<int>& counts) {
    if (counts.empty()) {
        XDeleteProperty(display_, root_, atoms_.PB_WORKSPACE_WINDOW_COUNTS);
        return;
    }
    
    std::vector<unsigned long> data(counts.size());
    for (size_t i = 0; i < counts.size(); ++i) {
        data[i] = static_cast<unsigned long>(counts[i]);
    }
    
    XChangeProperty(display_, root_, atoms_.PB_WORKSPACE_WINDOW_COUNTS,
                   XA_CARDINAL, 32, PropModeReplace,
                   reinterpret_cast<unsigned char*>(data.data()),
                   static_cast<int>(data.size()));
}

} 
} 