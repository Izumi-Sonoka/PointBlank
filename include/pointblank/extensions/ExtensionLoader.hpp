#pragma once

/**
 * @file ExtensionLoader.hpp
 * @brief High-Fidelity Extension Loader with ABI Validation
 * 
 * Implements the extension loading pipeline with:
 * - Dynamic shared object loading from pointblank.wmi manifest
 * - User extension scanning from ~/.config/pblank/extensions/user/
 * - ABI stability validation and symbol versioning
 * - Runtime hook validation before injection
 * - Performance monitoring and health checks
 * 
 * @author Point Blank Systems Engineering Team
 * @version 2.0.0
 */

#include "pointblank/extensions/ExtensionAPI.hpp"
#include "pointblank/performance/LockFreeStructures.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <filesystem>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <dlfcn.h>
#include <chrono>

namespace pblank {

class ConfigParser;
class Toaster;

namespace api { namespace v2 { class IExtension_v2; } }

struct ExtensionLoadResult {
    Result result;
    std::string extension_name;
    std::string error_message;
    uint32_t api_version_major;
    uint32_t api_version_minor;
    uint64_t load_time_ns;  
    std::filesystem::path path;  
    bool is_user_extension{false};  
};

struct ExtensionStats {
    std::string name;
    uint64_t events_processed{0};
    uint64_t events_blocked{0};
    uint64_t total_processing_time_ns{0};
    uint64_t errors{0};
    std::chrono::steady_clock::time_point last_activity;
    bool is_healthy{true};
};

struct LoadedExtension {
    ExtensionInfo info;
    void* handle{nullptr};
    IExtension* instance{nullptr};
    ExtensionStats stats;
    bool is_user_extension{false};
    std::filesystem::path path;
    
    api::v2::CreateExtensionFunc_v2 create_func{nullptr};
    api::v2::DestroyExtensionFunc_v2 destroy_func{nullptr};
    api::v2::GetExtensionInfoFunc info_func{nullptr};
};

struct ValidationContext {
    uint32_t core_api_major;
    uint32_t core_api_minor;
    uint32_t core_api_patch;
    uint64_t core_checksum;
    bool strict_mode{true};  
    std::vector<std::string> required_capabilities;
};

class ExtensionLoader {
public:
    
    using EventCallback = std::function<bool(IExtension*, const void* event_data)>;
    
    ExtensionLoader(Display* display, Window root);
    
    ~ExtensionLoader();
    
    ExtensionLoader(const ExtensionLoader&) = delete;
    ExtensionLoader& operator=(const ExtensionLoader&) = delete;
    
    void setToaster(Toaster* toaster) { toaster_ = toaster; }
    
    void setStrictMode(bool strict) { validation_context_.strict_mode = strict; }
    
    void addRequiredCapability(const std::string& cap);
    
    std::vector<ExtensionLoadResult> loadFromManifest(const std::filesystem::path& config_path);
    
    int loadUserExtensions();
    
    ExtensionLoadResult loadExtension(const std::filesystem::path& path, 
                                       bool is_user_extension = false);
    
    Result unloadExtension(const std::string& name);
    
    void unloadAll();
    
    ExtensionLoadResult reloadExtension(const std::string& name);
    
    std::vector<LoadedExtension> getLoadedExtensions() const;
    
    LoadedExtension* getExtension(const std::string& name);
    
    bool isLoaded(const std::string& name) const;
    
    ExtensionStats getStats(const std::string& name) const;
    
    std::vector<ExtensionStats> getAllStats() const;
    
    template<typename E>
    bool dispatchEvent(api::v2::EventType event_id, const E* event_data);
    
    bool dispatchWindowMap(const WindowHandle* window);
    
    bool dispatchWindowUnmap(const WindowHandle* window);
    
    bool dispatchWindowDestroy(const WindowHandle* window);
    
    bool dispatchWindowFocus(const WindowHandle* old_win, const WindowHandle* new_win);
    
    bool dispatchWindowMove(const WindowHandle* window, int16_t x, int16_t y);
    
    bool dispatchWindowResize(const WindowHandle* window, uint16_t w, uint16_t h);
    
    bool dispatchWorkspaceSwitch(uint32_t old_ws, uint32_t new_ws);
    
    bool dispatchLayoutChange(uint32_t workspace, const char* layout_name);
    
    bool dispatchConfigReload();
    
    void dispatchPreRender();
    
    void dispatchPostRender();
    
    std::vector<IExtension*> getLayoutProviders() const;
    
    IExtension* getLayoutProvider(const std::string& name) const;
    
    void checkHealth();
    
    std::vector<std::string> getUnhealthyExtensions() const;
    
    void setHealthMonitoring(bool enabled) { health_monitoring_enabled_ = enabled; }
    
private:
    
    Display* display_;
    Window root_;
    Toaster* toaster_{nullptr};
    
    mutable std::shared_mutex extensions_mutex_;
    std::unordered_map<std::string, LoadedExtension> extensions_;
    
    std::vector<std::pair<int32_t, std::string>> dispatch_order_;
    bool dispatch_order_dirty_{true};
    
    ValidationContext validation_context_;
    
    bool health_monitoring_enabled_{true};
    std::chrono::seconds health_check_interval_{30};
    std::chrono::steady_clock::time_point last_health_check_;
    
    std::filesystem::path user_extension_dir_;
    
    lockfree::CacheAlignedAtomic<uint64_t> total_events_dispatched_{0};
    lockfree::CacheAlignedAtomic<uint64_t> total_events_blocked_{0};
    
    bool validateABI(const ExtensionInfo* info) const;
    
    bool validateSymbols(void* handle, LoadedExtension& ext);
    
    bool validateHooks(IExtension* instance);
    
    bool resolveSymbols(void* handle, LoadedExtension& ext);
    
    bool initializeExtension(LoadedExtension& ext);
    
    void updateDispatchOrder();
    
    std::vector<std::filesystem::path> scanForExtensions(
        const std::filesystem::path& directory, bool recursive = true);
    
    std::vector<std::string> parseManifestImports(const std::filesystem::path& config_path);
    
    static std::filesystem::path getUserExtensionDir();
    
    void recordStats(const std::string& name, bool blocked, uint64_t processing_time_ns);
};

template<typename E>
bool ExtensionLoader::dispatchEvent(api::v2::EventType event_id, const E* event_data) {
    
    if (dispatch_order_dirty_) {
        updateDispatchOrder();
    }
    
    bool propagate = true;
    
    std::shared_lock lock(extensions_mutex_);
    
    for (const auto& [priority, name] : dispatch_order_) {
        auto it = extensions_.find(name);
        if (it == extensions_.end()) continue;
        
        LoadedExtension& ext = it->second;
        
        EventMask mask = ext.instance->getEventMask();
        if (!mask.has(event_id)) continue;
        
        bool result = true;
        auto event_start = std::chrono::steady_clock::now();
        
        if constexpr (std::is_same_v<E, WindowHandle>) {
            switch (static_cast<uint32_t>(event_id)) {
                case static_cast<uint32_t>(api::v2::EventType::WindowMap):
                    result = ext.instance->onWindowMap(event_data);
                    break;
                case static_cast<uint32_t>(api::v2::EventType::WindowUnmap):
                    result = ext.instance->onWindowUnmap(event_data);
                    break;
                case static_cast<uint32_t>(api::v2::EventType::WindowDestroy):
                    result = ext.instance->onWindowDestroy(event_data);
                    break;
            }
        }
        
        auto event_end = std::chrono::steady_clock::now();
        auto processing_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            event_end - event_start).count();
        
        ext.stats.events_processed++;
        ext.stats.total_processing_time_ns += processing_time;
        ext.stats.last_activity = event_start;
        
        if (!result) {
            ext.stats.events_blocked++;
            propagate = false;
            if (validation_context_.strict_mode) {
                break;  
            }
        }
    }
    
    total_events_dispatched_.fetchAdd(1, std::memory_order_relaxed);
    if (!propagate) {
        total_events_blocked_.fetchAdd(1, std::memory_order_relaxed);
    }
    
    return propagate;
}

} 
