#pragma once

/**
 * @file PerformanceTuner.hpp
 * @brief Performance Tuning Subsystem for Sub-Millisecond Response Times
 * 
 * Provides granular control over:
 * - Scheduler priority and policy (SCHED_FIFO, SCHED_RR, SCHED_OTHER)
 * - CPU affinity for thread pinning
 * - Render pipeline throttling and frame timing
 * - Real-time performance monitoring
 * 
 * Designed for high-frequency trading grade latency optimization.
 * 
 * @author Point Blank Systems Engineering Team
 * @version 2.0.0
 */

#include "pointblank/performance/LockFreeStructures.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <functional>
#include <sched.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <cpuid.h>

namespace pblank {

enum class SchedulerPolicy : int {
    Other = SCHED_OTHER,    
    FIFO = SCHED_FIFO,      
    RR = SCHED_RR,          
    Batch = 3,              
    Idle = 5                
};

struct ThreadPriority {
    SchedulerPolicy policy;
    int priority;           
    std::string thread_name;
    
    ThreadPriority() : policy(SchedulerPolicy::Other), priority(0) {}
    ThreadPriority(SchedulerPolicy p, int pr, const std::string& name = "")
        : policy(p), priority(pr), thread_name(name) {}
};

struct CPUAffinity {
    std::vector<int> cores;     
    bool exclusive;             
    bool hyperthreading_aware;  
    
    CPUAffinity() : exclusive(false), hyperthreading_aware(true) {}
};

struct RenderPipelineConfig {
    
    uint32_t target_fps{60};
    uint32_t min_fps{30};
    uint32_t max_fps{144};
    bool vsync_enabled{false};
    bool adaptive_sync{true};
    
    uint32_t throttle_threshold_us{1000};  
    uint32_t throttle_delay_us{100};       
    bool throttle_on_battery{true};
    
    uint32_t max_batch_size{16};           
    uint32_t batch_timeout_us{100};        
    
    bool dirty_rectangles_only{true};      
    bool double_buffer{true};
    bool triple_buffer{false};
};

struct PerformanceMetricsSnapshot {
    uint64_t frame_count{0};
    uint64_t total_frame_time_ns{0};
    uint64_t min_frame_time_ns{UINT64_MAX};
    uint64_t max_frame_time_ns{0};
    uint64_t events_processed{0};
    uint64_t events_dropped{0};
    uint64_t total_event_time_ns{0};
    uint64_t render_count{0};
    uint64_t total_render_time_ns{0};
    uint32_t p50_latency_us{0};
    uint32_t p95_latency_us{0};
    uint32_t p99_latency_us{0};
    uint32_t cpu_usage_percent{0};
    uint64_t memory_used_bytes{0};
};

struct PerformanceMetrics {
    
    std::atomic<uint64_t> frame_count{0};
    std::atomic<uint64_t> total_frame_time_ns{0};
    std::atomic<uint64_t> min_frame_time_ns{UINT64_MAX};
    std::atomic<uint64_t> max_frame_time_ns{0};
    
    std::atomic<uint64_t> events_processed{0};
    std::atomic<uint64_t> events_dropped{0};
    std::atomic<uint64_t> total_event_time_ns{0};
    
    std::atomic<uint64_t> render_count{0};
    std::atomic<uint64_t> total_render_time_ns{0};
    
    std::atomic<uint32_t> p50_latency_us{0};
    std::atomic<uint32_t> p95_latency_us{0};
    std::atomic<uint32_t> p99_latency_us{0};
    
    std::atomic<uint32_t> cpu_usage_percent{0};
    
    std::atomic<uint64_t> memory_used_bytes{0};
    
    PerformanceMetricsSnapshot snapshot() const {
        PerformanceMetricsSnapshot s;
        s.frame_count = frame_count.load(std::memory_order_relaxed);
        s.total_frame_time_ns = total_frame_time_ns.load(std::memory_order_relaxed);
        s.min_frame_time_ns = min_frame_time_ns.load(std::memory_order_relaxed);
        s.max_frame_time_ns = max_frame_time_ns.load(std::memory_order_relaxed);
        s.events_processed = events_processed.load(std::memory_order_relaxed);
        s.events_dropped = events_dropped.load(std::memory_order_relaxed);
        s.total_event_time_ns = total_event_time_ns.load(std::memory_order_relaxed);
        s.render_count = render_count.load(std::memory_order_relaxed);
        s.total_render_time_ns = total_render_time_ns.load(std::memory_order_relaxed);
        s.p50_latency_us = p50_latency_us.load(std::memory_order_relaxed);
        s.p95_latency_us = p95_latency_us.load(std::memory_order_relaxed);
        s.p99_latency_us = p99_latency_us.load(std::memory_order_relaxed);
        s.cpu_usage_percent = cpu_usage_percent.load(std::memory_order_relaxed);
        s.memory_used_bytes = memory_used_bytes.load(std::memory_order_relaxed);
        return s;
    }
    
    void reset() {
        frame_count.store(0, std::memory_order_relaxed);
        total_frame_time_ns.store(0, std::memory_order_relaxed);
        min_frame_time_ns.store(UINT64_MAX, std::memory_order_relaxed);
        max_frame_time_ns.store(0, std::memory_order_relaxed);
        events_processed.store(0, std::memory_order_relaxed);
        events_dropped.store(0, std::memory_order_relaxed);
        total_event_time_ns.store(0, std::memory_order_relaxed);
        render_count.store(0, std::memory_order_relaxed);
        total_render_time_ns.store(0, std::memory_order_relaxed);
    }
};

struct CpuTopology {
    int num_cores;
    int num_threads;
    int num_sockets;
    std::vector<std::vector<int>> cores_per_socket;
    std::vector<std::vector<int>> threads_per_core;  
    
    static CpuTopology detect() {
        CpuTopology topo;
        topo.num_cores = get_nprocs();
        topo.num_threads = sysconf(_SC_NPROCESSORS_ONLN);
        topo.num_sockets = 1;  
        
        topo.cores_per_socket.resize(1);
        for (int i = 0; i < topo.num_cores; ++i) {
            topo.cores_per_socket[0].push_back(i);
        }
        
        topo.threads_per_core.resize(topo.num_cores);
        for (int i = 0; i < topo.num_threads; ++i) {
            topo.threads_per_core[i % topo.num_cores].push_back(i);
        }
        
        return topo;
    }
};

class PerformanceTuner {
public:
    using FrameCallback = std::function<void(std::chrono::nanoseconds)>;
    
    PerformanceTuner();
    
    ~PerformanceTuner();
    
    PerformanceTuner(const PerformanceTuner&) = delete;
    PerformanceTuner& operator=(const PerformanceTuner&) = delete;
    
    void loadFromConfig(const std::unordered_map<std::string, std::string>& config);
    
    bool setMainThreadPriority(const ThreadPriority& priority);
    
    bool setThreadPriority(std::thread::native_handle_type thread, 
                          const ThreadPriority& priority);
    
    bool setMainThreadAffinity(const CPUAffinity& affinity);
    
    bool setThreadAffinity(std::thread::native_handle_type thread,
                          const CPUAffinity& affinity);
    
    void setRenderPipelineConfig(const RenderPipelineConfig& config);
    
    const RenderPipelineConfig& getRenderPipelineConfig() const { return render_config_; }
    
    bool enableRealTimeMode(int priority = 50);
    
    void disableRealTimeMode();
    
    bool lockMemory(size_t size_mb = 64);
    
    void unlockMemory();
    
    void* preallocateMemory(size_t size);
    
    std::chrono::steady_clock::time_point beginFrame();
    
    void endFrame(std::chrono::steady_clock::time_point frame_start);
    
    bool shouldThrottle() const;
    
    std::chrono::microseconds getTimeUntilNextFrame() const;
    
    void waitForNextFrame();
    
    void setFrameCallback(FrameCallback callback) { frame_callback_ = std::move(callback); }
    
    PerformanceMetricsSnapshot getMetrics() const { return metrics_.snapshot(); }
    
    void resetMetrics() { metrics_.reset(); }
    
    double getCurrentFPS() const;
    
    std::chrono::microseconds getAverageFrameTime() const;
    
    struct LatencyPercentiles {
        uint32_t p50_us;
        uint32_t p95_us;
        uint32_t p99_us;
    };
    LatencyPercentiles getLatencyPercentiles() const;
    
    void updateLatencyPercentiles();
    
    const CpuTopology& getCpuTopology() const { return cpu_topology_; }
    
    std::vector<int> getRecommendedCores() const;
    
    bool hasCpuFeature(const std::string& feature) const;
    
    void recordEventTime(std::chrono::nanoseconds duration);
    
    void recordRenderTime(std::chrono::nanoseconds duration);
    
    void incrementEventCount(bool dropped = false);
    
private:
    
    CpuTopology cpu_topology_;
    
    ThreadPriority main_thread_priority_;
    CPUAffinity main_thread_affinity_;
    RenderPipelineConfig render_config_;
    
    int original_scheduler_policy_;
    int original_priority_;
    cpu_set_t original_affinity_;
    bool original_settings_saved_{false};
    
    bool memory_locked_{false};
    size_t locked_memory_size_{0};
    
    std::chrono::steady_clock::time_point last_frame_start_;
    std::chrono::steady_clock::time_point last_frame_end_;
    std::chrono::nanoseconds frame_budget_{16666667};  
    std::atomic<bool> throttling_{false};
    
    PerformanceMetrics metrics_;
    
    static constexpr size_t LATENCY_HISTORY_SIZE = 1024;
    lockfree::SPSCRingBuffer<uint32_t, LATENCY_HISTORY_SIZE> latency_history_;
    
    FrameCallback frame_callback_;
    
    std::unordered_map<std::string, bool> cpu_features_;
    
    void detectCpuFeatures();
    bool setCpuAffinity(pthread_t thread, const cpu_set_t& mask);
    bool getCpuAffinity(pthread_t thread, cpu_set_t& mask);
    void updateCpuUsage();
};

inline std::chrono::steady_clock::time_point PerformanceTuner::beginFrame() {
    last_frame_start_ = std::chrono::steady_clock::now();
    return last_frame_start_;
}

inline void PerformanceTuner::endFrame(std::chrono::steady_clock::time_point frame_start) {
    auto frame_end = std::chrono::steady_clock::now();
    last_frame_end_ = frame_end;
    
    auto frame_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
        frame_end - frame_start);
    
    uint64_t frame_time_ns = frame_time.count();
    metrics_.frame_count.fetch_add(1, std::memory_order_relaxed);
    metrics_.total_frame_time_ns.fetch_add(frame_time_ns, std::memory_order_relaxed);
    
    uint64_t current_min = metrics_.min_frame_time_ns.load(std::memory_order_relaxed);
    while (frame_time_ns < current_min && 
           !metrics_.min_frame_time_ns.compare_exchange_weak(
               current_min, frame_time_ns, std::memory_order_relaxed)) {}
    
    uint64_t current_max = metrics_.max_frame_time_ns.load(std::memory_order_relaxed);
    while (frame_time_ns > current_max && 
           !metrics_.max_frame_time_ns.compare_exchange_weak(
               current_max, frame_time_ns, std::memory_order_relaxed)) {}
    
    uint32_t latency_us = static_cast<uint32_t>(frame_time_ns / 1000);
    latency_history_.push(latency_us);
    
    if (frame_callback_) {
        frame_callback_(frame_time);
    }
}

inline bool PerformanceTuner::shouldThrottle() const {
    if (!throttling_.load(std::memory_order_relaxed)) {
        return false;
    }
    
    auto elapsed = std::chrono::steady_clock::now() - last_frame_start_;
    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed);
    
    return elapsed_us.count() < render_config_.throttle_delay_us;
}

inline std::chrono::microseconds PerformanceTuner::getTimeUntilNextFrame() const {
    auto elapsed = std::chrono::steady_clock::now() - last_frame_start_;
    auto remaining = frame_budget_ - std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed);
    
    if (remaining.count() <= 0) {
        return std::chrono::microseconds(0);
    }
    
    return std::chrono::duration_cast<std::chrono::microseconds>(remaining);
}

inline void PerformanceTuner::waitForNextFrame() {
    auto remaining = getTimeUntilNextFrame();
    
    if (remaining.count() > 0) {
        std::this_thread::sleep_for(remaining);
    }
}

inline double PerformanceTuner::getCurrentFPS() const {
    uint64_t frames = metrics_.frame_count.load(std::memory_order_relaxed);
    uint64_t total_time = metrics_.total_frame_time_ns.load(std::memory_order_relaxed);
    
    if (total_time == 0 || frames == 0) {
        return 0.0;
    }
    
    double avg_frame_time_ns = static_cast<double>(total_time) / frames;
    return 1e9 / avg_frame_time_ns;
}

inline std::chrono::microseconds PerformanceTuner::getAverageFrameTime() const {
    uint64_t frames = metrics_.frame_count.load(std::memory_order_relaxed);
    uint64_t total_time = metrics_.total_frame_time_ns.load(std::memory_order_relaxed);
    
    if (frames == 0) {
        return std::chrono::microseconds(0);
    }
    
    return std::chrono::microseconds(total_time / frames / 1000);
}

inline void PerformanceTuner::recordEventTime(std::chrono::nanoseconds duration) {
    metrics_.total_event_time_ns.fetch_add(duration.count(), std::memory_order_relaxed);
}

inline void PerformanceTuner::recordRenderTime(std::chrono::nanoseconds duration) {
    metrics_.render_count.fetch_add(1, std::memory_order_relaxed);
    metrics_.total_render_time_ns.fetch_add(duration.count(), std::memory_order_relaxed);
}

inline void PerformanceTuner::incrementEventCount(bool dropped) {
    if (dropped) {
        metrics_.events_dropped.fetch_add(1, std::memory_order_relaxed);
    } else {
        metrics_.events_processed.fetch_add(1, std::memory_order_relaxed);
    }
}

} 
