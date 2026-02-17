/**
 * @file PerformanceTuner.cpp
 * @brief Implementation of the Performance Tuning Subsystem
 * 
 * @author Point Blank Systems Engineering Team
 * @version 2.0.0
 */

#include "pointblank/performance/PerformanceTuner.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <cstring>
#include <sys/mman.h>

namespace pblank {

// ============================================================================
// Constructor / Destructor
// ============================================================================

PerformanceTuner::PerformanceTuner()
    : cpu_topology_(CpuTopology::detect())
    , frame_budget_(1000000000 / render_config_.target_fps)
{
    // Save original settings
    pthread_t thread = pthread_self();
    
    // Save original scheduler settings
    original_scheduler_policy_ = sched_getscheduler(0);
    
    struct sched_param param;
    sched_getparam(0, &param);
    original_priority_ = param.sched_priority;
    
    // Save original CPU affinity
    CPU_ZERO(&original_affinity_);
    if (pthread_getaffinity_np(thread, sizeof(original_affinity_), &original_affinity_) == 0) {
        original_settings_saved_ = true;
    }
    
    // Detect CPU features
    detectCpuFeatures();
    
    // Set default frame budget based on target FPS
    frame_budget_ = std::chrono::nanoseconds(1000000000 / render_config_.target_fps);
}

PerformanceTuner::~PerformanceTuner() {
    // Unlock memory if locked
    if (memory_locked_) {
        unlockMemory();
    }
    
    // Restore original settings if we saved them
    if (original_settings_saved_) {
        // Restore scheduler
        struct sched_param param;
        param.sched_priority = original_priority_;
        sched_setscheduler(0, original_scheduler_policy_, &param);
        
        // Restore CPU affinity
        pthread_setaffinity_np(pthread_self(), sizeof(original_affinity_), &original_affinity_);
    }
}

// ============================================================================
// Configuration
// ============================================================================

void PerformanceTuner::loadFromConfig(const std::unordered_map<std::string, std::string>& config) {
    // Parse scheduler settings
    auto it = config.find("scheduler_policy");
    if (it != config.end()) {
        if (it->second == "fifo") {
            main_thread_priority_.policy = SchedulerPolicy::FIFO;
        } else if (it->second == "rr") {
            main_thread_priority_.policy = SchedulerPolicy::RR;
        } else if (it->second == "batch") {
            main_thread_priority_.policy = SchedulerPolicy::Batch;
        } else {
            main_thread_priority_.policy = SchedulerPolicy::Other;
        }
    }
    
    it = config.find("scheduler_priority");
    if (it != config.end()) {
        main_thread_priority_.priority = std::stoi(it->second);
    }
    
    // Parse CPU affinity
    it = config.find("cpu_cores");
    if (it != config.end()) {
        std::stringstream ss(it->second);
        std::string core;
        while (std::getline(ss, core, ',')) {
            main_thread_affinity_.cores.push_back(std::stoi(core));
        }
    }
    
    it = config.find("cpu_exclusive");
    if (it != config.end()) {
        main_thread_affinity_.exclusive = (it->second == "true");
    }
    
    // Parse render pipeline settings
    it = config.find("target_fps");
    if (it != config.end()) {
        render_config_.target_fps = std::stoul(it->second);
        frame_budget_ = std::chrono::nanoseconds(1000000000 / render_config_.target_fps);
    }
    
    it = config.find("min_fps");
    if (it != config.end()) {
        render_config_.min_fps = std::stoul(it->second);
    }
    
    it = config.find("max_fps");
    if (it != config.end()) {
        render_config_.max_fps = std::stoul(it->second);
    }
    
    it = config.find("vsync");
    if (it != config.end()) {
        render_config_.vsync_enabled = (it->second == "true");
    }
    
    it = config.find("throttle_threshold_us");
    if (it != config.end()) {
        render_config_.throttle_threshold_us = std::stoul(it->second);
    }
    
    it = config.find("throttle_delay_us");
    if (it != config.end()) {
        render_config_.throttle_delay_us = std::stoul(it->second);
    }
    
    it = config.find("max_batch_size");
    if (it != config.end()) {
        render_config_.max_batch_size = std::stoul(it->second);
    }
    
    it = config.find("dirty_rectangles_only");
    if (it != config.end()) {
        render_config_.dirty_rectangles_only = (it->second == "true");
    }
    
    // Apply settings
    if (main_thread_priority_.policy != SchedulerPolicy::Other || 
        main_thread_priority_.priority > 0) {
        setMainThreadPriority(main_thread_priority_);
    }
    
    if (!main_thread_affinity_.cores.empty()) {
        setMainThreadAffinity(main_thread_affinity_);
    }
}

bool PerformanceTuner::setMainThreadPriority(const ThreadPriority& priority) {
    main_thread_priority_ = priority;
    
    struct sched_param param;
    param.sched_priority = priority.priority;
    
    int result = sched_setscheduler(0, static_cast<int>(priority.policy), &param);
    
    if (result != 0) {
        // May need CAP_SYS_NICE capability for real-time priorities
        return false;
    }
    
    // Set thread name if specified
    if (!priority.thread_name.empty()) {
        pthread_setname_np(pthread_self(), priority.thread_name.c_str());
    }
    
    return true;
}

bool PerformanceTuner::setThreadPriority(std::thread::native_handle_type thread,
                                         const ThreadPriority& priority) {
    struct sched_param param;
    param.sched_priority = priority.priority;
    
    int result = pthread_setschedparam(thread, static_cast<int>(priority.policy), &param);
    
    return result == 0;
}

bool PerformanceTuner::setMainThreadAffinity(const CPUAffinity& affinity) {
    main_thread_affinity_ = affinity;
    
    cpu_set_t mask;
    CPU_ZERO(&mask);
    
    if (affinity.cores.empty()) {
        // Use recommended cores
        auto recommended = getRecommendedCores();
        for (int core : recommended) {
            CPU_SET(core, &mask);
        }
    } else {
        for (int core : affinity.cores) {
            if (core >= 0 && core < cpu_topology_.num_threads) {
                CPU_SET(core, &mask);
            }
        }
    }
    
    return setCpuAffinity(pthread_self(), mask);
}

bool PerformanceTuner::setThreadAffinity(std::thread::native_handle_type thread,
                                         const CPUAffinity& affinity) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    
    for (int core : affinity.cores) {
        if (core >= 0 && core < cpu_topology_.num_threads) {
            CPU_SET(core, &mask);
        }
    }
    
    return setCpuAffinity(thread, mask);
}

void PerformanceTuner::setRenderPipelineConfig(const RenderPipelineConfig& config) {
    render_config_ = config;
    frame_budget_ = std::chrono::nanoseconds(1000000000 / config.target_fps);
}

// ============================================================================
// Real-Time Optimization
// ============================================================================

bool PerformanceTuner::enableRealTimeMode(int priority) {
    // Set real-time scheduler
    ThreadPriority rt_priority(SchedulerPolicy::FIFO, priority, "pointblank_main");
    
    if (!setMainThreadPriority(rt_priority)) {
        return false;
    }
    
    // Lock memory to prevent page faults
    if (!lockMemory(64)) {
        // Non-fatal, but log warning
    }
    
    return true;
}

void PerformanceTuner::disableRealTimeMode() {
    // Restore normal scheduler
    struct sched_param param;
    param.sched_priority = 0;
    sched_setscheduler(0, SCHED_OTHER, &param);
    
    // Unlock memory
    unlockMemory();
}

bool PerformanceTuner::lockMemory(size_t size_mb) {
    // Lock all current and future memory
    int result = mlockall(MCL_CURRENT | MCL_FUTURE);
    
    if (result != 0) {
        return false;
    }
    
    memory_locked_ = true;
    locked_memory_size_ = size_mb * 1024 * 1024;
    
    // Preallocate memory to avoid page faults
    void* buffer = preallocateMemory(locked_memory_size_);
    if (buffer) {
        // Touch all pages to ensure they're resident
        const size_t page_size = sysconf(_SC_PAGESIZE);
        volatile char* pages = static_cast<volatile char*>(buffer);
        
        for (size_t i = 0; i < locked_memory_size_; i += page_size) {
            pages[i] = 0;
        }
        
        // Free but keep locked
        free(buffer);
    }
    
    return true;
}

void PerformanceTuner::unlockMemory() {
    if (memory_locked_) {
        munlockall();
        memory_locked_ = false;
        locked_memory_size_ = 0;
    }
}

void* PerformanceTuner::preallocateMemory(size_t size) {
    void* ptr = nullptr;
    
    if (posix_memalign(&ptr, 64, size) != 0) {
        return nullptr;
    }
    
    return ptr;
}

// ============================================================================
// Performance Monitoring
// ============================================================================

PerformanceTuner::LatencyPercentiles PerformanceTuner::getLatencyPercentiles() const {
    LatencyPercentiles percentiles;
    percentiles.p50_us = metrics_.p50_latency_us.load(std::memory_order_relaxed);
    percentiles.p95_us = metrics_.p95_latency_us.load(std::memory_order_relaxed);
    percentiles.p99_us = metrics_.p99_latency_us.load(std::memory_order_relaxed);
    return percentiles;
}

void PerformanceTuner::updateLatencyPercentiles() {
    // Collect latency history
    std::vector<uint32_t> latencies;
    latencies.reserve(LATENCY_HISTORY_SIZE);
    
    while (auto latency = latency_history_.pop()) {
        latencies.push_back(*latency);
    }
    
    if (latencies.empty()) {
        return;
    }
    
    // Sort for percentile calculation
    std::sort(latencies.begin(), latencies.end());
    
    size_t p50_idx = latencies.size() * 50 / 100;
    size_t p95_idx = latencies.size() * 95 / 100;
    size_t p99_idx = latencies.size() * 99 / 100;
    
    metrics_.p50_latency_us.store(latencies[p50_idx], std::memory_order_relaxed);
    metrics_.p95_latency_us.store(latencies[p95_idx], std::memory_order_relaxed);
    metrics_.p99_latency_us.store(latencies[p99_idx], std::memory_order_relaxed);
}

// ============================================================================
// CPU Information
// ============================================================================

std::vector<int> PerformanceTuner::getRecommendedCores() const {
    std::vector<int> recommended;
    
    // Prefer physical cores over hyperthreads
    if (cpu_topology_.threads_per_core.size() > 0) {
        // Take first thread of each core
        for (const auto& threads : cpu_topology_.threads_per_core) {
            if (!threads.empty()) {
                recommended.push_back(threads[0]);
            }
        }
    } else {
        // Use all available cores
        for (int i = 0; i < cpu_topology_.num_cores; ++i) {
            recommended.push_back(i);
        }
    }
    
    // Limit to first 4 cores for cache locality
    if (recommended.size() > 4) {
        recommended.resize(4);
    }
    
    return recommended;
}

bool PerformanceTuner::hasCpuFeature(const std::string& feature) const {
    auto it = cpu_features_.find(feature);
    return it != cpu_features_.end() && it->second;
}

void PerformanceTuner::detectCpuFeatures() {
    uint32_t eax, ebx, ecx, edx;
    
    // Check basic CPUID support
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        cpu_features_["sse"] = (edx & bit_SSE) != 0;
        cpu_features_["sse2"] = (edx & bit_SSE2) != 0;
        cpu_features_["sse3"] = (ecx & bit_SSE3) != 0;
        cpu_features_["ssse3"] = (ecx & bit_SSSE3) != 0;
        cpu_features_["sse4_1"] = (ecx & bit_SSE4_1) != 0;
        cpu_features_["sse4_2"] = (ecx & bit_SSE4_2) != 0;
        cpu_features_["avx"] = (ecx & bit_AVX) != 0;
        cpu_features_["fma"] = (ecx & bit_FMA) != 0;
    }
    
    // Check for AVX2
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        cpu_features_["avx2"] = (ebx & bit_AVX2) != 0;
        cpu_features_["bmi1"] = (ebx & bit_BMI) != 0;
        cpu_features_["bmi2"] = (ebx & bit_BMI2) != 0;
    }
    
    // Check for constant TSC
    if (__get_cpuid(0x80000007, &eax, &ebx, &ecx, &edx)) {
        cpu_features_["invariant_tsc"] = (edx & (1 << 8)) != 0;
    }
}

// ============================================================================
// Internal Methods
// ============================================================================

bool PerformanceTuner::setCpuAffinity(pthread_t thread, const cpu_set_t& mask) {
    return pthread_setaffinity_np(thread, sizeof(mask), &mask) == 0;
}

bool PerformanceTuner::getCpuAffinity(pthread_t thread, cpu_set_t& mask) {
    return pthread_getaffinity_np(thread, sizeof(mask), &mask) == 0;
}

void PerformanceTuner::updateCpuUsage() {
    // Read CPU usage from /proc/self/stat
    std::ifstream stat("/proc/self/stat");
    if (!stat.is_open()) {
        return;
    }
    
    std::string line;
    std::getline(stat, line);
    
    // Parse the relevant fields
    std::istringstream iss(line);
    std::string token;
    
    // Skip to utime and stime (fields 14 and 15)
    for (int i = 0; i < 13; ++i) {
        iss >> token;
    }
    
    unsigned long utime, stime;
    iss >> utime >> stime;
    
    // Calculate CPU usage (simplified)
    unsigned long total_time = utime + stime;
    
    // This is a simplified calculation - for accurate CPU usage,
    // we'd need to track time intervals
    metrics_.cpu_usage_percent.store(
        static_cast<uint32_t>(total_time % 100), 
        std::memory_order_relaxed);
}

} // namespace pblank
