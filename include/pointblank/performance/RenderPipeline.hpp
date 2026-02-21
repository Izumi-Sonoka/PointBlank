#pragma once

/**
 * @file RenderPipeline.hpp
 * @brief Zero-Overhead Render Pipeline with Cache Locality Optimization
 * 
 * Implements a high-performance rendering pipeline optimized for:
 * - Sub-millisecond frame times
 * - Minimal cache misses through data-oriented design
 * - Batch rendering for reduced draw calls
 * - Dirty rectangle tracking for partial updates
 * 
 * @author Point Blank Systems Engineering Team
 * @version 2.0.0
 */

#include "pointblank/performance/LockFreeStructures.hpp"
#include "pointblank/performance/PerformanceTuner.hpp"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>
#include <vector>
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <unordered_map>

namespace pblank {

class WindowManager;
class LayoutEngine;

struct DirtyRect {
    int16_t x, y;
    uint16_t width, height;
    uint32_t generation;  
    
    bool intersects(const DirtyRect& other) const {
        return x < other.x + other.width &&
               x + width > other.x &&
               y < other.y + other.height &&
               y + height > other.y;
    }
    
    void merge(const DirtyRect& other) {
        int16_t new_x = std::min(x, other.x);
        int16_t new_y = std::min(y, other.y);
        uint16_t new_right = std::max(x + width, other.x + other.width);
        uint16_t new_bottom = std::max(y + height, other.y + other.height);
        
        x = new_x;
        y = new_y;
        width = new_right - new_x;
        height = new_bottom - new_y;
    }
    
    int area() const { return width * height; }
};

struct alignas(64) WindowRenderData {
    Window window;
    int16_t x, y;
    uint16_t width, height;
    uint16_t border_width;
    uint32_t border_color;
    uint32_t flags;
    float opacity;
    
    static constexpr uint32_t FLAG_VISIBLE    = 1 << 0;
    static constexpr uint32_t FLAG_FOCUSED    = 1 << 1;
    static constexpr uint32_t FLAG_FULLSCREEN = 1 << 2;
    static constexpr uint32_t FLAG_FLOATING   = 1 << 3;
    static constexpr uint32_t FLAG_DIRTY      = 1 << 4;
};

enum class RenderCommandType : uint8_t {
    DrawBorder,
    FillRect,
    SetOpacity,
    MoveWindow,
    ResizeWindow,
    RaiseWindow,
    LowerWindow,
    FocusWindow
};

struct RenderCommand {
    RenderCommandType type;
    Window window;
    union {
        struct { int16_t x, y; uint16_t w, h; } rect;
        struct { uint32_t color; uint16_t width; } border;
        struct { float opacity; } opacity;
        struct { uint32_t flags; } flags;
    } data;
};

class RenderBatch {
    static constexpr size_t MAX_COMMANDS = 256;
    
    std::array<RenderCommand, MAX_COMMANDS> commands_;
    size_t count_{0};
    
public:
    void addCommand(const RenderCommand& cmd) {
        if (count_ < MAX_COMMANDS) {
            commands_[count_++] = cmd;
        }
    }
    
    void clear() { count_ = 0; }
    size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }
    bool full() const { return count_ >= MAX_COMMANDS; }
    
    const RenderCommand* begin() const { return commands_.data(); }
    const RenderCommand* end() const { return commands_.data() + count_; }
    const RenderCommand* data() const { return commands_.data(); }
};

template<typename T>
class DoubleBuffer {
    std::array<T, 2> buffers_;
    std::atomic<uint32_t> active_{0};
    
public:
    T& front() { return buffers_[active_.load(std::memory_order_acquire)]; }
    T& back() { return buffers_[1 - active_.load(std::memory_order_acquire)]; }
    
    void swap() {
        active_.store(1 - active_.load(std::memory_order_acquire), 
                      std::memory_order_release);
    }
    
    const T& front() const { 
        return buffers_[active_.load(std::memory_order_acquire)]; 
    }
};

class RenderPipeline {
public:
    using RenderCallback = std::function<void(Display*, const RenderBatch&)>;
    
    RenderPipeline(Display* display, Window root);
    
    ~RenderPipeline();
    
    RenderPipeline(const RenderPipeline&) = delete;
    RenderPipeline& operator=(const RenderPipeline&) = delete;
    
    void setPerformanceTuner(PerformanceTuner* tuner) { tuner_ = tuner; }
    
    void configure(bool dirty_rectangles_only, bool double_buffer);
    
    std::chrono::steady_clock::time_point beginFrame();
    
    void endFrame();
    
    bool isFrameInProgress() const { return frame_in_progress_; }
    
    void updateWindow(const WindowRenderData& data);
    
    void removeWindow(Window window);
    
    void markDirty(Window window);
    
    void markDirtyRect(const DirtyRect& rect);
    
    void clearDirtyRegions();
    
    void drawBorder(Window window, uint32_t color, uint16_t width);
    
    void moveWindow(Window window, int16_t x, int16_t y);
    
    void resizeWindow(Window window, uint16_t width, uint16_t height);
    
    void setWindowOpacity(Window window, float opacity);
    
    void focusWindow(Window window, bool focused);
    
    void raiseWindow(Window window);
    
    void lowerWindow(Window window);
    
    void flush();
    
    void flushDirty();
    
    RenderBatch& getCurrentBatch() { return current_batch_; }
    
    struct Stats {
        uint64_t frames_rendered;
        uint64_t commands_processed;
        uint64_t dirty_rectangles_processed;
        uint64_t total_render_time_ns;
        uint64_t avg_frame_time_ns;
    };
    Stats getStats() const;
    
    void resetStats();
    
private:
    
    Display* display_;
    Window root_;
    int screen_;
    
    PerformanceTuner* tuner_{nullptr};
    
    bool dirty_rectangles_only_{true};
    bool double_buffer_enabled_{true};
    
    std::atomic<bool> frame_in_progress_{false};
    std::chrono::steady_clock::time_point frame_start_;
    
    static constexpr size_t MAX_WINDOWS = 256;
    std::array<WindowRenderData, MAX_WINDOWS> window_data_;
    std::atomic<uint32_t> window_count_{0};
    
    std::unordered_map<Window, uint16_t> window_index_;
    
    Atom opacity_atom_{None};
    
    static constexpr size_t MAX_DIRTY_RECTS = 32;
    std::array<DirtyRect, MAX_DIRTY_RECTS> dirty_rects_;
    std::atomic<uint32_t> dirty_count_{0};
    uint32_t generation_{0};
    
    DoubleBuffer<RenderBatch> batches_;
    RenderBatch current_batch_;
    
    std::atomic<uint64_t> frames_rendered_{0};
    std::atomic<uint64_t> commands_processed_{0};
    std::atomic<uint64_t> dirty_rects_processed_{0};
    std::atomic<uint64_t> total_render_time_ns_{0};
    
    GC gc_{nullptr};
    XRenderPictFormat* pict_format_{nullptr};
    
    void executeBatch(const RenderBatch& batch);
    void executeCommand(const RenderCommand& cmd);
    void coalesceDirtyRects();
    bool isInDirtyRegion(int16_t x, int16_t y, uint16_t w, uint16_t h) const;
    WindowRenderData* findWindowData(Window window);
    uint16_t findWindowIndex(Window window) const;
};

inline std::chrono::steady_clock::time_point RenderPipeline::beginFrame() {
    frame_start_ = std::chrono::steady_clock::now();
    frame_in_progress_.store(true, std::memory_order_release);
    return frame_start_;
}

inline void RenderPipeline::endFrame() {
    
    if (double_buffer_enabled_) {
        batches_.swap();
    }
    
    flush();
    
    auto frame_end = std::chrono::steady_clock::now();
    auto frame_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
        frame_end - frame_start_);
    
    total_render_time_ns_.fetch_add(frame_time.count(), std::memory_order_relaxed);
    frames_rendered_.fetch_add(1, std::memory_order_relaxed);
    
    frame_in_progress_.store(false, std::memory_order_release);
}

inline void RenderPipeline::drawBorder(Window window, uint32_t color, uint16_t width) {
    RenderCommand cmd;
    cmd.type = RenderCommandType::DrawBorder;
    cmd.window = window;
    cmd.data.border.color = color;
    cmd.data.border.width = width;
    current_batch_.addCommand(cmd);
}

inline void RenderPipeline::moveWindow(Window window, int16_t x, int16_t y) {
    RenderCommand cmd;
    cmd.type = RenderCommandType::MoveWindow;
    cmd.window = window;
    cmd.data.rect.x = x;
    cmd.data.rect.y = y;
    current_batch_.addCommand(cmd);
}

inline void RenderPipeline::resizeWindow(Window window, uint16_t width, uint16_t height) {
    RenderCommand cmd;
    cmd.type = RenderCommandType::ResizeWindow;
    cmd.window = window;
    cmd.data.rect.w = width;
    cmd.data.rect.h = height;
    current_batch_.addCommand(cmd);
}

inline void RenderPipeline::setWindowOpacity(Window window, float opacity) {
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetOpacity;
    cmd.window = window;
    cmd.data.opacity.opacity = opacity;
    current_batch_.addCommand(cmd);
}

inline void RenderPipeline::focusWindow(Window window, bool focused) {
    RenderCommand cmd;
    cmd.type = RenderCommandType::FocusWindow;
    cmd.window = window;
    cmd.data.flags.flags = focused ? WindowRenderData::FLAG_FOCUSED : 0;
    current_batch_.addCommand(cmd);
}

inline void RenderPipeline::raiseWindow(Window window) {
    RenderCommand cmd;
    cmd.type = RenderCommandType::RaiseWindow;
    cmd.window = window;
    current_batch_.addCommand(cmd);
}

inline void RenderPipeline::lowerWindow(Window window) {
    RenderCommand cmd;
    cmd.type = RenderCommandType::LowerWindow;
    cmd.window = window;
    current_batch_.addCommand(cmd);
}

inline void RenderPipeline::markDirty(Window window) {
    WindowRenderData* data = findWindowData(window);
    if (data) {
        data->flags |= WindowRenderData::FLAG_DIRTY;
        markDirtyRect({data->x, data->y, data->width, data->height, generation_});
    }
}

inline void RenderPipeline::markDirtyRect(const DirtyRect& rect) {
    uint32_t idx = dirty_count_.load(std::memory_order_relaxed);
    if (idx < MAX_DIRTY_RECTS) {
        dirty_rects_[idx] = rect;
        dirty_count_.store(idx + 1, std::memory_order_release);
    }
}

inline void RenderPipeline::clearDirtyRegions() {
    dirty_count_.store(0, std::memory_order_release);
    ++generation_;
}

inline WindowRenderData* RenderPipeline::findWindowData(Window window) {
    uint16_t idx = findWindowIndex(window);
    return idx < MAX_WINDOWS ? &window_data_[idx] : nullptr;
}

inline uint16_t RenderPipeline::findWindowIndex(Window window) const {
    auto it = window_index_.find(window);
    return it != window_index_.end() ? it->second : 0xFFFF;
}

inline RenderPipeline::Stats RenderPipeline::getStats() const {
    Stats stats;
    stats.frames_rendered = frames_rendered_.load(std::memory_order_relaxed);
    stats.commands_processed = commands_processed_.load(std::memory_order_relaxed);
    stats.dirty_rectangles_processed = dirty_rects_processed_.load(std::memory_order_relaxed);
    stats.total_render_time_ns = total_render_time_ns_.load(std::memory_order_relaxed);
    
    if (stats.frames_rendered > 0) {
        stats.avg_frame_time_ns = stats.total_render_time_ns / stats.frames_rendered;
    } else {
        stats.avg_frame_time_ns = 0;
    }
    
    return stats;
}

inline void RenderPipeline::resetStats() {
    frames_rendered_.store(0, std::memory_order_relaxed);
    commands_processed_.store(0, std::memory_order_relaxed);
    dirty_rects_processed_.store(0, std::memory_order_relaxed);
    total_render_time_ns_.store(0, std::memory_order_relaxed);
}

} 
