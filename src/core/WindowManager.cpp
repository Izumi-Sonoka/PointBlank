#include "pointblank/core/WindowManager.hpp"
#include "pointblank/config/ConfigParser.hpp"
#include "pointblank/layout/LayoutEngine.hpp"
#include "pointblank/config/LayoutConfigParser.hpp"
#include "pointblank/core/Toaster.hpp"
#include "pointblank/window/KeybindManager.hpp"
#include "pointblank/config/ConfigWatcher.hpp"
#include "pointblank/display/EWMHManager.hpp"
#include "pointblank/display/MonitorManager.hpp"
#include "pointblank/core/SessionManager.hpp"
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <unistd.h>
#include <fstream>
#include <cerrno>
#include <sys/wait.h>

namespace pblank {

bool WindowManager::wm_detected_ = false;

WindowManager::WindowManager() = default;

WindowManager::~WindowManager() = default;

void WindowManager::setConfigPath(const std::filesystem::path& path) {
    custom_config_path_ = path;
}

bool WindowManager::initialize() {
    // Open X11 display
    Display* raw_display = XOpenDisplay(nullptr);
    if (!raw_display) {
        std::cerr << "Failed to open X display" << std::endl;
        return false;
    }
    
    display_ = DisplayPtr(raw_display, DisplayDeleter{});
    screen_ = DefaultScreen(display_.get());
    root_ = RootWindow(display_.get(), screen_);
    
    // Check if another window manager is running
    if (!becomeWindowManager()) {
        std::cerr << "Another window manager is already running" << std::endl;
        return false;
    }
    
    // Initialize EWMH manager for desktop integration
    ewmh_manager_ = std::make_unique<ewmh::EWMHManager>(display_.get(), root_);
    if (!ewmh_manager_->initialize("Pointblank")) {
        std::cerr << "Warning: EWMH initialization failed" << std::endl;
        // Non-fatal - continue without EWMH support
    } else {
        
        // Set up EWMH callbacks
        ewmh_manager_->setDesktopSwitchCallback([this](int desktop) {
            // Convert from 0-indexed to 1-indexed for switchWorkspace
            switchWorkspace(desktop + 1);
        });
        
        ewmh_manager_->setWindowActionCallback([this](Window window, Atom action) {
            // Handle window actions from EWMH
            if (action == ewmh_manager_->getAtoms().NET_CLOSE_WINDOW) {
                if (clients_.find(window) != clients_.end()) {
                    killActiveWindow();
                }
            }
        });
    }
    
    // Initialize workspace last focus tracking
    workspace_last_focus_.resize(max_workspaces_, None);
    
    // Initialize components
    toaster_ = std::make_unique<Toaster>(display_.get(), root_);
    if (!toaster_->initialize()) {
        std::cerr << "Failed to initialize Toaster OSD" << std::endl;
        return false;
    }
    
    config_parser_ = std::make_unique<ConfigParser>(toaster_.get());
    layout_engine_ = std::make_unique<LayoutEngine>();
    layout_engine_->setDisplay(display_.get());
    layout_config_parser_ = std::make_unique<LayoutConfigParser>(layout_engine_.get());
    keybind_manager_ = std::make_unique<KeybindManager>();
    monitor_manager_ = std::make_unique<MonitorManager>();
    if (!monitor_manager_->initialize(display_.get())) {
        std::cerr << "Failed to initialize MonitorManager - running without multi-monitor support" << std::endl;
    }
    
    // Load configuration with error handling
    auto config_path = ConfigParser::getDefaultConfigPath();
    
    if (!loadConfigSafe()) {
        std::cerr << "Config load failed!" << std::endl;
        toaster_->error("Configuration failed - using defaults");
        fallbackToDefaultConfig();
    } else {
        toaster_->success("Point Blank initialized");
        
        // Apply configuration to layout engine
        applyConfigToLayout();
        
        // Register keybinds from config
        const auto& config = config_parser_->getConfig();
        
        for (const auto& bind : config.keybinds) {
            std::string keybind_str;
            if (!bind.modifiers.empty()) {
                keybind_str = bind.modifiers + ", " + bind.key;
            } else {
                keybind_str = bind.key;
            }
            
            std::string action = bind.exec_command.has_value() ? 
                "exec: " + *bind.exec_command : bind.action;
            
            keybind_manager_->registerKeybind(keybind_str, action);
        }
        
        // Grab keys on root window
        keybind_manager_->grabKeys(display_.get(), root_);
        
        // Execute autostart commands
        if (!config.autostart.commands.empty()) {
            for (const auto& cmd : config.autostart.commands) {
                
                int result = fork();
                if (result == 0) {
                    // Child process - redirect stderr to stdout for debugging
                    dup2(STDOUT_FILENO, STDERR_FILENO);
                    
                    // Use execlp to search PATH
                    execlp("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
                    
                    // If exec fails
                    int err = errno;
                    std::cerr << "[AUTOSTART]   ERROR: execlp failed with errno " << err << ": " << strerror(err) << std::endl;
                    _exit(1);
                } else if (result > 0) {
                    // Parent process - don't wait, let it run in background
                } else {
                    std::cerr << "[AUTOSTART]   ERROR: fork() failed for: " << cmd << std::endl;
                }
            }
        }
        
        // Setup hot-reload watcher for configuration
        setupConfigWatcher();
    }
    
    // Setup window manager event mask
    setupEventMask();
    
    // Update EWMH with workspace count
    updateEWMHWorkspaceCount();
    
    // Sync X server to ensure all EWMH properties (like StatusBar's DOCK type) are visible
    XSync(display_.get(), False);
    
    // Manage existing windows
    scanExistingWindows();
    
    // Update EWMH client list after scanning existing windows
    updateEWMHClientList();
    
    return true;
}

bool WindowManager::becomeWindowManager() {
    // Set error handler to detect other WMs
    wm_detected_ = false;
    XSetErrorHandler(&WindowManager::onWMDetected);
    
    // Try to select SubstructureRedirect on root
    XSelectInput(display_.get(), root_,
                 SubstructureRedirectMask | SubstructureNotifyMask);
    XSync(display_.get(), False);
    
    if (wm_detected_) {
        return false;
    }
    
    // Set our error handler
    XSetErrorHandler(&WindowManager::onXError);
    return true;
}

void WindowManager::setupEventMask() {
    XSelectInput(display_.get(), root_,
                 SubstructureRedirectMask |
                 SubstructureNotifyMask |
                 StructureNotifyMask |
                 PropertyChangeMask |
                 KeyPressMask |
                 KeyReleaseMask |
                 EnterWindowMask |
                 LeaveWindowMask |
                 FocusChangeMask |
                 ButtonPressMask |
                 ButtonReleaseMask |
                 PointerMotionMask);
    
    // Set default left pointer cursor on root window to prevent X-shaped cursor
    // when no window is focused or during window transitions
    Cursor default_cursor = XCreateFontCursor(display_.get(), XC_left_ptr);
    XDefineCursor(display_.get(), root_, default_cursor);
    XFlush(display_.get());
}

void WindowManager::scanExistingWindows() {
    Window returned_root, returned_parent;
    Window* top_level_windows;
    unsigned int num_windows;
    
    if (!XQueryTree(display_.get(), root_, &returned_root, &returned_parent,
                    &top_level_windows, &num_windows)) {
        return;
    }
    
    for (unsigned int i = 0; i < num_windows; ++i) {
        XWindowAttributes attrs;
        if (XGetWindowAttributes(display_.get(), top_level_windows[i], &attrs) &&
            !attrs.override_redirect &&
            attrs.map_state == IsViewable) {
            
            // Check EWMH window type for floating detection
            bool should_float = false;
            if (ewmh_manager_) {
                ewmh::WindowType win_type = ewmh_manager_->getWindowType(top_level_windows[i]);
                
                // Skip dock, desktop, and other non-tiled windows entirely
                if (win_type == ewmh::WindowType::Dock || 
                    win_type == ewmh::WindowType::Desktop) {

                    
                    // Register dock windows for strut tracking
                    if (win_type == ewmh::WindowType::Dock) {
                        ewmh_manager_->registerDockWindow(top_level_windows[i]);
                    }
                    continue;
                }
                
                // Dialog, utility, toolbar, splash, and notification windows should float
                if (ewmh::EWMHManager::isFloatingType(win_type)) {
                    should_float = true;


                }
            }
            
            // Grab button presses for click-to-focus
            XGrabButton(display_.get(), AnyButton, AnyModifier, top_level_windows[i],
                        False, ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);
            
            // Select input for focus-follows-mouse - receive EnterNotify events
            XSelectInput(display_.get(), top_level_windows[i], 
                        EnterWindowMask | LeaveWindowMask | FocusChangeMask);
            
            // Create managed window
            auto managed = std::make_unique<ManagedWindow>(top_level_windows[i], display_.get());
            managed->setWorkspace(current_workspace_);
            
            if (should_float) {
                managed->setFloating(true);
            } else {
                layout_engine_->addWindow(top_level_windows[i]);
            }
            
            clients_[top_level_windows[i]] = std::move(managed);
        }
    }
    
    XFree(top_level_windows);
    applyLayout();
}

void WindowManager::run() {
    XEvent event;
    
    // Cache atom for ClientMessage handling
    Atom net_active_window = XInternAtom(display_.get(), "_NET_ACTIVE_WINDOW", False);
    
    while (running_) {
        // Update toaster (render notifications)
        toaster_->update();
        
        // Process X11 events
        if (XPending(display_.get()) > 0) {
            XNextEvent(display_.get(), &event);
            
            // Skip events for toaster window
            if (event.xany.window == toaster_->getWindow()) {
                continue;
            }
            
            switch (event.type) {
                case MapRequest:
                    handleMapRequest(event.xmaprequest);
                    break;
                    
                case ConfigureRequest:
                    handleConfigureRequest(event.xconfigurerequest);
                    break;
                    
                case KeyPress:
                    handleKeyPress(event.xkey);
                    break;
                    
                case ButtonPress:
                    handleButtonPress(event.xbutton);
                    break;
                    
                case ButtonRelease:
                    handleButtonRelease(event.xbutton);
                    break;
                    
                case MotionNotify:
                    handleMotionNotify(event.xmotion);
                    break;
                        
                case DestroyNotify:
                    handleDestroyNotify(event.xdestroywindow);
                    break;
                    
                case UnmapNotify:
                    handleUnmapNotify(event.xunmap);
                break;
                    
                case EnterNotify:
                    handleEnterNotify(event.xcrossing);
                    break;
                    
                case FocusIn:
                    handleFocusIn(event.xfocus);
                    break;
                    
                case PropertyNotify:
                    handlePropertyNotify(event.xproperty);
                    break;
                    
                case ClientMessage:
                    // Let EWMH manager handle client messages first
                    if (ewmh_manager_ && ewmh_manager_->handleClientMessage(event.xclient)) {
                        // EWMH manager handled the message
                        break;
                    }
                    
                    // Handle _NET_ACTIVE_WINDOW for workspace auto-switch (fallback)
                    if (event.xclient.message_type == net_active_window) {
                        Window target_window = event.xclient.window;
                        auto it = clients_.find(target_window);
                        if (it != clients_.end()) {
                            int window_workspace = it->second->getWorkspace();
                            
                            // If the window is on a different workspace, switch to it
                            if (window_workspace != current_workspace_) {


                                
                                // Hide windows on current workspace
                                hideWorkspaceWindows(current_workspace_);
                                
                                // Update current workspace
                                current_workspace_ = window_workspace;
                                layout_engine_->setCurrentWorkspace(window_workspace);
                                
                                // Show windows on new workspace
                                showWorkspaceWindows(window_workspace);
                                
                                // Apply layout for new workspace
                                applyLayout();
                                
                                // Update EWMH current desktop
                                updateEWMHCurrentWorkspace();
                                
                                // Flush to ensure all changes are committed
                                XFlush(display_.get());
                            }
                            
                            // Focus the window
                            XSetInputFocus(display_.get(), target_window, RevertToPointerRoot, CurrentTime);
                            layout_engine_->focusWindow(target_window);
                            layout_engine_->updateBorderColors();
                            workspace_last_focus_[current_workspace_] = target_window;
                            
                            // Update EWMH active window
                            updateEWMHActiveWindow(target_window);
                        }
                    }
                    break;
            }
        } else {
            // No events pending - small sleep to avoid busy waiting
            usleep(1000); // 1ms
        }
    }
}

void WindowManager::handleMapRequest(const XMapRequestEvent& event) {
    if (clients_.find(event.window) != clients_.end()) {
        return;
    }
    
    manageWindow(event.window);
}

void WindowManager::manageWindow(Window window) {
    // Check EWMH window type first - skip dock and desktop windows entirely
    if (ewmh_manager_) {
        ewmh::WindowType win_type = ewmh_manager_->getWindowType(window);
        
        // Skip dock and desktop windows - they should never be managed
        if (win_type == ewmh::WindowType::Dock || 
            win_type == ewmh::WindowType::Desktop) {
            
            // Register dock windows for strut tracking
            if (win_type == ewmh::WindowType::Dock) {
                ewmh_manager_->registerDockWindow(window);
                
                // Re-apply layout to account for new dock struts
                applyLayout();
            }
            
            // Just map the window but don't manage it
            XMapWindow(display_.get(), window);
            return;
        }
    }
    
    auto managed = std::make_unique<ManagedWindow>(window, display_.get());
    managed->setWorkspace(current_workspace_);
    
    // Apply window rules from config
    const auto& rules = config_parser_->getConfig().window_rules;
    if (rules.opacity) {
        managed->setOpacity(*rules.opacity);
    }
    
    // Check EWMH window type for floating detection
    bool should_float = false;
    if (ewmh_manager_) {
        ewmh::WindowType win_type = ewmh_manager_->getWindowType(window);
        
        // Dialog, utility, toolbar, splash, and notification windows should float
        if (ewmh::EWMHManager::isFloatingType(win_type)) {
            should_float = true;
            managed->setFloating(true);

        }
        
        // Set EWMH properties for the window
        ewmh_manager_->setWindowDesktop(window, current_workspace_);
        ewmh_manager_->setWindowPID(window, getpid());
    }
    
    // Grab button presses for click-to-focus
    XGrabButton(display_.get(), AnyButton, AnyModifier, window,
                False, ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);
    
    // Select input for focus-follows-mouse - receive EnterNotify events
    XSelectInput(display_.get(), window, EnterWindowMask | LeaveWindowMask | FocusChangeMask);

    // Add to layout engine (respects floating state)
    if (!should_float) {
        layout_engine_->addWindow(window);
    }
    
    // Map the window
    XMapWindow(display_.get(), window);
    
    // Focus the new window
    XSetInputFocus(display_.get(), window, RevertToPointerRoot, CurrentTime);
    
    // Store in clients map
    clients_[window] = std::move(managed);
    
    // Update last focus for this workspace
    workspace_last_focus_[current_workspace_] = window;
    
    // Reapply layout
    applyLayout();
    
    // Update border colors to reflect new focus
    layout_engine_->updateBorderColors();
    
    // Update EWMH client list
    updateEWMHClientList();
    
    // Update EWMH active window
    updateEWMHActiveWindow(window);
}

void WindowManager::handleConfigureRequest(const XConfigureRequestEvent& event) {
    auto it = clients_.find(event.window);
    
    if (it != clients_.end() && it->second->isFloating()) {
        // Allow floating windows to configure themselves
        XWindowChanges changes;
        changes.x = event.x;
        changes.y = event.y;
        changes.width = event.width;
        changes.height = event.height;
        changes.border_width = event.border_width;
        changes.sibling = event.above;
        changes.stack_mode = event.detail;
        
        XConfigureWindow(display_.get(), event.window, event.value_mask, &changes);
    } else {
        // Tiled windows: just acknowledge the request but keep our geometry
        XWindowChanges changes;
        int x, y;
        unsigned int width, height;
        
        if (it != clients_.end()) {
            it->second->getGeometry(x, y, width, height);
            changes.x = x;
            changes.y = y;
            changes.width = width;
            changes.height = height;
        } else {
            // Unmanaged window - grant the request
            changes.x = event.x;
            changes.y = event.y;
            changes.width = event.width;
            changes.height = event.height;
        }
        
        changes.border_width = event.border_width;
        changes.sibling = event.above;
        changes.stack_mode = event.detail;
        
        XConfigureWindow(display_.get(), event.window, event.value_mask, &changes);
    }
}

void WindowManager::handleKeyPress(const XKeyEvent& event) {
    keybind_manager_->handleKeyPress(event, this);
}

void WindowManager::handleButtonPress(const XButtonEvent& event) {
    // Check for Super+left-click drag-to-move
    // Super modifier is typically Mod4Mask
    const bool super_held = (event.state & Mod4Mask) != 0;
    const bool left_click = event.button == Button1;
    
    if (super_held && left_click) {
        auto it = clients_.find(event.window);
        if (it != clients_.end() && 
            it->second->getWorkspace() == current_workspace_) {
            // Start drag-to-move for any window (floating or tiled)
            startDrag(event.window, event.x_root, event.y_root);
            // Grab pointer for continuous motion events
            XGrabPointer(display_.get(), event.window, True,
                        ButtonReleaseMask | PointerMotionMask,
                        GrabModeAsync, GrabModeAsync,
                        None, None, CurrentTime);
            return;
        }
    }
    
    // Check for floating window edge resize (left-click without modifiers)
    // Only if floating_resize_enabled is true
    if (floating_resize_enabled_ && left_click && !super_held) {
        auto it = clients_.find(event.window);
        if (it != clients_.end() && 
            it->second->getWorkspace() == current_workspace_ &&
            it->second->isFloating()) {
            // Check if click is near the edge
            std::string edge = getEdgeAtPosition(event.window, event.x_root, event.y_root);
            if (!edge.empty()) {
                // Start resize
                startResize(event.window, event.x_root, event.y_root, edge);
                XGrabPointer(display_.get(), event.window, True,
                            ButtonReleaseMask | PointerMotionMask,
                            GrabModeAsync, GrabModeAsync,
                            None, None, CurrentTime);
                return;
            }
        }
    }
    
    // Click to focus when focus_follows_mouse is disabled
    auto it = clients_.find(event.window);
    if (it != clients_.end() && it->second->getWorkspace() == current_workspace_) {
        // Focus the clicked window
        XSetInputFocus(display_.get(), event.window, RevertToPointerRoot, CurrentTime);
        layout_engine_->focusWindow(event.window);
        layout_engine_->updateBorderColors();
        workspace_last_focus_[current_workspace_] = event.window;
        
        // Replay the button event to the window so it receives it normally
        XAllowEvents(display_.get(), ReplayPointer, event.time);
    } else {
        // Not our window or wrong workspace - let the event through
        XAllowEvents(display_.get(), ReplayPointer, event.time);
    }
}

void WindowManager::handleButtonRelease(const XButtonEvent& event) {
    if (dragging_) {
        endDrag();
        XUngrabPointer(display_.get(), CurrentTime);
    }
    if (resizing_) {
        endResize();
        XUngrabPointer(display_.get(), CurrentTime);
    }
}

void WindowManager::handleMotionNotify(const XMotionEvent& event) {
    if (dragging_) {
        updateDrag(event.x_root, event.y_root);
    }
    if (resizing_) {
        updateResize(event.x_root, event.y_root);
    }
}

void WindowManager::startDrag(Window window, int root_x, int root_y) {
    auto it = clients_.find(window);
    if (it == clients_.end()) {
        return;
    }
    
    dragging_ = true;
    drag_window_ = window;
    drag_start_x_ = root_x;
    drag_start_y_ = root_y;
    
    // Get current window position in ROOT coordinates
    // XGetGeometry returns coordinates relative to parent window, not root.
    // We need to translate them to root coordinates using XTranslateCoordinates.
    Window root_return;
    int win_x, win_y;
    unsigned int width, height, border_width, depth;
    
    // Use XGetGeometry to get window size and position relative to parent
    if (XGetGeometry(display_.get(), window, &root_return, &win_x, &win_y,
                     &width, &height, &border_width, &depth)) {
        // Translate coordinates from window-relative to root-relative
        // XGetGeometry gives us position relative to parent, but we need root coordinates
        int root_x_translated, root_y_translated;
        Window child_return;
        
        if (XTranslateCoordinates(display_.get(), window, root_,
                                  0, 0,  // Position relative to window itself
                                  &root_x_translated, &root_y_translated,
                                  &child_return)) {
            // Now we have the window's top-left corner in root coordinates
            drag_window_start_x_ = root_x_translated;
            drag_window_start_y_ = root_y_translated;
        } else {
            // Fallback: use XGetGeometry coordinates (may be relative to parent)
            drag_window_start_x_ = win_x;
            drag_window_start_y_ = win_y;
        }
    } else {
        // Fallback to cached geometry if XGetGeometry fails
        int x, y;
        it->second->getGeometry(x, y, width, height);
        drag_window_start_x_ = x;
        drag_window_start_y_ = y;
    }
    
    // Store the original floating state
    drag_was_floating_ = it->second->isFloating();
    
    // Note: We do NOT automatically set floating during drag
    // The window remains in its layout and can be repositioned within constraints
    // Floating should only be toggled via explicit keybinding/command
    
    // Raise window to top
    XRaiseWindow(display_.get(), window);
}

void WindowManager::updateDrag(int root_x, int root_y) {
    if (!dragging_ || drag_window_ == None) {
        return;
    }
    
    auto it = clients_.find(drag_window_);
    if (it == clients_.end()) {
        return;
    }
    
    // Get drag configuration
    const auto& drag_config = config_parser_->getConfig().drag;
    
    // Calculate new position based on cursor movement
    int dx = root_x - drag_start_x_;
    int dy = root_y - drag_start_y_;
    
    int new_x = drag_window_start_x_ + dx;
    int new_y = drag_window_start_y_ + dy;
    
    // Get current window size
    int x, y;
    unsigned int width, height;
    it->second->getGeometry(x, y, width, height);
    
    // Move window - this provides visual feedback during drag
    // For both floating and tiled windows, we allow free movement during drag
    // Tiled windows will snap back to layout on release
    XMoveWindow(display_.get(), drag_window_, new_x, new_y);
    it->second->setGeometry(new_x, new_y, width, height);
    
    // For tiled windows, check for swap-on-drag if enabled
    if (!drag_was_floating_ && drag_config.swap_on_drag) {
        // Find window under cursor
        Window target_window = findWindowAtPosition(root_x, root_y);
        
        // Only swap if we found a different target and it's not the same as last swap
        if (target_window != None && target_window != drag_window_ && target_window != drag_last_swap_target_) {
            // Check if target is also a tiled window on the same workspace
            auto target_it = clients_.find(target_window);
            if (target_it != clients_.end() && 
                !target_it->second->isFloating() &&
                target_it->second->getWorkspace() == current_workspace_) {
                
                // Swap windows in BSP tree (internal structure only)
                layout_engine_->swapWindows(drag_window_, target_window);
                drag_last_swap_target_ = target_window;
                
                // Note: We do NOT call applyLayout() here to avoid jitter
                // The layout will be applied on endDrag() to snap windows to their positions
                // The dragged window continues to follow the cursor during drag
            }
        } else if (target_window == None) {
            // Clear last swap target when not over any window
            drag_last_swap_target_ = None;
        }
    }
}

void WindowManager::endDrag() {
    if (!dragging_ || drag_window_ == None) {
        dragging_ = false;
        drag_window_ = None;
        drag_last_swap_target_ = None;
        return;
    }
    
    auto it = clients_.find(drag_window_);
    if (it != clients_.end()) {
        // If the window was originally tiled (not floating), 
        // reapply layout to snap it back to layout constraints
        if (!drag_was_floating_) {
            // Reapply layout to snap window back to its position
            applyLayout();
            layout_engine_->updateBorderColors();
        }
    }
    
    dragging_ = false;
    drag_window_ = None;
    drag_last_swap_target_ = None;
}

Window WindowManager::findWindowAtPosition(int root_x, int root_y) {
    // Find a managed window that contains the given root coordinates
    // Use actual X11 window positions, not cached geometry, for accuracy during drag
    for (const auto& [window, managed] : clients_) {
        // Skip the currently dragged window
        if (window == drag_window_) {
            continue;
        }
        
        // Skip windows not on current workspace
        if (managed->getWorkspace() != current_workspace_) {
            continue;
        }
        
        // Skip floating windows when looking for swap target
        if (managed->isFloating()) {
            continue;
        }
        
        // Get actual window position from X11
        Window root_return;
        int win_x, win_y;
        unsigned int width, height, border_width, depth;
        
        if (XGetGeometry(display_.get(), window, &root_return, &win_x, &win_y,
                         &width, &height, &border_width, &depth)) {
            // Translate to root coordinates
            int root_x_win, root_y_win;
            Window child_return;
            
            if (XTranslateCoordinates(display_.get(), window, root_,
                                      0, 0,
                                      &root_x_win, &root_y_win,
                                      &child_return)) {
                // Check if the point is within this window's bounds
                if (root_x >= root_x_win && root_x < root_x_win + static_cast<int>(width) &&
                    root_y >= root_y_win && root_y < root_y_win + static_cast<int>(height)) {
                    return window;
                }
            }
        }
    }
    
    return None;
}

std::string WindowManager::getEdgeAtPosition(Window window, int root_x, int root_y) {
    // Get the edge size from config
    int edge_size = floating_resize_edge_size_;
    
    // Get window geometry
    Window root_return;
    int win_x, win_y;
    unsigned int width, height, border_width, depth;
    
    if (!XGetGeometry(display_.get(), window, &root_return, &win_x, &win_y,
                     &width, &height, &border_width, &depth)) {
        return "";
    }
    
    // Translate to root coordinates
    int root_x_win, root_y_win;
    Window child_return;
    
    if (!XTranslateCoordinates(display_.get(), window, root_,
                              0, 0,
                              &root_x_win, &root_y_win,
                              &child_return)) {
        return "";
    }
    
    // Calculate relative position within window
    int rel_x = root_x - root_x_win;
    int rel_y = root_y - root_y_win;
    
    // Check which edge(s) the cursor is near
    bool near_left = (rel_x >= 0 && rel_x < edge_size);
    bool near_right = (rel_x >= static_cast<int>(width) - edge_size && rel_x < static_cast<int>(width));
    bool near_top = (rel_y >= 0 && rel_y < edge_size);
    bool near_bottom = (rel_y >= static_cast<int>(height) - edge_size && rel_y < static_cast<int>(height));
    
    // Determine edge string
    if (near_left && near_top) return "topleft";
    if (near_left && near_bottom) return "bottomleft";
    if (near_right && near_top) return "topright";
    if (near_right && near_bottom) return "bottomright";
    if (near_left) return "left";
    if (near_right) return "right";
    if (near_top) return "top";
    if (near_bottom) return "bottom";
    
    return "";
}

void WindowManager::startResize(Window window, int root_x, int root_y, const std::string& edge) {
    auto it = clients_.find(window);
    if (it == clients_.end()) {
        return;
    }
    
    // Only allow resizing floating windows
    if (!it->second->isFloating()) {
        return;
    }
    
    resizing_ = true;
    resize_window_ = window;
    resize_start_x_ = root_x;
    resize_start_y_ = root_y;
    resize_edge_ = edge;
    
    // Get current window geometry
    int x, y;
    unsigned int width, height;
    it->second->getGeometry(x, y, width, height);
    resize_start_width_ = width;
    resize_start_height_ = height;
    
    // Raise window to top
    XRaiseWindow(display_.get(), window);
    
}

void WindowManager::updateResize(int root_x, int root_y) {
    if (!resizing_ || resize_window_ == None) {
        return;
    }
    
    auto it = clients_.find(resize_window_);
    if (it == clients_.end()) {
        return;
    }
    
    // Calculate deltas
    int dx = root_x - resize_start_x_;
    int dy = root_y - resize_start_y_;
    
    // Get current window geometry
    int x, y;
    unsigned int width, height;
    it->second->getGeometry(x, y, width, height);
    
    // Minimum size constraints
    const unsigned int min_width = 100;
    const unsigned int min_height = 100;
    
    // Calculate new dimensions based on edge
    int new_x = x;
    int new_y = y;
    int new_width = resize_start_width_;
    int new_height = resize_start_height_;
    
    if (resize_edge_.find("left") != std::string::npos) {
        new_width = std::max(min_width, static_cast<unsigned int>(resize_start_width_ - dx));
        new_x = x + (resize_start_width_ - new_width);
    }
    if (resize_edge_.find("right") != std::string::npos) {
        new_width = std::max(min_width, static_cast<unsigned int>(resize_start_width_ + dx));
    }
    if (resize_edge_.find("top") != std::string::npos) {
        new_height = std::max(min_height, static_cast<unsigned int>(resize_start_height_ - dy));
        new_y = y + (resize_start_height_ - new_height);
    }
    if (resize_edge_.find("bottom") != std::string::npos) {
        new_height = std::max(min_height, static_cast<unsigned int>(resize_start_height_ + dy));
    }
    
    // Apply new geometry
    XMoveResizeWindow(display_.get(), resize_window_, new_x, new_y, new_width, new_height);
    it->second->setGeometry(new_x, new_y, new_width, new_height);
}

void WindowManager::endResize() {
    if (!resizing_) {
        return;
    }
    
    // Store the new geometry as tiled geometry for this floating window
    if (resize_window_ != None) {
        auto it = clients_.find(resize_window_);
        if (it != clients_.end()) {
            int x, y;
            unsigned int width, height;
            it->second->getGeometry(x, y, width, height);
            it->second->storeTiledGeometry(x, y, width, height);
        }
    }
    
    resizing_ = false;
    resize_window_ = None;
    resize_edge_ = "";
    
}

void WindowManager::handleDestroyNotify(const XDestroyWindowEvent& event) {
    unmanageWindow(event.window);
}

void WindowManager::unmanageWindow(Window window) {
    auto it = clients_.find(window);
    if (it == clients_.end()) {
        return;
    }
    
    // Clean up any pending unmap tracking for this window
    pending_unmaps_.erase(window);
    
    int ws = it->second->getWorkspace();
    
    // Remove from BSP tree
    Window next_focus = layout_engine_->removeWindow(window);
    
    // Clear from workspace last focus if needed
    if (workspace_last_focus_[ws] == window) {
        workspace_last_focus_[ws] = next_focus;
    }
    
    clients_.erase(it);
    
    // Focus the next window
    if (next_focus != None) {
        auto next_it = clients_.find(next_focus);
        if (next_it != clients_.end() && next_it->second->getWorkspace() == current_workspace_) {
            XSetInputFocus(display_.get(), next_focus, RevertToPointerRoot, CurrentTime);
            layout_engine_->focusWindow(next_focus);
            updateEWMHActiveWindow(next_focus);
        }
    } else {
        updateEWMHActiveWindow(None);
    }
    
    applyLayout();
    layout_engine_->updateBorderColors();
    
    // Update EWMH client list
    updateEWMHClientList();
}

void WindowManager::handleUnmapNotify(const XUnmapEvent& event) {
    // Only handle if it's not due to us reparenting or destroying
    if (event.send_event) {
        return;
    }
    
    // Check if this is an intentional unmap for workspace operations
    // If so, skip unmanageWindow - the window is just being hidden, not closed
    if (pending_unmaps_.count(event.window)) {
        pending_unmaps_.erase(event.window);
        return;
    }
    
    unmanageWindow(event.window);
}

void WindowManager::handleEnterNotify(const XCrossingEvent& event) {
    // Ignore events that aren't normal enter notifications
    if (event.mode != NotifyNormal && event.mode != NotifyUngrab) {
        return;
    }
    
    // Ignore if the event is from a grab or popup
    if (event.detail == NotifyInferior) {
        return;
    }
    
    // Skip focus-follows-mouse during pointer warping
    if (is_warping_) {
        is_warping_ = false;  // Clear the warping flag
        return;
    }
    
    if (!focus_follows_mouse_) {
        return;  // Click to focus mode - ignore EnterNotify events
    }
    
    // Handle monitor focus follows mouse - switch workspace when mouse moves to different monitor
    if (monitor_focus_follows_mouse_ && monitor_manager_) {
        // Get current mouse position
        Window root_return, child_return;
        int root_x, root_y, win_x, win_y;
        unsigned int mask_return;
        
        if (XQueryPointer(display_.get(), root_, &root_return, &child_return, 
                          &root_x, &root_y, &win_x, &win_y, &mask_return)) {
            const auto* monitor = monitor_manager_->getMonitorAt(root_x, root_y);
            if (monitor && monitor->id != current_monitor_id_) {
                current_monitor_id_ = monitor->id;
                

                
                // Switch to this monitor's workspace if we have more than one monitor
                if (monitor_manager_->getMonitorCount() > 1) {
                    // Map monitor ID to workspace - for simplicity, use monitor ID as workspace
                    int target_workspace = monitor->id;
                    
                    // Only switch if the target workspace is different
                    if (target_workspace != current_workspace_ && 
                        target_workspace < max_workspaces_) {

                        switchWorkspace(target_workspace);
                    }
                }
            }
        }
    }
    
    // Focus follows mouse - focus the window under cursor
    auto it = clients_.find(event.window);
    if (it != clients_.end() && it->second->getWorkspace() == current_workspace_) {
        // Don't re-focus if already focused
        Window current_focus = layout_engine_->getFocusedWindow();
        if (current_focus == event.window) {
            return;
        }
        
        // Raise the window to the top
        XRaiseWindow(display_.get(), event.window);
        
        // Set input focus
        XSetInputFocus(display_.get(), event.window, RevertToPointerRoot, CurrentTime);
        
        // Update layout engine focus state
        layout_engine_->focusWindow(event.window);
        layout_engine_->updateBorderColors();
        
        // Remember as last focused window for this workspace
        workspace_last_focus_[current_workspace_] = event.window;
        
        // Flush to ensure immediate visual feedback
        XFlush(display_.get());
    }
}

void WindowManager::handleFocusIn(const XFocusChangeEvent& event) {
    // Ignore certain focus modes
    if (event.mode == NotifyGrab || event.mode == NotifyUngrab ||
        event.mode == NotifyWhileGrabbed) {
        return;
    }
    
    auto it = clients_.find(event.window);
    if (it != clients_.end()) {
        int window_workspace = it->second->getWorkspace();
        
        // If the window is on a different workspace, switch to it automatically
        // This handles external focus requests (e.g., Alt+Tab, taskbar clicks)
        if (window_workspace != current_workspace_) {
            // Switch to the window's workspace without triggering another focus event
            int target_ws = window_workspace;
            
            // Hide windows on current workspace
            hideWorkspaceWindows(current_workspace_);
            
            // Update current workspace
            current_workspace_ = target_ws;
            layout_engine_->setCurrentWorkspace(target_ws);
            
            // Show windows on new workspace
            showWorkspaceWindows(target_ws);
            
            // Apply layout for new workspace
            applyLayout();
            
            // Flush to ensure all changes are committed
            XFlush(display_.get());
        }
        
        // Now handle the focus for the current workspace
        layout_engine_->focusWindow(event.window);
        layout_engine_->updateBorderColors();
        workspace_last_focus_[current_workspace_] = event.window;
    }
}

void WindowManager::handlePropertyNotify(const XPropertyEvent& event) {
    auto it = clients_.find(event.window);
    if (it != clients_.end()) {
        // Window properties changed - could update title, class, etc.
    }
}

void WindowManager::applyLayout() {
    // Get screen dimensions
    int screen_width = DisplayWidth(display_.get(), screen_);
    int screen_height = DisplayHeight(display_.get(), screen_);
    
    // Get combined struts from all dock windows
    unsigned long left_strut = 0, right_strut = 0, top_strut = 0, bottom_strut = 0;
    
    if (ewmh_manager_) {
        auto struts = ewmh_manager_->getCombinedStruts(screen_width, screen_height);
        left_strut = struts.left;
        right_strut = struts.right;
        top_strut = struts.top;
        bottom_strut = struts.bottom;
        
        // Update _NET_WORKAREA with the reserved space
        ewmh_manager_->updateWorkarea(screen_width, screen_height,
                                      left_strut, right_strut, top_strut, bottom_strut);
    }
    
    // Calculate usable area (screen minus reserved struts)
    Rect screen_bounds{
        static_cast<int>(left_strut), 
        static_cast<int>(top_strut),
        static_cast<unsigned int>(screen_width - left_strut - right_strut),
        static_cast<unsigned int>(screen_height - top_strut - bottom_strut)
    };
    
    
    layout_engine_->applyLayout(current_workspace_, screen_bounds);
}

bool WindowManager::loadConfigSafe() {
    try {
        if (custom_config_path_.has_value()) {
            return config_parser_->load(*custom_config_path_);
        }
        return config_parser_->load();
    } catch (const std::exception& e) {
        std::cerr << "Config loading exception: " << e.what() << std::endl;
        return false;
    }
}

void WindowManager::fallbackToDefaultConfig() {
    
    // Load embedded config
    if (!config_parser_->loadFromString(ConfigParser::getEmbeddedConfig())) {
        std::cerr << "[ERROR] Failed to parse embedded config!" << std::endl;
        return;
    }
    
    
    // Apply the config to the layout engine
    applyConfigToLayout();
    
    // Register keybinds from embedded config
    if (keybind_manager_) {
        keybind_manager_->clearKeybinds();
        const auto& config = config_parser_->getConfig();
        for (const auto& bind : config.keybinds) {
            std::string keybind_str;
            std::string action;
            
            // Build keybind string from modifiers + key
            if (!bind.modifiers.empty()) {
                keybind_str = bind.modifiers + ", " + bind.key;
            } else {
                keybind_str = bind.key;
            }
            
            // Handle exec commands
            if (bind.exec_command.has_value()) {
                action = "exec: " + *bind.exec_command;
            } else {
                action = bind.action;
            }
            
            keybind_manager_->registerKeybind(keybind_str, action);
        }
        
        // Grab the keys
        if (display_ && root_ != None) {
            keybind_manager_->grabKeys(display_.get(), root_);
        }
    }
}

void WindowManager::applyConfigToLayout() {
    const auto& config = config_parser_->getConfig();
    
    
    // Apply focus behavior
    focus_follows_mouse_ = config.focus_follows_mouse;
    
    // Apply monitor focus behavior
    monitor_focus_follows_mouse_ = config.monitor_focus_follows_mouse;
    
    // Initialize current monitor if monitor focus is enabled
    if (monitor_focus_follows_mouse_ && monitor_manager_) {
        monitor_manager_->refresh();
        Window root_return, child_return;
        int root_x, root_y, win_x, win_y;
        unsigned int mask_return;
        if (XQueryPointer(display_.get(), root_, &root_return, &child_return, 
                          &root_x, &root_y, &win_x, &win_y, &mask_return)) {
            const auto* monitor = monitor_manager_->getMonitorAt(root_x, root_y);
            if (monitor) {
                current_monitor_id_ = monitor->id;

            }
        }
    }

    // Apply workspace configuration
    infinite_workspaces_ = config.workspaces.infinite;
    max_workspaces_ = config.workspaces.max_workspaces;
    dynamic_workspace_creation_ = config.workspaces.dynamic_creation;
    auto_remove_empty_workspaces_ = config.workspaces.auto_remove;
    min_persist_workspaces_ = config.workspaces.min_persist;
    
    // Apply multi-monitor workspace configuration
    per_monitor_workspaces_ = config.workspaces.per_monitor;
    virtual_workspace_mapping_ = config.workspaces.virtual_mapping;
    workspace_to_monitor_ = config.workspaces.workspace_to_monitor;
    
    
    if (!workspace_to_monitor_.empty()) {
        for (const auto& [ws, mon] : workspace_to_monitor_) {
        }
    }
    
    // Initialize per-monitor focus tracking for per-monitor workspaces
    if (per_monitor_workspaces_ && monitor_manager_) {
        size_t monitor_count = monitor_manager_->getMonitorCount();
        per_monitor_last_focus_.resize(monitor_count);
        for (auto& monitor_focus : per_monitor_last_focus_) {
            monitor_focus.resize(max_workspaces_, None);
        }
    }
    
    // Resize workspace tracking if needed
    if (!infinite_workspaces_ && workspace_last_focus_.size() < static_cast<size_t>(max_workspaces_)) {
        workspace_last_focus_.resize(max_workspaces_, None);
    }

    // Apply drag configuration
    
    // Apply windows configuration
    auto_resize_non_docks_ = config.windows.auto_resize_non_docks;
    floating_resize_enabled_ = config.windows.floating_resize_enabled;
    floating_resize_edge_size_ = config.windows.floating_resize_edge_size;
    

    // Apply default layout settings
    // Gap size and border width are set via config file values
    // Default values are used if not specified
    layout_engine_->setGapSize(config.layout_gaps.inner_gap);
    layout_engine_->setOuterGap(config.layout_gaps.outer_gap);
    layout_engine_->setEdgeGaps(
        config.layout_gaps.top_gap,
        config.layout_gaps.bottom_gap,
        config.layout_gaps.left_gap,
        config.layout_gaps.right_gap
    );
    layout_engine_->setBorderWidth(2);  // Default border
    
    // Parse border colors from config and apply them
    unsigned long focused_color = 0x00FF00;   // Default green
    unsigned long unfocused_color = 0x333333; // Default gray
    
    // Parse focused color
    if (!config.borders.focused_color.empty()) {
        try {
            std::string hex = config.borders.focused_color;
            if (!hex.empty() && hex[0] == '#') {
                hex = hex.substr(1);
            }
            focused_color = std::stoul(hex, nullptr, 16);
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Failed to parse focused border color: " << e.what() << std::endl;
        }
    }
    
    // Parse unfocused color
    if (!config.borders.unfocused_color.empty()) {
        try {
            std::string hex = config.borders.unfocused_color;
            if (!hex.empty() && hex[0] == '#') {
                hex = hex.substr(1);
            }
            unfocused_color = std::stoul(hex, nullptr, 16);
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Failed to parse unfocused border color: " << e.what() << std::endl;
        }
    }
    
    layout_engine_->setBorderColors(focused_color, unfocused_color);
}

void WindowManager::setupConfigWatcher() {
    config_watcher_ = std::make_unique<ConfigWatcher>();
    
    // Get config directory path
    auto config_path = ConfigParser::getDefaultConfigPath();
    auto config_dir = config_path.parent_path();
    
    // Set up validation callback
    config_watcher_->setValidationCallback([this](const std::filesystem::path& path) {
        ValidationResult result;
        
        // Basic syntax validation
        std::ifstream file(path);
        if (!file.is_open()) {
            result.success = false;
            result.errors.push_back("Cannot open file");
            return result;
        }
        
        std::string line;
        int line_num = 0;
        int brace_count = 0;
        int bracket_count = 0;
        
        while (std::getline(file, line)) {
            line_num++;
            for (char c : line) {
                if (c == '{') brace_count++;
                else if (c == '}') brace_count--;
                else if (c == '[') bracket_count++;
                else if (c == ']') bracket_count--;
            }
        }
        
        if (brace_count != 0) {
            result.success = false;
            ValidationResult::ErrorLocation loc;
            loc.line = line_num;
            loc.message = "Unmatched braces";
            result.error_locations.push_back(loc);
            return result;
        }
        if (bracket_count != 0) {
            result.success = false;
            ValidationResult::ErrorLocation loc;
            loc.line = line_num;
            loc.message = "Unmatched brackets";
            result.error_locations.push_back(loc);
            return result;
        }
        
        result.success = true;
        return result;
    });
    
    // Set up apply callback
    config_watcher_->setApplyCallback([this](const std::filesystem::path& path) {
        
        // Reload configuration
        if (loadConfigSafe()) {
            // Clear any previous config errors on success
            toaster_->clearConfigErrors();
            
            applyConfigToLayout();
            applyLayout();
            layout_engine_->updateBorderColors();
            
            // Re-register keybinds from config only
            keybind_manager_->clearKeybinds();
            const auto& config = config_parser_->getConfig();
            for (const auto& bind : config.keybinds) {
                std::string keybind_str;
                if (!bind.modifiers.empty()) {
                    keybind_str = bind.modifiers + ", " + bind.key;
                } else {
                    keybind_str = bind.key;
                }
                
                std::string action = bind.exec_command.has_value() ? 
                    "exec: " + *bind.exec_command : bind.action;
                
                keybind_manager_->registerKeybind(keybind_str, action);
            }
            keybind_manager_->grabKeys(display_.get(), root_);
            
            toaster_->success("Config reloaded");
            return true;
        }
        
        toaster_->error("Config reload failed");
        return false;
    });
    
    // Set up error callback
    config_watcher_->setErrorCallback([this](const ValidationResult& result) {
        // Clear previous config errors first
        toaster_->clearConfigErrors();
        
        for (const auto& err : result.errors) {
            toaster_->configError("Config error: " + err);
        }
        for (const auto& loc : result.error_locations) {
            toaster_->configError("Line " + std::to_string(loc.line) + ": " + loc.message);
        }
    });
    
    // Set up notification callback
    config_watcher_->setNotifyCallback([this](const std::string& message, const std::string& level) {
        if (level == "info") {
            toaster_->info(message);
        } else if (level == "success") {
            toaster_->success(message);
        } else if (level == "error") {
            toaster_->error(message);
        }
    });
    
    // Start watching the config directory
    if (!config_watcher_->addWatch(config_dir)) {
        std::cerr << "Failed to add config watch" << std::endl;
    }
    
    // Start the watcher thread
    if (!config_watcher_->start()) {
        std::cerr << "Failed to start config watcher thread" << std::endl;
    } else {
    }
}

void WindowManager::reloadConfig() {
    toaster_->info("Reloading configuration...");
    
    if (loadConfigSafe()) {
        toaster_->success("Configuration reloaded");
        applyConfigToLayout();
        applyLayout();
        layout_engine_->updateBorderColors();
    } else {
        toaster_->error("Configuration reload failed");
    }
}

// ============================================================================
// Workspace Management
// ============================================================================

void WindowManager::switchWorkspace(int workspace) {
    // Convert from 1-indexed to 0-indexed if needed
    int target_ws = (workspace > 0) ? workspace - 1 : workspace;
    
    // Check workspace bounds
    if (target_ws < 0) {
        return;
    }
    
    // For non-infinite mode, check upper bound
    if (!infinite_workspaces_ && target_ws >= max_workspaces_) {
        return;
    }
    
    if (target_ws == current_workspace_) {
        return;
    }
    

    // Auto-remove empty workspace if enabled (only if leaving an empty workspace)
    if (auto_remove_empty_workspaces_ && current_workspace_ >= min_persist_workspaces_) {
        int window_count = getWorkspaceWindowCount(current_workspace_);
        if (window_count == 0 && current_workspace_ > highest_used_workspace_) {
            // Shrink workspace_last_focus_ if needed
            if (workspace_last_focus_.size() > static_cast<size_t>(current_workspace_ + 1)) {
                workspace_last_focus_.resize(current_workspace_ + 1);
            }
        }
    }
    
    // Expand tracking vectors if needed for infinite workspaces
    if (infinite_workspaces_ && target_ws >= static_cast<int>(workspace_last_focus_.size())) {
        workspace_last_focus_.resize(target_ws + 10, None);  // Pre-allocate extra space
    }
    
    // Update highest used workspace
    if (target_ws > highest_used_workspace_) {
        highest_used_workspace_ = target_ws;
    }

    // Notify
    toaster_->info("Workspace " + std::to_string(target_ws + 1));
    
    // Store current focused window for this workspace
    Window current_focus = layout_engine_->getFocusedWindow();
    if (current_focus != None) {
        workspace_last_focus_[current_workspace_] = current_focus;
    }
    
    // Workspace operations
    hideWorkspaceWindows(current_workspace_);
    current_workspace_ = target_ws;
    layout_engine_->setCurrentWorkspace(target_ws);
    showWorkspaceWindows(target_ws);
    applyLayout();
    updateFocusAfterSwitch();
    layout_engine_->updateBorderColors();
    
    // Update EWMH current desktop
    updateEWMHCurrentWorkspace();
    
    // Update external status bar properties
    updateExternalBarWorkspace();
    
    // Single final sync to ensure everything is committed
    XSync(display_.get(), False);
}

void WindowManager::moveWindowToWorkspace(int workspace, bool follow) {
    // Convert from 1-indexed to 0-indexed if needed
    int target_ws = (workspace > 0) ? workspace - 1 : workspace;
    
    // Check workspace bounds
    if (target_ws < 0) {
        return;
    }
    
    // For non-infinite mode, check upper bound
    if (!infinite_workspaces_ && target_ws >= max_workspaces_) {
        return;
    }
    
    // Expand tracking vectors if needed for infinite workspaces
    if (infinite_workspaces_ && target_ws >= static_cast<int>(workspace_last_focus_.size())) {
        workspace_last_focus_.resize(target_ws + 10, None);
    }
    
    // Update highest used workspace
    if (target_ws > highest_used_workspace_) {
        highest_used_workspace_ = target_ws;
    }
    
    Window focused = layout_engine_->getFocusedWindow();
    if (focused == None) {
        return;
    }
    
    auto it = clients_.find(focused);
    if (it == clients_.end()) {
        return;
    }
    
    int old_workspace = it->second->getWorkspace();
    if (old_workspace == target_ws) {
        return;
    }
    
    
    if (!follow) {
        toaster_->info("Moved to workspace " + std::to_string(target_ws + 1));
    }

    // Remove from current workspace's BSP tree
    Window next_focus = layout_engine_->removeWindow(focused);
    
    // Update window's workspace
    it->second->setWorkspace(target_ws);
    
    // Hide the window (it's not on current workspace anymore)
    // Register as pending unmap BEFORE calling XUnmapWindow to prevent
    // handleUnmapNotify from unmanaging the window
    if (!follow && target_ws != current_workspace_) {
        it->second->setHidden(true);
        pending_unmaps_.insert(focused);
        XUnmapWindow(display_.get(), focused);
    }
    
    // Switch layout engine to target workspace and add window
    int prev_ws = layout_engine_->getCurrentWorkspace();
    layout_engine_->setCurrentWorkspace(target_ws);
    layout_engine_->addWindow(focused);
    layout_engine_->setCurrentWorkspace(prev_ws);
    
    // Update focus on old workspace
    if (next_focus != None) {
        auto next_it = clients_.find(next_focus);
        if (next_it != clients_.end() && next_it->second->getWorkspace() == current_workspace_) {
            XSetInputFocus(display_.get(), next_focus, RevertToPointerRoot, CurrentTime);
            layout_engine_->focusWindow(next_focus);
        }
    }
    
    // Store as last focus for target workspace
    workspace_last_focus_[target_ws] = focused;
    
    // Reapply layout
    applyLayout();
    layout_engine_->updateBorderColors();
    
    // If following, switch to the target workspace
    if (follow) {
        switchWorkspace(target_ws + 1);  // switchWorkspace expects 1-indexed
    }
}

void WindowManager::hideWorkspaceWindows(int workspace) {
    for (auto& [window, managed] : clients_) {
        if (managed->getWorkspace() == workspace && !managed->isHidden()) {
            managed->setHidden(true);
            pending_unmaps_.insert(window);
            XUnmapWindow(display_.get(), window);
        }
    }

    XFlush(display_.get());
}

void WindowManager::showWorkspaceWindows(int workspace) {
    std::vector<Window> windows_to_show;
    
    // Collect all windows on this workspace that should be visible
    // Note: We show windows regardless of hidden state to handle initial workspace switch
    // where windows were never hidden in the first place
    for (auto& [window, managed] : clients_) {
        if (managed->getWorkspace() == workspace) {
            windows_to_show.push_back(window);
        }
    }
    
    // Map and raise all windows
    for (Window window : windows_to_show) {
        auto it = clients_.find(window);
        if (it != clients_.end()) {
            it->second->setHidden(false);
            XMapWindow(display_.get(), window);
            XRaiseWindow(display_.get(), window);
        }
    }
    
    // Use XSync(False) to flush the output buffer and wait for X server processing
    // WITHOUT discarding events (XSync(True) discards events which breaks our event loop)
    XSync(display_.get(), False);
}

void WindowManager::updateFocusAfterSwitch() {
    Window to_focus = findWindowToFocus(current_workspace_);
    
    if (to_focus != None) {
        XSetInputFocus(display_.get(), to_focus, RevertToPointerRoot, CurrentTime);
        layout_engine_->focusWindow(to_focus);
        workspace_last_focus_[current_workspace_] = to_focus;
    } else {
        // No windows on this workspace, focus root
        XSetInputFocus(display_.get(), root_, RevertToPointerRoot, CurrentTime);
    }
}

Window WindowManager::findWindowToFocus(int workspace) {
    // First try the last focused window for this workspace
    Window last = workspace_last_focus_[workspace];
    if (last != None) {
        auto it = clients_.find(last);
        if (it != clients_.end() && it->second->getWorkspace() == workspace) {
            return last;
        }
    }
    
    // Find any window on this workspace
    for (auto& [window, managed] : clients_) {
        if (managed->getWorkspace() == workspace) {
            return window;
        }
    }
    
    return None;
}

int WindowManager::getWorkspaceWindowCount(int workspace) const {
    int count = 0;
    for (const auto& [window, managed] : clients_) {
        if (managed->getWorkspace() == workspace) {
            ++count;
        }
    }
    return count;
}

// ============================================================================
// Multi-Monitor Workspace Management
// ============================================================================

void WindowManager::switchWorkspaceOnMonitor(int workspace, int monitor_id) {
    // Convert from 1-indexed to 0-indexed if needed
    int target_ws = (workspace > 0) ? workspace - 1 : workspace;
    
    // Check workspace bounds
    if (target_ws < 0) {
        return;
    }
    
    // For non-infinite mode, check upper bound
    if (!infinite_workspaces_ && target_ws >= max_workspaces_) {
        return;
    }
    
    // Update current monitor
    current_monitor_ = monitor_id;
    
    // If virtual mapping is enabled, check if this workspace should be on this monitor
    if (virtual_workspace_mapping_) {
        auto it = workspace_to_monitor_.find(target_ws);
        if (it != workspace_to_monitor_.end() && it->second != monitor_id) {

            return;
        }
    }
    
    // For per-monitor workspaces, we need to adjust the workspace index
    if (per_monitor_workspaces_) {
        // In per-monitor mode, workspace index is relative to the monitor
        // So workspace 0 on monitor 0 is different from workspace 0 on monitor 1
        int monitor_count = monitor_manager_ ? static_cast<int>(monitor_manager_->getMonitorCount()) : 1;
        (void)monitor_count;  // Reserved for future use
        target_ws = target_ws + (monitor_id * max_workspaces_);
    }
    
    switchWorkspace(target_ws + 1);  // switchWorkspace expects 1-indexed
}

void WindowManager::setCurrentMonitor(int monitor_id) {
    if (monitor_manager_) {
        size_t monitor_count = monitor_manager_->getMonitorCount();
        if (monitor_id >= 0 && monitor_id < static_cast<int>(monitor_count)) {
            current_monitor_ = monitor_id;
            
            // If per-monitor workspaces and virtual mapping enabled,
            // switch to the workspace mapped to this monitor
            if (per_monitor_workspaces_ || virtual_workspace_mapping_) {
                for (const auto& [ws, mon] : workspace_to_monitor_) {
                    if (mon == monitor_id) {
                        // Found a workspace mapped to this monitor
                        switchWorkspace(ws + 1);
                        break;
                    }
                }
            }
        }
    }
}

void WindowManager::mapWorkspaceToMonitor(int workspace, int monitor_id) {
    // Convert from 1-indexed to 0-indexed if needed
    int ws = (workspace > 0) ? workspace - 1 : workspace;
    
    if (ws >= 0 && monitor_id >= 0) {
        workspace_to_monitor_[ws] = monitor_id;
        virtual_workspace_mapping_ = true;
    }
}

int WindowManager::getWorkspaceMonitor(int workspace) const {
    // Convert from 1-indexed to 0-indexed if needed
    int ws = (workspace > 0) ? workspace - 1 : workspace;
    
    auto it = workspace_to_monitor_.find(ws);
    if (it != workspace_to_monitor_.end()) {
        return it->second;
    }
    return -1;  // Not mapped
}

// ============================================================================
// Focus Navigation
// ============================================================================

void WindowManager::moveFocus(const std::string& direction) {
    Window next = layout_engine_->moveFocus(direction);
    
    if (next != None) {
        auto it = clients_.find(next);
        if (it != clients_.end() && it->second->getWorkspace() == current_workspace_) {
            XSetInputFocus(display_.get(), next, RevertToPointerRoot, CurrentTime);
            workspace_last_focus_[current_workspace_] = next;
            layout_engine_->updateBorderColors();
            
            // Warp pointer to the newly focused window
            is_warping_ = true;
            layout_engine_->warpPointerToWindow(next);
        }
    }
}

void WindowManager::focusWindow(Window window) {
    auto it = clients_.find(window);
    if (it != clients_.end() && it->second->getWorkspace() == current_workspace_) {
        XSetInputFocus(display_.get(), window, RevertToPointerRoot, CurrentTime);
        layout_engine_->focusWindow(window);
        workspace_last_focus_[current_workspace_] = window;
        layout_engine_->updateBorderColors();
        
        // Update external status bar with new window title
        updateExternalBarActiveWindow();
    }
}

// ============================================================================
// Window Movement (within workspace)
// ============================================================================

void WindowManager::swapFocusedWindow(const std::string& direction) {
    layout_engine_->swapFocused(direction);
    applyLayout();
    layout_engine_->updateBorderColors();
}

// ============================================================================
// Window Resizing
// ============================================================================

void WindowManager::resizeFocusedWindow(const std::string& direction) {
    double delta = 0.0;
    
    if (direction == "left") {
        delta = -0.05;
    } else if (direction == "right") {
        delta = 0.05;
    } else if (direction == "up") {
        delta = -0.05;
    } else if (direction == "down") {
        delta = 0.05;
    }
    
    if (delta != 0.0) {
        resizeFocusedWindow(delta);
    }
}

void WindowManager::resizeFocusedWindow(double delta) {
    layout_engine_->resizeFocused(delta);
    applyLayout();
}

// ============================================================================
// Window State
// ============================================================================

void WindowManager::toggleFloating() {
    Window focused = layout_engine_->getFocusedWindow();
    if (focused == None) {
        return;
    }
    
    auto it = clients_.find(focused);
    if (it == clients_.end()) {
        return;
    }
    
    bool is_floating = it->second->isFloating();
    it->second->setFloating(!is_floating);
    
    if (!is_floating) {
        // Switching to floating - store tiled geometry and allow free positioning
        int x, y;
        unsigned int w, h;
        it->second->getGeometry(x, y, w, h);
        it->second->storeTiledGeometry(x, y, w, h);
        
        // Raise the window
        XRaiseWindow(display_.get(), focused);
        toaster_->info("Floating mode");
    } else {
        // Switching back to tiled - restore tiled geometry
        int x, y;
        unsigned int w, h;
        it->second->getTiledGeometry(x, y, w, h);
        
        // Remove from BSP and re-add to get proper placement
        layout_engine_->removeWindow(focused);
        layout_engine_->addWindow(focused);
        
        applyLayout();
        toaster_->info("Tiled mode");
    }
}

void WindowManager::toggleFullscreen() {
    Window focused = layout_engine_->getFocusedWindow();
    if (focused == None) {
        return;
    }
    
    auto it = clients_.find(focused);
    if (it == clients_.end()) {
        return;
    }
    
    bool is_fullscreen = it->second->isFullscreen();
    setFullscreen(focused, !is_fullscreen);
}

void WindowManager::setFullscreen(Window window, bool fullscreen) {
    auto it = clients_.find(window);
    if (it == clients_.end()) {
        return;
    }
    
    it->second->setFullscreen(fullscreen);
    
    if (fullscreen) {
        // Save current geometry
        int x, y;
        unsigned int w, h;
        it->second->getGeometry(x, y, w, h);
        it->second->storeTiledGeometry(x, y, w, h);
        
        // Make fullscreen
        int screen_width = DisplayWidth(display_.get(), screen_);
        int screen_height = DisplayHeight(display_.get(), screen_);
        
        // Remove border
        XWindowChanges changes;
        changes.border_width = 0;
        XConfigureWindow(display_.get(), window, CWBorderWidth, &changes);
        
        // Resize to screen
        XMoveResizeWindow(display_.get(), window, 0, 0, screen_width, screen_height);
        
        // Raise above all
        XRaiseWindow(display_.get(), window);
        
        // Set _NET_WM_STATE_FULLSCREEN
        Atom wm_state = XInternAtom(display_.get(), "_NET_WM_STATE", False);
        Atom fullscreen_atom = XInternAtom(display_.get(), "_NET_WM_STATE_FULLSCREEN", False);
        
        XChangeProperty(display_.get(), window, wm_state, XA_ATOM, 32,
                       PropModeReplace, 
                       reinterpret_cast<unsigned char*>(&fullscreen_atom), 1);
        
        toaster_->info("Fullscreen");
    } else {
        // Restore from fullscreen
        int x, y;
        unsigned int w, h;
        it->second->getTiledGeometry(x, y, w, h);
        
        // Restore border
        XWindowChanges changes;
        changes.border_width = 2;  // Default border width
        XConfigureWindow(display_.get(), window, CWBorderWidth, &changes);
        
        // Remove _NET_WM_STATE_FULLSCREEN
        Atom wm_state = XInternAtom(display_.get(), "_NET_WM_STATE", False);
        XDeleteProperty(display_.get(), window, wm_state);
        
        // Re-apply layout
        applyLayout();
    }
    
    XFlush(display_.get());
}

// ============================================================================
// Layout Management
// ============================================================================

void WindowManager::setLayout(const std::string& layout_name) {
    std::unique_ptr<LayoutVisitor> layout;
    
    if (layout_name == "bsp") {
        BSPLayout::Config config;
        config.gap_size = 10;
        config.border_width = 2;
        
        layout = std::make_unique<BSPLayout>(config);
        toaster_->info("BSP Layout");
    } else if (layout_name == "monocle") {
        layout = std::make_unique<MonocleLayout>();
        toaster_->info("Monocle Layout");
    } else if (layout_name == "masterstack") {
        MasterStackLayout::Config config;
        config.master_ratio = 0.55;
        config.gap_size = 10;
        config.max_master = 1;
        config.border_width = 2;
        config.focused_border_color = 0x00FF00;
        config.unfocused_border_color = 0x808080;
        
        layout = std::make_unique<MasterStackLayout>(config);
        toaster_->info("Master-Stack Layout");
    } else if (layout_name == "centered_master" || layout_name == "centered-master") {
        CenteredMasterLayout::Config config;
        config.gap_size = 10;
        layout = std::make_unique<CenteredMasterLayout>(config);
        toaster_->info("Centered Master Layout");
    } else if (layout_name == "dynamic_grid" || layout_name == "dynamic-grid") {
        DynamicGridLayout::Config config;
        config.gap_size = 10;
        layout = std::make_unique<DynamicGridLayout>(config);
        toaster_->info("Dynamic Grid Layout");
    } else if (layout_name == "dwindle_spiral" || layout_name == "dwindle-spiral") {
        DwindleSpiralLayout::Config config;
        config.gap_size = 10;
        layout = std::make_unique<DwindleSpiralLayout>(config);
        toaster_->info("Dwindle Spiral Layout");
    } else if (layout_name == "tabbed_stacked" || layout_name == "tabbed-stacked" 
               || layout_name == "tabbed" || layout_name == "stacked") {
        TabbedStackedLayout::Config config;
        layout = std::make_unique<TabbedStackedLayout>(config);
        toaster_->info("Tabbed/Stacked Layout");
    } else {
        return;
    }
    
    layout_engine_->setLayout(current_workspace_, std::move(layout));
    applyLayout();
    layout_engine_->updateBorderColors();
    
    // Update external status bar with new layout mode
    updateExternalBarLayoutMode();
}

void WindowManager::cycleLayoutNext() {
    // Determine direction based on configuration
    bool forward = true;
    if (layout_config_parser_) {
        const auto& config = layout_config_parser_->getConfig();
        forward = (config.cycle_direction == LayoutCycleDirection::Forward);
    }
    
    std::string layout_name = layout_engine_->cycleLayout(forward);
    toaster_->info(layout_name + " Layout");
    applyLayout();
    layout_engine_->updateBorderColors();
}

void WindowManager::cycleLayoutPrev() {
    // Determine direction based on configuration (opposite of next)
    bool backward = true;
    if (layout_config_parser_) {
        const auto& config = layout_config_parser_->getConfig();
        backward = (config.cycle_direction == LayoutCycleDirection::Backward);
    }
    
    std::string layout_name = layout_engine_->cycleLayout(!backward);
    toaster_->info(layout_name + " Layout");
    applyLayout();
    layout_engine_->updateBorderColors();
}

void WindowManager::toggleSplitDirection() {
    layout_engine_->toggleSplitDirection();
    applyLayout();
}

// ============================================================================
// Utility Methods
// ============================================================================

void WindowManager::execCommand(const std::string& command) {
    
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        setsid();
        
        // Convert command to args
        std::string cmd = command;
        execl("/bin/sh", "/bin/sh", "-c", cmd.c_str(), nullptr);
        
        // If exec fails
        _exit(1);
    }
}

void WindowManager::killActiveWindow() {
    Window focused = layout_engine_->getFocusedWindow();
    
    if (focused == None) {
        return;
    }
    
    
    // Try graceful close first using WM_DELETE_WINDOW
    Atom wm_protocols = XInternAtom(display_.get(), "WM_PROTOCOLS", True);
    Atom wm_delete = XInternAtom(display_.get(), "WM_DELETE_WINDOW", True);
    
    if (wm_protocols != None && wm_delete != None) {
        // Check if window supports WM_DELETE_WINDOW
        Atom* protocols;
        int num_protocols;
        
        if (XGetWMProtocols(display_.get(), focused, &protocols, &num_protocols)) {
            bool supports_delete = false;
            for (int i = 0; i < num_protocols; ++i) {
                if (protocols[i] == wm_delete) {
                    supports_delete = true;
                    break;
                }
            }
            XFree(protocols);
            
            if (supports_delete) {
                // Send WM_DELETE_WINDOW
                XEvent close_event;
                memset(&close_event, 0, sizeof(close_event));
                close_event.xclient.type = ClientMessage;
                close_event.xclient.window = focused;
                close_event.xclient.message_type = wm_protocols;
                close_event.xclient.format = 32;
                close_event.xclient.data.l[0] = wm_delete;
                close_event.xclient.data.l[1] = CurrentTime;
                
                XSendEvent(display_.get(), focused, False, NoEventMask, &close_event);
                XFlush(display_.get());
                return;
            }
        }
    }
    
    // Fallback: force kill with XKillClient
    XKillClient(display_.get(), focused);
}

Window WindowManager::getFocusedWindow() const {
    return layout_engine_->getFocusedWindow();
}

int WindowManager::onXError(Display* display, XErrorEvent* error) {
    char error_text[1024];
    XGetErrorText(display, error->error_code, error_text, sizeof(error_text));
    
    std::cerr << "X Error: " << error_text << std::endl;
    std::cerr << "  Request: " << static_cast<int>(error->request_code) << std::endl;
    std::cerr << "  Resource: " << error->resourceid << std::endl;
    
    return 0;
}

int WindowManager::onWMDetected(Display* display, XErrorEvent* error) {
    if (error->error_code == BadAccess) {
        wm_detected_ = true;
    }
    return 0;
}

// ============================================================================
// EWMH Helper Methods Implementation
// ============================================================================

void WindowManager::updateEWMHClientList() {
    if (!ewmh_manager_) return;
    
    std::vector<Window> windows;
    windows.reserve(clients_.size());
    
    for (const auto& pair : clients_) {
        windows.push_back(pair.first);
    }
    
    ewmh_manager_->setClientList(windows);
}

void WindowManager::updateEWMHActiveWindow(Window window) {
    if (!ewmh_manager_) return;
    
    ewmh_manager_->setActiveWindow(window);
}

void WindowManager::updateEWMHWorkspaceCount() {
    if (!ewmh_manager_) return;
    
    // Calculate workspace count - use at least max_workspaces_ or current+1 for infinite mode
    int count;
    if (infinite_workspaces_) {
        // For infinite workspaces, show at least the current workspace + 1, or highest used + 1
        count = std::max({highest_used_workspace_ + 1, current_workspace_ + 1, max_workspaces_});
    } else {
        count = max_workspaces_;
    }
    ewmh_manager_->setNumberOfDesktops(count);
    
    // Set desktop names
    std::vector<std::string> names;
    for (int i = 0; i < count; ++i) {
        names.push_back(std::to_string(i + 1));
    }
    ewmh_manager_->setDesktopNames(names);
    
}

void WindowManager::updateEWMHCurrentWorkspace() {
    if (!ewmh_manager_) return;
    
    ewmh_manager_->setCurrentDesktop(current_workspace_);
}

void WindowManager::updateEWMHWindowDesktop(Window window, int desktop) {
    if (!ewmh_manager_) return;
    
    ewmh_manager_->setWindowDesktop(window, desktop);
}

void WindowManager::updateEWMHWindowState(Window window) {
    if (!ewmh_manager_) return;
    
    auto it = clients_.find(window);
    if (it == clients_.end()) return;
    
    std::vector<Atom> states;
    
    if (it->second->isFullscreen()) {
        states.push_back(ewmh_manager_->getAtoms().NET_WM_STATE_FULLSCREEN);
    }
    if (it->second->isHidden()) {
        states.push_back(ewmh_manager_->getAtoms().NET_WM_STATE_HIDDEN);
    }
    
    ewmh_manager_->setWindowState(window, states);
}

// ============================================================================
// External Status Bar Property Updates
// ============================================================================

void WindowManager::updateExternalBarProperties() {
    updateExternalBarWorkspace();
    updateExternalBarActiveWindow();
    updateExternalBarLayoutMode();
}

void WindowManager::updateExternalBarWorkspace() {
    if (!ewmh_manager_) return;
    
    // Update current workspace
    ewmh_manager_->setCurrentWorkspacePB(current_workspace_);
    
    // Build list of occupied workspaces
    std::vector<int> occupied;
    for (const auto& [window, client] : clients_) {
        int ws = client->getWorkspace();
        if (std::find(occupied.begin(), occupied.end(), ws) == occupied.end()) {
            occupied.push_back(ws);
        }
    }
    ewmh_manager_->setOccupiedWorkspacesPB(occupied);
    
    // Build window count per workspace
    int total_workspaces = infinite_workspaces_ ? 
        std::max(highest_used_workspace_ + 1, current_workspace_ + 1) : 
        max_workspaces_;
    std::vector<int> counts(total_workspaces, 0);
    for (const auto& [window, client] : clients_) {
        int ws = client->getWorkspace();
        if (ws >= 0 && ws < static_cast<int>(counts.size())) {
            counts[ws]++;
        }
    }
    ewmh_manager_->setWorkspaceWindowCountsPB(counts);
}

void WindowManager::updateExternalBarActiveWindow() {
    if (!ewmh_manager_) return;
    
    Window focused = layout_engine_->getFocusedWindow();
    if (focused == None) {
        ewmh_manager_->setActiveWindowTitlePB("");
        ewmh_manager_->setActiveWindowClassPB("");
        return;
    }
    
    auto it = clients_.find(focused);
    if (it != clients_.end()) {
        ewmh_manager_->setActiveWindowTitlePB(it->second->getTitle());
        ewmh_manager_->setActiveWindowClassPB(it->second->getClass());
    }
}

void WindowManager::updateExternalBarLayoutMode() {
    if (!ewmh_manager_) return;
    
    // Get current layout mode from layout engine
    auto layout_mode = layout_engine_->getCurrentLayoutMode(current_workspace_);
    
    std::string layout_name;
    if (layout_mode) {
        switch (*layout_mode) {
            case LayoutMode::BSP:
                layout_name = "BSP";
                break;
            case LayoutMode::Monocle:
                layout_name = "Monocle";
                break;
            case LayoutMode::MasterStack:
                layout_name = "MasterStack";
                break;
            case LayoutMode::CenteredMaster:
                layout_name = "Centered";
                break;
            case LayoutMode::DynamicGrid:
                layout_name = "Grid";
                break;
            case LayoutMode::DwindleSpiral:
                layout_name = "Dwindle";
                break;
            case LayoutMode::TabbedStacked:
                layout_name = "Tabbed";
                break;
            default:
                layout_name = "Unknown";
                break;
        }
    }
    
    ewmh_manager_->setLayoutModePB(layout_name);
}

// ============================================================================
// ManagedWindow Implementation
// ============================================================================

ManagedWindow::ManagedWindow(Window window, Display* display)
    : window_(window), display_(display) {
    // Get initial geometry
    Window root;
    unsigned int border_width, depth;
    XGetGeometry(display_, window_, &root, &x_, &y_, &width_, &height_,
                 &border_width, &depth);
    
    // Store as tiled geometry initially
    tiled_x_ = x_;
    tiled_y_ = y_;
    tiled_width_ = width_;
    tiled_height_ = height_;
}

std::string ManagedWindow::getClass() const {
    XClassHint class_hint;
    if (XGetClassHint(display_, window_, &class_hint)) {
        std::string result = class_hint.res_class ? class_hint.res_class : "";
        if (class_hint.res_name) XFree(class_hint.res_name);
        if (class_hint.res_class) XFree(class_hint.res_class);
        return result;
    }
    return "";
}

std::string ManagedWindow::getTitle() const {
    char* name = nullptr;
    XFetchName(display_, window_, &name);
    std::string result = name ? name : "";
    if (name) XFree(name);
    return result;
}

void ManagedWindow::setGeometry(int x, int y, unsigned int width, unsigned int height) {
    x_ = x;
    y_ = y;
    width_ = width;
    height_ = height;
    
    XMoveResizeWindow(display_, window_, x, y, width, height);
}

void ManagedWindow::getGeometry(int& x, int& y, unsigned int& width, unsigned int& height) const {
    x = x_;
    y = y_;
    width = width_;
    height = height_;
}

void ManagedWindow::setFullscreen(bool fullscreen) {
    fullscreen_ = fullscreen;
}

void ManagedWindow::setOpacity(double opacity) {
    unsigned long opacity_value = static_cast<unsigned long>(opacity * 0xFFFFFFFF);
    Atom opacity_atom = XInternAtom(display_, "_NET_WM_WINDOW_OPACITY", False);
    XChangeProperty(display_, window_, opacity_atom, XA_CARDINAL, 32,
                   PropModeReplace, reinterpret_cast<unsigned char*>(&opacity_value), 1);
}

void ManagedWindow::storeTiledGeometry(int x, int y, unsigned int width, unsigned int height) {
    tiled_x_ = x;
    tiled_y_ = y;
    tiled_width_ = width;
    tiled_height_ = height;
}

void ManagedWindow::getTiledGeometry(int& x, int& y, unsigned int& width, unsigned int& height) const {
    x = tiled_x_;
    y = tiled_y_;
    width = tiled_width_;
    height = tiled_height_;
}

} // namespace pblank
