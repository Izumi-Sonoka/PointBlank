/**
 * @file RenderPipeline.cpp
 * @brief Implementation of the Zero-Overhead Render Pipeline
 * 
 * @author Point Blank Systems Engineering Team
 * @version 2.0.0
 */

#include "pointblank/performance/RenderPipeline.hpp"

#include <algorithm>
#include <cstring>
#include <X11/Xatom.h>

namespace pblank {





RenderPipeline::RenderPipeline(Display* display, Window root)
    : display_(display)
    , root_(root)
    , screen_(DefaultScreen(display))
{
    
    
    
    XGCValues gc_values;
    gc_values.graphics_exposures = False;
    gc_ = XCreateGC(display, root, GCGraphicsExposures, &gc_values);
    
    
    pict_format_ = XRenderFindVisualFormat(display, DefaultVisual(display, screen_));
    
    
    for (auto& wd : window_data_) {
        wd.window = None;
        wd.flags = 0;
    }
}

RenderPipeline::~RenderPipeline() {
    if (gc_) {
        XFreeGC(display_, gc_);
    }
}





void RenderPipeline::configure(bool dirty_rectangles_only, bool double_buffer) {
    dirty_rectangles_only_ = dirty_rectangles_only;
    double_buffer_enabled_ = double_buffer;
}





void RenderPipeline::updateWindow(const WindowRenderData& data) {
    
    uint16_t idx = findWindowIndex(data.window);
    
    if (idx == 0xFFFF) {
        
        uint32_t count = window_count_.load(std::memory_order_relaxed);
        
        if (count >= MAX_WINDOWS) {
            return;  
        }
        
        
        for (idx = 0; idx < MAX_WINDOWS; ++idx) {
            if (window_data_[idx].window == None) {
                break;
            }
        }
        
        if (idx >= MAX_WINDOWS) {
            return;  
        }
        
        window_count_.fetch_add(1, std::memory_order_relaxed);
        window_index_[data.window] = idx;
    }
    
    
    window_data_[idx] = data;
    window_data_[idx].flags |= WindowRenderData::FLAG_DIRTY;
    
    
    markDirtyRect({data.x, data.y, data.width, data.height, generation_});
}

void RenderPipeline::removeWindow(Window window) {
    uint16_t idx = findWindowIndex(window);
    
    if (idx != 0xFFFF && window_data_[idx].window == window) {
        
        const auto& data = window_data_[idx];
        markDirtyRect({data.x, data.y, data.width, data.height, generation_});
        
        
        window_data_[idx].window = None;
        window_data_[idx].flags = 0;
        window_index_.erase(window);  
        
        window_count_.fetch_sub(1, std::memory_order_relaxed);
    }
}





void RenderPipeline::flush() {
    
    if (dirty_rectangles_only_) {
        coalesceDirtyRects();
    }
    
    
    executeBatch(current_batch_);
    
    
    commands_processed_.fetch_add(current_batch_.size(), std::memory_order_relaxed);
    dirty_rects_processed_.fetch_add(dirty_count_.load(std::memory_order_relaxed), 
                                     std::memory_order_relaxed);
    
    
    current_batch_.clear();
    
    
    for (uint32_t i = 0; i < window_count_.load(std::memory_order_relaxed); ++i) {
        window_data_[i].flags &= ~WindowRenderData::FLAG_DIRTY;
    }
}

void RenderPipeline::flushDirty() {
    if (dirty_count_.load(std::memory_order_relaxed) == 0) {
        return;
    }
    
    coalesceDirtyRects();
    
    
    RenderBatch dirty_batch;
    
    for (const auto& cmd : current_batch_) {
        
        if (cmd.type == RenderCommandType::MoveWindow ||
            cmd.type == RenderCommandType::ResizeWindow ||
            cmd.type == RenderCommandType::DrawBorder) {
            
            WindowRenderData* data = findWindowData(cmd.window);
            if (data && (data->flags & WindowRenderData::FLAG_DIRTY)) {
                dirty_batch.addCommand(cmd);
                continue;
            }
        }
        
        
        dirty_batch.addCommand(cmd);
    }
    
    executeBatch(dirty_batch);
    commands_processed_.fetch_add(dirty_batch.size(), std::memory_order_relaxed);
    
    current_batch_.clear();
}

void RenderPipeline::executeBatch(const RenderBatch& batch) {
    
    
    
    
    for (const auto& cmd : batch) {
        if (cmd.type == RenderCommandType::MoveWindow ||
            cmd.type == RenderCommandType::ResizeWindow) {
            executeCommand(cmd);
        }
    }
    
    
    XFlush(display_);
    
    
    for (const auto& cmd : batch) {
        if (cmd.type == RenderCommandType::DrawBorder ||
            cmd.type == RenderCommandType::SetOpacity) {
            executeCommand(cmd);
        }
    }
    
    
    for (const auto& cmd : batch) {
        if (cmd.type == RenderCommandType::RaiseWindow ||
            cmd.type == RenderCommandType::LowerWindow) {
            executeCommand(cmd);
        }
    }
    
    
    for (const auto& cmd : batch) {
        if (cmd.type == RenderCommandType::FocusWindow) {
            executeCommand(cmd);
        }
    }
    
    
    XFlush(display_);
}

void RenderPipeline::executeCommand(const RenderCommand& cmd) {
    switch (cmd.type) {
        case RenderCommandType::MoveWindow: {
            WindowRenderData* data = findWindowData(cmd.window);
            if (data) {
                XMoveWindow(display_, cmd.window, cmd.data.rect.x, cmd.data.rect.y);
                data->x = cmd.data.rect.x;
                data->y = cmd.data.rect.y;
            }
            break;
        }
        
        case RenderCommandType::ResizeWindow: {
            WindowRenderData* data = findWindowData(cmd.window);
            if (data) {
                XResizeWindow(display_, cmd.window, cmd.data.rect.w, cmd.data.rect.h);
                data->width = cmd.data.rect.w;
                data->height = cmd.data.rect.h;
            }
            break;
        }
        
        case RenderCommandType::DrawBorder: {
            WindowRenderData* data = findWindowData(cmd.window);
            if (data) {
                
                XSetWindowBorder(display_, cmd.window, cmd.data.border.color);
                XSetWindowBorderWidth(display_, cmd.window, cmd.data.border.width);
                data->border_color = cmd.data.border.color;
                data->border_width = cmd.data.border.width;
            }
            break;
        }
        
        case RenderCommandType::SetOpacity: {
            
            Atom opacity_atom = XInternAtom(display_, "_NET_WM_WINDOW_OPACITY", False);
            uint32_t opacity = static_cast<uint32_t>(cmd.data.opacity.opacity * 0xFFFFFFFF);
            XChangeProperty(display_, cmd.window, opacity_atom, XA_CARDINAL, 32,
                           PropModeReplace, 
                           reinterpret_cast<unsigned char*>(&opacity), 1);
            
            WindowRenderData* data = findWindowData(cmd.window);
            if (data) {
                data->opacity = cmd.data.opacity.opacity;
            }
            break;
        }
        
        case RenderCommandType::RaiseWindow: {
            XRaiseWindow(display_, cmd.window);
            break;
        }
        
        case RenderCommandType::LowerWindow: {
            XLowerWindow(display_, cmd.window);
            break;
        }
        
        case RenderCommandType::FocusWindow: {
            if (cmd.data.flags.flags & WindowRenderData::FLAG_FOCUSED) {
                XSetInputFocus(display_, cmd.window, RevertToPointerRoot, CurrentTime);
                
                WindowRenderData* data = findWindowData(cmd.window);
                if (data) {
                    data->flags |= WindowRenderData::FLAG_FOCUSED;
                }
            }
            break;
        }
        
        default:
            break;
    }
}

void RenderPipeline::coalesceDirtyRects() {
    uint32_t count = dirty_count_.load(std::memory_order_relaxed);
    
    if (count <= 1) {
        return;
    }
    
    
    bool changed = true;
    while (changed) {
        changed = false;
        
        for (uint32_t i = 0; i < count; ++i) {
            for (uint32_t j = i + 1; j < count; ++j) {
                if (dirty_rects_[i].intersects(dirty_rects_[j])) {
                    dirty_rects_[i].merge(dirty_rects_[j]);
                    
                    
                    dirty_rects_[j] = dirty_rects_[count - 1];
                    --count;
                    
                    changed = true;
                    break;
                }
            }
            if (changed) break;
        }
    }
    
    dirty_count_.store(count, std::memory_order_release);
}

bool RenderPipeline::isInDirtyRegion(int16_t x, int16_t y, uint16_t w, uint16_t h) const {
    uint32_t count = dirty_count_.load(std::memory_order_acquire);
    
    for (uint32_t i = 0; i < count; ++i) {
        const auto& rect = dirty_rects_[i];
        
        if (x < rect.x + rect.width &&
            x + w > rect.x &&
            y < rect.y + rect.height &&
            y + h > rect.y) {
            return true;
        }
    }
    
    return false;
}

} 
