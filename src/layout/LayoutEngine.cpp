#include "pointblank/layout/LayoutEngine.hpp"
#include "pointblank/utils/Camera.hpp"
#include "pointblank/utils/SpatialGrid.hpp"
#include "pointblank/utils/GapConfig.hpp"
#include <algorithm>
#include <iostream>
#include <cmath>
#include <limits>

namespace pblank {

// ============================================================================
// Rect Implementation
// ============================================================================

Rect Rect::subRect(bool is_left, SplitType split, double ratio) const {
    if (split == SplitType::Vertical) {
        // Vertical split: divide width (left/right)
        if (is_left) {
            return {x, y, static_cast<unsigned int>(width * ratio), height};
        } else {
            unsigned int left_w = static_cast<unsigned int>(width * ratio);
            return {x + static_cast<int>(left_w), y, width - left_w, height};
        }
    } else {
        // Horizontal split: divide height (top/bottom)
        if (is_left) {
            return {x, y, width, static_cast<unsigned int>(height * ratio)};
        } else {
            unsigned int top_h = static_cast<unsigned int>(height * ratio);
            return {x, y + static_cast<int>(top_h), width, height - top_h};
        }
    }
}

int Rect::distanceTo(const Rect& other, const std::string& direction) const {
    if (direction == "left") {
        if (isLeftOf(other)) {
            return other.left() - right();
        }
        return std::numeric_limits<int>::max();
    } else if (direction == "right") {
        if (isRightOf(other)) {
            return left() - other.right();
        }
        return std::numeric_limits<int>::max();
    } else if (direction == "up") {
        if (isAbove(other)) {
            return other.top() - bottom();
        }
        return std::numeric_limits<int>::max();
    } else if (direction == "down") {
        if (isBelow(other)) {
            return top() - other.bottom();
        }
        return std::numeric_limits<int>::max();
    }
    return std::numeric_limits<int>::max();
}

// ============================================================================
// BSPNode Implementation
// ============================================================================

BSPNode::BSPNode(Window window) : window_(window), focused_(false) {}

BSPNode::BSPNode(std::unique_ptr<BSPNode> left, std::unique_ptr<BSPNode> right,
                 SplitType split, double ratio)
    : window_(None), split_type_(split), ratio_(ratio), focused_(false) {
    
    left_ = std::move(left);
    right_ = std::move(right);
    
    if (left_) left_->setParent(this);
    if (right_) right_->setParent(this);
}

BSPNode* BSPNode::getSibling() const {
    if (!parent_) return nullptr;
    return (parent_->left_.get() == this) ? parent_->right_.get() : parent_->left_.get();
}

BSPNode* BSPNode::findWindow(Window win) {
    if (isLeaf()) {
        return (window_ == win) ? this : nullptr;
    }
    
    if (left_) {
        if (auto* found = left_->findWindow(win)) return found;
    }
    if (right_) {
        if (auto* found = right_->findWindow(win)) return found;
    }
    return nullptr;
}

BSPNode* BSPNode::findFocused() {
    if (isLeaf()) {
        return focused_ ? this : nullptr;
    }
    
    if (left_) {
        if (auto* found = left_->findFocused()) return found;
    }
    if (right_) {
        if (auto* found = right_->findFocused()) return found;
    }
    return nullptr;
}

BSPNode* BSPNode::findFirstLeaf() {
    if (isLeaf()) return this;
    if (left_) return left_->findFirstLeaf();
    if (right_) return right_->findFirstLeaf();
    return nullptr;
}

BSPNode* BSPNode::findLastLeaf() {
    if (isLeaf()) return this;
    if (right_) return right_->findLastLeaf();
    if (left_) return left_->findLastLeaf();
    return nullptr;
}

int BSPNode::countWindows() const {
    if (isLeaf()) return 1;
    int count = 0;
    if (left_) count += left_->countWindows();
    if (right_) count += right_->countWindows();
    return count;
}

void BSPNode::collectWindows(std::vector<Window>& windows) const {
    if (isLeaf()) {
        windows.push_back(window_);
        return;
    }
    // Reserve estimated space to reduce reallocations
    int est_count = countWindows();
    if (windows.capacity() < windows.size() + est_count) {
        windows.reserve(windows.size() + est_count);
    }
    if (left_) left_->collectWindows(windows);
    if (right_) right_->collectWindows(windows);
}

void BSPNode::collectLeavesWithBounds(std::vector<std::pair<BSPNode*, Rect>>& leaves, 
                                       const Rect& bounds) const {
    if (isLeaf()) {
        leaves.emplace_back(const_cast<BSPNode*>(this), bounds);
        return;
    }
    
    Rect left_bounds = bounds.subRect(true, split_type_, ratio_);
    Rect right_bounds = bounds.subRect(false, split_type_, ratio_);
    
    if (left_) left_->collectLeavesWithBounds(leaves, left_bounds);
    if (right_) right_->collectLeavesWithBounds(leaves, right_bounds);
}

BSPNode* BSPNode::findByWindow(BSPNode* root, Window win) {
    if (!root) return nullptr;
    return root->findWindow(win);
}

BSPNode* BSPNode::findFocusedInTree(BSPNode* root) {
    if (!root) return nullptr;
    return root->findFocused();
}

// ============================================================================
// BSPLayout Implementation
// ============================================================================

void BSPLayout::visit(BSPNode* root, const Rect& bounds, Display* display) {
    if (!root) return;
    
    // Find the focused node for border color rendering
    BSPNode* focused_node = BSPNode::findFocusedInTree(root);
    
    // Count windows for smart gaps
    int window_count = root->countWindows();
    
    // Determine effective gap values from GapConfig if available
    int effective_inner_gap = config_.gap_size;
    int effective_outer_gap = config_.gap_size;
    bool use_smart_gaps = config_.smart_gaps;
    
    // Override with GapConfig values if available
    if (gap_config_) {
        effective_inner_gap = gap_config_->inner_gap;
        effective_outer_gap = gap_config_->outer_gap;
    }
    
    // Smart gaps: disable INNER gaps only when only one window
    // Outer gaps (screen edges) should ALWAYS be applied regardless of window count
    if (use_smart_gaps && window_count == 1) {
        effective_inner_gap = 0;
        // Note: We do NOT set effective_outer_gap to 0 here - outer gaps always apply
    }
    
    // Apply padding to create screen margins
    unsigned int adjusted_width = bounds.width;
    unsigned int adjusted_height = bounds.height;
    
    if (bounds.width > static_cast<unsigned int>(2 * config_.padding)) {
        adjusted_width = bounds.width - 2 * config_.padding;
    }
    if (bounds.height > static_cast<unsigned int>(2 * config_.padding)) {
        adjusted_height = bounds.height - 2 * config_.padding;
    }
    
    Rect adjusted = {
        bounds.x + config_.padding,
        bounds.y + config_.padding,
        adjusted_width,
        adjusted_height
    };
    
    // Outer gaps (distance from screen edges) are ALWAYS applied
    // Smart gaps only affects inner gaps between windows, not outer gaps at screen edges
    if (gap_config_) {
        int left_gap = gap_config_->getLeftGap();
        int top_gap = gap_config_->getTopGap();
        int right_gap = gap_config_->getRightGap();
        int bottom_gap = gap_config_->getBottomGap();
        
        // Apply outer gaps
        adjusted.x += left_gap;
        adjusted.y += top_gap;
        if (adjusted.width > static_cast<unsigned int>(left_gap + right_gap)) {
            adjusted.width -= (left_gap + right_gap);
        }
        if (adjusted.height > static_cast<unsigned int>(top_gap + bottom_gap)) {
            adjusted.height -= (top_gap + bottom_gap);
        }
    } else {
        // Fallback if GapConfig not available
        adjusted.x += effective_outer_gap;
        adjusted.y += effective_outer_gap;
        if (adjusted.width > static_cast<unsigned int>(2 * effective_outer_gap)) {
            adjusted.width -= 2 * effective_outer_gap;
        }
        if (adjusted.height > static_cast<unsigned int>(2 * effective_outer_gap)) {
            adjusted.height -= 2 * effective_outer_gap;
        }
    }
    
    // Store effective gaps for use in visitNode
    effective_inner_gap_ = effective_inner_gap;
    
    visitNode(root, adjusted, display, focused_node, bounds, window_count);
}

void BSPLayout::visitNode(BSPNode* node, const Rect& bounds, Display* display, BSPNode* focused_node,
                          const Rect& screen_bounds, int window_count) {
    if (!node) return;
    
    if (node->isLeaf()) {
        // Apply geometry to window
        Window win = node->getWindow();
        if (win != None && display) {
            // Calculate window geometry with gap offset
            int x = bounds.x;
            int y = bounds.y;
            unsigned int w = bounds.width;
            unsigned int h = bounds.height;
            
            // Ensure minimum size
            if (w < 50) w = 50;
            if (h < 50) h = 50;
            
            // Apply border width adjustment
            if (config_.border_width > 0) {
                if (w > static_cast<unsigned int>(2 * config_.border_width)) {
                    w -= 2 * config_.border_width;
                }
                if (h > static_cast<unsigned int>(2 * config_.border_width)) {
                    h -= 2 * config_.border_width;
                }
            }
            
            // Move and resize window
            XMoveResizeWindow(display, win, x, y, w, h);
            
            // Set border width
            XWindowChanges changes;
            changes.border_width = config_.border_width;
            XConfigureWindow(display, win, CWBorderWidth, &changes);
            
            // Set border color based on focus state
            unsigned long border_color = (node == focused_node) ? 
                config_.focused_border_color : config_.unfocused_border_color;
            
            XSetWindowBorder(display, win, border_color);
        }
    } else {
        // Container: split and recurse
        Rect left_bounds = bounds.subRect(true, node->getSplitType(), node->getRatio());
        Rect right_bounds = bounds.subRect(false, node->getSplitType(), node->getRatio());
        
        // Apply inner gap between splits using cached effective_inner_gap
        int split_gap = effective_inner_gap_;
        
        if (node->getSplitType() == SplitType::Vertical) {
            if (left_bounds.width > static_cast<unsigned int>(split_gap / 2)) {
                left_bounds.width -= split_gap / 2;
            }
            right_bounds.x += split_gap / 2;
            if (right_bounds.width > static_cast<unsigned int>(split_gap / 2)) {
                right_bounds.width -= split_gap / 2;
            }
        } else {
            if (left_bounds.height > static_cast<unsigned int>(split_gap / 2)) {
                left_bounds.height -= split_gap / 2;
            }
            right_bounds.y += split_gap / 2;
            if (right_bounds.height > static_cast<unsigned int>(split_gap / 2)) {
                right_bounds.height -= split_gap / 2;
            }
        }
        
        visitNode(node->getLeft(), left_bounds, display, focused_node, screen_bounds, window_count);
        visitNode(node->getRight(), right_bounds, display, focused_node, screen_bounds, window_count);
    }
}

// ============================================================================
// MonocleLayout Implementation
// ============================================================================

void MonocleLayout::visit(BSPNode* root, const Rect& bounds, Display* display) {
    if (!root || !display) return;
    
    // In monocle, only the focused window is shown fullscreen
    BSPNode* focused = BSPNode::findFocusedInTree(root);
    
    if (focused && focused->isLeaf()) {
        Window win = focused->getWindow();
        if (win != None) {
            XMoveResizeWindow(display, win, bounds.x, bounds.y, bounds.width, bounds.height);
        }
    }
    
    // Hide other windows (move them off-screen)
    std::vector<Window> all_windows;
    all_windows.reserve(root->countWindows());
    root->collectWindows(all_windows);
    
    for (Window win : all_windows) {
        if (!focused || win != focused->getWindow()) {
            XMoveWindow(display, win, -9999, -9999);
        }
    }
}

// ============================================================================
// MasterStackLayout Implementation
// ============================================================================

void MasterStackLayout::visit(BSPNode* root, const Rect& bounds, Display* display) {
    if (!root || !display) return;
    
    auto windows = collectWindows(root);
    if (windows.empty()) return;
    
    // Find the focused window in the tree
    BSPNode* focused_node = BSPNode::findFocusedInTree(root);
    Window focused_win = focused_node ? focused_node->getWindow() : None;
    
    // If no window is focused, default to the first window
    if (focused_win == None && !windows.empty()) {
        focused_win = windows[0];
    }
    
    if (windows.size() == 1) {
        // Single window takes full screen with border
        XMoveResizeWindow(display, windows[0], 
                         bounds.x, bounds.y, bounds.width, bounds.height);
        
        // Set border width and color
        XWindowChanges changes;
        changes.border_width = config_.border_width;
        XConfigureWindow(display, windows[0], CWBorderWidth, &changes);
        
        unsigned long color = (windows[0] == focused_win) ? 
            config_.focused_border_color : config_.unfocused_border_color;
        XSetWindowBorder(display, windows[0], color);
        return;
    }
    
    // Calculate master and stack areas
    int master_width = static_cast<int>(bounds.width * config_.master_ratio);
    int stack_width = bounds.width - master_width - config_.gap_size;
    
    size_t num_master = std::min(static_cast<size_t>(config_.max_master), windows.size());
    int master_height = bounds.height / static_cast<int>(num_master);
    
    // Position master windows with border colors
    for (size_t i = 0; i < num_master; ++i) {
        Window win = windows[i];
        XMoveResizeWindow(display, win,
                         bounds.x,
                         bounds.y + static_cast<int>(i) * master_height,
                         master_width - config_.gap_size / 2,
                         master_height - config_.gap_size);
        
        // Set border width and color
        XWindowChanges changes;
        changes.border_width = config_.border_width;
        XConfigureWindow(display, win, CWBorderWidth, &changes);
        
        unsigned long color = (win == focused_win) ? 
            config_.focused_border_color : config_.unfocused_border_color;
        XSetWindowBorder(display, win, color);
    }
    
    // Position stack windows with border colors
    if (windows.size() > num_master) {
        size_t num_stack = windows.size() - num_master;
        int stack_height = bounds.height / static_cast<int>(num_stack);
        
        for (size_t i = 0; i < num_stack; ++i) {
            Window win = windows[num_master + i];
            XMoveResizeWindow(display, win,
                             bounds.x + master_width + config_.gap_size,
                             bounds.y + static_cast<int>(i) * stack_height,
                             stack_width,
                             stack_height - config_.gap_size);
            
            // Set border width and color
            XWindowChanges changes;
            changes.border_width = config_.border_width;
            XConfigureWindow(display, win, CWBorderWidth, &changes);
            
            unsigned long color = (win == focused_win) ? 
                config_.focused_border_color : config_.unfocused_border_color;
            XSetWindowBorder(display, win, color);
        }
    }
}

std::vector<Window> MasterStackLayout::collectWindows(BSPNode* root) {
    std::vector<Window> windows;
    if (root) {
        windows.reserve(root->countWindows());
        root->collectWindows(windows);
    }
    return windows;
}

// ============================================================================
// CenteredMasterLayout Implementation
// ============================================================================

void CenteredMasterLayout::visit(BSPNode* root, const Rect& bounds, Display* display) {
    if (!root || !display) return;
    
    auto windows = collectWindows(root);
    if (windows.empty()) return;
    
    // Find focused window
    BSPNode* focused_node = BSPNode::findFocusedInTree(root);
    Window focused_win = focused_node ? focused_node->getWindow() : None;
    
    // If center_on_focus is enabled, move focused window to first position
    if (config_.center_on_focus && focused_win != None && windows.size() > 1) {
        auto it = std::find(windows.begin(), windows.end(), focused_win);
        if (it != windows.end() && it != windows.begin()) {
            std::rotate(windows.begin(), it, it + 1);
        }
    }
    
    // Single window takes full screen
    if (windows.size() == 1) {
        positionWindow(windows[0], bounds, display, windows[0] == focused_win);
        return;
    }
    
    // Calculate column widths
    int total_width = static_cast<int>(bounds.width);
    int center_width = static_cast<int>(total_width * config_.center_ratio);
    int side_width = (total_width - center_width) / 2;
    
    // Adjust for gaps
    int gap = config_.gap_size;
    center_width -= gap * 2;
    side_width -= gap;
    
    // Determine center window count
    size_t num_center = std::min(static_cast<size_t>(config_.max_center), windows.size());
    size_t remaining = windows.size() - num_center;
    size_t left_count = remaining / 2;
    size_t right_count = remaining - left_count;
    
    // Position left stack
    if (left_count > 0) {
        int left_height = static_cast<int>(bounds.height) / static_cast<int>(left_count);
        for (size_t i = 0; i < left_count; ++i) {
            Rect rect{
                bounds.x + gap / 2,
                bounds.y + static_cast<int>(i) * left_height,
                static_cast<unsigned int>(side_width),
                static_cast<unsigned int>(left_height - gap)
            };
            size_t win_idx = num_center + i;  // Windows after center go to left
            if (win_idx < windows.size()) {
                positionWindow(windows[win_idx], rect, display, windows[win_idx] == focused_win);
            }
        }
    }
    
    // Position center windows
    int center_height = static_cast<int>(bounds.height) / static_cast<int>(num_center);
    int center_x = bounds.x + side_width + gap;
    
    for (size_t i = 0; i < num_center; ++i) {
        Rect rect{
            center_x,
            bounds.y + static_cast<int>(i) * center_height,
            static_cast<unsigned int>(center_width),
            static_cast<unsigned int>(center_height - gap)
        };
        positionWindow(windows[i], rect, display, windows[i] == focused_win);
    }
    
    // Position right stack
    if (right_count > 0) {
        int right_height = static_cast<int>(bounds.height) / static_cast<int>(right_count);
        int right_x = center_x + center_width + gap;
        
        for (size_t i = 0; i < right_count; ++i) {
            Rect rect{
                right_x,
                bounds.y + static_cast<int>(i) * right_height,
                static_cast<unsigned int>(side_width),
                static_cast<unsigned int>(right_height - gap)
            };
            size_t win_idx = num_center + left_count + i;
            if (win_idx < windows.size()) {
                positionWindow(windows[win_idx], rect, display, windows[win_idx] == focused_win);
            }
        }
    }
}

std::vector<Window> CenteredMasterLayout::collectWindows(BSPNode* root) {
    std::vector<Window> windows;
    if (root) {
        windows.reserve(root->countWindows());
        root->collectWindows(windows);
    }
    return windows;
}

void CenteredMasterLayout::positionWindow(Window win, const Rect& rect, 
                                          Display* display, bool is_focused) {
    if (win == None || !display) return;
    
    // Ensure minimum size
    unsigned int w = rect.width;
    unsigned int h = rect.height;
    if (w < 50) w = 50;
    if (h < 50) h = 50;
    
    XMoveResizeWindow(display, win, rect.x, rect.y, w, h);
    
    // Set border
    XWindowChanges changes;
    changes.border_width = config_.border_width;
    XConfigureWindow(display, win, CWBorderWidth, &changes);
    
    unsigned long color = is_focused ? 
        config_.focused_border_color : config_.unfocused_border_color;
    XSetWindowBorder(display, win, color);
}

// ============================================================================
// DynamicGridLayout Implementation
// ============================================================================

void DynamicGridLayout::visit(BSPNode* root, const Rect& bounds, Display* display) {
    if (!root || !display) return;
    
    auto windows = collectWindows(root);
    if (windows.empty()) return;
    
    // Find focused window
    BSPNode* focused_node = BSPNode::findFocusedInTree(root);
    Window focused_win = focused_node ? focused_node->getWindow() : None;
    
    // Single window takes full screen
    if (windows.size() == 1) {
        positionWindow(windows[0], bounds, display, windows[0] == focused_win);
        return;
    }
    
    // Calculate grid dimensions
    auto [cols, rows] = calculateGridDimensions(static_cast<int>(windows.size()), bounds);
    
    // Calculate cell dimensions
    int gap = config_.gap_size;
    int cell_width = (static_cast<int>(bounds.width) - (cols - 1) * gap) / cols;
    int cell_height = (static_cast<int>(bounds.height) - (rows - 1) * gap) / rows;
    
    // Position windows in grid
    size_t idx = 0;
    for (int row = 0; row < rows && idx < windows.size(); ++row) {
        for (int col = 0; col < cols && idx < windows.size(); ++col) {
            Rect rect{
                bounds.x + col * (cell_width + gap),
                bounds.y + row * (cell_height + gap),
                static_cast<unsigned int>(cell_width),
                static_cast<unsigned int>(cell_height)
            };
            positionWindow(windows[idx], rect, display, windows[idx] == focused_win);
            ++idx;
        }
    }
}

std::pair<int, int> DynamicGridLayout::calculateGridDimensions(int count, const Rect& bounds) const {
    if (count <= 0) return {1, 1};
    if (count == 1) return {1, 1};
    
    // Calculate square root based dimensions
    double sqrt_count = std::sqrt(static_cast<double>(count));
    int base_dim = static_cast<int>(std::floor(sqrt_count));
    
    // Determine if we need more columns or rows
    int cols, rows;
    
    if (config_.prefer_horizontal) {
        // Prefer wider cells (more columns)
        cols = base_dim + 1;
        rows = base_dim;
        
        // Adjust if not enough cells
        while (cols * rows < count) {
            ++cols;
        }
    } else {
        // Prefer taller cells (more rows) - default
        cols = base_dim;
        rows = base_dim + 1;
        
        // Adjust if not enough cells
        while (cols * rows < count) {
            ++rows;
        }
    }
    
    // Check minimum cell size constraints
    int max_cols = static_cast<int>(bounds.width) / config_.min_cell_width;
    int max_rows = static_cast<int>(bounds.height) / config_.min_cell_height;
    
    cols = std::min(cols, std::max(1, max_cols));
    rows = std::min(rows, std::max(1, max_rows));
    
    // Ensure we have enough cells
    while (cols * rows < count) {
        if (cols <= rows) {
            ++cols;
        } else {
            ++rows;
        }
    }
    
    return {cols, rows};
}

std::vector<Window> DynamicGridLayout::collectWindows(BSPNode* root) {
    std::vector<Window> windows;
    if (root) {
        windows.reserve(root->countWindows());
        root->collectWindows(windows);
    }
    return windows;
}

void DynamicGridLayout::positionWindow(Window win, const Rect& rect, 
                                       Display* display, bool is_focused) {
    if (win == None || !display) return;
    
    unsigned int w = rect.width;
    unsigned int h = rect.height;
    if (w < 50) w = 50;
    if (h < 50) h = 50;
    
    XMoveResizeWindow(display, win, rect.x, rect.y, w, h);
    
    XWindowChanges changes;
    changes.border_width = config_.border_width;
    XConfigureWindow(display, win, CWBorderWidth, &changes);
    
    unsigned long color = is_focused ? 
        config_.focused_border_color : config_.unfocused_border_color;
    XSetWindowBorder(display, win, color);
}

// ============================================================================
// DwindleSpiralLayout Implementation
// ============================================================================

void DwindleSpiralLayout::visit(BSPNode* root, const Rect& bounds, Display* display) {
    if (!root || !display) return;
    
    auto windows = collectWindows(root);
    if (windows.empty()) return;
    
    // Find focused window
    BSPNode* focused_node = BSPNode::findFocusedInTree(root);
    Window focused_win = focused_node ? focused_node->getWindow() : None;
    
    // Single window takes full screen
    if (windows.size() == 1) {
        positionWindow(windows[0], bounds, display, windows[0] == focused_win);
        return;
    }
    
    // Calculate spiral bounds for all windows
    auto window_bounds = calculateSpiralBounds(static_cast<int>(windows.size()), bounds);
    
    // Position each window
    for (size_t i = 0; i < windows.size() && i < window_bounds.size(); ++i) {
        positionWindow(windows[i], window_bounds[i], display, windows[i] == focused_win);
    }
}

std::vector<Rect> DwindleSpiralLayout::calculateSpiralBounds(int count, const Rect& bounds) {
    std::vector<Rect> result;
    if (count <= 0) return result;
    
    result.reserve(count);
    
    // Start with full bounds
    Rect current = bounds;
    
    // Track split direction: true = vertical, false = horizontal
    bool vertical = true;
    
    // Track which side gets the next window (true = first half, false = second half)
    bool first_half = true;
    
    double ratio = config_.initial_ratio;
    int gap = config_.gap_size;
    
    for (int i = 0; i < count - 1; ++i) {
        // Calculate split
        if (vertical) {
            // Vertical split (left/right)
            int split_pos = static_cast<int>(current.width * ratio);
            
            if (first_half) {
                // First window gets left portion
                result.push_back(Rect{
                    current.x,
                    current.y,
                    static_cast<unsigned int>(split_pos - gap / 2),
                    current.height
                });
                
                // Remaining area is right portion
                current.x += split_pos + gap / 2;
                current.width = static_cast<unsigned int>(current.width - split_pos - gap / 2);
            } else {
                // First window gets right portion
                result.push_back(Rect{
                    current.x + static_cast<int>(current.width) - split_pos + gap / 2,
                    current.y,
                    static_cast<unsigned int>(split_pos - gap / 2),
                    current.height
                });
                
                // Remaining area is left portion
                current.width = static_cast<unsigned int>(current.width - split_pos - gap / 2);
            }
        } else {
            // Horizontal split (top/bottom)
            int split_pos = static_cast<int>(current.height * ratio);
            
            if (first_half) {
                // First window gets top portion
                result.push_back(Rect{
                    current.x,
                    current.y,
                    current.width,
                    static_cast<unsigned int>(split_pos - gap / 2)
                });
                
                // Remaining area is bottom portion
                current.y += split_pos + gap / 2;
                current.height = static_cast<unsigned int>(current.height - split_pos - gap / 2);
            } else {
                // First window gets bottom portion
                result.push_back(Rect{
                    current.x,
                    current.y + static_cast<int>(current.height) - split_pos + gap / 2,
                    current.width,
                    static_cast<unsigned int>(split_pos - gap / 2)
                });
                
                // Remaining area is top portion
                current.height = static_cast<unsigned int>(current.height - split_pos - gap / 2);
            }
        }
        
        // Toggle split direction for spiral effect
        vertical = !vertical;
        
        // Adjust ratio for Fibonacci-like progression
        ratio = ratio - config_.ratio_increment;
        ratio = std::max(0.2, std::min(0.8, ratio));
        
        // Alternate which side gets the window
        first_half = !first_half;
    }
    
    // Last window gets remaining space
    result.push_back(current);
    
    return result;
}

std::vector<Window> DwindleSpiralLayout::collectWindows(BSPNode* root) {
    std::vector<Window> windows;
    if (root) {
        windows.reserve(root->countWindows());
        root->collectWindows(windows);
    }
    return windows;
}

void DwindleSpiralLayout::positionWindow(Window win, const Rect& rect, 
                                         Display* display, bool is_focused) {
    if (win == None || !display) return;
    
    unsigned int w = rect.width;
    unsigned int h = rect.height;
    if (w < 50) w = 50;
    if (h < 50) h = 50;
    
    XMoveResizeWindow(display, win, rect.x, rect.y, w, h);
    
    XWindowChanges changes;
    changes.border_width = config_.border_width;
    XConfigureWindow(display, win, CWBorderWidth, &changes);
    
    unsigned long color = is_focused ? 
        config_.focused_border_color : config_.unfocused_border_color;
    XSetWindowBorder(display, win, color);
}

// ============================================================================
// TabbedStackedLayout Implementation
// ============================================================================

void TabbedStackedLayout::visit(BSPNode* root, const Rect& bounds, Display* display) {
    if (!root || !display) return;
    
    auto windows = collectWindows(root);
    if (windows.empty()) return;
    
    // Find focused window
    BSPNode* focused_node = BSPNode::findFocusedInTree(root);
    Window focused_win = focused_node ? focused_node->getWindow() : None;
    
    // Find focused index
    size_t focused_idx = 0;
    for (size_t i = 0; i < windows.size(); ++i) {
        if (windows[i] == focused_win) {
            focused_idx = i;
            break;
        }
    }
    
    // Calculate content area (excluding tab bar)
    Rect content_bounds = bounds;
    int tab_y_offset = config_.tab_height + config_.gap_size;
    
    if (config_.tab_position == TabPosition::Top) {
        content_bounds.y += tab_y_offset;
        content_bounds.height -= tab_y_offset;
    } else {
        content_bounds.height -= tab_y_offset;
    }
    
    // Render tab bar (this would typically be done by a separate UI component)
    // For now, we just manage window positions
    renderTabBar(windows, focused_idx, bounds, display);
    
    // Position windows
    if (config_.show_focused_only) {
        // Only show focused window
        if (focused_win != None) {
            positionWindow(focused_win, content_bounds, display, true);
        }
        
        // Hide other windows
        for (Window win : windows) {
            if (win != focused_win) {
                XMoveWindow(display, win, -9999, -9999);
            }
        }
    } else {
        // Stack all windows at same position (with tab bar for switching)
        for (size_t i = 0; i < windows.size(); ++i) {
            if (i == focused_idx) {
                positionWindow(windows[i], content_bounds, display, true);
            } else {
                // Move non-focused windows off-screen or behind
                XMoveWindow(display, windows[i], -9999, -9999);
            }
        }
    }
}

std::vector<Window> TabbedStackedLayout::collectWindows(BSPNode* root) {
    std::vector<Window> windows;
    if (root) {
        windows.reserve(root->countWindows());
        root->collectWindows(windows);
    }
    return windows;
}

void TabbedStackedLayout::positionWindow(Window win, const Rect& rect, 
                                         Display* display, bool is_focused) {
    if (win == None || !display) return;
    
    unsigned int w = rect.width;
    unsigned int h = rect.height;
    if (w < 50) w = 50;
    if (h < 50) h = 50;
    
    XMoveResizeWindow(display, win, rect.x, rect.y, w, h);
    
    XWindowChanges changes;
    changes.border_width = 0;  // No border in tabbed mode
    XConfigureWindow(display, win, CWBorderWidth, &changes);
    
    // Raise focused window to top
    if (is_focused) {
        XRaiseWindow(display, win);
    }
}

void TabbedStackedLayout::renderTabBar(const std::vector<Window>& windows, 
                                       size_t focused_idx,
                                       const Rect& bounds, 
                                       Display* display) {
    // Tab bar rendering would typically be done by a separate UI component
    // or using X11 drawing primitives. For now, we just ensure proper
    // window stacking order.
    
    // Stack windows in reverse order so focused is on top
    for (size_t i = 0; i < windows.size(); ++i) {
        if (i != focused_idx) {
            XLowerWindow(display, windows[i]);
        }
    }
    
    if (focused_idx < windows.size()) {
        XRaiseWindow(display, windows[focused_idx]);
    }
}

// ============================================================================
// InfiniteCanvasLayout Implementation
// ============================================================================

void InfiniteCanvasLayout::visit(BSPNode* root, const Rect& bounds, Display* display) {
    if (!root || !display) return;
    
    auto windows = collectWindows(root);
    if (windows.empty()) return;
    
    // Find focused window
    BSPNode* focused_node = BSPNode::findFocusedInTree(root);
    Window focused_win = focused_node ? focused_node->getWindow() : None;
    
    // Position each window based on its virtual coordinates
    for (Window win : windows) {
        // For now, use a simple grid placement
        // In a full implementation, this would read from WindowStats
        bool is_focused = (win == focused_win);
        
        if (is_focused) {
            // Focused window takes full screen
            positionWindow(win, bounds, display, true);
        } else {
            // Move other windows off-screen
            XMoveWindow(display, win, config_.off_screen_x, config_.off_screen_y);
        }
    }
}

void InfiniteCanvasLayout::setViewport(int x, int y) {
    viewport_x_ = x;
    viewport_y_ = y;
}

void InfiniteCanvasLayout::panViewport(int dx, int dy) {
    viewport_x_ += dx;
    viewport_y_ += dy;
}

void InfiniteCanvasLayout::panToWindow(const WindowStats& stats, 
                                       unsigned int screen_width,
                                       unsigned int screen_height) {
    // Calculate the real position of the window
    int real_x = stats.getRealX(viewport_x_);
    int real_y = stats.getRealY(viewport_y_);
    
    // Check if window is fully visible
    bool visible = (real_x >= 0 && 
                    real_y >= 0 &&
                    real_x + static_cast<int>(stats.width) <= static_cast<int>(screen_width) &&
                    real_y + static_cast<int>(stats.height) <= static_cast<int>(screen_height));
    
    if (!visible) {
        // Pan viewport to center the window
        viewport_x_ = stats.virtual_x - static_cast<int>(screen_width) / 2;
        viewport_y_ = stats.virtual_y - static_cast<int>(screen_height) / 2;
    }
}

std::pair<int, int> InfiniteCanvasLayout::calculateNewWindowPosition(int window_count) {
    // Simple grid placement for new windows
    int cols = 3;  // 3 columns
    int row = window_count / cols;
    int col = window_count % cols;
    
    int x = col * (config_.default_window_width + config_.gap_size);
    int y = row * (config_.default_window_height + config_.gap_size);
    
    return {x, y};
}

std::vector<Window> InfiniteCanvasLayout::collectWindows(BSPNode* root) {
    std::vector<Window> windows;
    if (root) {
        windows.reserve(root->countWindows());
        root->collectWindows(windows);
    }
    return windows;
}

void InfiniteCanvasLayout::positionWindow(Window win, const Rect& rect, 
                                          Display* display, bool is_focused) {
    if (win == None || !display) return;
    
    unsigned int w = rect.width;
    unsigned int h = rect.height;
    if (w < 50) w = 50;
    if (h < 50) h = 50;
    
    XMoveResizeWindow(display, win, rect.x, rect.y, w, h);
    
    XWindowChanges changes;
    changes.border_width = config_.border_width;
    XConfigureWindow(display, win, CWBorderWidth, &changes);
    
    unsigned long color = is_focused ? 
        config_.focused_border_color : config_.unfocused_border_color;
    XSetWindowBorder(display, win, color);
    
    if (is_focused) {
        XRaiseWindow(display, win);
    }
}

// ============================================================================
// LayoutEngine Implementation
// ============================================================================

LayoutEngine::LayoutEngine() {
    // Initialize with just 1 workspace by default
    // Additional workspaces will be created dynamically for infinite workspaces
    WorkspaceData ws;
    auto bsp_layout = std::make_unique<BSPLayout>();
    bsp_layout->setGapConfig(&gap_config_);
    ws.layout = std::move(bsp_layout);
    workspaces_.push_back(std::move(ws));
}

void LayoutEngine::ensureWorkspace(int workspace) {
    // Expand workspaces vector if needed for infinite workspaces
    if (workspace >= static_cast<int>(workspaces_.size())) {
        size_t old_size = workspaces_.size();
        size_t new_size = static_cast<size_t>(workspace) + 10;  // Pre-allocate extra
        
        
        workspaces_.resize(new_size);
        
        // Initialize new workspaces with default BSP layout
        for (size_t i = old_size; i < new_size; ++i) {
            auto bsp_layout = std::make_unique<BSPLayout>();
            bsp_layout->setGapConfig(&gap_config_);
            workspaces_[i].layout = std::move(bsp_layout);
        }
    }
}

// ============================================================================
// BSP Split - Add Window
// ============================================================================

BSPNode* LayoutEngine::addWindow(Window window) {
    if (current_workspace_ < 0) {
        return nullptr;
    }
    
    // Ensure workspace exists (for infinite workspaces)
    ensureWorkspace(current_workspace_);
    
    if (current_workspace_ >= static_cast<int>(workspaces_.size())) {
        return nullptr;
    }
    
    auto& ws = workspaces_[current_workspace_];
    
    
    if (!ws.tree) {
        // First window - becomes root
        ws.tree = std::make_unique<BSPNode>(window);
        ws.tree->setFocused(true);
        focused_node_ = ws.tree.get();
        
        return ws.tree.get();
    }
    
    // Find node to split (focused node, or first leaf)
    BSPNode* target = focused_node_;
    if (!target || !target->isLeaf()) {
        target = ws.tree->findFirstLeaf();
    }
    
    if (target) {
        splitLeaf(target, window);
        invalidateLeavesCache();  // Tree structure changed
        return focused_node_;
    }
    
    return nullptr;
}

SplitType LayoutEngine::determineSplitType() const {
    if (dwindle_mode_) {
        // Dwindle mode: alternate split types for spiral effect
        SplitType type = (split_counter_ % 2 == 0) ? 
            SplitType::Vertical : SplitType::Horizontal;
        return type;
    }
    return SplitType::Vertical;
}

void LayoutEngine::splitLeaf(BSPNode* leaf, Window new_window) {
    
    // Store old window
    Window old_window = leaf->window_;
    
    // Determine split type based on dwindle mode
    SplitType split_type = SplitType::Vertical;
    
    if (dwindle_mode_) {
        BSPNode* parent = leaf->getParent();
        
        if (parent) {
            // Dwindle: alternate split direction from parent
            SplitType parent_split = parent->getSplitType();
            split_type = (parent_split == SplitType::Vertical) ? 
                SplitType::Horizontal : SplitType::Vertical;
        } else {
            // Root node - use screen aspect ratio
            if (display_) {
                int screen = DefaultScreen(display_);
                int screen_width = DisplayWidth(display_, screen);
                int screen_height = DisplayHeight(display_, screen);
                
                split_type = (screen_width > screen_height) ? 
                    SplitType::Vertical : SplitType::Horizontal;
            }
        }
        
        // Increment counter for next split
        split_counter_++;
    } else {
        split_type = determineSplitType();
    }
    
    // Convert leaf to container
    leaf->window_ = None;
    leaf->split_type_ = split_type;
    leaf->ratio_ = default_ratio_;
    
    // Create two new leaf children
    leaf->left_ = std::make_unique<BSPNode>(old_window);
    leaf->left_->setParent(leaf);
    leaf->left_->setFocused(false);
    
    leaf->right_ = std::make_unique<BSPNode>(new_window);
    leaf->right_->setParent(leaf);
    leaf->right_->setFocused(true);  // New window gets focus
    
    // Update focused node to new window
    if (focused_node_ == leaf || focused_node_ == nullptr) {
        focused_node_ = leaf->right_.get();
    }
    
}

// ============================================================================
// BSP Removal - Promote Sibling
// ============================================================================

Window LayoutEngine::removeWindow(Window window) {
    
    if (current_workspace_ < 0 || current_workspace_ >= static_cast<int>(workspaces_.size())) {
        return None;
    }
    
    auto& ws = workspaces_[current_workspace_];
    if (!ws.tree) {
        return None;
    }
    
    BSPNode* leaf = ws.tree->findWindow(window);
    if (!leaf) {
        return None;
    }
    
    
    // Special case: only one window (root)
    if (leaf == ws.tree.get()) {
        ws.tree.reset();
        focused_node_ = nullptr;
        split_counter_ = 0;
        window_bounds_.clear();
        return None;
    }
    
    // Promote sibling to parent's position
    BSPNode* promoted = removeLeafAndPromoteSibling(leaf);
    
    // Return the promoted window for focus
    Window next_focus = promoted ? promoted->getWindow() : None;
    
    
    // Decrement split counter for dwindle consistency
    if (dwindle_mode_ && split_counter_ > 0) {
        split_counter_--;
    }
    
    // Remove from bounds cache
    window_bounds_.erase(window);
    
    // Invalidate leaves cache since tree structure changed
    invalidateLeavesCache();
    
    return next_focus;
}

BSPNode* LayoutEngine::removeLeafAndPromoteSibling(BSPNode* leaf) {
    if (!leaf || !leaf->parent_) {
        return nullptr;
    }
    
    BSPNode* parent = leaf->parent_;
    BSPNode* grandparent = parent->parent_;
    BSPNode* sibling = leaf->getSibling();
    
    if (!sibling) {
        return nullptr;
    }
    
    
    // Clear the focused flag on the removed leaf
    leaf->setFocused(false);
    
    // Store pointer to sibling - this will be our return value
    BSPNode* promoted_node = sibling;
    
    if (grandparent) {
        // Parent is not root - replace parent with sibling in grandparent's tree
        std::unique_ptr<BSPNode> sibling_owner;
        
        // Extract sibling ownership from parent
        if (parent->left_.get() == leaf) {
            sibling_owner = std::move(parent->right_);
        } else {
            sibling_owner = std::move(parent->left_);
        }
        
        // Now replace parent with sibling in grandparent
        if (grandparent->left_.get() == parent) {
            grandparent->left_ = std::move(sibling_owner);
            promoted_node = grandparent->left_.get();
            grandparent->left_->setParent(grandparent);
        } else {
            grandparent->right_ = std::move(sibling_owner);
            promoted_node = grandparent->right_.get();
            grandparent->right_->setParent(grandparent);
        }
    } else {
        // Parent is root - sibling becomes new root
        auto& ws = workspaces_[current_workspace_];
        
        if (parent->left_.get() == leaf) {
            ws.tree = std::move(parent->right_);
        } else {
            ws.tree = std::move(parent->left_);
        }
        ws.tree->setParent(nullptr);
        promoted_node = ws.tree.get();
    }
    
    // Update focused_node_ to the promoted sibling
    if (focused_node_ == leaf || focused_node_ == nullptr) {
        // If sibling is a container, find the first leaf to focus
        if (promoted_node && !promoted_node->isLeaf()) {
            BSPNode* first_leaf = promoted_node->findFirstLeaf();
            if (first_leaf) {
                promoted_node = first_leaf;
            }
        }
        
        focused_node_ = promoted_node;
        if (promoted_node) {
            promoted_node->setFocused(true);
        }
    }
    
    return promoted_node;
}

// ============================================================================
// Layout Application
// ============================================================================

void LayoutEngine::applyLayout(int workspace, const Rect& screen_bounds) {
    if (workspace < 0) return;
    
    // Ensure workspace exists (for infinite workspaces)
    ensureWorkspace(workspace);
    
    if (workspace >= static_cast<int>(workspaces_.size())) return;
    
    auto& ws = workspaces_[workspace];
    if (ws.tree && ws.layout) {
        ws.layout->visit(ws.tree.get(), screen_bounds, display_);
        
        // Update cached bounds for spatial navigation
        window_bounds_.clear();
        screen_bounds_ = screen_bounds;
        
        // For non-BSP layouts, we need to calculate bounds based on actual positions
        // Check if this is a BSP layout - if so, use tree-based calculation
        if (dynamic_cast<const BSPLayout*>(ws.layout.get())) {
            if (ws.tree) {
                calculateNodeBounds(ws.tree.get(), screen_bounds, window_bounds_);
            }
        } else {
            // For other layouts, calculate bounds based on actual window positions
            // by querying X11 for each window's geometry
            if (ws.tree && display_) {
                std::vector<Window> windows;
                ws.tree->collectWindows(windows);
                
                for (Window win : windows) {
                    XWindowAttributes attrs;
                    if (XGetWindowAttributes(display_, win, &attrs)) {
                        window_bounds_[win] = Rect{
                            attrs.x,
                            attrs.y,
                            static_cast<unsigned int>(attrs.width),
                            static_cast<unsigned int>(attrs.height)
                        };
                    }
                }
            }
        }
    }
}

void LayoutEngine::calculateNodeBounds(BSPNode* node, const Rect& bounds,
                                        std::unordered_map<Window, Rect>& bounds_map) const {
    if (!node) return;
    
    if (node->isLeaf()) {
        bounds_map[node->getWindow()] = bounds;
        return;
    }
    
    Rect left_bounds = bounds.subRect(true, node->getSplitType(), node->getRatio());
    Rect right_bounds = bounds.subRect(false, node->getSplitType(), node->getRatio());
    
    // Apply gaps
    if (node->getSplitType() == SplitType::Vertical) {
        if (left_bounds.width > static_cast<unsigned int>(gap_size_ / 2)) {
            left_bounds.width -= gap_size_ / 2;
        }
        right_bounds.x += gap_size_ / 2;
        if (right_bounds.width > static_cast<unsigned int>(gap_size_ / 2)) {
            right_bounds.width -= gap_size_ / 2;
        }
    } else {
        if (left_bounds.height > static_cast<unsigned int>(gap_size_ / 2)) {
            left_bounds.height -= gap_size_ / 2;
        }
        right_bounds.y += gap_size_ / 2;
        if (right_bounds.height > static_cast<unsigned int>(gap_size_ / 2)) {
            right_bounds.height -= gap_size_ / 2;
        }
    }
    
    calculateNodeBounds(node->getLeft(), left_bounds, bounds_map);
    calculateNodeBounds(node->getRight(), right_bounds, bounds_map);
}

void LayoutEngine::setLayout(int workspace, std::unique_ptr<LayoutVisitor> layout) {
    if (workspace < 0) return;
    
    // Ensure workspace exists (for infinite workspaces)
    ensureWorkspace(workspace);
    
    if (workspace < static_cast<int>(workspaces_.size())) {
        workspaces_[workspace].layout = std::move(layout);
    }
}

BSPNode* LayoutEngine::getTree(int workspace) {
    if (workspace >= 0 && workspace < static_cast<int>(workspaces_.size())) {
        return workspaces_[workspace].tree.get();
    }
    return nullptr;
}

const BSPNode* LayoutEngine::getTree(int workspace) const {
    if (workspace >= 0 && workspace < static_cast<int>(workspaces_.size())) {
        return workspaces_[workspace].tree.get();
    }
    return nullptr;
}

// ============================================================================
// Focus Management
// ============================================================================

void LayoutEngine::focusWindow(Window window) {
    if (current_workspace_ < 0 || current_workspace_ >= static_cast<int>(workspaces_.size())) return;
    
    auto& ws = workspaces_[current_workspace_];
    if (!ws.tree) return;
    
    BSPNode* node = ws.tree->findWindow(window);
    if (!node) return;
    
    // Clear previous focus
    if (focused_node_) {
        focused_node_->setFocused(false);
    }
    
    // Set new focus
    focused_node_ = node;
    node->setFocused(true);
    
}

Window LayoutEngine::getFocusedWindow() const {
    return focused_node_ ? focused_node_->getWindow() : None;
}

Window LayoutEngine::moveFocus(const std::string& direction) {
    if (current_workspace_ < 0 || current_workspace_ >= static_cast<int>(workspaces_.size())) {
        return None;
    }
    
    auto& ws = workspaces_[current_workspace_];
    if (!ws.tree || !focused_node_) return None;
    
    BSPNode* next = findSpatialNeighbor(focused_node_, direction);
    if (!next) return None;
    
    // Update focus
    focused_node_->setFocused(false);
    next->setFocused(true);
    focused_node_ = next;
    
    return next->getWindow();
}

BSPNode* LayoutEngine::findSpatialNeighbor(BSPNode* from, const std::string& direction) {
    if (!from || !from->isLeaf()) return nullptr;
    
    Window from_window = from->getWindow();
    
    // Get cached bounds for this window
    auto it = window_bounds_.find(from_window);
    if (it == window_bounds_.end()) {
        // Fallback to tree-order navigation
        auto& ws = workspaces_[current_workspace_];
        std::vector<Window> windows;
        ws.tree->collectWindows(windows);
        
        auto window_it = std::find(windows.begin(), windows.end(), from_window);
        if (window_it == windows.end()) return nullptr;
        
        size_t idx = std::distance(windows.begin(), window_it);
        
        if ((direction == "left" || direction == "up") && idx > 0) {
            return ws.tree->findWindow(windows[idx - 1]);
        }
        if ((direction == "right" || direction == "down") && idx + 1 < windows.size()) {
            return ws.tree->findWindow(windows[idx + 1]);
        }
        
        return nullptr;
    }
    
    Rect from_bounds = it->second;
    
    // Find the nearest window in the specified direction
    BSPNode* nearest = nullptr;
    int min_distance = std::numeric_limits<int>::max();
    
    for (const auto& [window, bounds] : window_bounds_) {
        if (window == from_window) continue;
        
        // Check if this window is in the correct direction
        bool in_direction = false;
        int distance = std::numeric_limits<int>::max();
        
        if (direction == "left") {
            if (bounds.right() <= from_bounds.left()) {
                in_direction = true;
                distance = from_bounds.left() - bounds.right();
                // Add vertical alignment bonus
                int v_overlap = std::min(bounds.bottom(), from_bounds.bottom()) - 
                               std::max(bounds.top(), from_bounds.top());
                if (v_overlap > 0) {
                    distance -= v_overlap;  // Prefer vertically aligned windows
                }
            }
        } else if (direction == "right") {
            if (bounds.left() >= from_bounds.right()) {
                in_direction = true;
                distance = bounds.left() - from_bounds.right();
                int v_overlap = std::min(bounds.bottom(), from_bounds.bottom()) - 
                               std::max(bounds.top(), from_bounds.top());
                if (v_overlap > 0) {
                    distance -= v_overlap;
                }
            }
        } else if (direction == "up") {
            if (bounds.bottom() <= from_bounds.top()) {
                in_direction = true;
                distance = from_bounds.top() - bounds.bottom();
                int h_overlap = std::min(bounds.right(), from_bounds.right()) - 
                               std::max(bounds.left(), from_bounds.left());
                if (h_overlap > 0) {
                    distance -= h_overlap;  // Prefer horizontally aligned windows
                }
            }
        } else if (direction == "down") {
            if (bounds.top() >= from_bounds.bottom()) {
                in_direction = true;
                distance = bounds.top() - from_bounds.bottom();
                int h_overlap = std::min(bounds.right(), from_bounds.right()) - 
                               std::max(bounds.left(), from_bounds.left());
                if (h_overlap > 0) {
                    distance -= h_overlap;
                }
            }
        }
        
        if (in_direction && distance < min_distance) {
            min_distance = distance;
            auto& ws = workspaces_[current_workspace_];
            nearest = ws.tree->findWindow(window);
        }
    }
    
    return nearest;
}

void LayoutEngine::swapFocused(const std::string& direction) {
    if (current_workspace_ < 0 || current_workspace_ >= static_cast<int>(workspaces_.size())) return;
    
    auto& ws = workspaces_[current_workspace_];
    if (!ws.tree || !focused_node_ || !focused_node_->isLeaf()) return;
    
    BSPNode* neighbor = findSpatialNeighbor(focused_node_, direction);
    if (!neighbor || !neighbor->isLeaf()) return;
    
    // Swap windows
    Window temp = focused_node_->window_;
    focused_node_->window_ = neighbor->window_;
    neighbor->window_ = temp;
    
    // Update bounds cache
    std::swap(window_bounds_[focused_node_->window_], window_bounds_[neighbor->window_]);
    
}

bool LayoutEngine::swapWindows(Window window1, Window window2) {
    if (current_workspace_ < 0 || current_workspace_ >= static_cast<int>(workspaces_.size())) {
        return false;
    }
    
    auto& ws = workspaces_[current_workspace_];
    if (!ws.tree) return false;
    
    // Find both nodes in the tree
    BSPNode* node1 = ws.tree->findWindow(window1);
    BSPNode* node2 = ws.tree->findWindow(window2);
    
    if (!node1 || !node2 || !node1->isLeaf() || !node2->isLeaf()) {
        return false;
    }
    
    // Don't swap if same window
    if (node1 == node2) {
        return false;
    }
    
    // Swap windows
    Window temp = node1->window_;
    node1->window_ = node2->window_;
    node2->window_ = temp;
    
    // Update bounds cache
    std::swap(window_bounds_[node1->window_], window_bounds_[node2->window_]);
    
    return true;
}

void LayoutEngine::resizeFocused(double delta) {
    if (!focused_node_) return;
    
    BSPNode* parent = focused_node_->getParent();
    if (!parent) return;  // Root has no ratio to adjust
    
    // Determine if focused is left or right child
    bool is_left = (parent->left_.get() == focused_node_);
    
    double new_ratio = parent->getRatio();
    if (is_left) {
        new_ratio += delta;
    } else {
        new_ratio -= delta;
    }
    
    parent->setRatio(new_ratio);
}

void LayoutEngine::toggleSplitDirection() {
    if (!focused_node_) return;
    
    BSPNode* parent = focused_node_->getParent();
    if (!parent) return;
    
    SplitType new_type = (parent->getSplitType() == SplitType::Vertical) 
        ? SplitType::Horizontal : SplitType::Vertical;
    
    parent->setSplitType(new_type);
}

void LayoutEngine::setCurrentWorkspace(int workspace) {
    if (workspace < 0) {
        return;
    }
    
    // Ensure workspace exists (for infinite workspaces)
    ensureWorkspace(workspace);
    
    if (workspace < static_cast<int>(workspaces_.size())) {
        current_workspace_ = workspace;
        
        // Update focused node to this workspace's tree
        auto& ws = workspaces_[current_workspace_];
        if (ws.tree) {
            focused_node_ = ws.tree->findFocused();
            if (!focused_node_) {
                focused_node_ = ws.tree->findFirstLeaf();
                if (focused_node_) focused_node_->setFocused(true);
            }
        } else {
            focused_node_ = nullptr;
        }
        
        // Clear bounds cache for new workspace
        window_bounds_.clear();
    }
}

bool LayoutEngine::isEmpty() const {
    if (current_workspace_ < 0 || current_workspace_ >= static_cast<int>(workspaces_.size())) {
        return true;
    }
    const auto& ws = workspaces_[current_workspace_];
    return !ws.tree || ws.tree->countWindows() == 0;
}

int LayoutEngine::getWindowCount() const {
    if (current_workspace_ < 0 || current_workspace_ >= static_cast<int>(workspaces_.size())) {
        return 0;
    }
    const auto& ws = workspaces_[current_workspace_];
    return ws.tree ? ws.tree->countWindows() : 0;
}

int LayoutEngine::getWindowCount(int workspace) const {
    if (workspace < 0 || workspace >= static_cast<int>(workspaces_.size())) {
        return 0;
    }
    const auto& ws = workspaces_[workspace];
    return ws.tree ? ws.tree->countWindows() : 0;
}

std::vector<Window> LayoutEngine::getWorkspaceWindows(int workspace) const {
    std::vector<Window> windows;
    if (workspace >= 0 && workspace < static_cast<int>(workspaces_.size())) {
        const auto& ws = workspaces_[workspace];
        if (ws.tree) {
            int count = ws.tree->countWindows();
            windows.reserve(count);
            ws.tree->collectWindows(windows);
        }
    }
    return windows;
}

void LayoutEngine::setBorderColors(unsigned long focused_color, unsigned long unfocused_color) {
    focused_border_color_ = focused_color;
    unfocused_border_color_ = unfocused_color;
}

void LayoutEngine::updateBorderColors() {
    if (!display_) return;
    
    if (current_workspace_ < 0 || current_workspace_ >= static_cast<int>(workspaces_.size())) return;
    
    auto& ws = workspaces_[current_workspace_];
    if (!ws.tree) return;
    
    // Get all windows in the tree
    std::vector<Window> windows;
    ws.tree->collectWindows(windows);
    
    // Get the currently focused window
    Window focused_win = getFocusedWindow();
    
    // Update border color for each window
    for (Window win : windows) {
        unsigned long color = (win == focused_win) ? 
            focused_border_color_ : unfocused_border_color_;
        XSetWindowBorder(display_, win, color);
    }
    
    XFlush(display_);
}

// ============================================================================
// Layout Cycling Implementation
// ============================================================================

std::string LayoutEngine::cycleLayout(bool forward) {
    // Define the order of layouts for cycling
    static const std::vector<LayoutMode> layout_order = {
        LayoutMode::BSP,
        LayoutMode::Monocle,
        LayoutMode::MasterStack,
        LayoutMode::CenteredMaster,
        LayoutMode::DynamicGrid,
        LayoutMode::DwindleSpiral,
        LayoutMode::TabbedStacked
    };
    
    // Get current layout mode
    auto current_mode = getCurrentLayoutMode();
    if (!current_mode) {
        current_mode = LayoutMode::BSP;
    }
    
    // Find current mode in the order
    auto it = std::find(layout_order.begin(), layout_order.end(), *current_mode);
    size_t current_idx = (it != layout_order.end()) ? 
        std::distance(layout_order.begin(), it) : 0;
    
    // Calculate next index based on direction
    size_t next_idx;
    if (forward) {
        next_idx = (current_idx + 1) % layout_order.size();
    } else {
        next_idx = (current_idx == 0) ? layout_order.size() - 1 : current_idx - 1;
    }
    
    LayoutMode next_mode = layout_order[next_idx];
    
    // Set the new layout
    setLayoutMode(current_workspace_, next_mode);
    
    return layoutModeToString(next_mode);
}

std::optional<LayoutMode> LayoutEngine::getCurrentLayoutMode(int workspace) const {
    int ws = (workspace < 0) ? current_workspace_ : workspace;
    
    if (ws < 0 || ws >= static_cast<int>(workspaces_.size())) {
        return std::nullopt;
    }
    
    // Try to identify the current layout type by dynamic_cast
    const auto& layout = workspaces_[ws].layout;
    if (!layout) {
        return LayoutMode::BSP;  // Default
    }
    
    // Check layout type using dynamic_cast
    if (dynamic_cast<const BSPLayout*>(layout.get())) {
        return LayoutMode::BSP;
    }
    if (dynamic_cast<const MonocleLayout*>(layout.get())) {
        return LayoutMode::Monocle;
    }
    if (dynamic_cast<const MasterStackLayout*>(layout.get())) {
        return LayoutMode::MasterStack;
    }
    if (dynamic_cast<const CenteredMasterLayout*>(layout.get())) {
        return LayoutMode::CenteredMaster;
    }
    if (dynamic_cast<const DynamicGridLayout*>(layout.get())) {
        return LayoutMode::DynamicGrid;
    }
    if (dynamic_cast<const DwindleSpiralLayout*>(layout.get())) {
        return LayoutMode::DwindleSpiral;
    }
    if (dynamic_cast<const TabbedStackedLayout*>(layout.get())) {
        return LayoutMode::TabbedStacked;
    }
    
    return std::nullopt;
}

void LayoutEngine::setLayoutMode(int workspace, LayoutMode mode) {
    if (workspace < 0) return;
    
    // Ensure workspace exists (for infinite workspaces)
    ensureWorkspace(workspace);
    
    if (workspace >= static_cast<int>(workspaces_.size())) {
        return;
    }
    
    std::unique_ptr<LayoutVisitor> layout;
    
    switch (mode) {
        case LayoutMode::BSP: {
            BSPLayout::Config config;
            config.gap_size = gap_size_;
            config.border_width = border_width_;
            config.focused_border_color = focused_border_color_;
            config.unfocused_border_color = unfocused_border_color_;
            auto bsp_layout = std::make_unique<BSPLayout>(config);
            bsp_layout->setGapConfig(&gap_config_);
            layout = std::move(bsp_layout);
            break;
        }
        case LayoutMode::Monocle:
            layout = std::make_unique<MonocleLayout>();
            break;
        case LayoutMode::MasterStack: {
            MasterStackLayout::Config config;
            config.gap_size = gap_size_;
            config.border_width = border_width_;
            config.focused_border_color = focused_border_color_;
            config.unfocused_border_color = unfocused_border_color_;
            layout = std::make_unique<MasterStackLayout>(config);
            break;
        }
        case LayoutMode::CenteredMaster: {
            CenteredMasterLayout::Config config;
            config.gap_size = gap_size_;
            config.border_width = border_width_;
            config.focused_border_color = focused_border_color_;
            config.unfocused_border_color = unfocused_border_color_;
            layout = std::make_unique<CenteredMasterLayout>(config);
            break;
        }
        case LayoutMode::DynamicGrid: {
            DynamicGridLayout::Config config;
            config.gap_size = gap_size_;
            config.border_width = border_width_;
            config.focused_border_color = focused_border_color_;
            config.unfocused_border_color = unfocused_border_color_;
            layout = std::make_unique<DynamicGridLayout>(config);
            break;
        }
        case LayoutMode::DwindleSpiral: {
            DwindleSpiralLayout::Config config;
            config.gap_size = gap_size_;
            config.border_width = border_width_;
            config.focused_border_color = focused_border_color_;
            config.unfocused_border_color = unfocused_border_color_;
            layout = std::make_unique<DwindleSpiralLayout>(config);
            break;
        }
        case LayoutMode::TabbedStacked: {
            TabbedStackedLayout::Config config;
            config.border_width = border_width_;
            config.focused_border_color = focused_border_color_;
            config.unfocused_border_color = unfocused_border_color_;
            layout = std::make_unique<TabbedStackedLayout>(config);
            break;
        }
        case LayoutMode::InfiniteCanvas: {
            InfiniteCanvasLayout::Config config;
            config.border_width = border_width_;
            config.focused_border_color = focused_border_color_;
            config.unfocused_border_color = unfocused_border_color_;
            layout = std::make_unique<InfiniteCanvasLayout>(config);
            break;
        }
    }
    
    if (layout) {
        workspaces_[workspace].layout = std::move(layout);
    }
}

// ============================================================================
// Infinite Canvas Implementation
// ============================================================================

WindowStats* LayoutEngine::getWindowStats(Window window) {
    auto it = window_stats_.find(window);
    if (it != window_stats_.end()) {
        return &it->second;
    }
    return nullptr;
}

const WindowStats* LayoutEngine::getWindowStats(Window window) const {
    auto it = window_stats_.find(window);
    if (it != window_stats_.end()) {
        return &it->second;
    }
    return nullptr;
}

void LayoutEngine::updateWindowStats(Window window, const WindowStats& stats) {
    window_stats_[window] = stats;
}

void LayoutEngine::setViewport(int x, int y) {
    viewport_x_ = x;
    viewport_y_ = y;
}

void LayoutEngine::panViewport(int dx, int dy) {
    viewport_x_ += dx;
    viewport_y_ += dy;
}

void LayoutEngine::panToFocusedWindow(unsigned int screen_width, unsigned int screen_height) {
    if (!focused_node_) return;
    
    Window focused = focused_node_->getWindow();
    auto* stats = getWindowStats(focused);
    if (!stats) return;
    
    // Check if window is visible in current viewport
    if (!stats->isVisibleInViewport(viewport_x_, viewport_y_, screen_width, screen_height)) {
        // Pan to center the window
        viewport_x_ = stats->virtual_x - static_cast<int>(screen_width) / 2;
        viewport_y_ = stats->virtual_y - static_cast<int>(screen_height) / 2;
    }
}

// ============================================================================
// Pointer Warping Implementation
// ============================================================================

bool LayoutEngine::warpPointerToWindow(Window window) {
    if (!display_ || window == None) return false;
    
    // Get window geometry
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(display_, window, &attrs)) {
        return false;
    }
    
    // Set warping flag to suppress focus-follows-mouse
    is_warping_ = true;
    
    // Warp pointer to center of window
    XWarpPointer(display_, None, window, 0, 0, 0, 0,
                 static_cast<int>(attrs.width / 2),
                 static_cast<int>(attrs.height / 2));
    
    XFlush(display_);
    
    
    return true;
}

// ============================================================================
// Auto-Expand Logic Implementation
// ============================================================================

bool LayoutEngine::wouldViolateMinSize(const Rect& bounds, SplitType split_type) const {
    using namespace layout_constants;
    
    if (split_type == SplitType::Vertical) {
        // Vertical split divides width
        int min_total_width = MIN_WINDOW_WIDTH * 2 + gap_size_;
        return static_cast<int>(bounds.width) < min_total_width;
    } else {
        // Horizontal split divides height
        int min_total_height = MIN_WINDOW_HEIGHT * 2 + gap_size_;
        return static_cast<int>(bounds.height) < min_total_height;
    }
}

bool LayoutEngine::canSplitWithoutViolation(BSPNode* node) const {
    if (!node || !node->isLeaf()) return false;
    
    // Get the bounds for this node
    auto it = window_bounds_.find(node->getWindow());
    if (it == window_bounds_.end()) return false;
    
    SplitType split_type = determineSplitType();
    return !wouldViolateMinSize(it->second, split_type);
}

int LayoutEngine::expandCanvasForSplit(Rect& bounds, SplitType split_type) {
    using namespace layout_constants;
    
    int expansion = 0;
    
    if (split_type == SplitType::Vertical) {
        int min_total_width = MIN_WINDOW_WIDTH * 2 + gap_size_;
        if (static_cast<int>(bounds.width) < min_total_width) {
            expansion = min_total_width - static_cast<int>(bounds.width);
            bounds.width += expansion;
        }
    } else {
        int min_total_height = MIN_WINDOW_HEIGHT * 2 + gap_size_;
        if (static_cast<int>(bounds.height) < min_total_height) {
            expansion = min_total_height - static_cast<int>(bounds.height);
            bounds.height += expansion;
        }
    }
    
    return expansion;
}

// ============================================================================
// DFS Focus Wrapping Implementation
// ============================================================================

void LayoutEngine::rebuildLeavesCache() const {
    cached_leaves_.clear();
    
    const BSPNode* root = getTree(current_workspace_);
    if (root) {
        collectLeavesDFSHelper(const_cast<BSPNode*>(root), cached_leaves_);
    }
    
    leaves_cache_valid_ = true;
}

void LayoutEngine::collectLeavesDFSHelper(BSPNode* node, std::vector<BSPNode*>& leaves) const {
    if (!node) return;
    
    if (node->isLeaf()) {
        leaves.push_back(node);
    } else {
        // DFS: left first, then right
        collectLeavesDFSHelper(node->getLeft(), leaves);
        collectLeavesDFSHelper(node->getRight(), leaves);
    }
}

std::vector<BSPNode*> LayoutEngine::collectLeavesDFS(BSPNode* root) const {
    std::vector<BSPNode*> leaves;
    
    if (!root) {
        root = const_cast<BSPNode*>(getTree(current_workspace_));
    }
    
    if (root) {
        collectLeavesDFSHelper(root, leaves);
    }
    
    return leaves;
}

Window LayoutEngine::focusNextLeafDFS() {
    if (!leaves_cache_valid_) {
        rebuildLeavesCache();
    }
    
    if (cached_leaves_.empty()) return None;
    
    // Find current focused leaf index
    size_t current_idx = 0;
    for (size_t i = 0; i < cached_leaves_.size(); ++i) {
        if (cached_leaves_[i] == focused_node_) {
            current_idx = i;
            break;
        }
    }
    
    // Move to next leaf
    size_t next_idx = (current_idx + 1) % cached_leaves_.size();
    
    if (focus_wrap_mode_ == FocusWrapMode::Traditional || next_idx != 0) {
        // Traditional: wrap around
        // Or: not at the end yet
        BSPNode* next_node = cached_leaves_[next_idx];
        if (next_node) {
            focusWindow(next_node->getWindow());
            return next_node->getWindow();
        }
    }
    // FocusWrapMode::Infinite at edge: could create placeholder
    // For now, just wrap like traditional
    
    return None;
}

Window LayoutEngine::focusPrevLeafDFS() {
    if (!leaves_cache_valid_) {
        rebuildLeavesCache();
    }
    
    if (cached_leaves_.empty()) return None;
    
    // Find current focused leaf index
    size_t current_idx = 0;
    for (size_t i = 0; i < cached_leaves_.size(); ++i) {
        if (cached_leaves_[i] == focused_node_) {
            current_idx = i;
            break;
        }
    }
    
    // Move to previous leaf
    size_t prev_idx = (current_idx == 0) ? cached_leaves_.size() - 1 : current_idx - 1;
    
    BSPNode* prev_node = cached_leaves_[prev_idx];
    if (prev_node) {
        focusWindow(prev_node->getWindow());
        return prev_node->getWindow();
    }
    
    return None;
}

// ============================================================================
// Workspace Teleportation Implementation
// ============================================================================

void LayoutEngine::teleportToWorkspace(int workspace_id) {
    // Ensure workspace exists
    ensureWorkspace(workspace_id);
    
    // Save current workspace camera position
    if (current_workspace_ >= 0 && current_workspace_ < static_cast<int>(workspace_nodes_.size())) {
        auto cam_pos = camera_.getOffset();
        workspace_nodes_[current_workspace_].saved_camera_x = cam_pos.first;
        workspace_nodes_[current_workspace_].saved_camera_y = cam_pos.second;
    }
    
    // Switch to new workspace
    setCurrentWorkspace(workspace_id);
    
    // Restore camera position for new workspace
    const WorkspaceNode* node = getWorkspaceNode(workspace_id);
    if (node) {
        camera_.teleportTo(node->saved_camera_x, node->saved_camera_y);
    }
    
}

const WorkspaceNode* LayoutEngine::getWorkspaceNode(int workspace_id) const {
    if (workspace_id < 0 || workspace_id >= static_cast<int>(workspace_nodes_.size())) {
        return nullptr;
    }
    return &workspace_nodes_[workspace_id];
}

int LayoutEngine::createWorkspaceAt(int64_t origin_x, int64_t origin_y) {
    int new_id = static_cast<int>(workspace_nodes_.size());
    
    WorkspaceNode node;
    node.id = new_id;
    node.origin_x = origin_x;
    node.origin_y = origin_y;
    node.name = "Workspace " + std::to_string(new_id);
    node.saved_camera_x = origin_x;
    node.saved_camera_y = origin_y;
    
    workspace_nodes_.push_back(node);
    ensureWorkspace(new_id);
    
    
    return new_id;
}

// ============================================================================
// Spatial Grid Update Implementation
// ============================================================================

void LayoutEngine::updateSpatialGrid() {
    // Clear and rebuild spatial grid from window stats
    spatial_grid_.clear();
    
    for (const auto& [window, stats] : window_stats_) {
        spatial_grid_.addWindow(window, stats.virtual_x, stats.virtual_y,
                                stats.width, stats.height);
    }
}

} // namespace pblank
