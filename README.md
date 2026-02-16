# Point:Blank - X11 Tiling Window Manager

A modern, DSL-configured tiling window manager for X11 with "Infinite Layouts", "Infinite Workspace" and crash-proof (not yet!!!) configuration.

## Features

- **DSL Configuration**: `.wmi` (Window Manager Interface) files with QML-like syntax
- **Crash-Proof Design**: Visual OSD "Toaster" + D-Bus notifications for errors
- **Binary Space Partitioning**: Flexible BSP tree-based layout engine
- **Multiple Layout Modes**: BSP, Monocle, Master-Stack, Centered Master, Dynamic Grid, Dwindle Spiral (extensible)
- **Extension System**: Load custom extensions for animations, rounded corners, and more
- **Modern C++20**: RAII everywhere, no raw pointers, `std::variant` AST
- **Preprocessor System**: `#import` and `#include` for modular configs and extensions
- **Conditional Rules**: Per-window configuration based on class/title
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
│    └─ Import Resolution (#import / #include)                │
├─────────────────────────────────────────────────────────────┤
│  LayoutEngine (BSP Tree & Layout Visitors)                  │
│    ├─ BSPNode: Binary tree with windows or splits           │
│    ├─ LayoutVisitor: Abstract interface for layouts         │
│    │   ├─ BSPLayout: Traditional BSP tiling                 │
│    │   ├─ MonocleLayout: Full-screen single window          │
│    │   └─ MasterStackLayout: Dwm-style master/stack         │
│    └─ Per-workspace trees with focus tracking               │
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

- **X11 Libraries**: `libx11`, `libxrender`, `libxft`
- **Cairo**: For OSD rendering
- **GLib/GIO**: For D-Bus notifications
- **C++20 Compiler**: GCC 10+ or Clang 12+
- **CMake**: 3.20+

### Build Instructions


#### Install dependencies 
- Debian / Ubuntu
``` bash
sudo apt install libx11-dev libxrender-dev libxft-dev libcairo2-dev libglib2.0-dev cmake g++
```
- Arch / Manjaro
``` bash
sudo pacman -S libx11 libxrender libxft cairo glib2 cmake gcc
```
- Fedora
``` bash
sudo dnf install libX11-devel libXrender-devel libXft-devel cairo-devel glib2-devel cmake gcc-c++
```
- Gentoo
``` bash
sudo emerge --ask x11-libs/libX11 x11-libs/libXrender x11-libs/libXft x11-libs/cairo dev-libs/glib dev-build/cmake sys-devel/gcc
```
- openSUSE
``` bash
sudo zypper install libX11-devel libXrender-devel libXft-devel cairo-devel glib2-devel cmake gcc-c++
```
- Alpine
``` bash
sudo apk add libx11-dev libxrender-dev libxft-dev cairo-dev glib-dev cmake g++
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
    └── user/               # User custom extensions (#import)
        └── custom.wmi
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

// Per-application rules
if (window.class == "Firefox") {
    window_rules: {
        opacity: 1.0
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

## File Organization

```
pointblank/
├── WindowManager.hpp/cpp    # Main WM orchestration
├── ConfigParser.hpp/cpp     # DSL lexer/parser/interpreter
├── LayoutEngine.hpp/cpp     # BSP tree & layout visitors
├── Toaster.hpp/cpp          # OSD notification system
├── KeybindManager.hpp/cpp   # Keyboard shortcut handling
├── ExtensionLoader.hpp/cpp  # Extension loading and management
├── ExtensionAPI.hpp         # Extension API definitions
├── main.cpp                 # Entry point
├── CMakeLists.txt           # Build configuration
├── text/
│   ├── GRAMMAR.md           # DSL specification
│   ├── DOCUMENTATION.md     # Developer documentation
│   ├── extensions.md        # Extension system guide
│   └── README.md            # This file
├── extension_template/      # Example extension template
├── extensions/              # Built-in extensions
│   ├── AnimationExtension.cpp
│   └── RoundedCornersExtension.cpp
└── config/
    └── pointblank.wmi       # Default configuration
```

## Documentation

- [GRAMMAR.md](GRAMMAR.md) - Complete DSL specification
- [DOCUMENTATION.md](DOCUMENTATION.md) - Developer documentation for layouts and extensions
- [extensions.md](extensions.md) - Extension system guide and API reference

## Contributing

Key areas for extension:

1. **New Layout Modes**: Implement `LayoutVisitor` interface
2. **Extensions**: Create custom extensions using the v2.0 API (see [extensions.md](extensions.md))
3. **Window Rules**: Extend conditional matching system
4. **Animation System**: Integrate with layout transitions
5. **Multi-monitor**: Add Xinerama/RandR support (please help me i dont have an extra monitor to test 😭)

## License

MIT License.

## Acknowledgments

- Inspired by i3, bspwm, and dwm
- Uses QML-style syntax for configuration
- Built with modern C++20 best practices
