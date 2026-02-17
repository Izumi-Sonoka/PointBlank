/**
 * @file ExtensionLoader.cpp
 * @brief Implementation of the High-Fidelity Extension Loader
 * 
 * @author Point Blank Systems Engineering Team
 * @version 2.0.0
 */

#include "pointblank/extensions/ExtensionLoader.hpp"
#include "pointblank/config/ConfigParser.hpp"
#include "pointblank/core/Toaster.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>

namespace pblank {

// ============================================================================
// Constructor / Destructor
// ============================================================================

ExtensionLoader::ExtensionLoader(Display* display, Window root)
    : display_(display)
    , root_(root)
    , user_extension_dir_(getUserExtensionDir())
{
    // Initialize validation context with current API version
    validation_context_.core_api_major = PB_API_VERSION_MAJOR;
    validation_context_.core_api_minor = PB_API_VERSION_MINOR;
    validation_context_.core_api_patch = PB_API_VERSION_PATCH;
    validation_context_.core_checksum = api::v2::API_CHECKSUM;
    
    // Create user extension directory if it doesn't exist
    if (!std::filesystem::exists(user_extension_dir_)) {
        std::filesystem::create_directories(user_extension_dir_);
    }
}

ExtensionLoader::~ExtensionLoader() {
    unloadAll();
}

// ============================================================================
// Configuration
// ============================================================================

void ExtensionLoader::addRequiredCapability(const std::string& cap) {
    validation_context_.required_capabilities.push_back(cap);
}

// ============================================================================
// Extension Discovery
// ============================================================================

std::vector<ExtensionLoadResult> ExtensionLoader::loadFromManifest(
    const std::filesystem::path& config_path) {
    
    std::vector<ExtensionLoadResult> results;
    
    // Parse manifest for import directives
    auto imports = parseManifestImports(config_path);
    
    // Look for extensions in system paths
    std::vector<std::filesystem::path> search_paths = {
        "/usr/lib/pointblank/extensions",
        "/usr/local/lib/pointblank/extensions",
        std::filesystem::current_path() / "extensions"
    };
    
    // Add user extension directory
    if (std::filesystem::exists(user_extension_dir_)) {
        search_paths.push_back(user_extension_dir_);
    }
    
    for (const auto& import_name : imports) {
        bool found = false;
        
        // Search for the extension in each path
        for (const auto& search_path : search_paths) {
            std::filesystem::path ext_path = search_path / (import_name + ".so");
            
            if (std::filesystem::exists(ext_path)) {
                auto result = loadExtension(ext_path, false);
                results.push_back(result);
                found = true;
                break;
            }
            
            // Also try lib prefix
            ext_path = search_path / ("lib" + import_name + ".so");
            if (std::filesystem::exists(ext_path)) {
                auto result = loadExtension(ext_path, false);
                results.push_back(result);
                found = true;
                break;
            }
        }
        
        if (!found) {
            ExtensionLoadResult result;
            result.result = Result::SymbolNotFound;
            result.extension_name = import_name;
            result.error_message = "Extension not found in any search path";
            results.push_back(result);
        }
    }
    
    return results;
}

int ExtensionLoader::loadUserExtensions() {
    int loaded_count = 0;
    
    if (!std::filesystem::exists(user_extension_dir_)) {
        return 0;
    }
    
    // Recursively scan for .so files
    auto extension_paths = scanForExtensions(user_extension_dir_, true);
    
    for (const auto& path : extension_paths) {
        auto result = loadExtension(path, true);
        
        if (result.result == Result::Success) {
            ++loaded_count;
            
            if (toaster_) {
                toaster_->success("Loaded user extension: " + result.extension_name);
            }
        } else {
            // Log error but continue loading other extensions
            if (toaster_) {
                toaster_->error("Failed to load extension: " + result.error_message);
            }
        }
    }
    
    return loaded_count;
}

ExtensionLoadResult ExtensionLoader::loadExtension(
    const std::filesystem::path& path, bool is_user_extension) {
    
    ExtensionLoadResult result;
    result.path = path;
    result.is_user_extension = is_user_extension;
    
    auto load_start = std::chrono::steady_clock::now();
    
    // Check if file exists
    if (!std::filesystem::exists(path)) {
        result.result = Result::InvalidArgument;
        result.error_message = "File not found: " + path.string();
        return result;
    }
    
    // Check file extension
    if (path.extension() != ".so") {
        result.result = Result::InvalidArgument;
        result.error_message = "Invalid extension file (must be .so)";
        return result;
    }
    
    // Load shared object
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        result.result = Result::SymbolNotFound;
        result.error_message = "dlopen failed: " + std::string(dlerror());
        return result;
    }
    
    LoadedExtension ext;
    ext.handle = handle;
    ext.path = path;
    ext.is_user_extension = is_user_extension;
    
    // Resolve symbols
    if (!resolveSymbols(handle, ext)) {
        dlclose(handle);
        result.result = Result::SymbolNotFound;
        result.error_message = "Failed to resolve required symbols";
        return result;
    }
    
    // Get extension info
    const ExtensionInfo* info = ext.info_func();
    if (!info) {
        dlclose(handle);
        result.result = Result::InvalidState;
        result.error_message = "getExtensionInfo() returned null";
        return result;
    }
    
    // Copy info
    ext.info = *info;
    result.extension_name = info->name;
    result.api_version_major = info->api_version_major;
    result.api_version_minor = info->api_version_minor;
    
    // Validate ABI
    if (!validateABI(info)) {
        dlclose(handle);
        result.result = Result::VersionMismatch;
        result.error_message = "ABI validation failed (version mismatch)";
        return result;
    }
    
    // Create extension instance
    ext.instance = ext.create_func();
    if (!ext.instance) {
        dlclose(handle);
        result.result = Result::InitializationFailed;
        result.error_message = "createExtension() returned null";
        return result;
    }
    
    // Validate hooks
    if (!validateHooks(ext.instance)) {
        ext.destroy_func(ext.instance);
        dlclose(handle);
        result.result = Result::InvalidState;
        result.error_message = "Hook validation failed";
        return result;
    }
    
    // Initialize extension
    ExtensionContext ctx;
    ctx.display = display_;
    ctx.root = root_;
    ctx.screen = DefaultScreen(display_);
    ctx.focused_window = nullptr;  // Will be updated by WindowManager
    ctx.current_workspace = 0;
    ctx.workspace_count = 12;
    ctx.frame_counter = nullptr;
    
    Result init_result = ext.instance->initialize(&ctx);
    if (init_result != Result::Success) {
        ext.destroy_func(ext.instance);
        dlclose(handle);
        result.result = init_result;
        result.error_message = "Extension initialization failed";
        return result;
    }
    
    // Initialize stats
    ext.stats.name = info->name;
    ext.stats.last_activity = std::chrono::steady_clock::now();
    
    // Add to loaded extensions
    {
        std::unique_lock lock(extensions_mutex_);
        extensions_[info->name] = std::move(ext);
        dispatch_order_dirty_ = true;
    }
    
    auto load_end = std::chrono::steady_clock::now();
    result.load_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        load_end - load_start).count();
    result.result = Result::Success;
    
    return result;
}

Result ExtensionLoader::unloadExtension(const std::string& name) {
    std::unique_lock lock(extensions_mutex_);
    
    auto it = extensions_.find(name);
    if (it == extensions_.end()) {
        return Result::InvalidArgument;
    }
    
    LoadedExtension& ext = it->second;
    
    // Shutdown extension
    Result shutdown_result = ext.instance->shutdown();
    if (shutdown_result != Result::Success && validation_context_.strict_mode) {
        return Result::ShutdownFailed;
    }
    
    // Destroy instance
    ext.destroy_func(ext.instance);
    
    // Close shared object
    dlclose(ext.handle);
    
    // Remove from map
    extensions_.erase(it);
    dispatch_order_dirty_ = true;
    
    return Result::Success;
}

void ExtensionLoader::unloadAll() {
    std::unique_lock lock(extensions_mutex_);
    
    for (auto& [name, ext] : extensions_) {
        ext.instance->shutdown();
        ext.destroy_func(ext.instance);
        dlclose(ext.handle);
    }
    
    extensions_.clear();
    dispatch_order_dirty_ = true;
}

ExtensionLoadResult ExtensionLoader::reloadExtension(const std::string& name) {
    std::filesystem::path path;
    bool is_user = false;
    
    {
        std::shared_lock lock(extensions_mutex_);
        auto it = extensions_.find(name);
        if (it != extensions_.end()) {
            path = it->second.path;
            is_user = it->second.is_user_extension;
        }
    }
    
    if (path.empty()) {
        ExtensionLoadResult result;
        result.result = Result::InvalidArgument;
        result.error_message = "Extension not found: " + name;
        return result;
    }
    
    // Unload first
    Result unload_result = unloadExtension(name);
    if (unload_result != Result::Success) {
        ExtensionLoadResult result;
        result.result = unload_result;
        result.error_message = "Failed to unload extension for reload";
        return result;
    }
    
    // Reload
    return loadExtension(path, is_user);
}

// ============================================================================
// Extension Query
// ============================================================================

std::vector<LoadedExtension> ExtensionLoader::getLoadedExtensions() const {
    std::shared_lock lock(extensions_mutex_);
    
    std::vector<LoadedExtension> result;
    result.reserve(extensions_.size());
    
    for (const auto& [name, ext] : extensions_) {
        result.push_back(ext);
    }
    
    return result;
}

LoadedExtension* ExtensionLoader::getExtension(const std::string& name) {
    std::shared_lock lock(extensions_mutex_);
    auto it = extensions_.find(name);
    return it != extensions_.end() ? &it->second : nullptr;
}

bool ExtensionLoader::isLoaded(const std::string& name) const {
    std::shared_lock lock(extensions_mutex_);
    return extensions_.find(name) != extensions_.end();
}

ExtensionStats ExtensionLoader::getStats(const std::string& name) const {
    std::shared_lock lock(extensions_mutex_);
    auto it = extensions_.find(name);
    return it != extensions_.end() ? it->second.stats : ExtensionStats{};
}

std::vector<ExtensionStats> ExtensionLoader::getAllStats() const {
    std::shared_lock lock(extensions_mutex_);
    
    std::vector<ExtensionStats> result;
    result.reserve(extensions_.size());
    
    for (const auto& [name, ext] : extensions_) {
        result.push_back(ext.stats);
    }
    
    return result;
}

// ============================================================================
// Event Dispatch
// ============================================================================

bool ExtensionLoader::dispatchWindowMap(const WindowHandle* window) {
    return dispatchEvent(api::v2::EventType::WindowMap, window);
}

bool ExtensionLoader::dispatchWindowUnmap(const WindowHandle* window) {
    return dispatchEvent(api::v2::EventType::WindowUnmap, window);
}

bool ExtensionLoader::dispatchWindowDestroy(const WindowHandle* window) {
    return dispatchEvent(api::v2::EventType::WindowDestroy, window);
}

bool ExtensionLoader::dispatchWindowFocus(const WindowHandle* old_win, const WindowHandle* new_win) {
    // Special handling for focus events (two windows)
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
        
        if (!mask.has(api::v2::EventType::WindowFocus)) continue;
        
        auto start = std::chrono::steady_clock::now();
        bool result = ext.instance->onWindowFocus(old_win, new_win);
        auto end = std::chrono::steady_clock::now();
        
        ext.stats.events_processed++;
        ext.stats.total_processing_time_ns += 
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        ext.stats.last_activity = start;
        
        if (!result) {
            ext.stats.events_blocked++;
            propagate = false;
        }
    }
    
    return propagate;
}

bool ExtensionLoader::dispatchWindowMove(const WindowHandle* window, int16_t x, int16_t y) {
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
        
        if (!mask.has(api::v2::EventType::WindowMove)) continue;
        
        auto start = std::chrono::steady_clock::now();
        bool result = ext.instance->onWindowMove(window, x, y);
        auto end = std::chrono::steady_clock::now();
        
        ext.stats.events_processed++;
        ext.stats.total_processing_time_ns += 
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        ext.stats.last_activity = start;
        
        if (!result) {
            ext.stats.events_blocked++;
            propagate = false;
        }
    }
    
    return propagate;
}

bool ExtensionLoader::dispatchWindowResize(const WindowHandle* window, uint16_t w, uint16_t h) {
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
        
        if (!mask.has(api::v2::EventType::WindowResize)) continue;
        
        auto start = std::chrono::steady_clock::now();
        bool result = ext.instance->onWindowResize(window, w, h);
        auto end = std::chrono::steady_clock::now();
        
        ext.stats.events_processed++;
        ext.stats.total_processing_time_ns += 
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        ext.stats.last_activity = start;
        
        if (!result) {
            ext.stats.events_blocked++;
            propagate = false;
        }
    }
    
    return propagate;
}

bool ExtensionLoader::dispatchWorkspaceSwitch(uint32_t old_ws, uint32_t new_ws) {
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
        
        if (!mask.has(api::v2::EventType::WorkspaceSwitch)) continue;
        
        auto start = std::chrono::steady_clock::now();
        bool result = ext.instance->onWorkspaceSwitch(old_ws, new_ws);
        auto end = std::chrono::steady_clock::now();
        
        ext.stats.events_processed++;
        ext.stats.total_processing_time_ns += 
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        ext.stats.last_activity = start;
        
        if (!result) {
            ext.stats.events_blocked++;
            propagate = false;
        }
    }
    
    return propagate;
}

bool ExtensionLoader::dispatchLayoutChange(uint32_t workspace, const char* layout_name) {
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
        
        if (!mask.has(api::v2::EventType::LayoutChange)) continue;
        
        auto start = std::chrono::steady_clock::now();
        bool result = ext.instance->onLayoutChange(workspace, layout_name);
        auto end = std::chrono::steady_clock::now();
        
        ext.stats.events_processed++;
        ext.stats.total_processing_time_ns += 
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        ext.stats.last_activity = start;
        
        if (!result) {
            ext.stats.events_blocked++;
            propagate = false;
        }
    }
    
    return propagate;
}

bool ExtensionLoader::dispatchConfigReload() {
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
        
        if (!mask.has(api::v2::EventType::ConfigReload)) continue;
        
        auto start = std::chrono::steady_clock::now();
        bool result = ext.instance->onConfigReload();
        auto end = std::chrono::steady_clock::now();
        
        ext.stats.events_processed++;
        ext.stats.total_processing_time_ns += 
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        ext.stats.last_activity = start;
        
        if (!result) {
            ext.stats.events_blocked++;
            propagate = false;
        }
    }
    
    return propagate;
}

void ExtensionLoader::dispatchPreRender() {
    std::shared_lock lock(extensions_mutex_);
    
    for (const auto& [priority, name] : dispatch_order_) {
        auto it = extensions_.find(name);
        if (it == extensions_.end()) continue;
        
        it->second.instance->onPreRender();
    }
}

void ExtensionLoader::dispatchPostRender() {
    std::shared_lock lock(extensions_mutex_);
    
    for (const auto& [priority, name] : dispatch_order_) {
        auto it = extensions_.find(name);
        if (it == extensions_.end()) continue;
        
        it->second.instance->onPostRender();
    }
}

// ============================================================================
// Layout Provider Interface
// ============================================================================

std::vector<IExtension*> ExtensionLoader::getLayoutProviders() const {
    std::vector<IExtension*> providers;
    
    std::shared_lock lock(extensions_mutex_);
    
    for (const auto& [name, ext] : extensions_) {
        if (ext.instance->hasLayoutProvider()) {
            providers.push_back(ext.instance);
        }
    }
    
    return providers;
}

IExtension* ExtensionLoader::getLayoutProvider(const std::string& name) const {
    std::shared_lock lock(extensions_mutex_);
    
    auto it = extensions_.find(name);
    if (it != extensions_.end() && it->second.instance->hasLayoutProvider()) {
        return it->second.instance;
    }
    
    return nullptr;
}

// ============================================================================
// Health Monitoring
// ============================================================================

void ExtensionLoader::checkHealth() {
    if (!health_monitoring_enabled_) return;
    
    auto now = std::chrono::steady_clock::now();
    if (now - last_health_check_ < health_check_interval_) return;
    
    last_health_check_ = now;
    
    std::shared_lock lock(extensions_mutex_);
    
    for (auto& [name, ext] : extensions_) {
        bool healthy = ext.instance->isHealthy();
        ext.stats.is_healthy = healthy;
        
        // Check for stalled extensions (no activity for 60 seconds with events)
        if (ext.stats.events_processed > 0) {
            auto inactive_time = std::chrono::duration_cast<std::chrono::seconds>(
                now - ext.stats.last_activity).count();
            
            if (inactive_time > 60) {
                ext.stats.is_healthy = false;
            }
        }
    }
}

std::vector<std::string> ExtensionLoader::getUnhealthyExtensions() const {
    std::vector<std::string> unhealthy;
    
    std::shared_lock lock(extensions_mutex_);
    
    for (const auto& [name, ext] : extensions_) {
        if (!ext.stats.is_healthy) {
            unhealthy.push_back(name);
        }
    }
    
    return unhealthy;
}

// ============================================================================
// Internal Methods
// ============================================================================

bool ExtensionLoader::validateABI(const ExtensionInfo* info) const {
    // Check version compatibility (major must match, minor can be <=)
    if (info->api_version_major != validation_context_.core_api_major) {
        return false;
    }
    
    if (info->api_version_minor > validation_context_.core_api_minor) {
        // Extension was built against newer API
        return false;
    }
    
    // Verify checksum for strict ABI validation
    if (info->api_checksum != validation_context_.core_checksum) {
        // Checksum mismatch - structures may have changed
        if (validation_context_.strict_mode) {
            return false;
        }
    }
    
    return true;
}

bool ExtensionLoader::validateSymbols(void* handle, LoadedExtension& ext) {
    // Clear any existing error
    dlerror();
    
    // Look for versioned symbols first
    ext.create_func = reinterpret_cast<api::v2::CreateExtensionFunc_v2>(
        dlsym(handle, "createExtension_v2"));
    
    if (!ext.create_func) {
        // Try unversioned symbol for backward compatibility
        ext.create_func = reinterpret_cast<api::v2::CreateExtensionFunc_v2>(
            dlsym(handle, "createExtension"));
        
        if (!ext.create_func) {
            return false;
        }
    }
    
    ext.destroy_func = reinterpret_cast<api::v2::DestroyExtensionFunc_v2>(
        dlsym(handle, "destroyExtension_v2"));
    
    if (!ext.destroy_func) {
        ext.destroy_func = reinterpret_cast<api::v2::DestroyExtensionFunc_v2>(
            dlsym(handle, "destroyExtension"));
        
        if (!ext.destroy_func) {
            return false;
        }
    }
    
    ext.info_func = reinterpret_cast<api::v2::GetExtensionInfoFunc>(
        dlsym(handle, "getExtensionInfo"));
    
    if (!ext.info_func) {
        return false;
    }
    
    return true;
}

bool ExtensionLoader::validateHooks(IExtension* instance) {
    // Get event mask
    EventMask mask = instance->getEventMask();
    
    // Verify the extension implements at least one hook
    if (mask.mask == 0) {
        // Extension doesn't subscribe to any events
        // This might be intentional for layout-only extensions
        return true;
    }
    
    // Verify extension info is valid
    const ExtensionInfo* info = instance->getInfo();
    if (!info || std::strlen(info->name) == 0) {
        return false;
    }
    
    return true;
}

bool ExtensionLoader::resolveSymbols(void* handle, LoadedExtension& ext) {
    return validateSymbols(handle, ext);
}

bool ExtensionLoader::initializeExtension(LoadedExtension& ext) {
    ExtensionContext ctx;
    ctx.display = display_;
    ctx.root = root_;
    ctx.screen = DefaultScreen(display_);
    
    return ext.instance->initialize(&ctx) == Result::Success;
}

void ExtensionLoader::updateDispatchOrder() {
    std::shared_lock lock(extensions_mutex_);
    
    dispatch_order_.clear();
    dispatch_order_.reserve(extensions_.size());
    
    for (const auto& [name, ext] : extensions_) {
        dispatch_order_.emplace_back(ext.info.priority, name);
    }
    
    // Sort by priority (highest first)
    std::sort(dispatch_order_.begin(), dispatch_order_.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });
    
    dispatch_order_dirty_ = false;
}

std::vector<std::filesystem::path> ExtensionLoader::scanForExtensions(
    const std::filesystem::path& directory, bool recursive) {
    
    std::vector<std::filesystem::path> extensions;
    
    if (!std::filesystem::exists(directory)) {
        return extensions;
    }
    
    if (recursive) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".so") {
                extensions.push_back(entry.path());
            }
        }
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".so") {
                extensions.push_back(entry.path());
            }
        }
    }
    
    return extensions;
}

std::vector<std::string> ExtensionLoader::parseManifestImports(
    const std::filesystem::path& config_path) {
    
    std::vector<std::string> imports;
    
    if (!std::filesystem::exists(config_path)) {
        return imports;
    }
    
    std::ifstream file(config_path);
    std::string line;
    
    // Regex patterns for import directives
    std::regex import_regex(R"(#import\s+(\w+))");
    std::regex include_regex(R"(#include\s+(\S+))");
    
    while (std::getline(file, line)) {
        std::smatch match;
        
        // Check for #import directive (user extensions from ~/.config/pblank/extensions/user/)
        if (std::regex_search(line, match, import_regex)) {
            imports.push_back(match[1].str());
        }
        // Check for #include directive (built-in extensions from ~/.config/pblank/extensions/pb/)
        else if (std::regex_search(line, match, include_regex)) {
            imports.push_back(match[1].str());
        }
    }
    
    return imports;
}

std::filesystem::path ExtensionLoader::getUserExtensionDir() {
    const char* home = std::getenv("HOME");
    if (!home) {
        home = std::getenv("USERPROFILE");
    }
    
    if (home) {
        return std::filesystem::path(home) / ".config" / "pblank" / "extensions" / "user";
    }
    
    return std::filesystem::path("/tmp") / "pblank" / "extensions" / "user";
}

void ExtensionLoader::recordStats(const std::string& name, bool blocked, uint64_t processing_time_ns) {
    std::shared_lock lock(extensions_mutex_);
    
    auto it = extensions_.find(name);
    if (it != extensions_.end()) {
        it->second.stats.events_processed++;
        it->second.stats.total_processing_time_ns += processing_time_ns;
        if (blocked) {
            it->second.stats.events_blocked++;
        }
    }
}

} // namespace pblank
