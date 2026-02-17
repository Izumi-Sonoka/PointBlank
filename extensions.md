# Point Blank Extension System

## Table of Contents

1. [Overview](#overview)
2. [Using Extensions in Configuration](#using-extensions-in-configuration)
3. [Creating Custom Extensions](#creating-custom-extensions)
4. [Extension API Reference](#extension-api-reference)
5. [Extension Loading Process](#extension-loading-process)
6. [Best Practices](#best-practices)

---

## Overview

Point Blank supports a powerful extension system that allows you to extend the window manager's functionality through dynamically loaded shared libraries (`.so` files). Extensions can:

- Handle window events (map, unmap, focus, move, resize, destroy)
- Provide custom layout algorithms
- Integrate with the rendering pipeline
- Add performance monitoring capabilities
- Filter and block events

### Extension Locations

Extensions are loaded from the following directories:

| Directory | Purpose | Import Directive |
|-----------|---------|------------------|
| `~/.config/pblank/extensions/user/` | User custom extensions | `#import` |
| `~/.config/pblank/extensions/pb/` | Point Blank built-in extensions | `#include` |
| `./extensions/build/` | Development built-in extensions (relative to executable) | `#include` |
| `/usr/lib/pointblank/extensions/` | System-wide built-in extensions | `#include` |
| `/usr/local/lib/pointblank/extensions/` | Local system built-in extensions | `#include` |

---

## Using Extensions in Configuration

### Import Syntax

Extensions are loaded using `#import` or `#include` directives in your configuration file:

```wmi
// Load a user extension from ~/.config/pblank/extensions/user/
#import my_custom_extension

// Load a built-in Point Blank extension from ~/.config/pblank/extensions/pb/
#include animation

// Load multiple extensions
#include animation
#include rounded_corners
#import my_custom_extension
```

### Difference Between #import and #include

| Directive | Purpose | Default Search Location |
|-----------|---------|-------------------------|
| `#import` | Load user-created extensions | `~/.config/pblank/extensions/user/` |
| `#include` | Load Point Blank built-in extensions | `~/.config/pblank/extensions/pb/` |

### Extension Configuration

Some extensions can be configured through the main config file:

```wmi
pointblank: {
    // Animation extension configuration
    animation_ext: {
        enabled: true
        open_type: "fade"
        open_duration: 200
        open_easing: "ease_out_cubic"
    };
    
    // Rounded corners extension configuration
    rounded_corners: {
        enabled: true
        radius: 8
        focused_radius: 10
        anti_aliasing: true
    };
};
```

### Example: Loading Extensions

```wmi
// Point Blank Configuration with Extensions

pointblank: {
    // ... other configuration ...
};

// Load built-in Point Blank extensions from ~/.config/pblank/extensions/pb/
#include animation
#include rounded_corners

// Load a user-created extension from ~/.config/pblank/extensions/user/
#import my_layout_plugin
```

---

## Creating Custom Extensions

### Extension Structure

Every extension must:

1. Inherit from `pblank::api::v2::IExtension_v2`
2. Implement required virtual methods
3. Export the `createExtension_v2` and `destroyExtension_v2` functions
4. Provide correct API version information

### Minimal Extension Template

```cpp
#include "../ExtensionAPI.hpp"
#include <iostream>

class MyExtension : public pblank::api::v2::IExtension_v2 {
public:
    // Required: Return extension metadata
    const pblank::api::v2::ExtensionInfo* getInfo() const override {
        static pblank::api::v2::ExtensionInfo info = {
            .name = "MyExtension",
            .version = "1.0.0",
            .author = "Your Name",
            .description = "A simple example extension",
            .api_version_major = PB_API_VERSION_MAJOR,
            .api_version_minor = PB_API_VERSION_MINOR,
            .api_version_patch = PB_API_VERSION_PATCH,
            .capabilities = 0,
            .priority = 0,
            .api_checksum = pblank::api::v2::computeAPIChecksum()
        };
        return &info;
    }
    
    // Required: Initialize the extension
    pblank::api::v2::Result initialize(
        const pblank::api::v2::ExtensionContext* ctx) override {
        
        display_ = ctx->display;
        root_ = ctx->root;
        
        std::cout << "MyExtension initialized!" << std::endl;
        return pblank::api::v2::Result::Success;
    }
    
    // Required: Clean up resources
    pblank::api::v2::Result shutdown() override {
        std::cout << "MyExtension shutting down..." << std::endl;
        return pblank::api::v2::Result::Success;
    }
    
private:
    Display* display_ = nullptr;
    Window root_ = 0;
};

// Required: Export creation function
extern "C" PB_API_EXPORT pblank::api::v2::IExtension_v2* createExtension_v2() {
    return new MyExtension();
}

// Required: Export destruction function
extern "C" PB_API_EXPORT void destroyExtension_v2(pblank::api::v2::IExtension_v2* ext) {
    delete ext;
}
```

### Building an Extension

Compile your extension as a shared library:

```bash
# Simple compilation
g++ -shared -fPIC -O3 -o my_extension.so MyExtension.cpp

# With additional flags for ABI stability
g++ -shared -fPIC -O3 \
    -fvisibility=hidden \
    -DPB_API_EXPORT=__attribute__((visibility("default"))) \
    -o my_extension.so MyExtension.cpp
```

### Extension with Event Handling

```cpp
class EventExtension : public pblank::api::v2::IExtension_v2 {
public:
    // ... getInfo() and basic setup ...
    
    // Subscribe to events
    pblank::api::v2::EventMask getEventMask() const override {
        using EventType = pblank::api::v2::EventType;
        pblank::api::v2::EventMask mask;
        mask.set(EventType::WindowMap);
        mask.set(EventType::WindowFocus);
        mask.set(EventType::WorkspaceSwitch);
        return mask;
    }
    
    // Handle window creation
    bool onWindowMap(const pblank::api::v2::WindowHandle* window) override {
        std::cout << "Window mapped: " << window->x11_window << std::endl;
        return true;  // Return true to allow event propagation
    }
    
    // Handle focus changes
    bool onWindowFocus(
        const pblank::api::v2::WindowHandle* old_win,
        const pblank::api::v2::WindowHandle* new_win) override {
        
        if (new_win) {
            std::cout << "Focus changed to window: " << new_win->x11_window << std::endl;
        }
        return true;
    }
    
    // Handle workspace switches
    bool onWorkspaceSwitch(uint32_t old_ws, uint32_t new_ws) override {
        std::cout << "Switched from workspace " << old_ws 
                  << " to " << new_ws << std::endl;
        return true;
    }
};
```

### Extension with Custom Layout

```cpp
class ColumnsLayoutExtension : public pblank::api::v2::IExtension_v2 {
public:
    // ... basic setup ...
    
    // Indicate this extension provides a layout
    bool hasLayoutProvider() const override { return true; }
    
    const char* getLayoutName() const override { return "columns"; }
    
    // Calculate window positions
    pblank::api::v2::Result calculateLayout(
        const pblank::api::v2::LayoutContext* ctx,
        pblank::api::v2::LayoutOutput* output) override {
        
        if (ctx->window_count == 0) {
            return pblank::api::v2::Result::Success;
        }
        
        // Arrange windows in vertical columns
        int col_width = ctx->screen_bounds.width / ctx->window_count;
        
        for (uint32_t i = 0; i < ctx->window_count; ++i) {
            output->window_rects[i] = {
                .x = static_cast<int16_t>(i * col_width),
                .y = 0,
                .width = static_cast<uint16_t>(col_width),
                .height = ctx->screen_bounds.height
            };
        }
        
        output->count = ctx->window_count;
        return pblank::api::v2::Result::Success;
    }
};
```

---

## Extension API Reference

### API Version

The current Extension API version is **2.0.0**:

| Constant | Value |
|----------|-------|
| `PB_API_VERSION_MAJOR` | 2 |
| `PB_API_VERSION_MINOR` | 0 |
| `PB_API_VERSION_PATCH` | 0 |

Extensions must declare compatibility by setting the matching version values in their `ExtensionInfo`.

### ExtensionInfo Structure

```cpp
struct ExtensionInfo {
    char name[64];           // Extension name
    char version[32];        // Extension version (semver)
    char author[64];         // Author name
    char description[256];   // Brief description
    uint32_t api_version_major;  // API major version (must match)
    uint32_t api_version_minor;  // API minor version
    uint32_t api_version_patch;  // API patch version
    uint64_t capabilities;   // ExtensionCapability flags
    int32_t priority;        // Execution priority
    uint32_t reserved[4];    // Reserved for future use
    uint64_t api_checksum;   // ABI validation checksum
};
```

### ExtensionCapability Flags

| Flag | Description |
|------|-------------|
| `None` | No special capabilities |
| `LayoutProvider` | Provides custom layout algorithms |
| `EventFilter` | Can filter/block events |
| `Renderer` | Custom rendering pipeline |
| `Compositor` | Compositor integration |
| `InputHandler` | Custom input handling |
| `ConfigProvider` | Provides configuration schema |
| `Performance` | Performance monitoring hooks |

### ExtensionPriority Levels

| Level | Value | Use Case |
|-------|-------|----------|
| `Lowest` | -1000 | Non-critical background tasks |
| `Low` | -500 | Optional enhancements |
| `Normal` | 0 | Standard extensions |
| `High` | 500 | Important functionality |
| `Highest` | 1000 | Critical features |
| `Critical` | 10000 | Core functionality |

### EventType Enum

| Event | Description |
|-------|-------------|
| `WindowMap` | Window created/mapped |
| `WindowUnmap` | Window hidden/unmapped |
| `WindowDestroy` | Window destroyed |
| `WindowFocus` | Focus changed |
| `WindowMove` | Window moved |
| `WindowResize` | Window resized |
| `WorkspaceSwitch` | Workspace switched |
| `LayoutChange` | Layout mode changed |
| `ConfigReload` | Configuration reloaded |
| `PreRender` | Before frame render |
| `PostRender` | After frame render |
| `All` | All events |

### Result Codes

| Code | Value | Description |
|------|-------|-------------|
| `Success` | 0 | Operation succeeded |
| `InvalidArgument` | -1 | Invalid argument provided |
| `NotSupported` | -2 | Operation not supported |
| `OutOfMemory` | -3 | Memory allocation failed |
| `InvalidState` | -4 | Invalid state for operation |
| `PermissionDenied` | -5 | Permission denied |
| `VersionMismatch` | -6 | API version mismatch |
| `SymbolNotFound` | -7 | Required symbol not found |
| `InitializationFailed` | -8 | Initialization failed |
| `ShutdownFailed` | -9 | Shutdown failed |

### WindowHandle Structure

```cpp
struct WindowHandle {
    uint64_t x11_window;      // X11 Window ID
    uint32_t workspace_id;    // Workspace assignment
    uint32_t flags;           // Window flags
    
    // Flag constants
    static constexpr uint32_t FLAG_FLOATING   = 1 << 0;
    static constexpr uint32_t FLAG_FULLSCREEN = 1 << 1;
    static constexpr uint32_t FLAG_HIDDEN     = 1 << 2;
    static constexpr uint32_t FLAG_URGENT     = 1 << 3;
};
```

---

## Extension Loading Process

1. **Discovery**: The `ExtensionLoader` scans extension directories for `.so` files
2. **Parsing**: Config file `#import`/`#include` directives are parsed
3. **Loading**: Shared library is loaded using `dlopen()`
4. **Symbol Resolution**: Required symbols are resolved using `dlsym()`
5. **ABI Validation**: API version and checksum are verified
6. **Initialization**: `initialize()` is called with the extension context
7. **Event Registration**: Event mask is retrieved and hooks are registered

### ABI Validation

Extensions must match the API version:

```cpp
// In your extension's getInfo():
info.api_version_major = PB_API_VERSION_MAJOR;  // Must match exactly
info.api_version_minor = PB_API_VERSION_MINOR;  // Can be <=
info.api_checksum = pblank::api::v2::computeAPIChecksum();
```

---

## Best Practices

### 1. Use Proper Visibility

```cpp
// Hide all symbols by default, export only required ones
#define PB_API_EXPORT __attribute__((visibility("default")))
#define PB_API_LOCAL  __attribute__((visibility("hidden")))
```

### 2. Handle Errors Gracefully

```cpp
pblank::api::v2::Result initialize(
    const pblank::api::v2::ExtensionContext* ctx) override {
    
    if (!ctx) {
        return pblank::api::v2::Result::InvalidArgument;
    }
    
    if (!ctx->display) {
        return pblank::api::v2::Result::InvalidState;
    }
    
    // ... initialization code ...
    
    return pblank::api::v2::Result::Success;
}
```

### 3. Track Performance

```cpp
bool onWindowMap(const pblank::api::v2::WindowHandle* window) override {
    auto start = std::chrono::steady_clock::now();
    
    // ... event handling code ...
    
    auto elapsed = std::chrono::steady_clock::now() - start;
    total_processing_time_ += elapsed;
    event_count_++;
    
    return true;
}

pblank::api::v2::Nanoseconds getAverageProcessingTime() const override {
    if (event_count_ == 0) return pblank::api::v2::Nanoseconds(0);
    return total_processing_time_ / event_count_;
}
```

### 4. Clean Up Resources

```cpp
pblank::api::v2::Result shutdown() override {
    // Free all resources
    cached_windows_.clear();
    registered_hooks_.clear();
    
    // Reset state
    initialized_ = false;
    
    return pblank::api::v2::Result::Success;
}
```

### 5. Use Appropriate Priority

```cpp
// For critical functionality
info.priority = static_cast<int32_t>(pblank::api::v2::ExtensionPriority::Critical);

// For optional enhancements
info.priority = static_cast<int32_t>(pblank::api::v2::ExtensionPriority::Low);
```

### 6. Declare Capabilities

```cpp
// Layout provider extension
info.capabilities = static_cast<uint64_t>(
    pblank::api::v2::ExtensionCapability::LayoutProvider |
    pblank::api::v2::ExtensionCapability::Performance
);
```

---

## Implementation Reference

The extension system is implemented in the following files:

| File | Description |
|------|-------------|
| [`include/pointblank/extensions/ExtensionAPI.hpp`](/include/pointblank/extensions/ExtensionAPI.hpp) | API definitions and IExtension_v2 interface |
| [`include/pointblank/extensions/ExtensionLoader.hpp`](/include/pointblank/extensions/ExtensionLoader.hpp) | Loader header with class definition |
| [`src/extensions/ExtensionLoader.cpp`](/src/extensions/ExtensionLoader.cpp) | Extension loading logic and lifecycle management |
| [`extension_template/ExampleExtension.cpp`](/extension_template/ExampleExtension.cpp) | Template example with full implementation |

## Example: Complete Extension

See [`extension_template/ExampleExtension.cpp`](/extension_template/ExampleExtension.cpp) for a complete, working example that demonstrates:

- Extension lifecycle management
- Event subscription and handling
- Custom layout provider implementation
- Performance metrics tracking
- Proper ABI versioning

---

## Troubleshooting

### Extension Not Loading

1. Check file permissions: `chmod 644 my_extension.so`
2. Verify API version matches
3. Check for missing symbols: `nm -D my_extension.so | grep createExtension`
4. Review logs for error messages

### ABI Mismatch

```
Error: Extension ABI mismatch
Expected: 2.0.0
Got: 1.0.0
```

Solution: Update your extension to use the current API version from `ExtensionAPI.hpp`.

### Symbol Not Found

```
Error: Symbol not found: createExtension_v2
```

Solution: Ensure you're exporting the correct versioned symbol:

```cpp
extern "C" PB_API_EXPORT pblank::api::v2::IExtension_v2* createExtension_v2() {
    return new MyExtension();
}
```
