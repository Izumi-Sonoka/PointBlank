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
#include <unordered_set>

namespace pblank {

bool WindowManager::wm_detected_ = false;

WindowManager::WindowManager() = default;

WindowManager::~WindowManager() = default;

void WindowManager::setConfigPath(const std::filesystem::path& path) {
    custom_config_path_ = path;
}

bool WindowManager::initialize() {
    
    Display* raw_display = XOpenDisplay(nullptr);
    if (!raw_display) {
        std::cerr << "Failed to open X display" << std::endl;
        return false;
    }
    
    display_ = DisplayPtr(raw_display, DisplayDeleter{});
    screen_ = DefaultScreen(display_.get());
    root_ = RootWindow(display_.get(), screen_);
    
    
    if (!becomeWindowManager()) {
        std::cerr << "Another window manager is already running" << std::endl;
        return false;
    }
    
    
    ewmh_manager_ = std::make_unique<ewmh::EWMHManager>(display_.get(), root_);
    if (!ewmh_manager_->initialize("Pointblank")) {
        std::cerr << "Warning: EWMH initialization failed" << std::endl;
        
    } else {
        
        
        ewmh_manager_->setDesktopSwitchCallback([this](int desktop) {
            
            switchWorkspace(desktop + 1);
        });
        
        ewmh_manager_->setWindowActionCallback([this](Window window, Atom action) {
            
            if (action == ewmh_manager_->getAtoms().NET_CLOSE_WINDOW) {
                if (clients_.find(window) != clients_.end()) {
                    killActiveWindow();
                }
            }
        });
    }
    
    
    workspace_last_focus_.resize(max_workspaces_, None);
    
    
    toaster_ = std::make_unique<Toaster>(display_.get(), root_);
    if (!toaster_->initialize()) {
        std::cerr << "Failed to initialize Toaster OSD" << std::endl;
        return false;
    }
    
    config_parser_ = std::make_unique<ConfigParser>(toaster_.get());
    
    
    performance_tuner_ = std::make_unique<PerformanceTuner>();
    render_pipeline_ = std::make_unique<RenderPipeline>(display_.get(), root_);
    render_pipeline_->setPerformanceTuner(performance_tuner_.get());
    
    
    layout_engine_ = std::make_unique<LayoutEngine>();
    layout_engine_->setDisplay(display_.get());
    layout_engine_->setRenderPipeline(render_pipeline_.get());
    
    layout_config_parser_ = std::make_unique<LayoutConfigParser>(layout_engine_.get());
    keybind_manager_ = std::make_unique<KeybindManager>();
    monitor_manager_ = std::make_unique<MonitorManager>();
    
    
    window_swallower_ = std::make_unique<WindowSwallower>();
    
    if (!monitor_manager_->initialize(display_.get())) {
        std::cerr << "Failed to initialize MonitorManager - running without multi-monitor support" << std::endl;
    }
    
    
    auto config_path = ConfigParser::getDefaultConfigPath();
    
    if (!loadConfigSafe()) {
        std::cerr << "Config load failed!" << std::endl;
        toaster_->error("Configuration failed - using defaults");
        fallbackToDefaultConfig();
    } else {
        toaster_->success("Point Blank initialized");
        
        
        applyConfigToLayout();
        
        
        const auto& config = config_parser_->getConfig();
        
        std::cerr << "[KEYBIND] Number of keybinds in config: " << config.keybinds.size() << std::endl;
        
        for (const auto& bind : config.keybinds) {
            std::cerr << "[KEYBIND] Registering: key=" << bind.key << " modifiers=" << bind.modifiers << " action=" << bind.action << std::endl;
            
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
        
        
        if (!config.autostart.commands.empty()) {
            std::cerr << "[AUTOSTART] Found " << config.autostart.commands.size() << " commands to execute" << std::endl;
            for (const auto& cmd : config.autostart.commands) {
                std::cerr << "[AUTOSTART] Executing: " << cmd << std::endl;
                
                int result = fork();
                if (result == 0) {
                    
                    
                    dup2(STDOUT_FILENO, STDERR_FILENO);
                    
                    
                    execlp("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
                    
                    
                    int err = errno;
                    std::cerr << "[AUTOSTART]   ERROR: execlp failed with errno " << err << ": " << strerror(err) << std::endl;
                    _exit(1);
                } else if (result > 0) {
                    
                } else {
                    std::cerr << "[AUTOSTART]   ERROR: fork() failed for: " << cmd << std::endl;
                }
            }
        } else {
            std::cerr << "[AUTOSTART] No commands configured" << std::endl;
        }
        
        
        setupConfigWatcher();
    }
    
    
    setupEventMask();
    
    
    setupScratchpadManager();
    
    
    setupIPCServer();
    
    
    updateEWMHWorkspaceCount();
    
    
    XSync(display_.get(), False);
    
    
    scanExistingWindows();
    
    
    updateEWMHClientList();
    
    
    updateExternalBarLayoutMode();
    
    return true;
}

bool WindowManager::becomeWindowManager() {
    
    wm_detected_ = false;
    XSetErrorHandler(&WindowManager::onWMDetected);
    
    
    XSelectInput(display_.get(), root_,
                 SubstructureRedirectMask | SubstructureNotifyMask);
    XSync(display_.get(), False);
    
    if (wm_detected_) {
        return false;
    }
    
    
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
                 ButtonReleaseMask);
    
    
    
    
    
    Cursor default_cursor = XCreateFontCursor(display_.get(), XC_left_ptr);
    XDefineCursor(display_.get(), root_, default_cursor);
    XFlush(display_.get());
}

void WindowManager::setupScratchpadManager() {
    scratchpad_manager_ = std::make_unique<ScratchpadManager>();
}

void WindowManager::setupIPCServer() {
    ipc_server_ = std::make_unique<IPCServer>(display_.get(), root_);
    ipc_server_->start();
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
            
            
            if (clients_.bucket_count() < num_windows) {
                clients_.reserve(num_windows * 2);
            }
            
            
            bool should_float = false;
            if (ewmh_manager_) {
                ewmh::WindowType win_type = ewmh_manager_->getWindowType(top_level_windows[i]);
                
                
                if (win_type == ewmh::WindowType::Dock || 
                    win_type == ewmh::WindowType::Desktop) {

                    
                    
                    if (win_type == ewmh::WindowType::Dock) {
                        ewmh_manager_->registerDockWindow(top_level_windows[i]);
                    }
                    continue;
                }
                
                
                if (ewmh::EWMHManager::isFloatingType(win_type)) {
                    should_float = true;


                }
            }
            
            
            XGrabButton(display_.get(), AnyButton, AnyModifier, top_level_windows[i],
                        False, ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);
            
            
            XSelectInput(display_.get(), top_level_windows[i], 
                        EnterWindowMask | LeaveWindowMask | FocusChangeMask);
            
            
            auto managed = std::make_unique<ManagedWindow>(top_level_windows[i], display_.get());
            managed->setWorkspace(current_workspace_);
            
            if (should_float) {
                managed->setFloating(true);
            } else {
                layout_engine_->addWindow(top_level_windows[i]);
            }
            
            clients_[top_level_windows[i]] = std::move(managed);
            
            
            if (render_pipeline_) {
                auto it = clients_.find(top_level_windows[i]);
                if (it != clients_.end()) {
                    ManagedWindow* managed_win = it->second.get();
                    int x, y;
                    unsigned int w, h;
                    managed_win->getGeometry(x, y, w, h);
                    WindowRenderData data{};
                    data.window = top_level_windows[i];
                    data.x = x;
                    data.y = y;
                    data.width = w;
                    data.height = h;
                    data.border_width = 2;  
                    data.border_color = layout_engine_->getUnfocusedBorderColor();
                    data.flags = WindowRenderData::FLAG_VISIBLE;
                    data.flags |= (managed_win->isFloating()) ? WindowRenderData::FLAG_FLOATING : 0;
                    data.flags |= (managed_win->isFullscreen()) ? WindowRenderData::FLAG_FULLSCREEN : 0;
                    data.opacity = 1.0;  
                    render_pipeline_->updateWindow(data);
                }
            }
        }
    }
    
    XFree(top_level_windows);
    applyLayout();
}

void WindowManager::run() {
    XEvent event;
    
    
    Atom net_active_window = XInternAtom(display_.get(), "_NET_ACTIVE_WINDOW", False);
    
    while (running_) {
        
        auto frame_start = render_pipeline_->beginFrame();
        
        
        toaster_->update();
        
        
        if (XPending(display_.get()) > 0) {
            XNextEvent(display_.get(), &event);
            
            
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
                    
                    if (ewmh_manager_ && ewmh_manager_->handleClientMessage(event.xclient)) {
                        
                        break;
                    }
                    
                    
                    if (event.xclient.message_type == net_active_window) {
                        Window target_window = event.xclient.window;
                        auto it = clients_.find(target_window);
                        if (it != clients_.end()) {
                            int window_workspace = it->second->getWorkspace();
                            
                            
                            if (window_workspace != current_workspace_) {


                                
                                
                                hideWorkspaceWindows(current_workspace_);
                                
                                
                                current_workspace_ = window_workspace;
                                layout_engine_->setCurrentWorkspace(window_workspace);
                                
                                
                                showWorkspaceWindows(window_workspace);
                                
                                
                                applyLayout();
                                
                                
                                updateEWMHCurrentWorkspace();
                                
                                
                                XFlush(display_.get());
                            }
                            
                            
                            XSetInputFocus(display_.get(), target_window, RevertToPointerRoot, CurrentTime);
                            layout_engine_->focusWindow(target_window);
                            layout_engine_->updateBorderColors();
                            workspace_last_focus_[current_workspace_] = target_window;
                            
                            
                            updateEWMHActiveWindow(target_window);
                        }
                    }
                    break;
            }
        } else {
            
            usleep(1000); 
        }
        
        
        render_pipeline_->endFrame();
        
        
        performance_tuner_->endFrame(frame_start);
    }
}

void WindowManager::handleMapRequest(const XMapRequestEvent& event) {
    if (clients_.find(event.window) != clients_.end()) {
        return;
    }
    
    manageWindow(event.window);
}

void WindowManager::manageWindow(Window window) {
    
    if (ewmh_manager_) {
        ewmh::WindowType win_type = ewmh_manager_->getWindowType(window);
        
        
        if (win_type == ewmh::WindowType::Dock || 
            win_type == ewmh::WindowType::Desktop) {
            
            
            if (win_type == ewmh::WindowType::Dock) {
                ewmh_manager_->registerDockWindow(window);
                
                
                applyLayout();
            }
            
            
            XMapWindow(display_.get(), window);
            return;
        }
    }
    
    
    
    {
        XClassHint class_hint;
        if (XGetClassHint(display_.get(), window, &class_hint)) {
            bool is_feh = false;
            if (class_hint.res_name) {
                std::string class_name(class_hint.res_name);
                
                std::transform(class_name.begin(), class_name.end(), class_name.begin(), 
                             [](unsigned char c){ return std::tolower(c); });
                is_feh = (class_name == "feh");
            }
            if (class_hint.res_name) XFree(class_hint.res_name);
            if (class_hint.res_class) XFree(class_hint.res_class);
            
            if (is_feh) {
                
                
                XMapWindow(display_.get(), window);
                
                Window windows[1] = { window };
                XRestackWindows(display_.get(), windows, 1);
                return;
            }
        }
    }
    
    auto managed = std::make_unique<ManagedWindow>(window, display_.get());
    managed->setWorkspace(current_workspace_);
    
    
    const auto& rules = config_parser_->getConfig().window_rules;
    if (rules.opacity) {
        managed->setOpacity(*rules.opacity);
    }
    
    
    bool should_float = false;
    if (ewmh_manager_) {
        ewmh::WindowType win_type = ewmh_manager_->getWindowType(window);
        
        
        if (ewmh::EWMHManager::isFloatingType(win_type)) {
            should_float = true;
            managed->setFloating(true);

        }
        
        
        ewmh_manager_->setWindowDesktop(window, current_workspace_);
        ewmh_manager_->setWindowPID(window, getpid());
    }
    
    
    XGrabButton(display_.get(), AnyButton, AnyModifier, window,
                False, ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);
    
    
    XSelectInput(display_.get(), window, EnterWindowMask | LeaveWindowMask | FocusChangeMask);

    
    if (!should_float) {
        
        if (window_swallower_->isEnabled()) {
            Window swallower = window_swallower_->getSwallowerForWindow(window);
            if (swallower != None) {
                
                should_float = true;
                managed->setFloating(true);
            }
        }
        
        if (!should_float) {
            layout_engine_->addWindow(window);
        } else {
            
            
        }
    }
    
    
    if (window_swallower_->isEnabled()) {
        XClassHint class_hint;
        if (XGetClassHint(display_.get(), window, &class_hint)) {
            if (class_hint.res_name) {
                std::string class_name(class_hint.res_name);
                
                std::transform(class_name.begin(), class_name.end(), class_name.begin(), 
                             [](unsigned char c){ return std::tolower(c); });
                
                
                if (window_swallower_->shouldSwallow(None, window)) {
                    
                    window_swallower_->registerSwallower(window);
                }
            }
            if (class_hint.res_name) XFree(class_hint.res_name);
            if (class_hint.res_class) XFree(class_hint.res_class);
        }
    }
    
    
    XMapWindow(display_.get(), window);
    
    
    XSetInputFocus(display_.get(), window, RevertToPointerRoot, CurrentTime);
    
    
    clients_.emplace(window, std::move(managed));
    
    
    if (render_pipeline_) {
        
        auto it = clients_.find(window);
        if (it != clients_.end()) {
            ManagedWindow* managed_win = it->second.get();
            int x, y;
            unsigned int w, h;
            managed_win->getGeometry(x, y, w, h);
            WindowRenderData data{};
            data.window = window;
            data.x = x;
            data.y = y;
            data.width = w;
            data.height = h;
            data.border_width = 2;  
            data.border_color = layout_engine_->getFocusedBorderColor();
            data.flags = WindowRenderData::FLAG_VISIBLE | WindowRenderData::FLAG_FOCUSED;
            data.flags |= (managed_win->isFloating()) ? WindowRenderData::FLAG_FLOATING : 0;
            data.flags |= (managed_win->isFullscreen()) ? WindowRenderData::FLAG_FULLSCREEN : 0;
            data.opacity = 1.0;  
            render_pipeline_->updateWindow(data);
        }
    }
    
    
    workspace_last_focus_[current_workspace_] = window;
    
    
    applyLayout();
    
    
    layout_engine_->updateBorderColors();
    
    
    updateEWMHClientList();
    
    
    updateEWMHActiveWindow(window);
}

void WindowManager::handleConfigureRequest(const XConfigureRequestEvent& event) {
    auto it = clients_.find(event.window);
    
    if (it != clients_.end() && it->second->isFloating()) {
        
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
    
    
    const bool super_held = (event.state & Mod4Mask) != 0;
    const bool left_click = event.button == Button1;
    const bool right_click = event.button == Button3;
    
    if (super_held && left_click) {
        auto it = clients_.find(event.window);
        if (it != clients_.end() && 
            it->second->getWorkspace() == current_workspace_) {
            
            
            
            
            
            startDrag(event.window, event.x_root, event.y_root);
            
            XGrabPointer(display_.get(), event.window, True,
                        ButtonReleaseMask | PointerMotionMask,
                        GrabModeAsync, GrabModeAsync,
                        None, None, CurrentTime);
            return;
        }
    }
    
    
    if (super_held && right_click) {
        auto it = clients_.find(event.window);
        if (it != clients_.end() && 
            it->second->getWorkspace() == current_workspace_) {
            
            startBidirectionalResize(event.window, event.x_root, event.y_root);
            
            XGrabPointer(display_.get(), event.window, True,
                        ButtonReleaseMask | PointerMotionMask,
                        GrabModeAsync, GrabModeAsync,
                        None, None, CurrentTime);
            return;
        }
    }
    
    
    
    if (floating_resize_enabled_ && left_click && !super_held) {
        auto it = clients_.find(event.window);
        if (it != clients_.end() && 
            it->second->getWorkspace() == current_workspace_ &&
            it->second->isFloating()) {
            
            std::string edge = getEdgeAtPosition(event.window, event.x_root, event.y_root);
            if (!edge.empty()) {
                
                startResize(event.window, event.x_root, event.y_root, edge);
                XGrabPointer(display_.get(), event.window, True,
                            ButtonReleaseMask | PointerMotionMask,
                            GrabModeAsync, GrabModeAsync,
                            None, None, CurrentTime);
                return;
            }
        }
    }
    
    
    auto it = clients_.find(event.window);
    if (it != clients_.end() && it->second->getWorkspace() == current_workspace_) {
        
        XSetInputFocus(display_.get(), event.window, RevertToPointerRoot, CurrentTime);
        layout_engine_->focusWindow(event.window);
        layout_engine_->updateBorderColors();
        workspace_last_focus_[current_workspace_] = event.window;
        
        
        XAllowEvents(display_.get(), ReplayPointer, event.time);
    } else {
        
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
    if (bidirectional_resize_) {
        endBidirectionalResize();
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
    if (bidirectional_resize_) {
        updateBidirectionalResize(event.x_root, event.y_root);
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
    
    
    
    
    Window root_return;
    int win_x, win_y;
    unsigned int width, height, border_width, depth;
    
    
    if (XGetGeometry(display_.get(), window, &root_return, &win_x, &win_y,
                     &width, &height, &border_width, &depth)) {
        
        
        int root_x_translated, root_y_translated;
        Window child_return;
        
        if (XTranslateCoordinates(display_.get(), window, root_,
                                  0, 0,  
                                  &root_x_translated, &root_y_translated,
                                  &child_return)) {
            
            drag_window_start_x_ = root_x_translated;
            drag_window_start_y_ = root_y_translated;
        } else {
            
            drag_window_start_x_ = win_x;
            drag_window_start_y_ = win_y;
        }
    } else {
        
        int x, y;
        it->second->getGeometry(x, y, width, height);
        drag_window_start_x_ = x;
        drag_window_start_y_ = y;
    }
    
    
    drag_was_floating_ = it->second->isFloating();
    
    
    
    
    
    
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
    
    
    int dx = root_x - drag_start_x_;
    int dy = root_y - drag_start_y_;
    
    int new_x = drag_window_start_x_ + dx;
    int new_y = drag_window_start_y_ + dy;
    
    
    int x, y;
    unsigned int width, height;
    it->second->getGeometry(x, y, width, height);
    
    
    
    
    XMoveWindow(display_.get(), drag_window_, new_x, new_y);
    it->second->setGeometry(new_x, new_y, width, height);
    
    
    
    if (!drag_was_floating_) {
        
        Window target_window = findWindowAtPosition(root_x, root_y);
        
        
        if (target_window != None && target_window != drag_window_ && 
            target_window != drag_last_swap_target_) {
            
            auto target_it = clients_.find(target_window);
            if (target_it != clients_.end() && 
                !target_it->second->isFloating() &&
                target_it->second->getWorkspace() == current_workspace_) {
                
                
                
                drag_last_swap_target_ = target_window;
            }
        } else if (target_window == None) {
            
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
        if (!drag_was_floating_ && drag_last_swap_target_ != None) {
            Window swapped = drag_window_;
            layout_engine_->swapWindows(swapped, drag_last_swap_target_);
            applyLayout();
            layout_engine_->focusWindow(swapped);
            layout_engine_->updateBorderColors();
            XSetInputFocus(display_.get(), swapped, RevertToPointerRoot, CurrentTime);
            updateEWMHActiveWindow(swapped);
            workspace_last_focus_[current_workspace_] = swapped;
        } else if (!drag_was_floating_) {
            applyLayout();
            layout_engine_->updateBorderColors();
        }
    }
    
    dragging_ = false;
    drag_window_ = None;
    drag_last_swap_target_ = None;
}

Window WindowManager::findWindowAtPosition(int root_x, int root_y) {
    
    
    for (const auto& [window, managed] : clients_) {
        
        if (window == drag_window_) {
            continue;
        }
        
        
        if (managed->getWorkspace() != current_workspace_) {
            continue;
        }
        
        
        if (managed->isFloating()) {
            continue;
        }
        
        
        Window root_return;
        int win_x, win_y;
        unsigned int width, height, border_width, depth;
        
        if (XGetGeometry(display_.get(), window, &root_return, &win_x, &win_y,
                         &width, &height, &border_width, &depth)) {
            
            int root_x_win, root_y_win;
            Window child_return;
            
            if (XTranslateCoordinates(display_.get(), window, root_,
                                      0, 0,
                                      &root_x_win, &root_y_win,
                                      &child_return)) {
                
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
    
    int edge_size = floating_resize_edge_size_;
    
    
    Window root_return;
    int win_x, win_y;
    unsigned int width, height, border_width, depth;
    
    if (!XGetGeometry(display_.get(), window, &root_return, &win_x, &win_y,
                     &width, &height, &border_width, &depth)) {
        return "";
    }
    
    
    int root_x_win, root_y_win;
    Window child_return;
    
    if (!XTranslateCoordinates(display_.get(), window, root_,
                              0, 0,
                              &root_x_win, &root_y_win,
                              &child_return)) {
        return "";
    }
    
    
    int rel_x = root_x - root_x_win;
    int rel_y = root_y - root_y_win;
    
    
    bool near_left = (rel_x >= 0 && rel_x < edge_size);
    bool near_right = (rel_x >= static_cast<int>(width) - edge_size && rel_x < static_cast<int>(width));
    bool near_top = (rel_y >= 0 && rel_y < edge_size);
    bool near_bottom = (rel_y >= static_cast<int>(height) - edge_size && rel_y < static_cast<int>(height));
    
    
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
    
    
    if (!it->second->isFloating()) {
        return;
    }
    
    resizing_ = true;
    resize_window_ = window;
    resize_start_x_ = root_x;
    resize_start_y_ = root_y;
    resize_edge_ = edge;
    
    
    int x, y;
    unsigned int width, height;
    it->second->getGeometry(x, y, width, height);
    
    
    resize_start_width_ = width;
    resize_start_height_ = height;
    resize_start_window_x_ = x;  
    resize_start_window_y_ = y;
    
    
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
    
    
    int dx = root_x - resize_start_x_;
    int dy = root_y - resize_start_y_;
    
    
    unsigned int min_width = 100;
    unsigned int min_height = 100;
    
    
    
    int new_x = resize_start_window_x_;
    int new_y = resize_start_window_y_;
    int new_width = resize_start_width_;
    int new_height = resize_start_height_;
    
    if (resize_edge_.find("left") != std::string::npos) {
        new_width = std::max(min_width, static_cast<unsigned int>(resize_start_width_ - dx));
        new_x = resize_start_window_x_ + (resize_start_width_ - new_width);
    }
    if (resize_edge_.find("right") != std::string::npos) {
        new_width = std::max(min_width, static_cast<unsigned int>(resize_start_width_ + dx));
    }
    if (resize_edge_.find("top") != std::string::npos) {
        new_height = std::max(min_height, static_cast<unsigned int>(resize_start_height_ - dy));
        new_y = resize_start_window_y_ + (resize_start_height_ - new_height);
    }
    if (resize_edge_.find("bottom") != std::string::npos) {
        new_height = std::max(min_height, static_cast<unsigned int>(resize_start_height_ + dy));
    }
    
    
    XMoveResizeWindow(display_.get(), resize_window_, new_x, new_y, new_width, new_height);
    it->second->setGeometry(new_x, new_y, new_width, new_height);
}

void WindowManager::endResize() {
    if (!resizing_) {
        return;
    }
    
    
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

void WindowManager::startBidirectionalResize(Window window, int root_x, int root_y) {
    auto it = clients_.find(window);
    if (it == clients_.end()) {
        return;
    }
    
    bidirectional_resize_ = true;
    bidirectional_resize_window_ = window;
    bidirectional_resize_start_x_ = root_x;
    bidirectional_resize_start_y_ = root_y;
    
    
    bidirectional_resize_was_floating_ = it->second->isFloating();
    
    
    int x, y;
    unsigned int width, height;
    it->second->getGeometry(x, y, width, height);
    bidirectional_resize_window_x_ = x;
    bidirectional_resize_window_y_ = y;
    bidirectional_resize_window_width_ = width;
    bidirectional_resize_window_height_ = height;
    
    
    XRaiseWindow(display_.get(), window);
    
    
    layout_engine_->setResizeBorderHighlight(true, window);
}

void WindowManager::updateBidirectionalResize(int root_x, int root_y) {
    if (!bidirectional_resize_ || bidirectional_resize_window_ == None) {
        return;
    }
    
    auto it = clients_.find(bidirectional_resize_window_);
    if (it == clients_.end()) {
        return;
    }
    
    
    int dx = root_x - bidirectional_resize_start_x_;
    int dy = root_y - bidirectional_resize_start_y_;
    
    
    bool is_floating = it->second->isFloating();
    
    if (!is_floating) {
        
        
        
        
        
        const double resize_sensitivity = 0.015;
        
        
        double delta_x = dx * resize_sensitivity;
        double delta_y = dy * resize_sensitivity;
        
        
        
        
        
        if (std::abs(delta_x) > 0.001) {
            
            
            double horizontal_delta = (dx > 0) ? -delta_x : delta_x;
            layout_engine_->resizeWindow(bidirectional_resize_window_, horizontal_delta);
        }
        
        
        if (std::abs(delta_y) > 0.001) {
            
            
            double vertical_delta = (dy > 0) ? -delta_y : delta_y;
            layout_engine_->resizeWindow(bidirectional_resize_window_, vertical_delta);
        }
        
        
        applyLayout();
        layout_engine_->updateBorderColors();
        return;
    }
    
    
    
    
    
    
    
    
    
    
    const unsigned int min_width = 100;
    const unsigned int min_height = 100;
    
    
    unsigned int max_width = 32767;  
    unsigned int max_height = 32767;
    
    
    int initial_x = bidirectional_resize_window_x_;
    int initial_y = bidirectional_resize_window_y_;
    unsigned int initial_width = bidirectional_resize_window_width_;
    unsigned int initial_height = bidirectional_resize_window_height_;
    
    
    
    
    int new_x = initial_x;
    int new_y = initial_y;
    unsigned int new_width = initial_width;
    unsigned int new_height = initial_height;
    
    
    if (dx != 0) {
        int potential_width = static_cast<int>(initial_width) + dx;
        new_width = std::max(min_width, std::min(max_width, static_cast<unsigned int>(potential_width)));
        
        
        int width_delta = static_cast<int>(new_width) - static_cast<int>(initial_width);
        
        
        
        
        if (dx < 0) {
            
            
            new_x = initial_x + width_delta;
        }
        
    }
    
    
    if (dy != 0) {
        int potential_height = static_cast<int>(initial_height) + dy;
        new_height = std::max(min_height, std::min(max_height, static_cast<unsigned int>(potential_height)));
        
        
        int height_delta = static_cast<int>(new_height) - static_cast<int>(initial_height);
        
        
        
        
        if (dy < 0) {
            
            
            new_y = initial_y + height_delta;
        }
        
    }
    
    
    XMoveResizeWindow(display_.get(), bidirectional_resize_window_, new_x, new_y, new_width, new_height);
    it->second->setGeometry(new_x, new_y, new_width, new_height);
}

void WindowManager::endBidirectionalResize() {
    if (!bidirectional_resize_) {
        return;
    }
    
    
    layout_engine_->setResizeBorderHighlight(false, None);
    
    
    if (bidirectional_resize_window_ != None) {
        auto it = clients_.find(bidirectional_resize_window_);
        if (it != clients_.end()) {
            int x, y;
            unsigned int width, height;
            it->second->getGeometry(x, y, width, height);
            it->second->storeTiledGeometry(x, y, width, height);
        }
    }
    
    bidirectional_resize_ = false;
    bidirectional_resize_window_ = None;
}

void WindowManager::handleDestroyNotify(const XDestroyWindowEvent& event) {
    unmanageWindow(event.window);
}

void WindowManager::unmanageWindow(Window window) {
    auto it = clients_.find(window);
    if (it == clients_.end()) {
        return;
    }
    
    
    pending_unmaps_.erase(window);
    
    int ws = it->second->getWorkspace();
    
    
    Window next_focus = layout_engine_->removeWindow(window);
    
    
    if (workspace_last_focus_[ws] == window) {
        workspace_last_focus_[ws] = next_focus;
    }
    
    clients_.erase(it);
    
    
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
    
    
    updateEWMHClientList();
}

void WindowManager::handleUnmapNotify(const XUnmapEvent& event) {
    
    if (event.send_event) {
        return;
    }
    
    
    
    if (pending_unmaps_.count(event.window)) {
        pending_unmaps_.erase(event.window);
        return;
    }
    
    unmanageWindow(event.window);
}

void WindowManager::handleEnterNotify(const XCrossingEvent& event) {
    
    if (event.mode != NotifyNormal && event.mode != NotifyUngrab) {
        return;
    }
    
    
    if (event.detail == NotifyInferior) {
        return;
    }
    
    
    if (is_warping_) {
        is_warping_ = false;  
        return;
    }
    
    if (!focus_follows_mouse_) {
        return;  
    }
    
    
    if (monitor_focus_follows_mouse_ && monitor_manager_) {
        
        Window root_return, child_return;
        int root_x, root_y, win_x, win_y;
        unsigned int mask_return;
        
        if (XQueryPointer(display_.get(), root_, &root_return, &child_return, 
                          &root_x, &root_y, &win_x, &win_y, &mask_return)) {
            const auto* monitor = monitor_manager_->getMonitorAt(root_x, root_y);
            if (monitor && monitor->id != current_monitor_id_) {
                current_monitor_id_ = monitor->id;
                

                
                
                if (monitor_manager_->getMonitorCount() > 1) {
                    
                    int target_workspace = monitor->id;
                    
                    
                    if (target_workspace != current_workspace_ && 
                        target_workspace < max_workspaces_) {

                        switchWorkspace(target_workspace);
                    }
                }
            }
        }
    }
    
    
    auto it = clients_.find(event.window);
    if (it != clients_.end() && it->second->getWorkspace() == current_workspace_) {
        
        Window current_focus = layout_engine_->getFocusedWindow();
        if (current_focus == event.window) {
            return;
        }
        
        
        XRaiseWindow(display_.get(), event.window);
        
        
        XSetInputFocus(display_.get(), event.window, RevertToPointerRoot, CurrentTime);
        
        
        layout_engine_->focusWindow(event.window);
        layout_engine_->updateBorderColors();
        
        
        workspace_last_focus_[current_workspace_] = event.window;
        
        
        XFlush(display_.get());
    }
}

void WindowManager::handleFocusIn(const XFocusChangeEvent& event) {
    
    if (event.mode == NotifyGrab || event.mode == NotifyUngrab ||
        event.mode == NotifyWhileGrabbed) {
        return;
    }
    
    auto it = clients_.find(event.window);
    if (it != clients_.end()) {
        int window_workspace = it->second->getWorkspace();
        
        
        
        if (window_workspace != current_workspace_) {
            
            int target_ws = window_workspace;
            
            
            hideWorkspaceWindows(current_workspace_);
            
            
            current_workspace_ = target_ws;
            layout_engine_->setCurrentWorkspace(target_ws);
            
            
            showWorkspaceWindows(target_ws);
            
            
            applyLayout();
            
            
            XFlush(display_.get());
        }
        
        
        layout_engine_->focusWindow(event.window);
        layout_engine_->updateBorderColors();
        workspace_last_focus_[current_workspace_] = event.window;
    }
}

void WindowManager::handlePropertyNotify(const XPropertyEvent& event) {
    auto it = clients_.find(event.window);
    if (it != clients_.end()) {
        
    }
}

void WindowManager::applyLayout() {
    
    int screen_width = DisplayWidth(display_.get(), screen_);
    int screen_height = DisplayHeight(display_.get(), screen_);
    
    
    unsigned long left_strut = 0, right_strut = 0, top_strut = 0, bottom_strut = 0;
    
    if (ewmh_manager_) {
        auto struts = ewmh_manager_->getCombinedStruts(screen_width, screen_height);
        left_strut = struts.left;
        right_strut = struts.right;
        top_strut = struts.top;
        bottom_strut = struts.bottom;
        
        
        ewmh_manager_->updateWorkarea(screen_width, screen_height,
                                      left_strut, right_strut, top_strut, bottom_strut);
    }
    
    
    Rect screen_bounds{
        static_cast<int>(left_strut), 
        static_cast<int>(top_strut),
        static_cast<unsigned int>(screen_width - left_strut - right_strut),
        static_cast<unsigned int>(screen_height - top_strut - bottom_strut)
    };
    
    
    
    std::unordered_set<Window> floating_windows;
    for (const auto& [window, client] : clients_) {
        if (client->getWorkspace() == current_workspace_ && client->isFloating()) {
            floating_windows.insert(window);
        }
    }
    
    
    layout_engine_->setFloatingWindows(floating_windows);
    
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
    
    
    if (!config_parser_->loadFromString(ConfigParser::getEmbeddedConfig())) {
        std::cerr << "[ERROR] Failed to parse embedded config!" << std::endl;
        return;
    }
    
    
    
    applyConfigToLayout();
    
    
    if (keybind_manager_) {
        keybind_manager_->clearKeybinds();
        const auto& config = config_parser_->getConfig();
        for (const auto& bind : config.keybinds) {
            std::string keybind_str;
            std::string action;
            
            
            if (!bind.modifiers.empty()) {
                keybind_str = bind.modifiers + ", " + bind.key;
            } else {
                keybind_str = bind.key;
            }
            
            
            if (bind.exec_command.has_value()) {
                action = "exec: " + *bind.exec_command;
            } else {
                action = bind.action;
            }
            
            keybind_manager_->registerKeybind(keybind_str, action);
        }
        
        
        if (display_ && root_ != None) {
            keybind_manager_->grabKeys(display_.get(), root_);
        }
    }
}

void WindowManager::applyConfigToLayout() {
    const auto& config = config_parser_->getConfig();
    
    
    
    focus_follows_mouse_ = config.focus_follows_mouse;
    
    
    monitor_focus_follows_mouse_ = config.monitor_focus_follows_mouse;
    
    
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

    
    infinite_workspaces_ = config.workspaces.infinite;
    max_workspaces_ = config.workspaces.max_workspaces;
    dynamic_workspace_creation_ = config.workspaces.dynamic_creation;
    auto_remove_empty_workspaces_ = config.workspaces.auto_remove;
    min_persist_workspaces_ = config.workspaces.min_persist;
    
    
    per_monitor_workspaces_ = config.workspaces.per_monitor;
    virtual_workspace_mapping_ = config.workspaces.virtual_mapping;
    workspace_to_monitor_ = config.workspaces.workspace_to_monitor;
    
    
    if (!workspace_to_monitor_.empty()) {
        for ([[maybe_unused]] const auto& [ws, mon] : workspace_to_monitor_) {
        }
    }
    
    
    if (per_monitor_workspaces_ && monitor_manager_) {
        size_t monitor_count = monitor_manager_->getMonitorCount();
        per_monitor_last_focus_.resize(monitor_count);
        for (auto& monitor_focus : per_monitor_last_focus_) {
            monitor_focus.resize(max_workspaces_, None);
        }
    }
    
    
    if (!infinite_workspaces_ && workspace_last_focus_.size() < static_cast<size_t>(max_workspaces_)) {
        workspace_last_focus_.resize(max_workspaces_, None);
    }

    
    
    
    auto_resize_non_docks_ = config.windows.auto_resize_non_docks;
    floating_resize_enabled_ = config.windows.floating_resize_enabled;
    floating_resize_edge_size_ = config.windows.floating_resize_edge_size;
    

    
    
    
    
    int top_gap = config.layout_gaps.top_gap == 0 ? -1 : config.layout_gaps.top_gap;
    int bottom_gap = config.layout_gaps.bottom_gap == 0 ? -1 : config.layout_gaps.bottom_gap;
    int left_gap = config.layout_gaps.left_gap == 0 ? -1 : config.layout_gaps.left_gap;
    int right_gap = config.layout_gaps.right_gap == 0 ? -1 : config.layout_gaps.right_gap;
    
    layout_engine_->setGapSize(config.layout_gaps.inner_gap);
    layout_engine_->setOuterGap(config.layout_gaps.outer_gap);
    layout_engine_->setEdgeGaps(
        top_gap,
        bottom_gap,
        left_gap,
        right_gap
    );
    layout_engine_->setBorderWidth(2);  
    
    
    
    unsigned long focused_color = 0x89B4FA;    
    unsigned long unfocused_color = 0x45475A;  
    
    
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
    
    
    auto config_path = ConfigParser::getDefaultConfigPath();
    auto config_dir = config_path.parent_path();
    
    
    config_watcher_->setValidationCallback([this](const std::filesystem::path& path) {
        ValidationResult result;
        
        
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
    
    
    config_watcher_->setApplyCallback([this](const std::filesystem::path& path) {
        
        
        if (loadConfigSafe()) {
            
            toaster_->clearConfigErrors();
            
            applyConfigToLayout();
            applyLayout();
            layout_engine_->updateBorderColors();
            
            
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
    
    
    config_watcher_->setErrorCallback([this](const ValidationResult& result) {
        
        toaster_->clearConfigErrors();
        
        for (const auto& err : result.errors) {
            toaster_->configError("Config error: " + err);
        }
        for (const auto& loc : result.error_locations) {
            toaster_->configError("Line " + std::to_string(loc.line) + ": " + loc.message);
        }
    });
    
    
    config_watcher_->setNotifyCallback([this](const std::string& message, const std::string& level) {
        if (level == "info") {
            toaster_->info(message);
        } else if (level == "success") {
            toaster_->success(message);
        } else if (level == "error") {
            toaster_->error(message);
        }
    });
    
    
    if (!config_watcher_->addWatch(config_dir)) {
        std::cerr << "Failed to add config watch" << std::endl;
    }
    
    
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





void WindowManager::switchWorkspace(int workspace) {
    
    int target_ws = (workspace > 0) ? workspace - 1 : workspace;
    
    
    if (target_ws < 0) {
        return;
    }
    
    
    if (!infinite_workspaces_ && target_ws >= max_workspaces_) {
        return;
    }
    
    if (target_ws == current_workspace_) {
        return;
    }
    

    
    if (auto_remove_empty_workspaces_ && current_workspace_ >= min_persist_workspaces_) {
        int window_count = getWorkspaceWindowCount(current_workspace_);
        if (window_count == 0 && current_workspace_ > highest_used_workspace_) {
            
            if (workspace_last_focus_.size() > static_cast<size_t>(current_workspace_ + 1)) {
                workspace_last_focus_.resize(current_workspace_ + 1);
            }
        }
    }
    
    
    if (infinite_workspaces_ && target_ws >= static_cast<int>(workspace_last_focus_.size())) {
        workspace_last_focus_.resize(target_ws + 10, None);  
    }
    
    
    if (target_ws > highest_used_workspace_) {
        highest_used_workspace_ = target_ws;
    }
    
    
    Window current_focus = layout_engine_->getFocusedWindow();
    if (current_focus != None) {
        workspace_last_focus_[current_workspace_] = current_focus;
    }
    
    
    hideWorkspaceWindows(current_workspace_);
    current_workspace_ = target_ws;
    layout_engine_->setCurrentWorkspace(target_ws);
    showWorkspaceWindows(target_ws);
    applyLayout();
    updateFocusAfterSwitch();
    layout_engine_->updateBorderColors();
    
    
    updateEWMHCurrentWorkspace();
    
    
    updateExternalBarWorkspace();
    
    
    XSync(display_.get(), False);
}

void WindowManager::moveWindowToWorkspace(int workspace, bool follow) {
    
    int target_ws = (workspace > 0) ? workspace - 1 : workspace;
    
    
    if (target_ws < 0) {
        return;
    }
    
    
    if (!infinite_workspaces_ && target_ws >= max_workspaces_) {
        return;
    }
    
    
    if (infinite_workspaces_ && target_ws >= static_cast<int>(workspace_last_focus_.size())) {
        workspace_last_focus_.resize(target_ws + 10, None);
    }
    
    
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

    
    Window next_focus = layout_engine_->removeWindow(focused);
    
    
    it->second->setWorkspace(target_ws);
    
    
    
    
    if (!follow && target_ws != current_workspace_) {
        it->second->setHidden(true);
        pending_unmaps_.insert(focused);
        XUnmapWindow(display_.get(), focused);
    }
    
    
    int prev_ws = layout_engine_->getCurrentWorkspace();
    layout_engine_->setCurrentWorkspace(target_ws);
    layout_engine_->addWindow(focused);
    layout_engine_->setCurrentWorkspace(prev_ws);
    
    
    if (next_focus != None) {
        auto next_it = clients_.find(next_focus);
        if (next_it != clients_.end() && next_it->second->getWorkspace() == current_workspace_) {
            XSetInputFocus(display_.get(), next_focus, RevertToPointerRoot, CurrentTime);
            layout_engine_->focusWindow(next_focus);
        }
    }
    
    
    workspace_last_focus_[target_ws] = focused;
    
    
    applyLayout();
    layout_engine_->updateBorderColors();
    
    
    if (follow) {
        switchWorkspace(target_ws + 1);  
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
    
    
    
    
    for (auto& [window, managed] : clients_) {
        if (managed->getWorkspace() == workspace) {
            windows_to_show.push_back(window);
        }
    }
    
    
    for (Window window : windows_to_show) {
        auto it = clients_.find(window);
        if (it != clients_.end()) {
            it->second->setHidden(false);
            XMapWindow(display_.get(), window);
            XRaiseWindow(display_.get(), window);
        }
    }
    
    
    
    XSync(display_.get(), False);
}

void WindowManager::updateFocusAfterSwitch() {
    Window to_focus = findWindowToFocus(current_workspace_);
    
    if (to_focus != None) {
        XSetInputFocus(display_.get(), to_focus, RevertToPointerRoot, CurrentTime);
        layout_engine_->focusWindow(to_focus);
        workspace_last_focus_[current_workspace_] = to_focus;
    } else {
        
        XSetInputFocus(display_.get(), root_, RevertToPointerRoot, CurrentTime);
    }
    
    
    updateAllBorders();
}

void WindowManager::updateAllBorders() {
    
    layout_engine_->updateBorderColors();
    
    
    Window focused = getFocusedWindow();
    for (const auto& [window, managed] : clients_) {
        if (managed->getWorkspace() == current_workspace_ && managed->isFloating()) {
            unsigned long color = (window == focused) ? 
                layout_engine_->getFocusedBorderColor() : 
                layout_engine_->getUnfocusedBorderColor();
            XSetWindowBorder(display_.get(), window, color);
        }
    }
    XFlush(display_.get());
}

Window WindowManager::findWindowToFocus(int workspace) {
    
    Window last = workspace_last_focus_[workspace];
    if (last != None) {
        auto it = clients_.find(last);
        if (it != clients_.end() && it->second->getWorkspace() == workspace) {
            return last;
        }
    }
    
    
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





void WindowManager::switchWorkspaceOnMonitor(int workspace, int monitor_id) {
    
    int target_ws = (workspace > 0) ? workspace - 1 : workspace;
    
    
    if (target_ws < 0) {
        return;
    }
    
    
    if (!infinite_workspaces_ && target_ws >= max_workspaces_) {
        return;
    }
    
    
    current_monitor_ = monitor_id;
    
    
    if (virtual_workspace_mapping_) {
        auto it = workspace_to_monitor_.find(target_ws);
        if (it != workspace_to_monitor_.end() && it->second != monitor_id) {

            return;
        }
    }
    
    
    if (per_monitor_workspaces_) {
        
        
        int monitor_count = monitor_manager_ ? static_cast<int>(monitor_manager_->getMonitorCount()) : 1;
        (void)monitor_count;  
        target_ws = target_ws + (monitor_id * max_workspaces_);
    }
    
    switchWorkspace(target_ws + 1);  
}

void WindowManager::setCurrentMonitor(int monitor_id) {
    if (monitor_manager_) {
        size_t monitor_count = monitor_manager_->getMonitorCount();
        if (monitor_id >= 0 && monitor_id < static_cast<int>(monitor_count)) {
            current_monitor_ = monitor_id;
            
            
            
            if (per_monitor_workspaces_ || virtual_workspace_mapping_) {
                for (const auto& [ws, mon] : workspace_to_monitor_) {
                    if (mon == monitor_id) {
                        
                        switchWorkspace(ws + 1);
                        break;
                    }
                }
            }
        }
    }
}

void WindowManager::mapWorkspaceToMonitor(int workspace, int monitor_id) {
    
    int ws = (workspace > 0) ? workspace - 1 : workspace;
    
    if (ws >= 0 && monitor_id >= 0) {
        workspace_to_monitor_[ws] = monitor_id;
        virtual_workspace_mapping_ = true;
    }
}

int WindowManager::getWorkspaceMonitor(int workspace) const {
    
    int ws = (workspace > 0) ? workspace - 1 : workspace;
    
    auto it = workspace_to_monitor_.find(ws);
    if (it != workspace_to_monitor_.end()) {
        return it->second;
    }
    return -1;  
}





void WindowManager::moveFocus(const std::string& direction) {
    Window next = layout_engine_->moveFocus(direction);
    
    if (next != None) {
        auto it = clients_.find(next);
        if (it != clients_.end() && it->second->getWorkspace() == current_workspace_) {
            XSetInputFocus(display_.get(), next, RevertToPointerRoot, CurrentTime);
            workspace_last_focus_[current_workspace_] = next;
            layout_engine_->updateBorderColors();
            
            
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
        
        
        updateExternalBarActiveWindow();
    }
}





void WindowManager::swapFocusedWindow(const std::string& direction) {
    layout_engine_->swapFocused(direction);
    applyLayout();
    layout_engine_->updateBorderColors();
}





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
        
        int x, y;
        unsigned int w, h;
        it->second->getGeometry(x, y, w, h);
        it->second->storeTiledGeometry(x, y, w, h);
        
        
        XRaiseWindow(display_.get(), focused);
        toaster_->info("Floating mode");
    } else {
        
        int x, y;
        unsigned int w, h;
        it->second->getTiledGeometry(x, y, w, h);
        
        
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
        
        int x, y;
        unsigned int w, h;
        it->second->getGeometry(x, y, w, h);
        it->second->storeTiledGeometry(x, y, w, h);
        
        
        int screen_width = DisplayWidth(display_.get(), screen_);
        int screen_height = DisplayHeight(display_.get(), screen_);
        
        
        XWindowChanges changes;
        changes.border_width = 0;
        XConfigureWindow(display_.get(), window, CWBorderWidth, &changes);
        
        
        XMoveResizeWindow(display_.get(), window, 0, 0, screen_width, screen_height);
        
        
        XRaiseWindow(display_.get(), window);
        
        
        Atom wm_state = XInternAtom(display_.get(), "_NET_WM_STATE", False);
        Atom fullscreen_atom = XInternAtom(display_.get(), "_NET_WM_STATE_FULLSCREEN", False);
        
        XChangeProperty(display_.get(), window, wm_state, XA_ATOM, 32,
                       PropModeReplace, 
                       reinterpret_cast<unsigned char*>(&fullscreen_atom), 1);
        
        toaster_->info("Fullscreen");
    } else {
        
        int x, y;
        unsigned int w, h;
        it->second->getTiledGeometry(x, y, w, h);
        
        
        XWindowChanges changes;
        changes.border_width = 2;  
        XConfigureWindow(display_.get(), window, CWBorderWidth, &changes);
        
        
        Atom wm_state = XInternAtom(display_.get(), "_NET_WM_STATE", False);
        XDeleteProperty(display_.get(), window, wm_state);
        
        
        applyLayout();
    }
    
    XFlush(display_.get());
}





void WindowManager::showScratchpad() {
    if (!scratchpad_manager_ || scratchpad_manager_->count() == 0) {
        return;
    }
    
    
    if (!scratchpad_manager_->getWindows().empty()) {
        
        scratchpad_manager_->showScratchpad();
    }
}

void WindowManager::showScratchpadNext() {
    if (!scratchpad_manager_) {
        return;
    }
    scratchpad_manager_->showScratchpadNext();
}

void WindowManager::showScratchpadPrevious() {
    if (!scratchpad_manager_) {
        return;
    }
    scratchpad_manager_->showScratchpadPrevious();
}

void WindowManager::hideToScratchpad() {
    if (!scratchpad_manager_) {
        return;
    }
    
    Window focused = layout_engine_->getFocusedWindow();
    if (focused == None) {
        return;
    }
    
    auto it = clients_.find(focused);
    if (it == clients_.end()) {
        return;
    }
    
    
    int x, y;
    unsigned int width, height;
    it->second->getGeometry(x, y, width, height);
    
    
    int workspace = it->second->getWorkspace();
    
    
    bool was_floating = it->second->isFloating();
    
    
    scratchpad_manager_->hideToScratchpad(focused, workspace, x, y, width, height, was_floating);
    
    
    unmanageWindow(focused);
    
    
    applyLayout();
}





void WindowManager::setLayout(const std::string& layout_name) {
    std::unique_ptr<LayoutVisitor> layout;
    
    
    int gap_size = 10;
    if (layout_engine_) {
        
        
        gap_size = layout_engine_->getGapSize();
    }
    
    if (layout_name == "bsp") {
        BSPLayout::Config config;
        config.gap_size = gap_size;  
        config.border_width = 2;
        
        layout = std::make_unique<BSPLayout>(config);
        toaster_->info("BSP Layout");
    } else if (layout_name == "monocle") {
        layout = std::make_unique<MonocleLayout>();
        toaster_->info("Monocle Layout");
    } else if (layout_name == "masterstack") {
        MasterStackLayout::Config config;
        config.master_ratio = 0.55;
        config.gap_size = gap_size;  
        config.max_master = 1;
        config.border_width = 2;
        config.focused_border_color = 0x89B4FA;   
        config.unfocused_border_color = 0x45475A; 
        
        layout = std::make_unique<MasterStackLayout>(config);
        toaster_->info("Master-Stack Layout");
    } else if (layout_name == "centered_master" || layout_name == "centered-master") {
        CenteredMasterLayout::Config config;
        config.gap_size = gap_size;  
        layout = std::make_unique<CenteredMasterLayout>(config);
        toaster_->info("Centered Master Layout");
    } else if (layout_name == "dynamic_grid" || layout_name == "dynamic-grid") {
        DynamicGridLayout::Config config;
        config.gap_size = gap_size;  
        layout = std::make_unique<DynamicGridLayout>(config);
        toaster_->info("Dynamic Grid Layout");
    } else if (layout_name == "dwindle_spiral" || layout_name == "dwindle-spiral") {
        DwindleSpiralLayout::Config config;
        config.gap_size = gap_size;  
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
    
    
    updateExternalBarLayoutMode();
}

void WindowManager::cycleLayoutNext() {
    
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





void WindowManager::execCommand(const std::string& command) {
    
    pid_t pid = fork();
    if (pid == 0) {
        
        setsid();
        
        
        std::string cmd = command;
        execl("/bin/sh", "/bin/sh", "-c", cmd.c_str(), nullptr);
        
        
        _exit(1);
    }
}

void WindowManager::killActiveWindow() {
    Window focused = layout_engine_->getFocusedWindow();
    
    if (focused == None) {
        return;
    }
    
    
    
    Atom wm_protocols = XInternAtom(display_.get(), "WM_PROTOCOLS", True);
    Atom wm_delete = XInternAtom(display_.get(), "WM_DELETE_WINDOW", True);
    
    if (wm_protocols != None && wm_delete != None) {
        
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
    
    
    int count;
    if (infinite_workspaces_) {
        
        count = std::max({highest_used_workspace_ + 1, current_workspace_ + 1, max_workspaces_});
    } else {
        count = max_workspaces_;
    }
    ewmh_manager_->setNumberOfDesktops(count);
    
    
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





void WindowManager::updateExternalBarProperties() {
    updateExternalBarWorkspace();
    updateExternalBarActiveWindow();
    updateExternalBarLayoutMode();
}

void WindowManager::updateExternalBarWorkspace() {
    if (!ewmh_manager_) return;
    
    
    ewmh_manager_->setCurrentWorkspacePB(current_workspace_);
    
    
    std::vector<int> occupied;
    for (const auto& [window, client] : clients_) {
        int ws = client->getWorkspace();
        if (std::find(occupied.begin(), occupied.end(), ws) == occupied.end()) {
            occupied.push_back(ws);
        }
    }
    ewmh_manager_->setOccupiedWorkspacesPB(occupied);
    
    
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
    
    
    std::filesystem::create_directories("/tmp/pointblank");
    std::ofstream layout_file("/tmp/pointblank/currentlayout");
    if (layout_file.is_open()) {
        layout_file << layout_name << std::endl;
        layout_file.close();
    }
}





ManagedWindow::ManagedWindow(Window window, Display* display)
    : window_(window), display_(display) {
    
    Window root;
    unsigned int border_width, depth;
    XGetGeometry(display_, window_, &root, &x_, &y_, &width_, &height_,
                 &border_width, &depth);
    
    
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

} 
