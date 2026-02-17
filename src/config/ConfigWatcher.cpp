#include "pointblank/config/ConfigWatcher.hpp"
#include <sys/inotify.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <iomanip>

namespace pblank {

// Buffer size for inotify reads
constexpr size_t INOTIFY_BUFFER_SIZE = 4096;

ConfigWatcher::ConfigWatcher() = default;

ConfigWatcher::~ConfigWatcher() {
    stop();
    cleanupInotify();
}

bool ConfigWatcher::setupInotify() {
    inotify_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd_ == -1) {
        std::cerr << "ConfigWatcher: Failed to initialize inotify: " 
                  << strerror(errno) << std::endl;
        return false;
    }
    return true;
}

void ConfigWatcher::cleanupInotify() {
    if (inotify_fd_ != -1) {
        close(inotify_fd_);
        inotify_fd_ = -1;
    }
    watch_descriptors_.clear();
}

int ConfigWatcher::addInotifyWatch(const std::filesystem::path& path, bool recursive) {
    if (inotify_fd_ == -1) {
        return -1;
    }
    
    // Watch for modify, close write, move, and delete events
    uint32_t mask = IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO | 
                    IN_CREATE | IN_DELETE | IN_DELETE_SELF;
    
    int wd = inotify_add_watch(inotify_fd_, path.c_str(), mask);
    if (wd == -1) {
        std::cerr << "ConfigWatcher: Failed to add watch on " << path 
                  << ": " << strerror(errno) << std::endl;
        return -1;
    }
    
    watch_descriptors_[wd] = path;
    
    // If directory and recursive, add watches for subdirectories
    if (recursive && std::filesystem::is_directory(path)) {
        try {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
                if (entry.is_directory()) {
                    int sub_wd = inotify_add_watch(inotify_fd_, entry.path().c_str(), mask);
                    if (sub_wd != -1) {
                        watch_descriptors_[sub_wd] = entry.path();
                    }
                }
            }
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "ConfigWatcher: Error traversing directory: " 
                      << e.what() << std::endl;
        }
    }
    
    return wd;
}

bool ConfigWatcher::addWatch(const std::filesystem::path& path, bool recursive) {
    if (!std::filesystem::exists(path)) {
        std::cerr << "ConfigWatcher: Path does not exist: " << path << std::endl;
        return false;
    }
    
    if (inotify_fd_ == -1 && !setupInotify()) {
        return false;
    }
    
    return addInotifyWatch(path, recursive) != -1;
}

void ConfigWatcher::removeWatch(const std::filesystem::path& path) {
    for (auto it = watch_descriptors_.begin(); it != watch_descriptors_.end(); ) {
        if (it->second == path) {
            inotify_rm_watch(inotify_fd_, it->first);
            it = watch_descriptors_.erase(it);
        } else {
            ++it;
        }
    }
}

void ConfigWatcher::setValidationCallback(ValidationCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    validation_callback_ = std::move(callback);
}

void ConfigWatcher::setApplyCallback(ApplyCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    apply_callback_ = std::move(callback);
}

void ConfigWatcher::setErrorCallback(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    error_callback_ = std::move(callback);
}

void ConfigWatcher::setNotifyCallback(NotifyCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    notify_callback_ = std::move(callback);
}

void ConfigWatcher::setDebounceInterval(std::chrono::milliseconds ms) {
    debounce_interval_ = ms;
}

bool ConfigWatcher::start() {
    if (running_.load()) {
        return true;  // Already running
    }
    
    if (inotify_fd_ == -1 && !setupInotify()) {
        return false;
    }
    
    running_.store(true);
    watcher_thread_ = std::thread(&ConfigWatcher::watcherLoop, this);
    
    std::cout << "ConfigWatcher: Started monitoring configuration files" << std::endl;
    return true;
}

void ConfigWatcher::stop() {
    if (!running_.exchange(false)) {
        return;  // Already stopped
    }
    
    if (watcher_thread_.joinable()) {
        watcher_thread_.join();
    }
    
    std::cout << "ConfigWatcher: Stopped" << std::endl;
}

void ConfigWatcher::watcherLoop() {
    // Use poll to wait for inotify events with timeout
    struct pollfd pfd;
    pfd.fd = inotify_fd_;
    pfd.events = POLLIN;
    
    char buffer[INOTIFY_BUFFER_SIZE];
    
    while (running_.load()) {
        // Poll with 100ms timeout to allow checking running_ flag
        int ret = poll(&pfd, 1, 100);
        
        if (ret == -1) {
            if (errno == EINTR) {
                continue;  // Interrupted, check running_ and retry
            }
            std::cerr << "ConfigWatcher: Poll error: " << strerror(errno) << std::endl;
            break;
        }
        
        if (ret == 0) {
            // Timeout - process any pending debounced changes
            processDebouncedChanges();
            continue;
        }
        
        if (pfd.revents & POLLIN) {
            // Read inotify events
            ssize_t len = read(inotify_fd_, buffer, sizeof(buffer));
            
            if (len == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;  // No data available
                }
                std::cerr << "ConfigWatcher: Read error: " << strerror(errno) << std::endl;
                break;
            }
            
            // Process all events in buffer
            ssize_t i = 0;
            while (i < len) {
                const struct inotify_event* event = 
                    reinterpret_cast<const struct inotify_event*>(&buffer[i]);
                
                processEvent(event);
                
                i += sizeof(struct inotify_event) + event->len;
            }
            
            // If debounce is 0, process changes immediately after events
            if (debounce_interval_.count() == 0) {
                processDebouncedChanges();
            }
        }
    }
}

void ConfigWatcher::processEvent(const struct inotify_event* event) {
    // Find the path for this watch descriptor
    auto it = watch_descriptors_.find(event->wd);
    if (it == watch_descriptors_.end()) {
        return;
    }
    
    std::filesystem::path base_path = it->second;
    std::filesystem::path full_path = base_path;
    
    if (event->len > 0) {
        full_path /= event->name;
    }
    
    // Determine event type
    ConfigChangeEvent::Type change_type = ConfigChangeEvent::Type::Modified;
    
    if (event->mask & (IN_MODIFY | IN_CLOSE_WRITE)) {
        change_type = ConfigChangeEvent::Type::Modified;
    } else if (event->mask & IN_CREATE) {
        change_type = ConfigChangeEvent::Type::Created;
    } else if (event->mask & (IN_DELETE | IN_DELETE_SELF)) {
        change_type = ConfigChangeEvent::Type::Deleted;
    } else if (event->mask & IN_MOVED_TO) {
        change_type = ConfigChangeEvent::Type::Created;
    }
    
    // Only process .wmi files
    if (std::filesystem::is_regular_file(full_path) && 
        full_path.extension() == ".wmi") {
        handleFileChange(full_path, change_type);
    }
    
    // If new directory created in watched directory, add watch
    if ((event->mask & IN_CREATE) && (event->mask & IN_ISDIR)) {
        addInotifyWatch(full_path, true);
    }
}

void ConfigWatcher::handleFileChange(const std::filesystem::path& path, 
                                     ConfigChangeEvent::Type type) {
    if (type == ConfigChangeEvent::Type::Deleted) {
        std::cout << "ConfigWatcher: File deleted: " << path << std::endl;
        return;
    }
    
    std::cout << "ConfigWatcher: Configuration file changed: " << path << std::endl;
    
    if (auto_reload_) {
        debounceAndProcess(path);
    }
}

void ConfigWatcher::debounceAndProcess(const std::filesystem::path& path) {
    // Record the change time
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_changes_[path] = std::chrono::system_clock::now();
    }
}

void ConfigWatcher::processDebouncedChanges() {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    
    auto now = std::chrono::system_clock::now();
    
    for (auto it = pending_changes_.begin(); it != pending_changes_.end(); ) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - it->second);
        
        // If debounce is 0, process immediately
        if (debounce_interval_.count() == 0 || elapsed >= debounce_interval_) {
            // Process this change
            ValidationResult result = reload(it->first);
            
            if (result) {
                std::cout << "ConfigWatcher: Successfully reloaded: " 
                          << it->first << std::endl;
            } else {
                std::cerr << "ConfigWatcher: Failed to reload: " 
                          << it->first << std::endl;
            }
            
            it = pending_changes_.erase(it);
        } else {
            ++it;
        }
    }
}

ValidationResult ConfigWatcher::reload(const std::filesystem::path& path) {
    std::cout << "ConfigWatcher: Reloading configuration from " << path << std::endl;
    
    // Validate the configuration
    ValidationResult result = validateConfig(path);
    
    if (!result) {
        reportErrors(result);
        writeErrorLog(result, path);
        return result;
    }
    
    // Apply the configuration
    if (applyConfig(path)) {
        last_good_config_ = path;
        
        // Notify success
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (notify_callback_) {
                notify_callback_("Configuration reloaded successfully", "success");
            }
        }
    } else {
        result.success = false;
        result.errors.push_back("Failed to apply configuration");
        writeErrorLog(result, path);
        
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (notify_callback_) {
                notify_callback_("Failed to apply configuration", "error");
            }
        }
    }
    
    return result;
}

ValidationResult ConfigWatcher::validateConfig(const std::filesystem::path& path) {
    ValidationResult result;
    
    // Check file exists and is readable
    if (!std::filesystem::exists(path)) {
        result.success = false;
        result.errors.push_back("Configuration file does not exist: " + path.string());
        return result;
    }
    
    std::ifstream file(path);
    if (!file.is_open()) {
        result.success = false;
        result.errors.push_back("Cannot open configuration file: " + path.string());
        return result;
    }
    
    // Read file content for validation
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();
    
    // Basic syntax validation
    int brace_count = 0;
    int bracket_count = 0;
    int paren_count = 0;
    int line_num = 1;
    bool in_string = false;
    bool in_comment = false;
    char prev_char = '\0';
    
    for (size_t i = 0; i < content.size(); ++i) {
        char c = content[i];
        
        // Track line numbers
        if (c == '\n') {
            line_num++;
            in_comment = false;
            continue;
        }
        
        // Skip comment content
        if (in_comment) {
            prev_char = c;
            continue;
        }
        
        // Check for comment start
        if (c == '/' && prev_char == '/') {
            in_comment = true;
            continue;
        }
        
        // Handle string literals
        if (c == '"' && prev_char != '\\') {
            in_string = !in_string;
        }
        
        if (!in_string) {
            if (c == '{') brace_count++;
            else if (c == '}') {
                brace_count--;
                if (brace_count < 0) {
                    ValidationResult::ErrorLocation loc;
                    loc.line = line_num;
                    loc.message = "Unexpected closing brace '}'";
                    result.error_locations.push_back(loc);
                }
            }
            else if (c == '[') bracket_count++;
            else if (c == ']') {
                bracket_count--;
                if (bracket_count < 0) {
                    ValidationResult::ErrorLocation loc;
                    loc.line = line_num;
                    loc.message = "Unexpected closing bracket ']'";
                    result.error_locations.push_back(loc);
                }
            }
            else if (c == '(') paren_count++;
            else if (c == ')') {
                paren_count--;
                if (paren_count < 0) {
                    ValidationResult::ErrorLocation loc;
                    loc.line = line_num;
                    loc.message = "Unexpected closing parenthesis ')'";
                    result.error_locations.push_back(loc);
                }
            }
        }
        
        prev_char = c;
    }
    
    // Check for unclosed delimiters
    if (brace_count != 0) {
        ValidationResult::ErrorLocation loc;
        loc.line = line_num;
        loc.message = "Unclosed braces - missing " + std::to_string(brace_count) + " '}'";
        result.error_locations.push_back(loc);
    }
    
    if (bracket_count != 0) {
        ValidationResult::ErrorLocation loc;
        loc.line = line_num;
        loc.message = "Unclosed brackets - missing " + std::to_string(bracket_count) + " ']'";
        result.error_locations.push_back(loc);
    }
    
    if (paren_count != 0) {
        ValidationResult::ErrorLocation loc;
        loc.line = line_num;
        loc.message = "Unclosed parentheses - missing " + std::to_string(paren_count) + " ')'";
        result.error_locations.push_back(loc);
    }
    
    // Call custom validation callback if set
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (validation_callback_) {
            ValidationResult custom_result = validation_callback_(path);
            if (!custom_result) {
                // Merge errors
                result.success = false;
                for (const auto& err : custom_result.errors) {
                    result.errors.push_back(err);
                }
                for (const auto& loc : custom_result.error_locations) {
                    result.error_locations.push_back(loc);
                }
                for (const auto& warn : custom_result.warnings) {
                    result.warnings.push_back(warn);
                }
            }
        }
    }
    
    // If no errors found, mark as success
    if (result.error_locations.empty() && result.errors.empty()) {
        result.success = true;
    } else {
        result.success = false;
        
        // Build error list from locations
        for (const auto& loc : result.error_locations) {
            std::ostringstream oss;
            oss << "Line " << loc.line << ": " << loc.message;
            result.errors.push_back(oss.str());
        }
    }
    
    return result;
}

bool ConfigWatcher::applyConfig(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (apply_callback_) {
        return apply_callback_(path);
    }
    return false;
}

void ConfigWatcher::reportErrors(const ValidationResult& result) {
    std::cerr << "ConfigWatcher: Validation failed for configuration:" << std::endl;
    
    for (const auto& error : result.errors) {
        std::cerr << "  ERROR: " << error << std::endl;
    }
    
    for (const auto& loc : result.error_locations) {
        std::cerr << "  Line " << loc.line << ", Col " << loc.column 
                  << ": " << loc.message << std::endl;
        if (!loc.context.empty()) {
            std::cerr << "    Context: " << loc.context << std::endl;
        }
    }
    
    for (const auto& warning : result.warnings) {
        std::cerr << "  WARNING: " << warning << std::endl;
    }
    
    // Call error callback
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (error_callback_) {
            error_callback_(result);
        }
        
        // Also notify user
        if (notify_callback_) {
            std::string msg = "Configuration validation failed with " + 
                             std::to_string(result.errors.size()) + " error(s)";
            notify_callback_(msg, "error");
        }
    }
}

void ConfigWatcher::writeErrorLog(const ValidationResult& result, 
                                   const std::filesystem::path& config_path) {
    // Create error log directory if it doesn't exist
    std::filesystem::path error_dir(ERROR_LOG_DIR);
    std::error_code ec;
    
    if (!std::filesystem::exists(error_dir)) {
        if (!std::filesystem::create_directories(error_dir, ec)) {
            std::cerr << "ConfigWatcher: Failed to create error log directory: " 
                      << ec.message() << std::endl;
            return;
        }
    }
    
    // Generate timestamp for filename
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::ostringstream filename;
    filename << error_dir.string() << "/config_error_" 
              << std::put_time(std::localtime(&now_time_t), "%Y%m%d_%H%M%S")
              << "_" << std::setfill('0') << std::setw(3) << now_ms.count()
              << ".log";
    
    // Write error log
    std::ofstream log_file(filename.str());
    if (!log_file.is_open()) {
        std::cerr << "ConfigWatcher: Failed to create error log file: " 
                  << filename.str() << std::endl;
        return;
    }
    
    // Header
    log_file << "=== Pointblank Configuration Error Log ===" << std::endl;
    log_file << "Timestamp: " << std::put_time(std::localtime(&now_time_t), "%Y-%m-%d %H:%M:%S")
             << "." << std::setfill('0') << std::setw(3) << now_ms.count() << std::endl;
    log_file << "Config file: " << config_path.string() << std::endl;
    log_file << std::endl;
    
    // Errors
    log_file << "--- ERRORS (" << result.errors.size() << ") ---" << std::endl;
    for (const auto& error : result.errors) {
        log_file << "  " << error << std::endl;
    }
    log_file << std::endl;
    
    // Error locations
    if (!result.error_locations.empty()) {
        log_file << "--- ERROR LOCATIONS ---" << std::endl;
        for (const auto& loc : result.error_locations) {
            log_file << "  Line " << loc.line;
            if (loc.column > 0) {
                log_file << ", Col " << loc.column;
            }
            log_file << ": " << loc.message << std::endl;
            if (!loc.context.empty()) {
                log_file << "    Context: " << loc.context << std::endl;
            }
        }
        log_file << std::endl;
    }
    
    // Warnings
    if (!result.warnings.empty()) {
        log_file << "--- WARNINGS (" << result.warnings.size() << ") ---" << std::endl;
        for (const auto& warning : result.warnings) {
            log_file << "  " << warning << std::endl;
        }
        log_file << std::endl;
    }
    
    log_file.close();
    
    std::cout << "ConfigWatcher: Error log written to " << filename.str() << std::endl;
}

} // namespace pblank