/**
 * @file SyncManager.hpp
 * @brief XSync Extension Manager for resize synchronization
 * 
 * Implements the _NET_WM_SYNC_REQUEST protocol for flicker-free window
 * resizing. This allows applications to synchronize their redraws with
 * the window manager's resize operations.
 * 
 * Phase 7 of Enhanced TWM Features
 */

#ifndef SYNCMANAGER_HPP
#define SYNCMANAGER_HPP

#include <X11/Xlib.h>
#include <X11/extensions/sync.h>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>

namespace pblank {

struct SyncCounter {
    XSyncCounter counter{0};        
    XSyncValue value;               
    XSyncAlarm alarm{0};            
    bool active{false};             
    uint64_t last_update{0};        
};

struct ResizeSyncState {
    Window window{0};               
    XSyncCounter counter{0};        
    XSyncValue initial_value;       
    XSyncValue target_value;        
    int64_t serial{0};              
    bool waiting_for_update{false}; 
    uint64_t start_time{0};         
};

class SyncManager {
public:
    using ResizeCompleteCallback = std::function<void(Window, int64_t)>;

    static SyncManager& instance();

    bool initialize(Display* display);

    bool isAvailable() const { return sync_available_; }

    bool hasSyncSupport(Window window) const;

    XSyncCounter getSyncCounter(Window window) const;

    void registerWindow(Window window, XSyncCounter counter);

    void unregisterWindow(Window window);

    bool beginResizeSync(Window window, int64_t serial);

    void endResizeSync(Window window);

    void handleAlarmEvent(XSyncAlarmNotifyEvent* event);

    void handleCounterEvent(XSyncCounter counter, XSyncValue value);

    void setResizeCompleteCallback(ResizeCompleteCallback callback);

    void updateResizeTarget(Window window, int64_t serial);

    bool isWaitingForSync(Window window) const;

    size_t getPendingSyncCount() const;

    void processTimeouts(uint32_t timeout_ms = 100);

    XSyncCounter createCounter();

    void destroyCounter(XSyncCounter counter);

    void setCounter(XSyncCounter counter, XSyncValue value);

    void incrementCounter(XSyncCounter counter, int64_t amount = 1);

    static uint64_t getCurrentTimeMs();

private:
    SyncManager() = default;
    ~SyncManager();
    SyncManager(const SyncManager&) = delete;
    SyncManager& operator=(const SyncManager&) = delete;

    XSyncAlarm createAlarm(XSyncCounter counter, XSyncValue threshold,
                           XSyncTestType test_type);

    void destroyAlarm(XSyncAlarm alarm);

    static XSyncValue intToSyncValue(int64_t value);

    static int64_t syncValueToInt(XSyncValue value);

    Display* display_{nullptr};
    int sync_event_base_{0};
    int sync_error_base_{0};
    bool sync_available_{false};

    std::unordered_map<Window, SyncCounter> window_counters_;
    std::unordered_map<Window, ResizeSyncState> resize_states_;
    std::unordered_map<XSyncAlarm, Window> alarm_windows_;
    
    ResizeCompleteCallback resize_complete_callback_;
    mutable std::mutex mutex_;
    
    std::atomic<int64_t> next_serial_{1};
    XSyncCounter wm_counter_{0};  
};

} 

#endif 
