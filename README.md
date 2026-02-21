# Point:Blank -  X11 Infinite Layout Window Manager

> _"Direct, straight-to-the-metal performance. No bloat, no overhead, just efficiency. (And your Samsung smart fridge will love it too.)"_

Version: 1.0.1.0  
Last Updated: 19th February 2026 (1500 GMT+8)  
Maintained by:  Point:project

> [!WARNING]
> Still in WIP, Things may break.

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Point:Blank](#why-pointblank)
3. [Memory Footprint](#memory-footprint)
4. [Installation](#installation)
5. [Configuration](#configuration)
6. [Layout Modes](#layout-modes)
7. [The DSL](#the-dsl)
8. [Extension System](#extension-system)
9. [External Bar Integration](#external-bar-integration)
10. [EWMH Compliance](#ewmh-compliance)
11. [Architecture Overview](#architecture-overview)
12. [Contributing](#contributing)
13. [FAQ](#faq)

---

## Introduction

### What is Point:Blank?

Point:Blank is a modern, source-built tiling window manager for X11 with infinite layouts, infinite workspaces, and a crash-proof configuration system. It's configured with a proper DSL not shell scripts, not C patches, not YAML nightmares. Think of it as what happens when someone gets mad enough at existing WMs to write their own.

### The Name

- **Point**: The fundamental atom of coordinates. Every window is a singular, precise location in 2D space.
- **Blank**: The Total Failure state. Bad config? You get nothing but the void. Goodbye desktop.
- **Point-Blank**: Direct. Straight-to-the-metal. No middleware.

### Who Makes This?

**N3ZT POSSIBLE G3N** - upcoming  
**Point:project**  
Maintained by **Astaraxia Linux's Maniac.**

---

## Why Point:Blank?

- **401 KB PSS at idle** - leaner than dwm while doing more than i3
- **8 layout modes** - not just one "tiling" mode like every other WM
- **A real DSL** - `"SUPER, Q": "killactive"` just works
- **Full EWMH compliance** - 50+ atoms, polybar/lemonbar/yambar just works
- **Crash-proof** - bad config shows a toast notification, not a blank screen
- **Infinite workspaces** - dynamic creation, auto-remove on empty
- **Extension system v2.0** - dlopen-based plugins with ABI validation
- **Hot-reload** - edit config, save, done. No restart.

### Why Not Point:Blank?

- You want Wayland (this is X11 only)
- You're a normal person who uses a DE
- You don't want to think about window managers at all (valid, honestly)

---

## Memory Footprint
Because this is the whole point. verified with `smem`, `pmap`, and `/proc/[pid]/smaps`.

### IDLE
Idle Memory usage.

#### Numbers

|Metric|Value|What it means|
|---|---|---|
|**PSS (Fair Share)**|**401 KB**|Real-world RAM impact on your system|
|**Own Memory**|**250 KB**|Binary + heap|
|**Active Dirty Pages**|**90 KB**|Memory actually modified by core logic|
|**Total RSS**|**3.6 MB**|Including all shared system libraries|

#### Breakdown

```
Pointblank Core:
  Binary Code:  184 KB  (76.7% of own memory)
  Heap:          48 KB  (20.0%)
  Stack:          8 KB  (3.3%)
  ─────────────────────
  Total:        240 KB RSS | 56 KB Dirty

System Libraries (shared pages, counted fractionally):
  libc.so.6:          1,284 KB
  libm.so.6:            568 KB
  libharfbuzz.so:       388 KB
  libglib-2.0.so:       372 KB
  libgio-2.0.so:        288 KB
  ─────────────────────
  Total:            3,396 KB RSS | 16 KB Dirty
```

#### vs. Everyone Else

|Window Manager|PSS|vs Point:Blank|Notes|
|---|---|---|---|
|**Point:Blank**|**401 KB**|—|8 layouts, full EWMH, DSL|
|dwm|~2,500 KB|6x larger|3 layouts, patch C to configure|
|bspwm|~10,000 KB|25x larger|1 layout (BSP), shell scripts|
|i3|~18,000 KB|45x larger|1 layout, complex IPC|
|Openbox|~12,000 KB|30x larger|Floating only|

#### Why So Lean?

- **Zero-copy parsing**: `std::string_view` throughout the DSL — no string allocations during config parse
- **Stack preference**: RAII with `std::array` over heap where possible
- **Xlib purity**: Direct X Server communication, no middleware
- **No RTTI/exceptions in release**: `-fno-rtti -fno-exceptions` in Release build
- **LTO**: Full link-time optimisation across all 61 files

**Hardware tested on**: Lenovo 500e 2nd Gen Chromebook, Arch Linux  
**Codebase**: C++20, 19,010 lines across 61 files (29 .cpp + 32 .hpp)

> _"Optimized for everything. (including your samsung smart fridge.)"_

---

## Architecture Overview

Clean, modular, no spaghetti. Each component does one thing well:
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
│    ├─ Parser: Recursive Descent → AST (std::variant)        │
│    ├─ Interpreter: AST → Runtime Config                     │
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
│    ├─ Custom Point:Blank Atoms (external bar integration)   │
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
├─────────────────────────────────────────────────────────────┤
│  ScratchpadManager                                          │
│    ├─ Scratchpad window storage                             │
│    ├─ Show/hide toggle commands                             │
│    └─ Per-window scratchpad assignment                      │
├─────────────────────────────────────────────────────────────┤
│  WindowSwallower                                            │
│    ├─ Terminal window detection                             │
│    ├─ Child window tracking                                 │
│    └─ Automatic swallow/unswallow                           │
├─────────────────────────────────────────────────────────────┤
│  IPCServer                                                  │
│    ├─ Unix socket server                                    │
│    ├─ JSON command protocol                                 │
│    └─ External control (reload, workspace switch, etc.)     │
├─────────────────────────────────────────────────────────────┤
│  StartupApps                                                │
│    ├─ XDG autostart parsing                                 │
│    └─ Application launch on session start                   │
└─────────────────────────────────────────────────────────────┘
```

---

## Installation

### Prerequisites

You'll need:

- Linux
- A C++20 compiler (GCC 10+ or Clang 12+)
- CMake 3.20+
- X11 libraries (you're on X11, so probably already have most of these)
- Cairo (for the OSD toaster)
- GLib/GIO (for D-Bus notifications)
- Coffee (not technically required, but proven to help)

### Install Dependencies

**Arch / Manjaro**

```bash
sudo pacman -S libx11 libxrender libxft libxrandr cairo glib2 cmake gcc xcb-util-wm xcb-util-keysyms
```

**Debian / Ubuntu**

```bash
sudo apt install libx11-dev libxrender-dev libxft-dev libxrandr-dev libcairo2-dev libglib2.0-dev libxcb-ewmh-dev libxcb-icccm4-dev cmake g++
```

**Fedora**

```bash
sudo dnf install libX11-devel libXrender-devel libXft-devel libXrandr-devel cairo-devel glib2-devel xcb-util-wm-devel xcb-util-keysyms-devel cmake gcc-c++
```

**Gentoo**

```bash
sudo emerge --ask x11-libs/libX11 x11-libs/libXrender x11-libs/libXft x11-libs/libXrandr x11-libs/cairo dev-libs/glib dev-build/cmake sys-devel/gcc
```

**Alpine**

```bash
sudo apk add libx11-dev libxrender-dev libxft-dev libxrandr-dev cairo-dev glib-dev cmake g++
```

### Build

```bash
git clone https://github.com/Astaraxia-Linux/Pointblank
cd Pointblank
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
sudo make install
```

Go grab a coffee. 30 seconds on x86, a bit longer on ARM. Less than recompiling Firefox by a factor of approximately forever.

### Start Point:Blank

Add to your `~/.xinitrc`:

```bash
exec pointblank
```

Or use the installed `.desktop` file with your display manager.

---

## Configuration

No shell scripts. No C patching. No YAML. Just a DSL that makes sense.

### File Structure

```
~/.config/pblank/
├── pointblank.wmi              # Main configuration
└── extensions/
    ├── pb/                     # Built-in extensions (#include)
    │   └── lib*.so
    └── user/                   # Your custom extensions (#import)
        └── *.so
```

### Example Configuration

```wmi
// Core config
pointblank: {
    window_rules: {
        opacity: 0.95
    };

    workspaces: {
        max_workspace: 12
    };

    binds: {
        "SUPER, Q":          "killactive"
        "SUPER, F":          "fullscreen"
        "SUPER, RETURN":     exec: "alacritty"
        "SUPER, D":          exec: "rofi -show drun"
        "SUPER, 1":          "workspace 1"
        "SUPER, 2":          "workspace 2"
        "SUPER, H":          "focusleft"
        "SUPER, L":          "focusright"
        "SUPER, K":          "focusup"
        "SUPER, J":          "focusdown"
        "SUPER, SHIFT, Q":   "quit"
    };
};

// Per-application rules
if (window.class == "Firefox") {
    window_rules: {
        opacity: 1.0
    };
};
```

See [GRAMMAR.md](GRAMMAR.md) for the full DSL specification.

### Hot-Reload

Point:Blank watches your config file. Save it, it reloads. No restart needed. If your config is broken, you get a toast notification and it falls back to defaults — the WM keeps running.

---

## Layout Modes

Eight layouts. Not one "tiling" mode. Eight.

### Overview

|Layout|Description|
|---|---|
|**BSP**|Binary Space Partition — recursive screen splitting|
|**Monocle**|One window fullscreen at a time|
|**MasterStack**|One master, rest stack on the side|
|**CenteredMaster**|Master in center, stacks on both sides|
|**DynamicGrid**|Uniform N×M grid based on window count|
|**DwindleSpiral**|Fibonacci spiral outward|
|**TabbedStacked**|Tab bar with stacked tiling below|
|**InfiniteCanvas**|Unbounded virtual canvas with viewport panning|

### Layouts Visualised

**BSP**

```
┌─────────┬───┐
│         │   │
│    A    │ B │
│         │   │
├─────┬───┴───┤
│  C  │   D   │
└─────┴───────┘
```

**MasterStack**

```
┌─────────┬─────┐
│         │ S1  │
│ Master  ├─────┤
│         │ S2  │
│         ├─────┤
│         │ S3  │
└─────────┴─────┘
```

**CenteredMaster**

```
┌─────┬─────────┬─────┐
│ L1  │         │ R1  │
│     │ Master  │     │
├─────┤         ├─────┤
│ L2  │         │ R2  │
└─────┴─────────┴─────┘
```

**DwindleSpiral**

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

**InfiniteCanvas**

```
    ┌────────────────────┐
    │      Viewport      │
    │   ┌──────────┐     │
    │   │ Window A │     │
    │   └──────────┘     │
    │          ┌───────┐ │
    │          │   B   │ │
    └──────────┴───────┴─┘
    (Windows can live anywhere in virtual space)
```

### Switching Layouts

```wmi
binds: {
    "SUPER, SPACE":       "cyclelayout"
    "SUPER, M":           "layout monocle"
    "SUPER, T":           "layout bsp"
    "SUPER, G":           "layout grid"
};
```

### Adding Custom Layouts

Point:Blank uses the visitor pattern — implement one interface, register it:

```cpp
class MyLayout : public LayoutVisitor {
    void visit(BSPNode* root, const Rect& bounds, Display* display) override {
        // Your layout logic here
        // Traverse tree, call XMoveResizeWindow on each leaf
    }
};

layout_engine_->setLayout(workspace, std::make_unique<MyLayout>());
```

---

## The DSL

Point:Blank uses a custom DSL called `.wmi`. It's configured like a language, not like a config file.

### Basics

```wmi
// Single-line comment
/* Multi-line comment */

// Types
let opacity = 0.95;         // float
let max_ws  = 12;           // int
let terminal = "alacritty"; // string
let gaps = true;            // bool
```

### Blocks

```wmi
pointblank: {
    window_rules: {
        opacity: 0.95
    };
};
```

### Keybinds

```wmi
binds: {
    "SUPER, Q":          "killactive"
    "SUPER, RETURN":     exec: "alacritty"
    "SUPER, SHIFT, Q":   "quit"
};
```

Modifiers: `SUPER`, `ALT`, `CTRL`, `SHIFT`, `L_SHIFT`, `R_SHIFT`

### Conditionals

```wmi
if (window.class == "Firefox") {
    window_rules: {
        opacity: 1.0
    };
} else {
    window_rules: {
        opacity: 0.9
    };
};
```

### Preprocessor

```wmi
#include gaps           // Built-in extension from extensions/pb/
#import my_extension    // Your extension from extensions/user/
```

### Autostart

```wmi
autostart: {
    exec: "picom -b"
    exec: "dunst"
    exec: "nitrogen --restore"
};
```

See [GRAMMAR.md](GRAMMAR.md) for the complete specification.

---

## Extension System

Point:Blank loads extensions as shared objects at runtime. Write your extension in C or C++, compile it, drop the `.so` in the right folder, `#import` it in your config.

### API v2.0

```c
// myextension.c
#include <pointblank/extension.h>

static void on_window_map(Window window) {
    // do something when a window opens
}

ExtensionABI extension_abi = {
    .version         = PB_ABI_VERSION,
    .name            = "myextension",
    .onWindowMap     = on_window_map,
    .onWindowUnmap   = NULL,
    .onWindowFocus   = NULL,
    .onWorkspaceChange = NULL,
};
```

### Available Hooks

|Hook|Triggered when|
|---|---|
|`onWindowMap`|A window is opened/mapped|
|`onWindowUnmap`|A window is closed/unmapped|
|`onWindowFocus`|Focus changes to a different window|
|`onWorkspaceChange`|User switches workspace|

### ABI Validation

Extensions are checked for ABI version compatibility at load time. Outdated extension? Point:Blank logs a warning and skips it — it doesn't crash.

See extensions.md for the full API reference and examples.

---

## External Bar Integration

Polybar, lemonbar, yambar — they all work. Point:Blank writes state to both X11 root window properties and files so you can use whichever approach you prefer.

### File-Based (Easiest)

```bash
# Get current layout
cat /tmp/pointblank/currentlayout
# Output: BSP
```

### X11 Properties

```bash
xprop -root _PB_CURRENT_WORKSPACE    # Current workspace (0-indexed)
xprop -root _PB_ACTIVE_WINDOW_TITLE  # Focused window title
xprop -root _PB_LAYOUT_MODE          # Current layout name
xprop -root _PB_OCCUPIED_WORKSPACES  # Which workspaces have windows
```

### Custom Atoms

|Atom|Type|Description|
|---|---|---|
|`_PB_CURRENT_WORKSPACE`|CARDINAL|Current workspace (0-indexed)|
|`_PB_WORKSPACE_NAMES`|UTF8_STRING|Comma-separated workspace names|
|`_PB_OCCUPIED_WORKSPACES`|CARDINAL[]|Workspaces that have windows|
|`_PB_ACTIVE_WINDOW_TITLE`|UTF8_STRING|Title of focused window|
|`_PB_ACTIVE_WINDOW_CLASS`|UTF8_STRING|WM_CLASS of focused window|
|`_PB_LAYOUT_MODE`|UTF8_STRING|Current layout name|
|`_PB_WORKSPACE_WINDOW_COUNTS`|CARDINAL[]|Window count per workspace|

### Standard EWMH

Point:Blank also sets all standard `_NET_*` properties, so polybar's built-in `internal/xworkspaces` and `internal/xwindow` modules work out of the box.

See EXTERNAL_BAR_INTEGRATION.md for polybar/lemonbar/yambar config examples.

---

## EWMH Compliance

50+ atoms. Full compliance. Your desktop tools just work.

### What's Implemented

- **Root properties**: `_NET_SUPPORTED`, `_NET_SUPPORTING_WM_CHECK`, `_NET_CURRENT_DESKTOP`, `_NET_NUMBER_OF_DESKTOPS`, `_NET_DESKTOP_NAMES`, `_NET_WORKAREA`
- **Client list**: `_NET_CLIENT_LIST`, `_NET_CLIENT_LIST_STACKING`
- **Window types**: Normal, dialog, utility, toolbar, splash, menu, dock, desktop — all handled
- **Window states**: Fullscreen, maximized, hidden, sticky, above, below, demands attention
- **Window actions**: Move, resize, minimize, fullscreen, close, change desktop
- **Client messages**: `_NET_CLOSE_WINDOW`, `_NET_ACTIVE_WINDOW`, `_NET_WM_MOVERESIZE`
- **Struts**: Status bars reserve their space correctly via `_NET_WM_STRUT_PARTIAL`

---

## Architecture Overview

Clean, modular, no spaghetti. Each component does one thing.

```
┌─────────────────────────────────────────────────────────┐
│                    Point:Blank WM                       │
├─────────────────────────────────────────────────────────┤
│  WindowManager       — Event loop & orchestration       │
├─────────────────────────────────────────────────────────┤
│  ConfigParser        — Lexer → Parser → AST → Config    │
├─────────────────────────────────────────────────────────┤
│  LayoutEngine        — BSP tree + 8 layout visitors     │
├─────────────────────────────────────────────────────────┤
│  EWMHManager         — 50+ EWMH atoms + custom _PB_*    │
├─────────────────────────────────────────────────────────┤
│  MonitorManager      — XRandR multi-monitor support     │
├─────────────────────────────────────────────────────────┤
│  ExtensionLoader     — dlopen plugin system v2.0        │
├─────────────────────────────────────────────────────────┤
│  Toaster             — Cairo/D-Bus OSD notifications    │
├─────────────────────────────────────────────────────────┤
│  KeybindManager      — Key grabbing & action dispatch   │
├─────────────────────────────────────────────────────────┤
│  IPCServer           — Unix socket external control     │
├─────────────────────────────────────────────────────────┤
│  ScratchpadManager   — Hide/show window scratchpad      │
└─────────────────────────────────────────────────────────┘
```

### File Layout

```
Pointblank/
├── src/
│   ├── core/           # WindowManager, SessionManager, Toaster
│   ├── config/         # ConfigParser, ConfigWatcher, StartupApps
│   ├── layout/         # LayoutEngine, LayoutProvider
│   ├── display/        # EWMHManager, MonitorManager, SyncManager
│   ├── window/         # KeybindManager, FloatingWindowManager, Scratchpad
│   ├── ipc/            # IPCServer
│   ├── performance/    # PerformanceTuner, RenderPipeline, LockFreeStructures
│   └── utils/          # GapConfig, SpatialGrid, Camera
├── include/pointblank/ # Public headers
├── extension_template/ # Example extension to copy from
├── contrib/            # .desktop file, xinitrc example
├── text/               # Documentation
└── CMakeLists.txt
```

---

## Contributing

### Writing Extensions

1. Copy `extension_template/` as a starting point
2. Implement the hooks you need
3. Compile to a `.so`
4. Drop it in `~/.config/pblank/extensions/user/`
5. Add `#import yourextension` to your config

### Contributing Core Changes

1. Read the code (good luck! it's dense but it's clean)
2. Test your changes (non-negotiable)
3. Submit a PR
4. Wait for the One Maniac™ to review

**Style**: Modern C++20, RAII everywhere, no raw owning pointers, no exceptions in hot paths.

### Contributing Extensions to the Official Set

Submit to the Point:project extension repository. Extensions go through ABI review before they're bundled.

---

## FAQ

### Why not Hyprland/i3/bspwm?

Hyprland is Wayland. i3 and bspwm are great but sit at 15–45x Point:Blank's memory usage with fewer layout options. Also, writing your own is character development at its peak, ngl.

### Why X11 and not Wayland?

Later lah boss, wait yeah. wait for AkibaraWM

### Does it work on [insert distro here]?

If it has X11 and a C++20 compiler, probably. Tested on Arch Linux. Everything else is "should work".

### What's the minimum hardware?

Tested on a Lenovo 500e Chromebook (a machine that has no business running a custom WM). If it runs there, it runs anywhere (*including your Samsung smart frid-*).

### Is it stable?

Define stable. The core event loop and EWMH are solid. Some features are still being wired together. Check the current release notes (non-existence).

### Why a custom DSL and not TOML/JSON/YAML?

Because `"SUPER, Q": "killactive"` is more readable than any of those for keybinds. Because `if (window.class == "Firefox")` is how humans think. Because we could.

### Who maintains this?

One Maniac. Send help. Or coffee. Or bug reports, those are also useful.

---

## Documentation

|File|What's in it|
|---|---|
|GRAMMAR.md|Complete DSL specification with EBNF grammar|
|DOCUMENTATION.md|Developer docs — layouts, internals, extending|
|extensions.md|Extension API reference and examples|
|ARCHITECTURE_SUMMARY.md|System architecture deep-dive|
|EXTERNAL_BAR_INTEGRATION.md|Polybar/lemonbar/yambar config examples|

---

## Credits

- **Created by**: N3ZT POSSIBLE G3N x Point:project
- **Inspired by**: i3, bspwm, dwm, and pure spite
- **Tested on**: Hardware that most people forgot existed
- **Special thanks**: Anyone who filed a bug instead of just silently giving up (no one)

---

## License

MIT — see LICENSE file.

---

## Final Notes

> _"If there's a limit, we break through it._  
> _If there's no limit, we become the limit._  
> _Repeat until it fails."_  
> - _N3ZT POSSIBLE G3N_

Point:Blank is still evolving. The philosophy is based, the implementation is transparent, and the memory usage is genuinely embarrassingly small. If you made it this far, you're either building a WM or very bored. Either way happy using Point:Blank!!

---

**Last updated**: 21th February 2026 (1133 GMT+8)  
**Documentation version**: 2.0  
**Sanity level**: Questionable but functional
