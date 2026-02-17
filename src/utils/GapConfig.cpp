/**
 * @file GapConfig.cpp
 * @brief Implementation of configurable gap system
 * 
 * @author Point Blank Systems Engineering Team
 * @version 1.0.0
 */

#include "pointblank/utils/GapConfig.hpp"
#include <algorithm>

namespace pblank {

int calculateEdgeGap(const GapConfig& config, bool touches_edge, bool has_adjacent) {
    int gap = 0;
    
    // Outer gap applies if touching screen edge
    if (touches_edge) {
        gap += config.outer_gap;
    }
    
    // Inner gap applies if there's an adjacent window (half on each side)
    if (has_adjacent) {
        gap += config.inner_gap / 2;
    }
    
    return gap;
}

GapRect applyGaps(const GapRect& rect, const GapConfig& config,
                  bool is_single_window,
                  bool touches_top, bool touches_bottom,
                  bool touches_left, bool touches_right,
                  bool has_adjacent_top,
                  bool has_adjacent_bottom,
                  bool has_adjacent_left,
                  bool has_adjacent_right) {
    GapRect result = rect;
    
    // Calculate gap for each edge
    int top_gap = 0;
    int bottom_gap = 0;
    int left_gap = 0;
    int right_gap = 0;
    
    if (is_single_window) {
        // Single window: use outer gaps on all edges
        top_gap = config.getTopGap();
        bottom_gap = config.getBottomGap();
        left_gap = config.getLeftGap();
        right_gap = config.getRightGap();
    } else {
        // Multiple windows: combine outer and inner gaps
        
        // Top edge
        if (touches_top) {
            top_gap = config.getTopGap();
        }
        if (has_adjacent_top) {
            top_gap += config.inner_gap / 2;
        }
        
        // Bottom edge
        if (touches_bottom) {
            bottom_gap = config.getBottomGap();
        }
        if (has_adjacent_bottom) {
            bottom_gap += config.inner_gap / 2;
        }
        
        // Left edge
        if (touches_left) {
            left_gap = config.getLeftGap();
        }
        if (has_adjacent_left) {
            left_gap += config.inner_gap / 2;
        }
        
        // Right edge
        if (touches_right) {
            right_gap = config.getRightGap();
        }
        if (has_adjacent_right) {
            right_gap += config.inner_gap / 2;
        }
    }
    
    // Apply gaps
    result.shrink(left_gap, top_gap, right_gap, bottom_gap);
    
    return result;
}

GapRect applyOuterGaps(const GapRect& rect, const GapConfig& config,
                       bool touches_top, bool touches_bottom,
                       bool touches_left, bool touches_right) {
    GapRect result = rect;
    
    int top_gap = touches_top ? config.getTopGap() : 0;
    int bottom_gap = touches_bottom ? config.getBottomGap() : 0;
    int left_gap = touches_left ? config.getLeftGap() : 0;
    int right_gap = touches_right ? config.getRightGap() : 0;
    
    result.shrink(left_gap, top_gap, right_gap, bottom_gap);
    
    return result;
}

GapRect applyInnerGaps(const GapRect& rect, const GapConfig& config,
                       bool has_adjacent_top, bool has_adjacent_bottom,
                       bool has_adjacent_left, bool has_adjacent_right) {
    GapRect result = rect;
    
    // Inner gap is split between adjacent windows
    int top_gap = has_adjacent_top ? config.inner_gap / 2 : 0;
    int bottom_gap = has_adjacent_bottom ? config.inner_gap / 2 : 0;
    int left_gap = has_adjacent_left ? config.inner_gap / 2 : 0;
    int right_gap = has_adjacent_right ? config.inner_gap / 2 : 0;
    
    result.shrink(left_gap, top_gap, right_gap, bottom_gap);
    
    return result;
}

} // namespace pblank