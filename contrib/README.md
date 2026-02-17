# Pointblank Window Manager - Startup Files

This directory contains startup files for launching Pointblank from different environments.

## Files

### pointblank.desktop
XDG desktop entry file for display managers (GDM, LightDM, SDDM, etc.).

**Installation:**
```bash
sudo cp contrib/pointblank.desktop /usr/share/xsessions/
```

After installation, Pointblank will appear as an option in your display manager's session menu.

### xinitrc
Startup script for launching via `startx` from a TTY.

**Installation:**
```bash
cp contrib/xinitrc ~/.xinitrc
chmod +x ~/.xinitrc
```

**Usage:**
```bash
# From TTY (Ctrl+Alt+F2 through F6)
startx
```

## Configuration Directory

Ensure your configuration directory exists:
```bash
mkdir -p ~/.config/pblank
cp config/pointblank.wmi ~/.config/pblank/
```

## Customization

### xinitrc
Edit `~/.xinitrc` to customize your startup:
- Set keyboard layout with `setxkbmap`
- Set wallpaper with `feh`
- Start a compositor with `picom` for transparency/blur
- Start a status bar like `polybar` or `waybar`
- Start a notification daemon like `dunst`

### pointblank.desktop
Edit the `Exec` line to point to your installed location:
```
Exec=/usr/local/bin/pointblank
```

## System-wide Installation

For system-wide installation, update the paths in these files:

1. Build and install the binary:
```bash
cd /home/sonoka/Pointblank
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make
sudo make install
```

2. Update `pointblank.desktop`:
```
Exec=/usr/local/bin/pointblank
```

3. Update `~/.xinitrc`:
```
exec /usr/local/bin/pointblank
```
