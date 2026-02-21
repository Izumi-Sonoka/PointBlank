#pragma once

/**
 * @file GapConfig.hpp
 * @brief Configurable gap system for layout
 * 
 * Implements configurable gaps with outer/inner distinction and
 * directional overrides for the Pointblank window manager.
 * 
 * Supports both numeric gap values and string commands for
 * cryptographic easter egg activation.
 * 
 * @author Point Blank Systems Engineering Team
 * @version 1.0.0
 */

#include <string>

namespace pblank {

struct GapRect {
    int x, y, width, height;
    
    GapRect() : x(0), y(0), width(0), height(0) {}
    GapRect(int x_, int y_, int w_, int h_) : x(x_), y(y_), width(w_), height(h_) {}
    
    void shrink(int left, int top, int right, int bottom) {
        x += left;
        y += top;
        width -= left + right;
        height -= top + bottom;
    }
};

struct GapConfig {
    int outer_gap;      
    int inner_gap;      
    
    std::string outer_gap_str;         
    std::string inner_gap_str;         
    
    int top_gap;       
    int bottom_gap;      
    int left_gap;       
    int right_gap;      
    
    static constexpr int DEFAULT_INNER = 4;
    static constexpr int DEFAULT_OUTER = 2;
    
    GapConfig() : outer_gap(DEFAULT_OUTER), inner_gap(DEFAULT_INNER),
                  top_gap(-1), bottom_gap(-1), left_gap(-1), right_gap(-1) {}
    
    bool innerGapIsSet() const { return inner_gap != DEFAULT_INNER || !inner_gap_str.empty(); }
    
    bool outerGapIsSet() const { return outer_gap != DEFAULT_OUTER || !outer_gap_str.empty(); }
    
    bool innerGapStrIsSet() const { return !inner_gap_str.empty(); }
    
    bool outerGapStrIsSet() const { return !outer_gap_str.empty(); }
    
    std::string getInnerGapStr() const { return inner_gap_str.empty() ? std::to_string(inner_gap) : inner_gap_str; }
    
    std::string getOuterGapStr() const { return outer_gap_str.empty() ? std::to_string(outer_gap) : outer_gap_str; }
    
    int getInnerGap() const { return inner_gap; }
    
    int getOuterGap() const { return outer_gap; }
    
    int getLeftGap() const { return left_gap >= 0 ? left_gap : outer_gap; }
    
    int getRightGap() const { return right_gap >= 0 ? right_gap : outer_gap; }
    
    int getTopGap() const { return top_gap >= 0 ? top_gap : outer_gap; }
    
    int getBottomGap() const { return bottom_gap >= 0 ? bottom_gap : outer_gap; }
};

} 
