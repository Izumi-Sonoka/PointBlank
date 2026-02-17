# Point Blank: Implementation Guide

## Overview

This document details the complete implementation of Point Blank, an infinite tiling layout manager featuring dynamic algorithms including dwindle, monocle, master, BSP, and columns. Version 2.0 introduces a high-fidelity extension system with ABI stability and zero-overhead performance optimizations.

---

## Table of Contents

1. [Extension System v2.0](#extension-system-v20)
2. [Performance Tuning](#performance-tuning)
3. [Lock-Free Data Structures](#lock-free-data-structures)
4. [Render Pipeline](#render-pipeline)
5. [KeybindManager Implementation](#keybindmanager-implementation)
6. [LayoutEngine Implementation](#layoutengine-implementation)
7. [Configuration Schema](#configuration-schema)

---

## Extension System v2.0

> **Note**: For comprehensive extension documentation, see [extensions.md](extensions.md).

### Architecture Overview

The extension system provides a stable ABI for dynamically loaded shared objects (`.so` files). Extensions can hook into window manager events, provide custom layouts, and integrate with the rendering pipeline.

### Key Components

#### 1. ExtensionAPI.hpp
> Location: `include/pointblank/extensions/ExtensionAPI.hpp`

Defines the versioned extension interface:

```cpp
// API Version
#define PB_API_VERSION_MAJOR 2
#define PB_API_VERSION_MINOR 0
#define PB_API_VERSION_PATCH 0

// Extension Interface
class IExtension_v2 {
public:
    virtual const ExtensionInfo* getInfo() const = 0;
    virtual Result initialize(const ExtensionContext* ctx) = 0;
    virtual Result shutdown() = 0;
    
    // Event hooks
    virtual EventMask getEventMask() const;
    virtual bool onWindowMap(const WindowHandle* window);
    virtual bool onWindowFocus(const WindowHandle* old_win, const WindowHandle* new_win);
    // ... more hooks
    
    // Layout provider
    virtual bool hasLayoutProvider() const;
    virtual Result calculateLayout(const LayoutContext* ctx, LayoutOutput* output);
};
```

#### 2. ExtensionLoader.cpp
> Location: `src/extensions/ExtensionLoader.cpp`

Implements the extension loading pipeline with dlopen() based shared object loading, ABI validation with version checking and checksums, hook system (onWindowMap, onWindowUnmap, onWindowFocus, onWorkspaceChange, etc.), and health monitoring with performance tracking.

##### ABI Stability

- **Symbol Versioning**: Versioned symbols prevent runtime mismatches
- **Checksum Validation**: Compile-time checksums verify structure layouts
- **Version Checks**: Major version must match, minor can be <=

```cpp
// Validate extension ABI
bool validateABI(const ExtensionInfo* info) const {
    if (info->api_version_major != PB_API_VERSION_MAJOR) return false;
    if (info->api_version_minor > PB_API_VERSION_MINOR) return false;
    if (info->api_checksum != API_CHECKSUM) return false;
    return true;
}
```

#### 3. Extension Loading Pipeline

Extensions are loaded from two sources:

1. **User Extensions** (`#import` directive → `~/.config/pblank/extensions/user/`)
2. **Built-in Extensions** (`#include` directive → `~/.config/pblank/extensions/pb/`)

```cpp
// Load from manifest
auto results = extensionLoader.loadFromManifest(config_path);

// Load user extensions
int count = extensionLoader.loadUserExtensions();
```

#### 4. Creating an Extension

```cpp
#include "ExtensionAPI.hpp"

class MyExtension : public pblank::IExtension {
public:
    const pblank::ExtensionInfo* getInfo() const override {
        static pblank::ExtensionInfo info = 
            PB_DEFINE_EXTENSION_INFO(
                "MyExtension",
                "1.0.0",
                "Author Name",
                "Description of extension",
                pblank::ExtensionCapability::LayoutProvider,
                pblank::ExtensionPriority::Normal
            );
        return &info;
    }
    
    pblank::Result initialize(const pblank::ExtensionContext* ctx) override {
        // Store context, initialize resources
        return pblank::Result::Success;
    }
    
    pblank::Result shutdown() override {
        // Cleanup resources
        return pblank::Result::Success;
    }
    
    // Event handling
    bool onWindowMap(const pblank::WindowHandle* window) override {
        // Handle window creation
        return true;  // Allow propagation
    }
};

// Export extension
PB_DECLARE_EXTENSION(MyExtension)
```

Build command:
```bash
g++ -shared -fPIC -O3 -o my_extension.so MyExtension.cpp
```

---

## Performance Tuning (src/performance/PerformanceTuner.cpp)

### Scheduler Configuration

Control process scheduling for optimal responsiveness:

```wmi
performance: {
    scheduler_policy: "fifo"      // other, fifo, rr, batch
    scheduler_priority: 50        // 1-99 for real-time
    realtime_mode: true
    lock_memory: true
}
```

### CPU Affinity

Pin the window manager to specific cores for cache locality:

```wmi
performance: {
    cpu_cores: "0,1,2,3"
    cpu_exclusive: false
    hyperthreading_aware: true
}
```

### Frame Timing

Configure render pipeline throttling:

```wmi
performance: {
    target_fps: 60
    min_fps: 30
    max_fps: 144
    vsync: false
    throttle_threshold_us: 1000
    throttle_delay_us: 100
}
```

### Performance Monitoring

```cpp
// Get metrics
auto metrics = performanceTuner.getMetrics();
double fps = performanceTuner.getCurrentFPS();
auto percentiles = performanceTuner.getLatencyPercentiles();

std::cout << "FPS: " << fps << std::endl;
std::cout << "P50 latency: " << percentiles.p50_us << " µs" << std::endl;
std::cout << "P99 latency: " << percentiles.p99_us << " µs" << std::endl;
```

---

## Lock-Free Data Structures (include/pointblank/performance/LockFreeStructures.hpp)

### SPSC Ring Buffer

Single-producer single-consumer queue for event processing:

```cpp
// Create buffer
pblank::lockfree::SPSCRingBuffer<Event, 1024> eventQueue;

// Producer (extension hook)
eventQueue.push(event);

// Consumer (event loop)
auto event = eventQueue.pop();
if (event) {
    processEvent(*event);
}
```

### MPSC Queue

Multi-producer single-consumer for multiple extensions:

```cpp
pblank::lockfree::MPSCQueue<WindowEvent> eventQueue;

// Multiple producers
eventQueue.push(event);

// Single consumer
auto event = eventQueue.pop();
```

### Work-Stealing Deque

For parallel layout calculations:

```cpp
pblank::lockfree::WorkStealingDeq<LayoutTask> tasks;

// Owner pushes/pops
tasks.push(task);
auto t = tasks.pop();

// Thieves steal
auto stolen = tasks.steal();
```

### Cache-Aligned Atomics

Prevent false sharing:

```cpp
pblank::lockfree::CacheAlignedAtomic<uint64_t> counter;
counter.fetchAdd(1, std::memory_order_relaxed);
```

---

## Render Pipeline (src/performance/RenderPipeline.cpp)

### Zero-Overhead Rendering

The render pipeline minimizes overhead through:

1. **Batch Rendering**: Coalesce draw calls
2. **Dirty Rectangles**: Only render changed regions
3. **Double Buffering**: Avoid rendering stalls
4. **Cache-Friendly Layout**: Packed data structures

### Usage

```cpp
// Create pipeline
pblank::RenderPipeline pipeline(display, root);

// Begin frame
auto frameStart = pipeline.beginFrame();

// Queue render commands
pipeline.drawBorder(window, 0x00FF00, 2);
pipeline.moveWindow(window, x, y);
pipeline.setWindowOpacity(window, 0.9f);

// End frame (executes batch)
pipeline.endFrame();
```

### Window Render Data

```cpp
pblank::WindowRenderData data;
data.window = window;
data.x = 100;
data.y = 100;
data.width = 800;
data.height = 600;
data.border_color = 0x00FF00;
data.border_width = 2;
data.opacity = 0.9f;
data.flags = WindowRenderData::FLAG_VISIBLE | WindowRenderData::FLAG_FOCUSED;

pipeline.updateWindow(data);
```

---

## KeybindManager Implementation (src/window/KeybindManager.cpp)

### 1. Keybind String Parsing

The KeybindManager parses keybind strings from the `.wmi` configuration file:

```wmi
binds: {
    "SUPER, SHIFT, Q": "killactive"
    "SUPER, RETURN": exec: "alacritty"
}
```

#### Parsing Logic

**Input Format**: `"MODIFIER1, MODIFIER2, KEY"`

**Process**:
1. Find the last comma to separate modifiers from key
2. Parse modifiers (comma-separated list): SUPER, ALT, CTRL, SHIFT
3. Parse key name using lookup table or XStringToKeysym
4. Convert to X11 KeySym and modifier mask

#### Example Parsing

```cpp
"SUPER, SHIFT, Q" →
  modifiers: Mod4Mask | ShiftMask
  keysym: XK_q
```

### 2. Modifier Parsing

Supported modifiers:
- **SUPER** / **MOD4** → `Mod4Mask`
- **ALT** / **MOD1** → `Mod1Mask`
- **CTRL** / **CONTROL** → `ControlMask`
- **SHIFT** / **L_SHIFT** / **R_SHIFT** → `ShiftMask`

### 3. Key Name Mapping

Common keys are mapped to X11 KeySyms:

```cpp
"RETURN" / "ENTER" → XK_Return
"SPACE"            → XK_space
"ESC" / "ESCAPE"   → XK_Escape
"F1" - "F12"       → XK_F1 - XK_F12
"1" - "9", "0"     → XK_1 - XK_9, XK_0
```

### 4. Key Grabbing with Lock Key Handling

```cpp
// Grab key with all lock combinations
unsigned int lock_modifiers[] = {
    0,                      // No locks
    Mod2Mask,              // Num Lock
    LockMask,              // Caps Lock
    Mod2Mask | LockMask    // Both locks
};

for (unsigned int lock_mod : lock_modifiers) {
    XGrabKey(display, keycode, bind.modifiers | lock_mod, 
             root, True, GrabModeAsync, GrabModeAsync);
}
```

---

## LayoutEngine Implementation (src/layout/LayoutEngine.cpp)

### BSP Tree Structure

The LayoutEngine uses a Binary Space Partitioning tree:

```cpp
class BSPNode {
    Window window_;           // Leaf: window ID
    std::unique_ptr<BSPNode> left_;
    std::unique_ptr<BSPNode> right_;
    SplitType split_type_;    // Horizontal or Vertical
    double ratio_;            // Split ratio (0.1 - 0.9)
};
```

### Dwindle Layout

Automatic alternating split direction:

```cpp
SplitType determineSplitType() {
    // Alternate between horizontal and vertical
    return (split_counter_++ % 2 == 0) 
        ? SplitType::Vertical 
        : SplitType::Horizontal;
}
```

### Layout Algorithms

The LayoutEngine implements 8 layout modes: BSP, Dwindle, Monocle, Master, Columns, Grid, Spiral, and Stack.

#### BSP Layout
```
┌─────────┬───┐
│         │   │
│    A    │ B │
│         │   │
├─────┬───┴───┤
│  C  │   D   │
└─────┴───────┘
```

#### Monocle Layout
```
┌───────────────┐
│               │
│   Focused     │
│    Window     │
│               │
└───────────────┘
```

#### Master-Stack Layout
```
┌─────────┬─────┐
│         │ S1  │
│ Master  ├─────┤
│         │ S2  │
│         ├─────┤
│         │ S3  │
└─────────┴─────┘
```

### Spatial Navigation

Navigate windows by direction:

```cpp
Window moveFocus(const std::string& direction) {
    // "left", "right", "up", "down"
    BSPNode* neighbor = findSpatialNeighbor(focused_node_, direction);
    if (neighbor) {
        focusWindow(neighbor->getWindow());
        return neighbor->getWindow();
    }
    return None;
}
```

---

## Configuration Schema

### Complete Configuration Example

```wmi
pointblank: {
    // Performance tuning
    performance: {
        scheduler_policy: "other"
        scheduler_priority: 0
        cpu_cores: "0,1,2,3"
        target_fps: 60
        vsync: false
        dirty_rectangles_only: true
    };
    
    // Extension system
    extensions: {
        enabled: true
        strict_validation: true
        user_extension_dir: "~/.config/pblank/extensions/user"
    };
    
    // Window rules
    window_rules: {
        opacity: 0.8
        border_width: 2
        gap_size: 10
    };
    
    // Workspaces
    workspaces: {
        max_workspace: 12
        default_workspace: 1
    };
    
    // Key bindings
    binds: {
        "SUPER, Q": "killactive"
        "SUPER, F": "fullscreen"
        "SUPER, H": "focusleft"
        "SUPER, L": "focusright"
        "SUPER, RETURN": exec: "alacritty"
    };
    
    // Layout settings
    layout: {
        bsp: {
            gap_size: 10
            border_width: 2
        };
        masterstack: {
            master_ratio: 0.55
            max_master: 1
        };
    };
    
    // Borders
    borders: {
        focused: "#00FF00"
        unfocused: "#333333"
        urgent: "#FF0000"
    };
};

// Per-application rules
if (window.class == "Firefox") {
    window_rules: { opacity: 1.0; };
};

// Extension imports
#include animation        // Built-in extension from ~/.config/pblank/extensions/pb/
#import custom_layout     // User extension from ~/.config/pblank/extensions/user/
```

---

## Building

### Debug Build

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

### Release Build (Optimized)

```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

### With Tests

```bash
cmake -DBUILD_TESTS=ON ..
make
ctest
```

### Build Extension

```bash
g++ -shared -fPIC -O3 -o my_extension.so MyExtension.cpp
cp my_extension.so ~/.config/pblank/extensions/user/
```

---

## Performance Targets

| Metric | Target | Typical |
|--------|--------|---------|
| Frame time | < 1ms | 0.3ms |
| Event latency P50 | < 100µs | 50µs |
| Event latency P99 | < 500µs | 200µs |
| Memory overhead | < 50MB | 30MB |
| Extension load time | < 10ms | 5ms |

---

## Troubleshooting

### Extension Won't Load

1. Check ABI compatibility:
   ```bash
   nm -D my_extension.so | grep createExtension
   ```

2. Verify dependencies:
   ```bash
   ldd my_extension.so
   ```

3. Check validation logs:
   ```cpp
   auto result = extensionLoader.loadExtension(path);
   if (result.result != Result::Success) {
       std::cerr << result.error_message << std::endl;
   }
   ```

### High Latency

1. Enable real-time mode (requires CAP_SYS_NICE):
   ```wmi
   performance: {
       realtime_mode: true
       realtime_priority: 50
   }
   ```

2. Lock memory:
   ```wmi
   performance: {
       lock_memory: true
   }
   ```

3. Check CPU affinity:
   ```cpp
   auto recommended = performanceTuner.getRecommendedCores();
   ```

---

## License

MIT License - See LICENSE file for details.
