#include "pointblank/extensions/PluginManager.hpp"
#include <iostream>
#include <filesystem>
#include <cstring>

namespace pblank {

PluginManager::PluginManager(Display* display, Window root)
    : display_(display), root_(root) {
    std::cout << "PluginManager initialized" << std::endl;
}

PluginManager::~PluginManager() {
    unloadAll();
}

bool PluginManager::resolvePluginSymbols(void* handle, CreatePluginFunc& create,
                                          DestroyPluginFunc& destroy) {
    
    dlerror();
    
    
    create = reinterpret_cast<CreatePluginFunc>(dlsym(handle, "createPlugin"));
    const char* error = dlerror();
    if (error) {
        std::cerr << "Failed to resolve createPlugin: " << error << std::endl;
        return false;
    }
    
    
    destroy = reinterpret_cast<DestroyPluginFunc>(dlsym(handle, "destroyPlugin"));
    error = dlerror();
    if (error) {
        std::cerr << "Failed to resolve destroyPlugin: " << error << std::endl;
        return false;
    }
    
    return true;
}

bool PluginManager::loadPlugin(const std::string& path) {
    std::cout << "Loading plugin: " << path << std::endl;
    
    
    if (!std::filesystem::exists(path)) {
        std::cerr << "Plugin file not found: " << path << std::endl;
        return false;
    }
    
    
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        std::cerr << "Failed to load plugin: " << dlerror() << std::endl;
        return false;
    }
    
    
    CreatePluginFunc create = nullptr;
    DestroyPluginFunc destroy = nullptr;
    
    if (!resolvePluginSymbols(handle, create, destroy)) {
        dlclose(handle);
        return false;
    }
    
    
    IPlugin* plugin = create();
    if (!plugin) {
        std::cerr << "Failed to create plugin instance" << std::endl;
        dlclose(handle);
        return false;
    }
    
    
    if (!plugin->initialize(display_, root_)) {
        std::cerr << "Plugin initialization failed: " << plugin->getName() << std::endl;
        destroy(plugin);
        dlclose(handle);
        return false;
    }
    
    
    PluginInfo info;
    info.name = plugin->getName();
    info.version = plugin->getVersion();
    info.handle = handle;
    info.instance = plugin;
    
    plugins_[info.name] = info;
    
    std::cout << "Plugin loaded successfully: " << info.name << " v" << info.version << std::endl;
    return true;
}

bool PluginManager::unloadPlugin(const std::string& name) {
    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        std::cerr << "Plugin not found: " << name << std::endl;
        return false;
    }
    
    PluginInfo& info = it->second;
    
    
    info.instance->shutdown();
    
    
    DestroyPluginFunc destroy = reinterpret_cast<DestroyPluginFunc>(
        dlsym(info.handle, "destroyPlugin"));
    if (destroy) {
        destroy(info.instance);
    } else {
        
        delete info.instance;
    }
    
    
    dlclose(info.handle);
    
    
    plugins_.erase(it);
    
    std::cout << "Plugin unloaded: " << name << std::endl;
    return true;
}

int PluginManager::loadPluginsFromDirectory(const std::string& directory) {
    int loaded_count = 0;
    
    if (!std::filesystem::exists(directory)) {
        std::cerr << "Plugin directory not found: " << directory << std::endl;
        return 0;
    }
    
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file()) {
            std::string path = entry.path().string();
            
            
            if (path.size() >= 3 && path.substr(path.size() - 3) == ".so") {
                if (loadPlugin(path)) {
                    ++loaded_count;
                }
            }
        }
    }
    
    std::cout << "Loaded " << loaded_count << " plugins from " << directory << std::endl;
    return loaded_count;
}

void PluginManager::unloadAll() {
    
    std::vector<std::string> names;
    for (const auto& pair : plugins_) {
        names.push_back(pair.first);
    }
    
    for (const auto& name : names) {
        unloadPlugin(name);
    }
}

std::vector<PluginInfo> PluginManager::getLoadedPlugins() const {
    std::vector<PluginInfo> result;
    for (const auto& pair : plugins_) {
        result.push_back(pair.second);
    }
    return result;
}

void PluginManager::notifyWindowOpen(Window w) {
    for (auto& pair : plugins_) {
        if (pair.second.instance) {
            pair.second.instance->onWindowOpen(w);
        }
    }
}

void PluginManager::notifyWindowClose(Window w) {
    for (auto& pair : plugins_) {
        if (pair.second.instance) {
            pair.second.instance->onWindowClose(w);
        }
    }
}

void PluginManager::notifyWindowFocus(Window w) {
    for (auto& pair : plugins_) {
        if (pair.second.instance) {
            pair.second.instance->onWindowFocus(w);
        }
    }
}

void PluginManager::notifyWindowMove(Window w, int x, int y) {
    for (auto& pair : plugins_) {
        if (pair.second.instance) {
            pair.second.instance->onWindowMove(w, x, y);
        }
    }
}

void PluginManager::notifyWindowResize(Window w, unsigned int width, unsigned int height) {
    for (auto& pair : plugins_) {
        if (pair.second.instance) {
            pair.second.instance->onWindowResize(w, width, height);
        }
    }
}

void PluginManager::notifyWorkspaceChange(int old_workspace, int new_workspace) {
    for (auto& pair : plugins_) {
        if (pair.second.instance) {
            pair.second.instance->onWorkspaceChange(old_workspace, new_workspace);
        }
    }
}

} 
