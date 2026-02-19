# Point Blank - Infinite X11 Tiling Window Manager

A modern, DSL-configured tiling window manager for X11 with "Infinite Layouts", "Infinite Workspace" and crash-proof configuration system.

## Features

- **DSL Configuration**: `.wmi` (Window Manager Interface) files with QML-like syntax
- **Crash-Proof Design**: Visual OSD "Toaster" + D-Bus notifications for errors
- **8 Layout Modes**: BSP, Monocle, Master-Stack, Centered Master, Dynamic Grid, Dwindle Spiral, Tabbed Stacked, Infinite Canvas
- **Full EWMH Compliance**: 50+ atoms supported for desktop environment compatibility
- **Extension System v2.0**: Shared object loader with ABI validation and hook system
- **Multi-Monitor Support**: XRandR query and event handling for dynamic changes
- **Modern C++20**: RAII everywhere, no raw pointers, `std::variant` AST
- **Preprocessor System**: `#import` and `#include` for modular configs
- **Conditional Rules**: Per-window configuration with if statements
- **Infinite Workspaces**: Dynamic workspace creation with auto-remove
- **Hot-Reload**: Automatic configuration reload on file changes

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                      Point Blank WM                         │
├─────────────────────────────────────────────────────────────┤
│  WindowManager (Event Loop & Orchestration)                 │
│    ├─ X11 Event Handling (MapRequest, KeyPress, etc.)       │
│    ├─ Client Management (std::unordered_map<Window, ...>)   │
│    └─ Safe Config Loading with Fallback                     │
├─────────────────────────────────────────────────────────────┤
│  ConfigParser (DSL Interpreter)                             │
│    ├─ Lexer: Tokenization of .wmi files                     │
│    ├─ Parser: Recursive Descent → AST (std::variant)       │
│    ├─ Interpreter: AST → Runtime Config                    │
│    ├─ Preprocessor: #import / #include resolution           │
│    └─ Conditional Rules: if statements with member access   │
├─────────────────────────────────────────────────────────────┤
│  LayoutEngine (BSP Tree & Layout Visitors)                  │
│    ├─ BSPNode: Binary tree with windows or splits           │
│    ├─ LayoutVisitor: Abstract interface for layouts         │
│    │   ├─ BSPLayout: Binary Space Partitioning              │
│    │   ├─ MonocleLayout: Full-screen single window          │
│    │   ├─ MasterStackLayout: Classic master-stack           │
│    │   ├─ CenteredMasterLayout: Center column + stacks      │
│    │   ├─ DynamicGridLayout: Uniform N×M grid               │
│    │   ├─ DwindleSpiralLayout: Fibonacci spiral tiling      │
│    │   ├─ TabbedStackedLayout: Tabbed interface             │
│    │   └─ InfiniteCanvasLayout: Virtual coordinate system   │
│    └─ Per-workspace trees with focus tracking               │
├─────────────────────────────────────────────────────────────┤
│  EWMHManager (Desktop Compatibility)                        │
│    ├─ 50+ EWMH Atoms (_NET_SUPPORTED, _NET_CURRENT_DESKTOP) │
│    ├─ Window Types (normal, dialog, utility, dock)          │
│    ├─ Window States (fullscreen, maximized, sticky)         │
│    ├─ Custom Pointblank Atoms (external bar integration)    │
│    └─ Root Window Properties                                │
├─────────────────────────────────────────────────────────────┤
│  MonitorManager (Multi-Monitor)                             │
│    ├─ XRandR Query for monitor detection                    │
│    ├─ XRandR Event Handling (dynamic changes)               │
│    └─ Per-monitor cameras and viewports                     │
├─────────────────────────────────────────────────────────────┤
│  ExtensionLoader (v2.0 Plugin System)                       │
│    ├─ Shared Object Loading (dlopen)                        │
│    ├─ ABI Validation with Version Checking                  │
│    ├─ Hook System: onWindowMap, onWindowUnmap,              │
│    │               onWindowFocus, onWorkspaceChange         │
│    └─ Health Monitoring and Performance Tracking            │
├─────────────────────────────────────────────────────────────┤
│  Toaster (OSD Notifications)                                │
│    ├─ Cairo/Xlib Rendering: Visual notifications            │
│    ├─ D-Bus Integration: System notification fallback       │
│    ├─ Color-coded levels: Error/Success/Warning/Info        │
│    └─ Queue management with auto-expiry                     │
├─────────────────────────────────────────────────────────────┤
│  KeybindManager                                             │
│    ├─ Keybind registration from config                      │
│    ├─ X11 key grabbing                                      │
│    └─ Action dispatch (WM commands / exec)                  │
└─────────────────────────────────────────────────────────────┘
```

## Building

### Dependencies

- **X11 Libraries**: `libx11`, `libxrender`, `libxft`, `libxrandr`
- **Cairo**: For OSD rendering
- **GLib/GIO**: For D-Bus notifications
- **C++20 Compiler**: GCC 10+ or Clang 12+
- **CMake**: 3.20+

### Build Instructions

#### Install dependencies 
- Debian / Ubuntu
``` bash
sudo apt install libx11-dev libxrender-dev libxft-dev libxrandr-dev libcairo2-dev libglib2.0-dev cmake g++
```
- Arch / Manjaro
``` bash
sudo pacman -S libx11 libxrender libxft libxrandr cairo glib2 cmake gcc
```
- Fedora
``` bash
sudo dnf install libX11-devel libXrender-devel libXft-devel libXrandr-devel cairo-devel glib2-devel cmake gcc-c++
```
- Gentoo
``` bash
sudo emerge --ask x11-libs/libX11 x11-libs/libXrender x11-libs/libXft x11-libs/libXrandr x11-libs/cairo dev-libs/glib dev-build/cmake sys-devel/gcc
```
- openSUSE
``` bash
sudo zypper install libX11-devel libXrender-devel libXft-devel libXrandr-devel cairo-devel glib2-devel cmake gcc-c++
```
- Alpine
``` bash
sudo apk add libx11-dev libxrender-dev libxft-dev libxrandr-dev cairo-dev glib-dev cmake g++
```

``` bash
# Build
mkdir build && cd build
cmake ..
make -j$(nproc)

# Install
sudo make install
```

## Configuration

### File Structure

```
~/.config/pblank/
├── pointblank.wmi          # Main configuration
└── extensions/
    ├── pb/                 # Point Blank built-in extensions (#include)
    │   └── lib*.so         # Extension shared objects
    └── user/               # User custom extensions (#import)
        └── *.so            # User extension shared objects
```

### Example Configuration

```wmi
// Load built-in extension from ~/.config/pblank/extensions/pb/
#include animation

pointblank: {
    window_rules: {
        opacity: 0.8
        blur: true
    };
    
    binds: {
        "SUPER, Q": "killactive"
        "SUPER, F": "fullscreen"
        "SUPER, RETURN": exec: "alacritty"
    };
};

// Per-application rules with conditional if statements
if (window.class == "Firefox") {
    window_rules: {
        opacity: 1.0
    };
};

// Conditional with member access
if (window.title.contains(" - Visual Studio Code")) {
    window_rules: {
        layout: "monocle"
    };
};
```

See [GRAMMAR.md](GRAMMAR.md) for complete DSL specification.

## Memory Safety Design

Point Blank uses modern C++20 idioms to ensure memory safety:

### RAII for X11 Resources

```cpp
// Custom deleters for X11 types
struct DisplayDeleter {
    void operator()(Display* display) const {
        if (display) XCloseDisplay(display);
    }
};

using DisplayPtr = std::unique_ptr<Display, DisplayDeleter>;
```

### No Raw `new/delete`

All allocations use smart pointers:

```cpp
// Window management
std::unordered_map<Window, std::unique_ptr<ManagedWindow>> clients_;

// Component ownership
std::unique_ptr<ConfigParser> config_parser_;
std::unique_ptr<LayoutEngine> layout_engine_;
std::unique_ptr<Toaster> toaster_;
```

### Type-Safe AST with `std::variant`

```cpp
using ExpressionValue = std::variant<
    IntLiteral,
    FloatLiteral,
    StringLiteral,
    BinaryOp,
    UnaryOp
>;
```

## Error Handling: The "Toaster" System

Point Blank never crashes on configuration errors. Instead:

1. **Visual OSD**: Renders error message in top-left corner
   - Red (#FF0000) for errors
   - Green (#00FF00) for success
   - Yellow (#FFFF00) for warnings
   - Blue (#0080FF) for info

2. **D-Bus Notification**: Sends system notification simultaneously

3. **Fallback Config**: Falls back to hardcoded defaults if parsing fails

```cpp
if (!loadConfigSafe()) {
    toaster_->error("Configuration failed - using defaults");
    fallbackToDefaultConfig();
}
```

## Layout Engine

### BSP Tree Structure

```cpp
class BSPNode {
    // Leaf: Contains a window
    ManagedWindow* window_;
    
    // Internal: Split with two children
    std::unique_ptr<BSPNode> left_;
    std::unique_ptr<BSPNode> right_;
    SplitType split_type_;  // Horizontal or Vertical
    double ratio_;          // 0.0 - 1.0 split position
};
```

### Layout Visitor Pattern

Layouts are implemented as visitors that traverse the BSP tree:

```cpp
class LayoutVisitor {
    virtual void visit(BSPNode* root, const Rect& bounds) = 0;
};

// Example: BSP tiling
class BSPLayout : public LayoutVisitor {
    void visit(BSPNode* root, const Rect& bounds) override {
        // Calculate geometries recursively
    }
};
```

### Available Layout Modes

| Layout | Description |
|--------|-------------|
| BSPLayout | Binary Space Partition tiling |
| MonocleLayout | Fullscreen single window |
| MasterStackLayout | Classic master-stack (dwm-style) |
| CenteredMasterLayout | Center column with flanking stacks |
| DynamicGridLayout | Uniform N×M grid |
| DwindleSpiralLayout | Fibonacci spiral tiling |
| TabbedStackedLayout | Tabbed interface with tab bar |
| InfiniteCanvasLayout | Virtual coordinate system with viewport |

### Detailed Layout Documentation

#### BSPLayout (Binary Space Partition)
Traditional tiling layout that recursively splits the screen into two partitions.
```
┌─────────┬───┐
│         │   │
│    A    │ B │
│         │   │
├─────┬───┴───┤
│  C  │   D   │
└─────┴───────┘
```

#### MonocleLayout
All windows are displayed fullscreen, one at a time.
```
┌───────────────┐
│               │
│   Focused     │
│    Window     │
│               │
└───────────────┘
```

#### MasterStackLayout
One master window takes up half the screen, remaining windows stack on the other side.
```
┌─────────┬─────┐
│         │ S1  │
│ Master  ├─────┤
│         │ S2  │
│         ├─────┤
│         │ S3  │
└─────────┴─────┘
```

#### CenteredMasterLayout
Master window centered with equal-sized stacks on both sides.
```
┌─────┬─────────┬─────┐
│ S1  │ Master  │ S2  │
│     │         │     │
├─────┼─────────┼─────┤
│ S3  │         │ S4  │
└─────┴─────────┴─────┘
```

#### DynamicGridLayout
Windows arranged in a uniform grid based on window count.
```
┌─────┬─────┬─────┐
│  1  │  2  │  3  │
├─────┼─────┼─────┤
│  4  │  5  │  6  │
└─────┴─────┴─────┘
```

#### DwindleSpiralLayout
Fibonacci spiral pattern - windows spiral outward in size.
```
┌───────────────┐
│               │
│       1       │
│               │
├───────┬───────┤
│   2   │   3   │
├───────┼───────┤
│   4   │   5   │
└───────┴───────┘
```

#### TabbedStackedLayout
Tab bar at top with stacked windows below.
```
┌─────────────────────┐
│ [Tab1][Tab2][Tab3]  │
├─────────┬───────────┤
│         │           │
│   A     │     B     │
│         │           │
├─────────┼───────────┤
│         │           │
│   C     │     D     │
└─────────┴───────────┘
```

#### InfiniteCanvasLayout
Unbounded virtual canvas with viewport panning.
```
    ┌────────────────┐
    │   Viewport     │
    │  ┌──────────┐  │
    │  │ Window A │  │
    │  └──────────┘  │
    │         ┌───┐  │
    │         │ B │  │
    └─────────────┘  │
      Windows can be │
      anywhere!      │
```

### Adding New Layouts

```cpp
class MyCustomLayout : public LayoutVisitor {
    void visit(BSPNode* root, const Rect& bounds) override {
        // Implement custom layout logic
        // Traverse tree and call window->setGeometry(...)
    }
};

// Register layout
layout_engine_->setLayout(workspace, std::make_unique<MyCustomLayout>());
```

## EWMH Compliance

Pointblank implements full EWMH compliance with 50+ atoms:

### Supported Atoms

- **Root Properties**: `_NET_SUPPORTED`, `_NET_SUPPORTING_WM_CHECK`, `_NET_CURRENT_DESKTOP`, `_NET_NUMBER_OF_DESKTOPS`, `_NET_DESKTOP_NAMES`
- **Window Properties**: `_NET_WM_NAME`, `_NET_WM_VISIBLE_NAME`, `_NET_WM_DESKTOP`, `_NET_WM_WINDOW_TYPE`, `_NET_WM_STATE`
- **Window Types**: `_NET_WM_WINDOW_TYPE_NORMAL`, `_NET_WM_WINDOW_TYPE_DIALOG`, `_NET_WM_WINDOW_TYPE_UTILITY`, `_NET_WM_WINDOW_TYPE_DOCK`
- **Window States**: `_NET_WM_STATE_FULLSCREEN`, `_NET_WM_STATE_MAXIMIZED_VERT`, `_NET_WM_STATE_MAXIMIZED_HORZ`, `_NET_WM_STATE_STICKY`
- **Custom Pointblank Atoms**: For external bar integration

## Extension System v2.0

### Architecture

```cpp
class ExtensionLoader {
    // Load shared object
    void* handle = dlopen("libmyextension.so", RTLD_LAZY);
    
    // Validate ABI version
    ExtensionABI* abi = static_cast<ExtensionABI*>(dlsym(handle, "extension_abi"));
    if (abi->version != EXPECTED_ABI_VERSION) {
        dlclose(handle);
        return;
    }
    
    // Register hooks
    abi->onWindowMap = myWindowMapHandler;
    abi->onWindowFocus = myWindowFocusHandler;
};
```

### Available Hooks

- `onWindowMap` - Called when a window is mapped
- `onWindowUnmap` - Called when a window is unmapped
- `onWindowFocus` - Called when window focus changes
- `onWorkspaceChange` - Called when workspace changes

### Health Monitoring

Extensions are monitored for:
- Memory usage
- Response time
- Crash detection

## Multi-Monitor Support

### XRandR Integration

```cpp
class MonitorManager {
    // Query available monitors
    void queryMonitors() {
        XRRScreenResources* resources = XRRGetScreenResources(display, root);
        for (int i = 0; i < resources->noutput; i++) {
            XRROutputInfo* info = XRRGetOutputInfo(display, resources, resources->outputs[i]);
            // Process monitor info
        }
    }
    
    // Handle dynamic changes
    void handleXRandREvent(XRRUpdateNotifyEvent* event) {
        // Re-query monitors on change
        queryMonitors();
    }
};
```

## DSL Parser Architecture

### Three-Phase Design

1. **Lexer**: Source → Tokens
   ```cpp
   std::vector<Token> tokens = lexer.tokenize();
   ```

2. **Parser**: Tokens → AST
   ```cpp
   auto ast = parser.parse(); // std::unique_ptr<ast::ConfigFile>
   ```

3. **Interpreter**: AST → Runtime Config
   ```cpp
   config_parser_->interpret(ast);
   ```

### Recursive Descent Parser

Each grammar rule becomes a parsing function:

```cpp
std::unique_ptr<Expression> Parser::expression() {
    return logicalOr();
}

std::unique_ptr<Expression> Parser::logicalOr() {
    auto left = logicalAnd();
    while (match({TokenType::Or})) {
        auto right = logicalAnd();
        left = makeBinaryOp(BinaryOp::Or, left, right);
    }
    return left;
}
```

### Preprocessor Directives

```cpp
// Import from user extensions
#import my_custom_extension  // Loads ~/./config/pblank/extensions/user/libmy_custom_extension.so

// Include built-in extensions
#include animation
#include rounded_corners
```

### Conditional Rules

```cpp
// if statements with member access
if (window.class == "Firefox") {
    window_rules: { opacity: 1.0 };
};

if (window.title.contains(" - VS Code")) {
    window_rules: { layout: "monocle" };
};
```

### Error Recovery

Parser uses **panic mode** recovery:

```cpp
void Parser::synchronize() {
    while (!isAtEnd()) {
        if (previous().type == TokenType::Semicolon) return;
        if (peek().type == TokenType::RightBrace) return;
        advance();
    }
}
```

All errors are collected and reported through the Toaster.

## X11 Event Loop

Modern C++ event handling:

```cpp
void WindowManager::run() {
    XEvent event;
    while (true) {
        toaster_->update(); // Render notifications
        
        if (XPending(display_.get()) > 0) {
            XNextEvent(display_.get(), &event);
            
            switch (event.type) {
                case MapRequest:
                    handleMapRequest(event.xmaprequest);
                    break;
                case KeyPress:
                    handleKeyPress(event.xkey);
                    break;
                // ... other events
            }
        }
    }
}
```

## Keybind System

### Registration

```cpp
keybind_manager_->registerKeybind("SUPER, Q", "killactive");
keybind_manager_->registerKeybind("SUPER, RETURN", "exec: alacritty");
```

### Key Grabbing

```cpp
void KeybindManager::grabKeys(Display* display, Window root) {
    for (const auto& bind : keybinds_) {
        KeyCode keycode = XKeysymToKeycode(display, bind.keysym);
        XGrabKey(display, keycode, bind.modifiers, root, True,
                GrabModeAsync, GrabModeAsync);
    }
}
```

## Performance Optimizations

Pointblank is optimized for maximum execution speed targeting nanosecond-level improvements:

### String Operations
- Uses `std::string_view` for zero-copy string comparisons in parser
- `reserve()` pre-allocations for vectors to avoid heap reallocations
- `emplace_back()` instead of `push_back()` for in-place construction

### Hot Path Inlining
- Critical lexer functions: `peek()`, `advance()`, `match()`, `isAtEnd()`
- Parser functions: `previous()`, `check()`, `advance()`
- BSPNode query methods: `isLeaf()`, `getWindow()`, `getLeft()`, `getRight()`
- Rect geometry methods: `area()`, `contains()`, `centerX()`, `centerY()`

### Memory Optimizations
- `clients_.reserve()` pre-allocation for window map
- Atom caching in EWMHManager to avoid repeated lookups
- Lock-free data structures for multi-threaded extensions

### Event Processing
- Non-blocking event loop with minimal sleep (1ms) when idle
- Direct XEvent processing without intermediate buffering
- Toaster updates only when notifications are present

### Performance Metrics
- Frame timing optimized for 60Hz+ displays
- Input latency reduction through inline key handling
- Window placement calculation speedup via pre-sized containers

## File Organization

```
Pointblank/
├── src/
│   ├── config/       (ConfigParser, ConfigWatcher, LayoutConfigParser)
│   ├── core/         (WindowManager, SessionManager, Toaster)
│   ├── display/      (EWMHManager, MonitorManager, SyncManager)
│   ├── extensions/   (ExtensionLoader, PluginManager)
│   ├── layout/       (LayoutEngine, LayoutProvider)
│   ├── performance/  (PerformanceTuner, RenderPipeline)
│   ├── utils/        (GapConfig, SpatialGrid, Camera)
│   └── window/       (KeybindManager, FloatingWindowManager, etc.)
├── include/
│   └── pointblank/   (Header-only utilities)
└── text/             (Documentation)
```

## File Organization

```
pointblank/
├── GRAMMAR.md            # DSL specification
├── DOCUMENTATION.md      # Developer documentation
├── extensions.md         # Extension system guide
├── ARCHITECTURE_SUMMARY.md
│── README.md             # This file
├── src/
│   ├── config/           # ConfigParser, ConfigWatcher, LayoutConfigParser
│   ├── core/             # WindowManager, SessionManager, Toaster
│   ├── display/          # EWMHManager, MonitorManager, SyncManager
│   ├── extensions/       # ExtensionLoader, PluginManager
│   ├── layout/           # LayoutEngine, LayoutProvider
│   ├── performance/      # PerformanceTuner, RenderPipeline
│   ├── utils/            # GapConfig, SpatialGrid, Camera
│   └── window/           # KeybindManager, FloatingWindowManager, etc.
├── include/pointblank/   # Public headers
├── extension_template/   # Example extension template
├── contrib/              # Desktop entry, xinitrc
└── CMakeLists.txt        # Build configuration
```

## Documentation

- [GRAMMAR.md](GRAMMAR.md) - Complete DSL specification
- [DOCUMENTATION.md](DOCUMENTATION.md) - Developer documentation for layouts and extensions
- [extensions.md](extensions.md) - Extension system guide and API reference
- [ARCHITECTURE_SUMMARY.md](ARCHITECTURE_SUMMARY.md) - System architecture details

## Contributing

Key areas for extension:

1. **New Layout Modes**: Implement `LayoutVisitor` interface
2. **Extensions**: Create custom extensions using the v2.0 API (see [extensions.md](extensions.md))
3. **Window Rules**: Extend conditional matching system
4. **Animation System**: Integrate with layout transitions

## License


MIT License - See LICENSE file for details.

## Acknowledgments

- Inspired by i3, bspwm, and dwm
- Uses QML-style syntax for configuration
- Built with modern C++20 best practices
