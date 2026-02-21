#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <filesystem>
#include <vector>
#include <chrono>

struct inotify_event;

constexpr const char* ERROR_LOG_DIR = "/tmp/pointblank/errors";

namespace pblank {

/**
 * @brief Validation result for configuration changes
 */
struct ValidationResult {
    bool success{false};
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    
    struct ErrorLocation {
        int line{0};
        int column{0};
        std::string message;
        std::string context;  
    };
    std::vector<ErrorLocation> error_locations;
    
    operator bool() const { return success; }
};

struct ConfigChangeEvent {
    std::filesystem::path path;
    std::chrono::system_clock::time_point timestamp;
    enum class Type { Modified, Created, Deleted } type;
};

class ConfigWatcher {
public:
    using ValidationCallback = std::function<ValidationResult(const std::filesystem::path&)>;
    using ApplyCallback = std::function<bool(const std::filesystem::path&)>;
    using ErrorCallback = std::function<void(const ValidationResult&)>;
    using NotifyCallback = std::function<void(const std::string& message, const std::string& level)>;
    
    ConfigWatcher();
    ~ConfigWatcher();
    
    ConfigWatcher(const ConfigWatcher&) = delete;
    ConfigWatcher& operator=(const ConfigWatcher&) = delete;
    ConfigWatcher(ConfigWatcher&&) = delete;
    ConfigWatcher& operator=(ConfigWatcher&&) = delete;
    
    bool addWatch(const std::filesystem::path& path, bool recursive = true);
    
    void removeWatch(const std::filesystem::path& path);
    
    void setValidationCallback(ValidationCallback callback);
    
    void setApplyCallback(ApplyCallback callback);
    
    void setErrorCallback(ErrorCallback callback);
    
    void setNotifyCallback(NotifyCallback callback);
    
    bool start();
    
    void stop();
    
    bool isRunning() const { return running_.load(); }
    
    void setDebounceInterval(std::chrono::milliseconds ms);
    
    void setAutoReload(bool enabled) { auto_reload_ = enabled; }
    
    ValidationResult reload(const std::filesystem::path& path);
    
    const std::filesystem::path& getLastGoodConfig() const { return last_good_config_; }
    
    void setSchemaFile(const std::filesystem::path& path) { schema_file_ = path; }
    
private:
    
    int inotify_fd_{-1};
    
    std::unordered_map<int, std::filesystem::path> watch_descriptors_;
    
    ValidationCallback validation_callback_;
    ApplyCallback apply_callback_;
    ErrorCallback error_callback_;
    NotifyCallback notify_callback_;
    
    std::thread watcher_thread_;
    std::atomic<bool> running_{false};
    std::mutex callback_mutex_;
    
    std::chrono::milliseconds debounce_interval_{0};  
    bool auto_reload_{true};
    
    std::filesystem::path last_good_config_;
    std::filesystem::path schema_file_;
    
    std::unordered_map<std::filesystem::path, std::chrono::system_clock::time_point> pending_changes_;
    std::mutex pending_mutex_;
    
    void watcherLoop();
    void processEvent(const struct inotify_event* event);
    void handleFileChange(const std::filesystem::path& path, ConfigChangeEvent::Type type);
    void debounceAndProcess(const std::filesystem::path& path);
    void processDebouncedChanges();
    
    ValidationResult validateConfig(const std::filesystem::path& path);
    bool applyConfig(const std::filesystem::path& path);
    void reportErrors(const ValidationResult& result);
    void writeErrorLog(const ValidationResult& result, const std::filesystem::path& config_path);
    
    bool setupInotify();
    void cleanupInotify();
    int addInotifyWatch(const std::filesystem::path& path, bool recursive);
};

} 
