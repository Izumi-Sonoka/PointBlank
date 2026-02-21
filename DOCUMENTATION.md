# Point Blank Window Manager - Developer Documentation

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Custom Layout Development](#custom-layout-development)
3. [Extension Development](#extension-development)
4. [Configuration System](#configuration-system)
5. [Hot-Reload System](#hot-reload-system)
6. [Scratchpad System](#scratchpad-system)
7. [Window Swallowing](#window-swallowing)
8. [IPC Control](#ipc-control)
9. [API Reference](#api-reference)

---

## Architecture Overview

Point Blank is an infinite layout tiling window manager built with modern C++20/23 principles. The architecture follows a visitor pattern for layout algorithms and uses a Binary Space Partitioning (BSP) tree as the core data structure for window management.

### Core Components

```
┌───────────────────────────────────────────────────────────────────────────────────────┐
│                              WindowManager                                            │
│  ┌───────────────────────┐  ┌────────────────────────┐  ┌────────────────────────┐    │
│  │ ConfigParser          │  │ KeybindManager         │  │ LayoutConfigParser     │    │
│  │ src/config/           │  │ src/window/            │  │ src/config/            │    │
│  │ ConfigParser.hpp      │  │ KeybindManager.hpp     │  │ LayoutConfigParser.hpp │    │
│  └───────────────────────┘  └────────────────────────┘  └────────────────────────┘    │
│                                                                                       │
│  ┌───────────────────────────────────────────────────────────────────────────────┐    │
│  │                            LayoutEngine                                       │    │
│  │        ┌──────────────┐  ┌─────────────────┐  ┌────────────────┐              │    │
│  │        │   BSPNode    │  │ LayoutVisitor   │  │  WindowStats   │              │    │
│  │        └──────────────┘  └─────────────────┘  └────────────────┘              │    │
│  │                                                                               │    │
│  │  Layout Implementations:                                                      │    │
│  │  • BSPLayout         • CenteredMasterLayout                                   │    │
│  │  • MonocleLayout     • DynamicGridLayout                                      │    │
│  │  • MasterStackLayout • DwindleSpiralLayout                                    │    │
│  │  • TabbedStackedLayout • InfiniteCanvasLayout                                 │    │
│  └───────────────────────────────────────────────────────────────────────────────┘    │
│                                                                                       │
│  ┌───────────────────────┐  ┌────────────────────────┐  ┌────────────────────────┐    │
│  │  ConfigWatcher        │  │ Toaster                │  │ ExtensionLoader        │    │
│  │ src/config/           │  │ src/core/              │  │ src/extensions/        │    │
│  │ ConfigWatcher.hpp     │  │ Toaster.hpp            │  │ ExtensionLoader.hpp    │    │
│  └───────────────────────┘  └────────────────────────┘  └────────────────────────┘    │
│                                                                                       │
│  ┌───────────────────────┐  ┌────────────────────────┐  ┌────────────────────────┐    │
│  │ EWMHManager           │  │ MonitorManager         │  │ SessionManager         │    │
│  │ src/display/          │  │ src/display/           │  │ src/core/              │    │
│  │ EWMHManager.hpp       │  │ MonitorManager.hpp     │  │ SessionManager.hpp     │    │
│  └───────────────────────┘  └────────────────────────┘  └────────────────────────┘    │
│                                                                                       │
│  ┌───────────────────────┐  ┌────────────────────────┐  ┌────────────────────────┐    │
│  │ FloatingWindowMgr     │  │ PreselectionWn         │  │ SizeConstraints        │    │
│  │ src/window/           │  │ src/window/            │  │ src/window/            │    │
│  │ FloatingWindowMgr.hpp │  │ PreselectionWindow.hpp │  │ SizeConstraints.hpp    │    │
│  └───────────────────────┘  └────────────────────────┘  └────────────────────────┘    │
│                                                                                       │
│  ┌───────────────────────┐  ┌────────────────────────┐                                │
│  │ PerformanceTuner      │  │ RenderPipeline         │                                │
│  │ src/performance/      │  │ src/performance/       │                                │
│  │ PerformanceTuner.hpp  │  │ RenderPipeline.hpp     │                                │
│  └───────────────────────┘  └────────────────────────┘                                │
└───────────────────────────────────────────────────────────────────────────────────────┘
```

### Key Design Patterns

1. **Visitor Pattern**: [`LayoutVisitor`](/include/pointblank/layout/LayoutEngine.hpp:XX) base class enables polymorphic layout algorithms
2. **BSP Tree**: [`BSPNode`](/include/pointblank/layout/LayoutEngine.hpp:XX) provides efficient window spatial queries
3. **RAII Wrappers**: X11 resources managed via `DisplayPtr`, `WindowPtr`, `GCPtr`
4. **Zero-overhead Abstractions**: `constexpr` functions, inline where possible

---

## Custom Layout Development

### Creating a New Layout

To implement a custom layout, inherit from [`LayoutVisitor`](/include/pointblank/layout/LayoutEngine.hpp:XX) and implement the `visit()` method:

```cpp
#include <pointblank/layout/LayoutEngine.hpp>
#include <pointblank/layout/LayoutTypes.hpp>

namespace pblank {

class MyCustomLayout : public LayoutVisitor {
public:
    MyCustomLayout(LayoutEngine* engine) : LayoutVisitor(engine) {}
    
    void visit(BSPNode* node, const LayoutParams& params) override {
        // Get windows to arrange
        auto windows = collectWindows(node);
        
        if (windows.empty()) return;
        
        // Calculate layout geometry
        int x = params.x;
        int y = params.y;
        int width = params.width;
        int height = params.height;
        int gap = engine_->getGapSize();
        int border = engine_->getBorderWidth();
        
        // Arrange windows according to your algorithm
        for (size_t i = 0; i < windows.size(); ++i) {
            Window win = windows[i];
            
            // Calculate window position
            int win_x = calculateX(i, windows.size(), x, width);
            int win_y = calculateY(i, windows.size(), y, height);
            int win_w = calculateWidth(i, windows.size(), width, gap);
            int win_h = calculateHeight(i, windows.size(), height, gap);
            
            // Apply geometry
            engine_->configureWindow(win, win_x, win_y, win_w, win_h);
        }
        
        // Set focus window (first window by default)
        if (!windows.empty()) {
            engine_->setFocusWindow(windows[0]);
        }
    }
    
private:
    std::vector<Window> collectWindows(BSPNode* node) {
        std::vector<Window> windows;
        if (node->isWindow()) {
            windows.push_back(node->window);
        } else {
            if (node->left) {
                auto left_windows = collectWindows(node->left.get());
                windows.insert(windows.end(), 
                              left_windows.begin(), left_windows.end());
            }
            if (node->right) {
                auto right_windows = collectWindows(node->right.get());
                windows.insert(windows.end(), 
                              right_windows.begin(), right_windows.end());
            }
        }
        return windows;
    }
};

} // namespace pblank
```

### Layout Registration

Register your layout in [`LayoutEngine::setLayoutMode()`](LayoutEngine.cpp:XX):

```cpp
void LayoutEngine::setLayoutMode(LayoutMode mode) {
    current_layout_mode_ = mode;
    
    switch (mode) {
        case LayoutMode::MyCustom:
            current_layout_ = std::make_unique<MyCustomLayout>(this);
            break;
        // ... other cases
    }
}
```

### Layout Algorithm Contract

Your layout implementation must:

1. **Handle Empty Trees**: Return early if no windows exist
2. **Respect Gaps**: Apply `engine_->getGapSize()` between windows
3. **Account for Borders**: Use `engine_->getBorderWidth()` in calculations
4. **Set Focus Window**: Call `engine_->setFocusWindow()` with the default focused window
5. **Apply Geometry**: Use `engine_->configureWindow()` to position windows

### LayoutParams Structure

```cpp
struct LayoutParams {
    int x, y;           // Top-left corner of available area
    int width, height;  // Dimensions of available area
    int depth;          // Current recursion depth (for spiral layouts)
    bool horizontal;    // Split orientation hint
};
```

### Example: Dynamic Grid Layout

The Dynamic Grid layout calculates an N×M matrix based on window count:

```cpp
void DynamicGridLayout::visit(BSPNode* node, const LayoutParams& params) {
    auto windows = collectWindows(node);
    size_t count = windows.size();
    
    if (count == 0) return;
    
    // Calculate grid dimensions (square-ish)
    int cols = static_cast<int>(std::ceil(std::sqrt(count)));
    int rows = static_cast<int>(std::ceil(static_cast<double>(count) / cols));
    
    int gap = engine_->getGapSize();
    int cell_width = (params.width - (cols - 1) * gap) / cols;
    int cell_height = (params.height - (rows - 1) * gap) / rows;
    
    for (size_t i = 0; i < count; ++i) {
        int col = i % cols;
        int row = i / cols;
        
        int x = params.x + col * (cell_width + gap);
        int y = params.y + row * (cell_height + gap);
        
        // Last row may have fewer items
        int items_in_row = (row == rows - 1) ? (count - row * cols) : cols;
        if (items_in_row < cols) {
            // Center items in last row
            int offset = (cols - items_in_row) * (cell_width + gap) / 2;
            x += offset;
        }
        
        engine_->configureWindow(windows[i], x, y, cell_width, cell_height);
    }
    
    engine_->setFocusWindow(windows[0]);
}
```

---

## Extension Development

> **Note**: For comprehensive extension documentation, see [extensions.md](extensions.md).

### Extension API Overview

Point Blank provides a high-fidelity extension system via [`ExtensionAPI.hpp`](/include/pointblank/extensions/ExtensionAPI.hpp). The v2.0 API provides:

- **ABI Stability**: Versioned symbols and checksums ensure compatibility
- **Event Hooks**: Subscribe to window, workspace, and layout events
- **Layout Providers**: Create custom layout algorithms
- **Performance Monitoring**: Track extension processing time

### Quick Start

```cpp
#include <pointblank/extensions/ExtensionAPI.hpp>

class MyExtension : public pblank::api::v2::IExtension_v2 {
public:
    const pblank::api::v2::ExtensionInfo* getInfo() const override {
        static pblank::api::v2::ExtensionInfo info = {
            .name = "MyExtension",
            .version = "1.0.0",
            .author = "Your Name",
            .description = "A simple extension",
            .api_version_major = PB_API_VERSION_MAJOR,
            .api_version_minor = PB_API_VERSION_MINOR,
            .api_version_patch = PB_API_VERSION_PATCH,
            .capabilities = 0,
            .priority = 0,
            .api_checksum = pblank::api::v2::computeAPIChecksum()
        };
        return &info;
    }
    
    pblank::api::v2::Result initialize(
        const pblank::api::v2::ExtensionContext* ctx) override {
        // Store context, register hooks
        return pblank::api::v2::Result::Success;
    }
    
    pblank::api::v2::Result shutdown() override {
        return pblank::api::v2::Result::Success;
    }
};

extern "C" PB_API_EXPORT pblank::api::v2::IExtension_v2* createExtension_v2() {
    return new MyExtension();
}

extern "C" PB_API_EXPORT void destroyExtension_v2(pblank::api::v2::IExtension_v2* ext) {
    delete ext;
}
```

### Loading Extensions

Extensions are loaded via `#import` or `#include` directives:

```wmi
// In your pointblank.wmi config file:
#include animation       // Load built-in extension from ~/.config/pblank/extensions/pb/
#include rounded_corners
#import my_custom        // Load user extension from ~/.config/pblank/extensions/user/
```

### Extension Directories

| Directory | Purpose | Directive |
|-----------|---------|-----------|
| `~/.config/pblank/extensions/user/` | User custom extensions | `#import` |
| `~/.config/pblank/extensions/pb/` | Point Blank built-in extensions | `#include` |
| `./extensions/build/` | Development built-in extensions | `#include` |
| `/usr/lib/pointblank/extensions/` | System built-in extensions | `#include` |

For complete extension documentation including:
- Event handling
- Custom layout providers
- Performance monitoring
- ABI validation

See [extensions.md](extensions.md).

---

## Configuration System

### Configuration Files

Point Blank uses `.wmi` configuration files:

```
~/.config/pblank/
├── pointblank.wmi       # Main configuration
├── layout/
│   ├── default.wmi      # Layout presets
│   └── custom.wmi       # User customizations
└── extensions/
    ├── pb/              # Built-in extensions
    │   └── *.so         # Extension shared objects
    └── user/            # User extensions
        └── *.so         # User extension shared objects
```

### Configuration Syntax

```ini
# Comments start with //

# General settings
general {
    gap_size = 10
    border_width = 2
    focus_follows_mouse = false
}

# Colors
colors {
    focused_border = #00FF00
    unfocused_border = #333333
}

# Keybinds
bind = SUPER, Q, killactive
bind = SUPER, SHIFT, Q, exit
bind = SUPER, RETURN, exec: alacritty
bind = SUPER, TAB, cyclenext
bind = SUPER, SHIFT, TAB, cycleprev

# Layout configuration
layout {
    mode = bsp
    cycle_direction = forward
}

# Workspace bindings
bind = SUPER, 1, workspace 1
bind = SUPER, 2, workspace 2
bind = SUPER, SHIFT, 1, movetoworkspace 1
```

### Include Directives

```ini
# Include another layout file
#include layout default.wmi

# Include user-specific overrides
#included.layout user
```

### Layout Configuration Parser

The [`LayoutConfigParser`](include/pointblank/config/LayoutConfigParser.hpp:XX) handles layout-specific configuration:

```cpp
// Parse layout configuration
LayoutConfigParser parser(layout_engine);
parser.parseFile("~/.config/pblank/layout/default.wmi");

// Access parsed settings
LayoutMode mode = parser.getLayoutMode();
std::string direction = parser.getCycleDirection();
```

---

## Hot-Reload System

### Overview

The [`ConfigWatcher`](/include/pointblank/config/ConfigWatcher.hpp:XX) provides automatic configuration reloading using Linux inotify:

```cpp
class ConfigWatcher {
public:
    // Add watch path
    bool addWatch(const std::filesystem::path& path, bool recursive = true);
    
    // Set callbacks
    void setValidationCallback(ValidationCallback callback);
    void setApplyCallback(ApplyCallback callback);
    void setErrorCallback(ErrorCallback callback);
    void setNotifyCallback(NotifyCallback callback);
    
    // Control
    bool start();
    void stop();
    
    // Manual reload
    ValidationResult reload(const std::filesystem::path& path);
};
```

### Validation Callback

```cpp
config_watcher->setValidationCallback([](const auto& path) {
    ValidationResult result;
    
    // Parse and validate
    std::ifstream file(path);
    // ... validation logic ...
    
    if (has_errors) {
        result.success = false;
        result.errors.push_back("Error message");
        result.error_locations.push_back({line, column, "details"});
    } else {
        result.success = true;
    }
    
    return result;
});
```

### Apply Callback

```cpp
config_watcher->setApplyCallback([this](const auto& path) {
    // Reload configuration
    if (loadConfig(path)) {
        applyConfig();
        return true;
    }
    return false;
});
```

### Debouncing

The watcher includes built-in debouncing to prevent rapid reloads:

```cpp
// Set debounce interval (default: 500ms)
config_watcher->setDebounceInterval(std::chrono::milliseconds(300));
```

---

## API Reference

### LayoutEngine

| Method | Description |
|--------|-------------|
| `addWindow(Window)` | Add window to BSP tree |
| `removeWindow(Window)` | Remove window, returns next focus |
| `focusWindow(Window)` | Set focused window |
| `moveFocus(string)` | Move focus in direction |
| `swapFocused(string)` | Swap focused with neighbor |
| `setLayoutMode(LayoutMode)` | Change layout algorithm |
| `applyLayout()` | Recalculate and apply layout |
| `configureWindow(Window, x, y, w, h)` | Position a window |
| `warpPointerToWindow(Window)` | Move cursor to window center |

### BSPNode

| Method | Description |
|--------|-------------|
| `isWindow()` | True if leaf node |
| `isSplit()` | True if internal node |
| `getSplitRatio()` | Get split ratio (0.0-1.0) |
| `setSplitRatio(double)` | Set split ratio |
| `getSplitDirection()` | Get horizontal/vertical |

### LayoutMode Enum

```cpp
enum class LayoutMode {
    BSP,            // Binary Space Partitioning
    Monocle,        // Fullscreen single window
    MasterStack,    // Master + stack layout
    CenteredMaster, // Centered master + side stacks
    DynamicGrid,    // N×M grid based on count
    DwindleSpiral,  // Fibonacci spiral
    TabbedStacked,  // Fullscreen with tab bar
    InfiniteCanvas  // Virtual canvas with viewport
};
```

### WindowStats Structure

```cpp
struct WindowStats {
    Window window;
    int virtual_x;      // Virtual canvas position
    int virtual_y;
    int width;
    int height;
    int workspace;
    bool is_focused;
    bool is_floating;
};
```

---

## Bug Analysis & Known Issues

This section documents known issues, edge cases, and their mitigations.

### Fixes Applied

#### IPC Partial Write Fix
**Issue**: Socket writes could fail silently.
**Solution**: Loop until all data is sent.

#### Client Limit
**Issue**: Unbounded connections could exhaust resources.
**Solution**: `MAX_IPC_CLIENTS = 32` limit.

#### Floating Window Bounds
**Issue**: Unbounded floating window list.
**Solution**: `MAX_FLOATING_WINDOWS = 256` with LRU eviction.

### Edge Cases

| Scenario | Risk Level | Mitigation |
|----------|------------|------------|
| Rapid config reload | Low | Debouncing (300ms) |
| Monitor disconnect | Medium | Fallback to primary |
| Extension hang | Medium | Health monitoring |
| X11 error | Low | Custom error handler |

### Security

- IPC socket uses mode 0600
- Extension ABI validated via checksum
- Config files should not be world-writable

---

## Competitive Analysis

### Market Position

Pointblank differentiates through:
1. **DSL Configuration**: Unique `.wmi` syntax
2. **Crash-Proof**: Visual error reporting
3. **8 Layout Modes**: More than any competitor
4. **Extension v2.0**: ABI-stable plugin system

### Feature Comparison

| Feature | i3 | bspwm | dwm | Pointblank |
|---------|----|----|------|------------|
| DSL Config | ❌ | ❌ | ❌ | ✅ |
| 8+ Layouts | ❌ | ❌ | ❌ | ✅ |
| Extension API | ❌ | ❌ | ❌ | ✅ |
| Visual Errors | ❌ | ❌ | ❌ | ✅ |
| Hot Reload | ✅ | ❌ | ❌ | ✅ |

### Recommendations

1. **Add session saving** - P2 priority
2. **Improve documentation** - Critical for adoption
3. **Add tests** - CI/CD pipeline
4. **Package for distros** - AUR, deb, rpm

---

## Building and Installation

### Requirements

- C++20 compatible compiler (GCC 11+, Clang 14+)
- CMake 3.20+
- X11 development libraries
- Linux kernel 5.10+ (for inotify)

### Build

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### Install

```bash
sudo make install
```

### Development Build

```bash
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON ..
make
ctest
```

---

## Scratchpad System

The ScratchpadManager provides a hidden workspace for windows that need to be quickly shown/hidden:

### Usage

```wmi
// In configuration
binds: {
    "SUPER, S": "scratchpad toggle"    // Toggle scratchpad window
    "SUPER, SHIFT, S": "scratchpad add"  // Add focused window to scratchpad
}
```

### API Reference

| Method | Description |
|--------|-------------|
| `addToScratchpad(Window)` | Add window to scratchpad |
| `toggleScratchpad()` | Show/hide scratchpad window |
| `removeFromScratchpad(Window)` | Remove window from scratchpad |
| `getScratchpadWindow()` | Get current scratchpad window |

### Implementation Details

- Scratchpad windows are hidden from all workspaces
- Only one scratchpad window can be visible at a time
- Windows retain their original geometry when toggled

---

## Window Swallowing

The WindowSwallower allows terminal windows to "swallow" GUI applications launched from them:

### How It Works

1. Terminal window spawns child process
2. Child process creates GUI window
3. WindowSwallower detects parent-child relationship
4. Terminal window is hidden (swallowed)
5. When GUI closes, terminal is restored

### Configuration

```wmi
swallow: {
    enabled: true
    terminal_class: "Alacritty"  // Terminal WM_CLASS to swallow
    keep_term_focus: false       // Return focus to terminal on unswallow
}
``### Supported Terminals

- Alacritty
- Kitty
- URxvt
- St
- XTerm

---

## IPC Control

The IPCServer provides external control via Unix domain socket:

### Socket Location

```
/tmp/pointblank-<uid>.sock
```

### Protocol

JSON-based request/response:

```json
// Request
{"command": "workspace", "args": [2]}

// Response
{"success": true, "result": "Switched to workspace 2"}
```

### Available Commands

| Command | Args | Description |
|---------|------|-------------|
| `workspace` | `[n]` | Switch to workspace n |
| `move_to_workspace` | `[n]` | Move focused window to workspace n |
| `focus` | `["left"\|"right"\|"up"\|"down"]` | Move focus |
| `reload` | `[]` | Reload configuration |
| `exec` | `["cmd"]` | Execute shell command |
| `layout` | `["bsp"\|"monocle"\|...]` | Change layout |
| `scratchpad` | `["toggle"]` | Toggle scratchpad |
| `list_windows` | `[]` | List all managed windows |
| `get_focused` | `[]` | Get focused window info |

### Example Usage

```bash
# Switch to workspace 3
echo '{"command": "workspace", "args": [3]}' | socat - UNIX-CONNECT:/tmp/pointblank-1000.sock

# Reload config
echo '{"command": "reload", "args": []}' | socat - UNIX-CONNECT:/tmp/pointblank-1000.sock
```

---

## Contributing

1. Fork the repository
2. Create a feature branch
3. Follow the coding style (see `.clang-format`)
4. Add tests for new functionality
5. Submit a pull request

### Code Style

- C++20 features preferred
- `snake_case` for functions/variables
- `PascalCase` for types
- `CONSTANT_CASE` for macros
- Always use `[[nodiscard]]` for non-void functions
- Prefer `std::unique_ptr` over raw pointers

---

## License

MIT License - See LICENSE file for details.
