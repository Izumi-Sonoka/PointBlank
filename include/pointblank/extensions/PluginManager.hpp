#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <dlfcn.h>
#include <X11/Xlib.h>

namespace pblank {

/**
 * @brief Plugin interface for window manager extensions
 * 
 * All plugins must implement this interface to be loaded by the PluginManager.
 * Plugins are loaded from .so files and can hook into various window manager events.
 */
class IPlugin {
public:
    virtual ~IPlugin() = default;
    
    virtual std::string getName() const = 0;
    
    virtual std::string getVersion() const = 0;
    
    virtual bool initialize(Display* display, Window root) = 0;
    
    virtual void shutdown() = 0;
    
    virtual void onWindowOpen(Window w) {}
    virtual void onWindowClose(Window w) {}
    virtual void onWindowFocus(Window w) {}
    virtual void onWindowMove(Window w, int x, int y) {}
    virtual void onWindowResize(Window w, unsigned int width, unsigned int height) {}
    virtual void onWorkspaceChange(int old_workspace, int new_workspace) {}
};

struct PluginInfo {
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    void* handle{nullptr};
    IPlugin* instance{nullptr};
};

using CreatePluginFunc = IPlugin* (*)();

using DestroyPluginFunc = void (*)(IPlugin*);

class PluginManager {
public:
    
    PluginManager(Display* display, Window root);
    
    ~PluginManager();
    
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;
    
    bool loadPlugin(const std::string& path);
    
    bool unloadPlugin(const std::string& name);
    
    int loadPluginsFromDirectory(const std::string& directory);
    
    void unloadAll();
    
    std::vector<PluginInfo> getLoadedPlugins() const;
    
    void notifyWindowOpen(Window w);
    
    void notifyWindowClose(Window w);
    
    void notifyWindowFocus(Window w);
    
    void notifyWindowMove(Window w, int x, int y);
    
    void notifyWindowResize(Window w, unsigned int width, unsigned int height);
    
    void notifyWorkspaceChange(int old_workspace, int new_workspace);
    
private:
    Display* display_;
    Window root_;
    std::unordered_map<std::string, PluginInfo> plugins_;
    
    bool resolvePluginSymbols(void* handle, CreatePluginFunc& create, 
                              DestroyPluginFunc& destroy);
};

} 
