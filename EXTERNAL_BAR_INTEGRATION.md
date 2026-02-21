# External Status Bar Integration Guide

This document describes how to integrate external status bars (like polybar, lemonbar, yambar, etc.) with the Pointblank Window Manager.

## Implementation Status

**All properties listed in this document are fully implemented in [`EWMHManager`](/src/display/EWMHManager.cpp) and [`EWMHManager.hpp`](/include/pointblank/display/EWMHManager.hpp).** The EWMHManager class provides complete support for both custom Pointblank atoms and standard EWMH properties.

## Overview

Pointblank provides custom X11 root window properties that external status bars can read to display workspace information, active window details, and layout mode. This approach follows the same philosophy as other tiling window managers like bspwm and i3.

## File-Based Integration

In addition to X11 properties, Pointblank also writes current state to files for easier integration:

| File | Description |
|------|-------------|
| `/tmp/pointblank/currentlayout` | Current layout mode (BSP, Monocle, MasterStack, etc.) |

### Reading from Files

```bash
# Get current layout mode
cat /tmp/pointblank/currentlayout
# Output: BSP

# Monitor for changes (using inotifywait)
inotifywait -m /tmp/pointblank -e close_write | while read event; do
    cat /tmp/pointblank/currentlayout
done
```

This file-based approach is simpler for scripts and status bars that don't want to interact with X11 properties directly.

## Available Properties

All properties are set on the root window and can be read using `xprop` or X11 libraries.

### Custom Pointblank Properties

| Property | Type | Description | Implementation |
|----------|------|-------------|----------------|
| `_PB_CURRENT_WORKSPACE` | CARDINAL (32-bit) | Current workspace number (0-indexed) | [`EWMHManager::setCurrentWorkspacePB()`](/src/display/EWMHManager.cpp:975) |
| `_PB_WORKSPACE_NAMES` | UTF8_STRING | Comma-separated workspace names | [`EWMHManager::setWorkspaceNamesPB()`](/src/display/EWMHManager.cpp:981) |
| `_PB_OCCUPIED_WORKSPACES` | CARDINAL[] | Array of workspace numbers that have windows | [`EWMHManager::setOccupiedWorkspacesPB()`](/src/display/EWMHManager.cpp:995) |
| `_PB_ACTIVE_WINDOW_TITLE` | UTF8_STRING | Title of the currently focused window | [`EWMHManager::setActiveWindowTitlePB()`](/src/display/EWMHManager.cpp:1012) |
| `_PB_ACTIVE_WINDOW_CLASS` | UTF8_STRING | WM_CLASS of the currently focused window | [`EWMHManager::setActiveWindowClassPB()`](/src/display/EWMHManager.cpp:1019) |
| `_PB_LAYOUT_MODE` | UTF8_STRING | Current layout mode: "BSP", "Monocle", "MasterStack", "Centered", "Grid", "Dwindle", "Tabbed" | [`EWMHManager::setLayoutModePB()`](/src/display/EWMHManager.cpp:1026) |
| `_PB_WORKSPACE_WINDOW_COUNTS` | CARDINAL[] | Array of window counts per workspace | [`EWMHManager::setWorkspaceWindowCountsPB()`](/src/display/EWMHManager.cpp:1033) |

### Standard EWMH Properties

Pointblank also implements standard EWMH properties for compatibility with status bars and other desktop tools:

| Property | Type | Description | Implementation |
|----------|------|-------------|----------------|
| `_NET_CURRENT_DESKTOP` | CARDINAL (32-bit) | Current workspace number | [`EWMHManager::setCurrentDesktop()`](/src/display/EWMHManager.cpp:300) |
| `_NET_NUMBER_OF_DESKTOPS` | CARDINAL (32-bit) | Total number of workspaces | [`EWMHManager::setNumberOfDesktops()`](/src/display/EWMHManager.cpp:291) |
| `_NET_DESKTOP_NAMES` | UTF8_STRING | Workspace names (null-separated) | [`EWMHManager::setDesktopNames()`](/src/display/EWMHManager.cpp:309) |
| `_NET_ACTIVE_WINDOW` | WINDOW | Window ID of the active window | [`EWMHManager::setActiveWindow()`](/src/display/EWMHManager.cpp:384) |
| `_NET_CLIENT_LIST` | WINDOW[] | List of all managed windows | [`EWMHManager::setClientList()`](/src/display/EWMHManager.cpp:358) |

## Reading Properties from Command Line

### Using xprop

```bash
# Get current workspace
xprop -root _PB_CURRENT_WORKSPACE

# Get active window title
xprop -root _PB_ACTIVE_WINDOW_TITLE

# Get layout mode
xprop -root _PB_LAYOUT_MODE

# Get occupied workspaces
xprop -root _PB_OCCUPIED_WORKSPACES
```

### Example Output

```
$ xprop -root _PB_CURRENT_WORKSPACE
_PB_CURRENT_WORKSPACE(CARDINAL) = 2

$ xprop -root _PB_ACTIVE_WINDOW_TITLE
_PB_ACTIVE_WINDOW_TITLE(UTF8_STRING) = "main.cpp - Pointblank - Visual Studio Code"

$ xprop -root _PB_LAYOUT_MODE
_PB_LAYOUT_MODE(UTF8_STRING) = "BSP"
```

### Reading EWMH Properties

You can also use standard EWMH properties:

```bash
# Get current desktop (EWMH standard)
xprop -root _NET_CURRENT_DESKTOP

# Get number of desktops
xprop -root _NET_NUMBER_OF_DESKTOPS

# Get desktop names
xprop -root _NET_DESKTOP_NAMES

# Get active window ID
xprop -root _NET_ACTIVE_WINDOW

# Get client list (all windows)
xprop -root _NET_CLIENT_LIST
```

### Example EWMH Output

```
$ xprop -root _NET_CURRENT_DESKTOP
_NET_CURRENT_DESKTOP(CARDINAL) = 2

$ xprop -root _NET_DESKTOP_NAMES
_NET_DESKTOP_NAMES(UTF8_STRING) = "Workspace 1\0Workspace 2\0Workspace 3\0"

$ xprop -root _NET_ACTIVE_WINDOW
_NET_ACTIVE_WINDOW(WINDOW) = 0x4a0000c
```

## Polybar Configuration

Here's an example polybar configuration for Pointblank:

### Using Custom Pointblank Properties

```ini
; ~/.config/polybar/config.ini

[bar/pointblank]
width = 100%
height = 24
background = #1E1E2E
foreground = #CDD6F4
font-0 = "Sans:size=12;1"

modules-left = workspaces layout
modules-center = title
modules-right = date

[module/workspaces]
type = custom/script
exec = ~/.config/polybar/scripts/workspaces.sh
tail = true
click-left = ~/.config/polybar/scripts/switch-workspace.sh %index%

[module/layout]
type = custom/script
exec = xprop -root _PB_LAYOUT_MODE | cut -d'"' -f2
tail = true

[module/title]
type = custom/script
exec = xprop -root _PB_ACTIVE_WINDOW_TITLE | cut -d'"' -f2
tail = true

[module/date]
type = internal/date
date = %Y-%m-%d %H:%M
```

### Using Standard EWMH Properties

Some polybar modules can directly use EWMH properties:

```ini
[module/workspaces-ewmh]
type = internal/xworkspaces
; Use EWMH properties
format = <label-state>
format-icons = [1],[2],[3],[4],[5]
label-active = %index%
label-occupied = %index%
label-empty = %index%

[module/title-ewmh]
type = internal/xwindow
format = <title>
format-max = 50
truncate = end
```

### Helper Scripts

**`~/.config/polybar/scripts/workspaces.sh`:**
```bash
#!/bin/bash

# Get current workspace
current=$(xprop -root _PB_CURRENT_WORKSPACE 2>/dev/null | grep -oP '= \K\d+')

# Get occupied workspaces
occupied=$(xprop -root _PB_OCCUPIED_WORKSPACES 2>/dev/null | grep -oP '= \K.*' | tr -d ',')

# Build workspace string
output=""
for i in {0..9}; do
    if [ "$i" -eq "$current" ]; then
        # Current workspace - highlighted
        output+="%{B#89B4FA F#1E1E2E} $((i+1)) %{B- F-}"
    elif echo "$occupied" | grep -q "$i"; then
        # Occupied workspace
        output+="%{B#45475A} $((i+1)) %{B-}"
    else
        # Empty workspace
        output+=" $((i+1)) "
    fi
done

echo "$output"
```

**`~/.config/polybar/scripts/switch-workspace.sh`:**
```bash
#!/bin/bash
# Switch to workspace (1-indexed)
WORKSPACE=$1
# Use wmctrl or pointblank's own command
wmctrl -s $((WORKSPACE - 1))
```

## Lemonbar Configuration

Here's an example lemonbar configuration:

```bash
#!/bin/bash

# Function to get workspace info
workspaces() {
    current=$(xprop -root _PB_CURRENT_WORKSPACE 2>/dev/null | grep -oP '= \K\d+')
    occupied=$(xprop -root _PB_OCCUPIED_WORKSPACES 2>/dev/null | grep -oP '= \K.*' | tr -d ',')
    
    output=""
    for i in {0..9}; do
        if [ "$i" -eq "$current" ]; then
            output+="%{B#89B4FA F#1E1E2E} $((i+1)) %{B- F-}"
        elif echo "$occupied" | grep -q "$i"; then
            output+="%{B#45475A} $((i+1)) %{B-}"
        else
            output+=" $((i+1)) "
        fi
    done
    echo "$output"
}

# Function to get window title
title() {
    xprop -root _PB_ACTIVE_WINDOW_TITLE 2>/dev/null | cut -d'"' -f2
}

# Function to get layout mode
layout() {
    xprop -root _PB_LAYOUT_MODE 2>/dev/null | cut -d'"' -f2
}

# Main loop
while true; do
    echo "%{l}$(workspaces)  |  $(layout)%{c}$(title)%{r}$(date '+%Y-%m-%d %H:%M')"
    sleep 0.5
done | lemonbar -B '#1E1E2E' -F '#CDD6F4' -f 'Sans:size=12'
```

## Yambar Configuration

Here's an example yambar configuration:

```yaml
# ~/.config/yambar/config.yml

bar:
  location: top
  height: 24
  background: 1e1e2eff
  foreground: cdd6f4ff

  left:
    - script:
        path: ~/.config/yambar/scripts/workspaces.sh
        poll-interval: 500ms
        
    - script:
        path: ~/.config/yambar/scripts/layout.sh
        poll-interval: 500ms

  center:
    - script:
        path: ~/.config/yambar/scripts/title.sh
        poll-interval: 500ms

  right:
    - clock:
        time-format: "%Y-%m-%d %H:%M"
```

## EWMH Compatibility

Pointblank fully implements the EWMH (Extended Window Manager Hints) specification for standard desktop interoperability. These properties are used by many status bars and pager applications.

### Implemented EWMH Properties

| EWMH Property | Description | Implementation |
|---------------|-------------|----------------|
| `_NET_CURRENT_DESKTOP` | Current workspace number | [`EWMHManager::setCurrentDesktop()`](/src/display/EWMHManager.cpp:300) |
| `_NET_NUMBER_OF_DESKTOPS` | Total number of workspaces | [`EWMHManager::setNumberOfDesktops()`](/src/display/EWMHManager.cpp:291) |
| `_NET_DESKTOP_NAMES` | Workspace names | [`EWMHManager::setDesktopNames()`](/src/display/EWMHManager.cpp:309) |
| `_NET_ACTIVE_WINDOW` | Window ID of the active window | [`EWMHManager::setActiveWindow()`](src/display/EWMHManager.cpp:384) |
| `_NET_CLIENT_LIST` | List of all managed windows | [`EWMHManager::setClientList()`](/src/display/EWMHManager.cpp:358) |
| `_NET_CLIENT_LIST_STACKING` | Windows in stacking order | [`EWMHManager::setClientListStacking()`](/src/display/EWMHManager.cpp:372) |
| `_NET_SHOWING_DESKTOP` | Showing desktop mode | [`EWMHManager::setShowingDesktop()`](/src/display/EWMHManager.cpp:345) |
| `_NET_DESKTOP_GEOMETRY` | Desktop dimensions | Set in [`EWMHManager::initialize()`](/src/display/EWMHManager.cpp:57) |
| `_NET_DESKTOP_VIEWPORT` | Desktop viewport | Set in [`EWMHManager::initialize()`](/src/display/EWMHManager.cpp:66) |
| `_NET_WORKAREA` | Available screen area | [`EWMHManager::updateWorkarea()`](/src/display/EWMHManager.cpp:325) |

Pointblank also advertises full EWMH support via the `_NET_SUPPORTED` property, which is set during initialization in [`EWMHManager::setSupportedHints()`](/src/display/EWMHManager.cpp:215).

## Property Change Notifications

External bars can listen for property changes using X11's `PropertyNotify` events. When any of the Pointblank properties change, the window manager updates them on the root window, triggering these events.

## Integration with Other Tools

### rofi

You can use rofi to show workspace switchers or window lists:

```bash
# Show workspace switcher
rofi -show -modi "workspace:~/.config/rofi/scripts/workspace.sh"

# Show window list
rofi -show window
```

### dmenu

```bash
# Workspace switcher with dmenu
echo -e "1\n2\n3\n4\n5" | dmenu -p "Workspace:" | xargs -I{} wmctrl -s $(({} - 1))
```

## Bug Analysis

### Known Issues

| Issue | Severity | Fix |
|-------|----------|-----|
| Properties not updating | Low | Restart status bar |
| High CPU usage | Low | Increase poll interval |
| Missing properties | Low | Verify Pointblank running |

### Performance Optimization

Recommended polling intervals:
- **Workspaces**: 500ms
- **Title**: 500ms
- **Layout**: 1000ms
- **Window counts**: 1000ms

---

## Competitive Analysis

### Market Position

Pointblank provides the most comprehensive status bar integration among tiling WMs:

| Feature | i3 | bspwm | dwm | Pointblank |
|---------|----|----|------|------------|
| Custom Atoms | ❌ | ❌ | ❌ | ✅ |
| EWMH Full | Partial | Partial | Partial | ✅ |
| Real-time updates | ❌ | ❌ | ❌ | ✅ |

### Recommendations

1. Add IPC for push-based updates
2. Add D-Bus interface for notifications
3. Create official polybar/waybar modules

---

## Troubleshooting

### Properties not updating

Make sure the Pointblank window manager is running and has proper access to the X display:

```bash
# Check if Pointblank is running
ps aux | grep pointblank

# Verify properties are set
xprop -root | grep _PB
```

### Status bar not reading properties

Some status bars may need to be restarted after Pointblank starts to properly detect the properties. Also ensure your status bar has access to the X display.

### Performance considerations

Reading X properties is very fast. However, if you're polling at high frequency (e.g., 100ms intervals), consider increasing the interval to reduce CPU usage. A 500ms to 1s interval is usually sufficient for most use cases.

## Contributing

If you have configurations for other status bars or improvements to existing ones, please submit a pull request to the Pointblank repository.