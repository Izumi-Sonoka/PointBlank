/**
 * @file SyncManager.cpp
 * @brief XSync Extension Manager implementation
 * 
 * Phase 7 of Enhanced TWM Features
 */

#include "pointblank/display/SyncManager.hpp"
#include <X11/Xutil.h>
#include <chrono>
#include <cstring>
#include <algorithm>

namespace pblank {

SyncManager& SyncManager::instance() {
    static SyncManager instance;
    return instance;
}

SyncManager::~SyncManager() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Destroy all alarms
    for (auto& pair : alarm_windows_) {
        if (pair.first && sync_available_) {
            XSyncDestroyAlarm(display_, pair.first);
        }
    }
    
    // Destroy WM counter
    if (wm_counter_ && sync_available_) {
        XSyncDestroyCounter(display_, wm_counter_);
    }
}

bool SyncManager::initialize(Display* display) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (sync_available_) {
        return true;  // Already initialized
    }
    
    display_ = display;
    
    // Check for XSync extension
    int major_version = 0, minor_version = 0;
    if (!XSyncQueryExtension(display, &sync_event_base_, &sync_error_base_)) {
        return false;
    }
    
    if (!XSyncInitialize(display, &major_version, &minor_version)) {
        return false;
    }
    
    sync_available_ = true;
    
    // Create WM's own counter for compositing sync
    wm_counter_ = createCounter();
    
    return true;
}

bool SyncManager::hasSyncSupport(Window window) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return window_counters_.find(window) != window_counters_.end();
}

XSyncCounter SyncManager::getSyncCounter(Window window) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = window_counters_.find(window);
    if (it != window_counters_.end()) {
        return it->second.counter;
    }
    return 0;
}

void SyncManager::registerWindow(Window window, XSyncCounter counter) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!sync_available_ || counter == 0) {
        return;
    }
    
    SyncCounter& sc = window_counters_[window];
    sc.counter = counter;
    XSyncIntToValue(&sc.value, 0);
    sc.active = true;
    sc.last_update = getCurrentTimeMs();
    
    // Create an alarm to monitor counter changes
    XSyncValue threshold;
    XSyncIntToValue(&threshold, 0);
    sc.alarm = createAlarm(counter, threshold, XSyncPositiveComparison);
    
    if (sc.alarm) {
        alarm_windows_[sc.alarm] = window;
    }
}

void SyncManager::unregisterWindow(Window window) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = window_counters_.find(window);
    if (it != window_counters_.end()) {
        if (it->second.alarm) {
            alarm_windows_.erase(it->second.alarm);
            destroyAlarm(it->second.alarm);
        }
        window_counters_.erase(it);
    }
    
    // Also clean up any pending resize state
    resize_states_.erase(window);
}

bool SyncManager::beginResizeSync(Window window, int64_t serial) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!sync_available_) {
        return false;
    }
    
    auto counter_it = window_counters_.find(window);
    if (counter_it == window_counters_.end() || !counter_it->second.active) {
        return false;
    }
    
    ResizeSyncState& state = resize_states_[window];
    state.window = window;
    state.counter = counter_it->second.counter;
    state.initial_value = counter_it->second.value;
    state.serial = serial;
    state.waiting_for_update = true;
    state.start_time = getCurrentTimeMs();
    
    // Set target value to current + 1 (client will increment when ready)
    XSyncValue target;
    XSyncIntToValue(&target, syncValueToInt(state.initial_value) + 1);
    state.target_value = target;
    
    return true;
}

void SyncManager::endResizeSync(Window window) {
    std::lock_guard<std::mutex> lock(mutex_);
    resize_states_.erase(window);
}

void SyncManager::handleAlarmEvent(XSyncAlarmNotifyEvent* event) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!sync_available_) {
        return;
    }
    
    // Find the window for this alarm
    auto alarm_it = alarm_windows_.find(event->alarm);
    if (alarm_it == alarm_windows_.end()) {
        return;
    }
    
    Window window = alarm_it->second;
    
    // Update counter value
    auto counter_it = window_counters_.find(window);
    if (counter_it != window_counters_.end()) {
        counter_it->second.value = event->counter_value;
        counter_it->second.last_update = getCurrentTimeMs();
    }
    
    // Check if this completes a resize sync
    auto resize_it = resize_states_.find(window);
    if (resize_it != resize_states_.end() && resize_it->second.waiting_for_update) {
        int64_t current = syncValueToInt(event->counter_value);
        int64_t target = syncValueToInt(resize_it->second.target_value);
        
        if (current >= target) {
            // Resize sync completed
            resize_it->second.waiting_for_update = false;
            
            if (resize_complete_callback_) {
                int64_t serial = resize_it->second.serial;
                resize_complete_callback_(window, serial);
            }
        }
    }
    
    // Recreate alarm for next event
    // XSync alarms are automatically destroyed when triggered
    if (counter_it != window_counters_.end()) {
        XSyncValue next_threshold;
        XSyncIntToValue(&next_threshold, syncValueToInt(event->counter_value) + 1);
        counter_it->second.alarm = createAlarm(
            counter_it->second.counter,
            next_threshold,
            XSyncPositiveComparison
        );
        
        if (counter_it->second.alarm) {
            alarm_windows_[counter_it->second.alarm] = window;
        }
        alarm_windows_.erase(event->alarm);
    }
}

void SyncManager::handleCounterEvent(XSyncCounter counter, XSyncValue value) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Find window with this counter
    for (auto& pair : window_counters_) {
        if (pair.second.counter == counter) {
            pair.second.value = value;
            pair.second.last_update = getCurrentTimeMs();
            
            // Check resize sync completion
            auto resize_it = resize_states_.find(pair.first);
            if (resize_it != resize_states_.end() && resize_it->second.waiting_for_update) {
                int64_t current = syncValueToInt(value);
                int64_t target = syncValueToInt(resize_it->second.target_value);
                
                if (current >= target) {
                    resize_it->second.waiting_for_update = false;
                    
                    if (resize_complete_callback_) {
                        resize_complete_callback_(pair.first, resize_it->second.serial);
                    }
                }
            }
            break;
        }
    }
}

void SyncManager::setResizeCompleteCallback(ResizeCompleteCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    resize_complete_callback_ = std::move(callback);
}

void SyncManager::updateResizeTarget(Window window, int64_t serial) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = resize_states_.find(window);
    if (it != resize_states_.end()) {
        it->second.serial = serial;
        
        // Update target value
        auto counter_it = window_counters_.find(window);
        if (counter_it != window_counters_.end()) {
            int64_t current = syncValueToInt(counter_it->second.value);
            XSyncIntToValue(&it->second.target_value, current + 1);
        }
    }
}

bool SyncManager::isWaitingForSync(Window window) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = resize_states_.find(window);
    return it != resize_states_.end() && it->second.waiting_for_update;
}

size_t SyncManager::getPendingSyncCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (const auto& pair : resize_states_) {
        if (pair.second.waiting_for_update) {
            ++count;
        }
    }
    return count;
}

void SyncManager::processTimeouts(uint32_t timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    uint64_t now = getCurrentTimeMs();
    std::vector<Window> timed_out;
    
    for (auto& pair : resize_states_) {
        if (pair.second.waiting_for_update) {
            if (now - pair.second.start_time > timeout_ms) {
                timed_out.push_back(pair.first);
            }
        }
    }
    
    // Complete timed-out syncs
    for (Window window : timed_out) {
        auto it = resize_states_.find(window);
        if (it != resize_states_.end()) {
            it->second.waiting_for_update = false;
            
            if (resize_complete_callback_) {
                resize_complete_callback_(window, it->second.serial);
            }
        }
    }
}

XSyncCounter SyncManager::createCounter() {
    if (!sync_available_) {
        return 0;
    }
    
    XSyncValue initial;
    XSyncIntToValue(&initial, 0);
    return XSyncCreateCounter(display_, initial);
}

void SyncManager::destroyCounter(XSyncCounter counter) {
    if (sync_available_ && counter) {
        XSyncDestroyCounter(display_, counter);
    }
}

void SyncManager::setCounter(XSyncCounter counter, XSyncValue value) {
    if (sync_available_ && counter) {
        XSyncSetCounter(display_, counter, value);
    }
}

void SyncManager::incrementCounter(XSyncCounter counter, int64_t amount) {
    if (sync_available_ && counter) {
        XSyncValue current;
        XSyncIntToValue(&current, amount);
        XSyncChangeCounter(display_, counter, current);
    }
}

uint64_t SyncManager::getCurrentTimeMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

XSyncAlarm SyncManager::createAlarm(XSyncCounter counter, XSyncValue threshold,
                                     XSyncTestType test_type) {
    if (!sync_available_ || !counter) {
        return 0;
    }
    
    XSyncAlarmAttributes attr;
    attr.trigger.counter = counter;
    attr.trigger.value_type = XSyncAbsolute;
    attr.trigger.test_type = test_type;
    attr.trigger.wait_value = threshold;
    attr.delta = intToSyncValue(1);
    
    unsigned long mask = XSyncCACounter | XSyncCAValueType | XSyncCATestType |
                         XSyncCAValue | XSyncCADelta;
    
    return XSyncCreateAlarm(display_, mask, &attr);
}

void SyncManager::destroyAlarm(XSyncAlarm alarm) {
    if (sync_available_ && alarm) {
        XSyncDestroyAlarm(display_, alarm);
    }
}

XSyncValue SyncManager::intToSyncValue(int64_t value) {
    XSyncValue sync_value;
    XSyncIntsToValue(&sync_value, 
                     static_cast<unsigned int>(value & 0xFFFFFFFF),
                     static_cast<int>(value >> 32));
    return sync_value;
}

int64_t SyncManager::syncValueToInt(XSyncValue value) {
    return static_cast<int64_t>(XSyncValueLow32(value)) |
           (static_cast<int64_t>(XSyncValueHigh32(value)) << 32);
}

} // namespace pblank
