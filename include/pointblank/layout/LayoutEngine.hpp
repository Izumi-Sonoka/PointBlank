#pragma once

#include <memory>
#include <vector>
#include <functional>
#include <optional>
#include <algorithm>
#include <unordered_map>
#include <string_view>
#include <iostream>
#include <X11/Xlib.h>
#include "pointblank/config/LayoutConfigParser.hpp"
#include "pointblank/utils/Camera.hpp"
#include "pointblank/utils/SpatialGrid.hpp"
#include "pointblank/utils/GapConfig.hpp"

namespace pblank {

class RenderPipeline;

namespace layout_constants {
    
    constexpr int MIN_WINDOW_WIDTH = 300;
    
    constexpr int MIN_WINDOW_HEIGHT = 200;
    
    constexpr int CANVAS_EXPANSION_STEP = 400;
    
    constexpr unsigned int MAX_WINDOW_DIMENSION = 32767;
    
    constexpr int64_t WORKSPACE_INTERVAL = 1000000000;
}

class ManagedWindow;

/**
 * @brief Split direction for BSP nodes
 */
enum class SplitType {
    Horizontal,  
    Vertical     
};

enum class FocusWrapMode {
    Traditional,    
    Infinite        
};

struct WorkspaceNode {
    int id;
    int64_t origin_x;  
    int64_t origin_y;
    std::string name;
    
    int64_t saved_camera_x{0};
    int64_t saved_camera_y{0};
};

struct Rect {
    int x, y;
    unsigned int width, height;
    
    inline int area() const { return static_cast<int>(width * height); }
    
    inline bool contains(int px, int py) const {
        return px >= x && px < x + static_cast<int>(width) &&
               py >= y && py < y + static_cast<int>(height);
    }
    
    inline int centerX() const { return x + static_cast<int>(width) / 2; }
    inline int centerY() const { return y + static_cast<int>(height) / 2; }
    
    inline int left() const { return x; }
    inline int right() const { return x + static_cast<int>(width); }
    inline int top() const { return y; }
    inline int bottom() const { return y + static_cast<int>(height); }
    
    Rect subRect(bool is_left, SplitType split, double ratio) const;
    
    inline bool isLeftOf(const Rect& other) const { return right() <= other.left(); }
    inline bool isRightOf(const Rect& other) const { return left() >= other.right(); }
    inline bool isAbove(const Rect& other) const { return bottom() <= other.top(); }
    inline bool isBelow(const Rect& other) const { return top() >= other.bottom(); }
    
    int distanceTo(const Rect& other, const std::string& direction) const;
};

class BSPNode {
public:
    
    explicit BSPNode(Window window);
    
    BSPNode(std::unique_ptr<BSPNode> left, std::unique_ptr<BSPNode> right,
            SplitType split, double ratio = 0.5);
    
    inline bool isLeaf() const { return window_ != None; }
    inline bool isContainer() const { return window_ == None; }
    
    inline Window getWindow() const { return window_; }
    
    inline BSPNode* getLeft() const { return left_.get(); }
    inline BSPNode* getRight() const { return right_.get(); }
    inline BSPNode* getParent() const { return parent_; }
    
    inline SplitType getSplitType() const { return split_type_; }
    inline void setSplitType(SplitType type) { split_type_ = type; }
    
    inline double getRatio() const { return ratio_; }
    inline void setRatio(double ratio) { ratio_ = std::clamp(ratio, 0.1, 0.9); }
    
    inline bool isFocused() const { return focused_; }
    inline void setFocused(bool focused) { focused_ = focused; }
    
    BSPNode* getSibling() const;
    BSPNode* findWindow(Window win);
    BSPNode* findFocused();
    BSPNode* findFirstLeaf();
    BSPNode* findLastLeaf();
    int countWindows() const;
    void collectWindows(std::vector<Window>& windows) const;
    
    std::unique_ptr<BSPNode> filterOut(const std::unordered_set<Window>& windows) const;
    
    void collectLeavesWithBounds(std::vector<std::pair<BSPNode*, Rect>>& leaves, 
                                  const Rect& bounds) const;
    
    static BSPNode* findByWindow(BSPNode* root, Window win);
    static BSPNode* findFocusedInTree(BSPNode* root);
    
private:
    friend class LayoutEngine;
    friend class BSPLayout;
    
    Window window_{None};
    
    std::unique_ptr<BSPNode> left_;
    std::unique_ptr<BSPNode> right_;
    BSPNode* parent_{nullptr};  
    
    SplitType split_type_{SplitType::Vertical};
    double ratio_{0.5};
    bool focused_{false};
    
    void setParent(BSPNode* parent) { parent_ = parent; }
};

class LayoutVisitor {
public:
    virtual ~LayoutVisitor() = default;
    virtual void visit(BSPNode* root, const Rect& bounds, Display* display) = 0;
    
    void setRenderPipeline(RenderPipeline* pipeline) { render_pipeline_ = pipeline; }

    void setGapConfig(const GapConfig* gap_config) { gap_config_ = gap_config; }

protected:
    
    void placeWindow(Display* display, Window win,
                     int x, int y, unsigned int w, unsigned int h,
                     int border_width, unsigned long border_color);

    Rect applyOuterGaps(const Rect& bounds, int fallback_gap) const;
    
    RenderPipeline* render_pipeline_{nullptr};
    const GapConfig* gap_config_{nullptr};  
};

class BSPLayout : public LayoutVisitor {
public:
    struct Config {
        int gap_size = 10;
        int border_width = 2;
        int padding = 5;
        unsigned long focused_border_color = 0x89B4FA;  
        unsigned long unfocused_border_color = 0x45475A; 
        bool smart_gaps = true;  
    };
    
    BSPLayout() = default;
    explicit BSPLayout(const Config& config) : config_(config) {}
    
    void visit(BSPNode* root, const Rect& bounds, Display* display) override;
    
private:
    Config config_;
    int effective_inner_gap_{10};  
    void visitNode(BSPNode* node, const Rect& bounds, Display* display, BSPNode* focused_node,
                  const Rect& screen_bounds, int window_count);
};

class MonocleLayout : public LayoutVisitor {
public:
    void visit(BSPNode* root, const Rect& bounds, Display* display) override;
};

class MasterStackLayout : public LayoutVisitor {
public:
    struct Config {
        double master_ratio = 0.55;
        int gap_size = 10;
        int max_master = 1;
        int border_width = 2;
        unsigned long focused_border_color = 0x89B4FA;  
        unsigned long unfocused_border_color = 0x45475A; 
    };
    
    MasterStackLayout() = default;
    explicit MasterStackLayout(const Config& config) : config_(config) {}
    
    void visit(BSPNode* root, const Rect& bounds, Display* display) override;
    
private:
    Config config_;
    std::vector<Window> collectWindows(BSPNode* root);
};

class CenteredMasterLayout : public LayoutVisitor {
public:
    struct Config {
        double center_ratio = 0.5;      
        int max_center = 1;             
        int gap_size = 10;
        bool center_on_focus = true;    
        int border_width = 2;
        unsigned long focused_border_color = 0x89B4FA;  
        unsigned long unfocused_border_color = 0x45475A; 
    };
    
    CenteredMasterLayout() = default;
    explicit CenteredMasterLayout(const Config& config) : config_(config) {}
    
    void visit(BSPNode* root, const Rect& bounds, Display* display) override;
    
private:
    Config config_;
    
    std::vector<Window> collectWindows(BSPNode* root);
    void positionWindow(Window win, const Rect& rect, Display* display, bool is_focused);
};

class DynamicGridLayout : public LayoutVisitor {
public:
    struct Config {
        bool prefer_horizontal = false; 
        int min_cell_width = 200;       
        int min_cell_height = 150;      
        int gap_size = 10;
        int border_width = 2;
        unsigned long focused_border_color = 0x89B4FA;  
        unsigned long unfocused_border_color = 0x45475A; 
    };
    
    DynamicGridLayout() = default;
    explicit DynamicGridLayout(const Config& config) : config_(config) {}
    
    void visit(BSPNode* root, const Rect& bounds, Display* display) override;
    
    std::pair<int, int> calculateGridDimensions(int count, const Rect& bounds) const;
    
private:
    Config config_;
    
    std::vector<Window> collectWindows(BSPNode* root);
    void positionWindow(Window win, const Rect& rect, Display* display, bool is_focused);
};

class DwindleSpiralLayout : public LayoutVisitor {
public:
    struct Config {
        double initial_ratio = 0.55;    
        double ratio_increment = 0.02;  
        int gap_size = 10;
        bool shift_by_focus = true;     
        int border_width = 2;
        unsigned long focused_border_color = 0x89B4FA;  
        unsigned long unfocused_border_color = 0x45475A; 
    };
    
    DwindleSpiralLayout() = default;
    explicit DwindleSpiralLayout(const Config& config) : config_(config) {}
    
    void visit(BSPNode* root, const Rect& bounds, Display* display) override;
    
private:
    Config config_;
    
    std::vector<Window> collectWindows(BSPNode* root);
    void positionWindow(Window win, const Rect& rect, Display* display, bool is_focused);
    
    std::vector<Rect> calculateSpiralBounds(int count, const Rect& bounds);
};

class GoldenRatioLayout : public LayoutVisitor {
public:
    struct Config {
        double golden_ratio = 1.618033988749;  
        int gap_size = 10;
        bool rotate_splits = true;              
        bool alternate_sides = true;           
        int border_width = 2;
        unsigned long focused_border_color = 0x89B4FA;  
        unsigned long unfocused_border_color = 0x45475A; 
    };
    
    GoldenRatioLayout() = default;
    explicit GoldenRatioLayout(const Config& config) : config_(config) {}
    
    void visit(BSPNode* root, const Rect& bounds, Display* display) override;
    
private:
    Config config_;
    
    std::vector<Window> collectWindows(BSPNode* root);
    void positionWindow(Window win, const Rect& rect, Display* display, bool is_focused);
    
    std::vector<Rect> calculateGoldenBounds(int count, const Rect& bounds);
};

class TabbedStackedLayout : public LayoutVisitor {
public:
    enum class TabPosition { Top, Bottom };
    
    struct Config {
        int tab_height = 25;            
        int tab_min_width = 100;        
        int gap_size = 0;               
        bool show_focused_only = true;  
        TabPosition tab_position = TabPosition::Top;
        int border_width = 2;
        unsigned long focused_border_color = 0x89B4FA;  
        unsigned long unfocused_border_color = 0x45475A; 
        unsigned long tab_bg_color = 0x333333;       
        unsigned long tab_active_color = 0x0066CC;   
        unsigned long tab_inactive_color = 0x222222; 
        unsigned long tab_text_color = 0xFFFFFF;     
    };
    
    TabbedStackedLayout() = default;
    explicit TabbedStackedLayout(const Config& config) : config_(config) {}
    
    void visit(BSPNode* root, const Rect& bounds, Display* display) override;
    
private:
    Config config_;
    
    std::vector<Window> collectWindows(BSPNode* root);
    void positionWindow(Window win, const Rect& rect, Display* display, bool is_focused);
    void renderTabBar(const std::vector<Window>& windows, size_t focused_idx, 
                      const Rect& bounds, Display* display);
};

struct WindowStats {
    Window window{None};
    
    int virtual_x{0};
    int virtual_y{0};
    unsigned int width{0};
    unsigned int height{0};
    
    int workspace{0};
    
    bool floating{false};
    bool fullscreen{false};
    bool hidden{false};
    bool focused{false};
    
    std::string window_class;
    std::string title;
    
    int z_index{0};
    
    std::chrono::steady_clock::time_point last_focus_time;
    
    int getRealX(int viewport_x) const { return virtual_x - viewport_x; }
    int getRealY(int viewport_y) const { return virtual_y - viewport_y; }
    
    bool isVisibleInViewport(int viewport_x, int viewport_y, 
                             unsigned int screen_width, unsigned int screen_height) const {
        int real_x = getRealX(viewport_x);
        int real_y = getRealY(viewport_y);
        
        return (real_x + static_cast<int>(width) > 0 && 
                real_x < static_cast<int>(screen_width) &&
                real_y + static_cast<int>(height) > 0 && 
                real_y < static_cast<int>(screen_height));
    }
    
    void clampToX11Limits() {
        constexpr int X11_MAX = 32767;
        constexpr int X11_MIN = -32768;
        
        virtual_x = std::clamp(virtual_x, X11_MIN, X11_MAX);
        virtual_y = std::clamp(virtual_y, X11_MIN, X11_MAX);
    }
};

class InfiniteCanvasLayout : public LayoutVisitor {
public:
    struct Config {
        int default_window_width = 800;
        int default_window_height = 600;
        int off_screen_x = -9000;       
        int off_screen_y = -9000;       
        int gap_size = 10;
        int border_width = 2;
        unsigned long focused_border_color = 0x89B4FA;  
        unsigned long unfocused_border_color = 0x45475A; 
        bool auto_pan_to_focus = true;  
        int pan_animation_ms = 200;     
    };
    
    InfiniteCanvasLayout() = default;
    explicit InfiniteCanvasLayout(const Config& config) : config_(config) {}
    
    void visit(BSPNode* root, const Rect& bounds, Display* display) override;
    
    std::pair<int, int> getViewport() const { return {viewport_x_, viewport_y_}; }
    
    void setViewport(int x, int y);
    
    void panViewport(int dx, int dy);
    
    void panToWindow(const WindowStats& stats, unsigned int screen_width, 
                     unsigned int screen_height);
    
    std::pair<int, int> calculateNewWindowPosition(int window_count);
    
private:
    Config config_;
    int viewport_x_{0};
    int viewport_y_{0};
    
    std::vector<Window> collectWindows(BSPNode* root);
    void positionWindow(Window win, const Rect& rect, Display* display, bool is_focused);
};

class LayoutEngine {
public:
    LayoutEngine();
    ~LayoutEngine() = default;
    
    LayoutEngine(const LayoutEngine&) = delete;
    LayoutEngine& operator=(const LayoutEngine&) = delete;
    
    void setDisplay(Display* display) { display_ = display; }
    
    void setRenderPipeline(RenderPipeline* pipeline) {
        render_pipeline_ = pipeline;
        
        for (auto& ws : workspaces_) {
            if (ws.layout) {
                ws.layout->setRenderPipeline(pipeline);
            }
        }
    }
    
    BSPNode* addWindow(Window window);
    
    Window removeWindow(Window window);
    
    void applyLayout(int workspace, const Rect& screen_bounds);
    
    void setLayout(int workspace, std::unique_ptr<LayoutVisitor> layout);
    
    BSPNode* getTree(int workspace);
    const BSPNode* getTree(int workspace) const;
    
    void focusWindow(Window window);
    
    void setFocus(Window window) { focusWindow(window); }
    
    Window getFocusedWindow() const;
    
    BSPNode* getFocusedNode() const { return focused_node_; }
    
    Window moveFocus(const std::string& direction);
    
    void swapFocused(const std::string& direction);
    
    bool swapWindows(Window window1, Window window2);
    
    void resizeFocused(double delta);
    
    bool resizeWindow(Window window, double delta);
    
    void toggleSplitDirection();
    
    void setCurrentWorkspace(int workspace);
    
    void ensureWorkspace(int workspace);
    
    int getCurrentWorkspace() const { return current_workspace_; }
    
    bool isEmpty() const;
    
    int getWindowCount() const;
    
    int getWindowCount(int workspace) const;
    
    void setFloatingWindows(const std::unordered_set<Window>& windows);
    
    void setDwindleMode(bool enabled) { dwindle_mode_ = enabled; }
    
    void setGapSize(int gap) { 
        gap_size_ = gap; 
        gap_config_.inner_gap = gap;
        std::cerr << "[DEBUG] LayoutEngine::setGapSize(" << gap << ") - gap_config_.inner_gap=" << gap_config_.inner_gap << std::endl;
        
    }
    
    int getGapSize() const { return gap_size_; }
    
    void setOuterGap(int gap) { 
        outer_gap_ = gap;
        gap_config_.outer_gap = gap;
        std::cerr << "[DEBUG] LayoutEngine::setOuterGap(" << gap << ") - gap_config_.outer_gap=" << gap_config_.outer_gap << std::endl;
    }
    
    void setEdgeGaps(int top, int bottom, int left, int right) {
        top_gap_ = top;
        bottom_gap_ = bottom;
        left_gap_ = left;
        right_gap_ = right;
        gap_config_.top_gap = top;
        gap_config_.bottom_gap = bottom;
        gap_config_.left_gap = left;
        gap_config_.right_gap = right;
        std::cerr << "[DEBUG] LayoutEngine::setEdgeGaps(top=" << top << ", bottom=" << bottom 
                  << ", left=" << left << ", right=" << right << ")" << std::endl;
    }
    
    void setBorderWidth(int width) { border_width_ = width; }
    
    void setBorderColors(unsigned long focused_color, unsigned long unfocused_color);
    
    unsigned long getFocusedBorderColor() const { return focused_border_color_; }
    
    unsigned long getUnfocusedBorderColor() const { return unfocused_border_color_; }
    
    void updateBorderColors();
    
    void setResizeBorderHighlight(bool active, Window window = None);
    
    std::vector<Window> getWorkspaceWindows(int workspace) const;
    
    std::string cycleLayout(bool forward = true);
    
    std::optional<LayoutMode> getCurrentLayoutMode(int workspace = -1) const;
    
    void setLayoutMode(int workspace, LayoutMode mode);
    
    WindowStats* getWindowStats(Window window);
    const WindowStats* getWindowStats(Window window) const;
    
    void updateWindowStats(Window window, const WindowStats& stats);
    
    std::pair<int, int> getViewport() const { return {viewport_x_, viewport_y_}; }
    
    void setViewport(int x, int y);
    
    void panViewport(int dx, int dy);
    
    void panToFocusedWindow(unsigned int screen_width, unsigned int screen_height);
    
    bool warpPointerToWindow(Window window);
    
    bool isWarping() const { return is_warping_; }
    
    void clearWarpingFlag() { is_warping_ = false; }
    
    bool wouldViolateMinSize(const Rect& bounds, SplitType split_type) const;
    
    bool canSplitWithoutViolation(BSPNode* node) const;
    
    int expandCanvasForSplit(Rect& bounds, SplitType split_type);
    
    void setFocusWrapMode(FocusWrapMode mode) { focus_wrap_mode_ = mode; }
    
    FocusWrapMode getFocusWrapMode() const { return focus_wrap_mode_; }
    
    Window focusNextLeafDFS();
    
    Window focusPrevLeafDFS();
    
    std::vector<BSPNode*> collectLeavesDFS(BSPNode* root = nullptr) const;
    
    void teleportToWorkspace(int workspace_id);
    
    const WorkspaceNode* getWorkspaceNode(int workspace_id) const;
    
    int createWorkspaceAt(int64_t origin_x, int64_t origin_y);
    
    Camera& getCamera() { return camera_; }
    const Camera& getCamera() const { return camera_; }
    
    SpatialGrid& getSpatialGrid() { return spatial_grid_; }
    const SpatialGrid& getSpatialGrid() const { return spatial_grid_; }
    
    GapConfig& getGapConfig() { return gap_config_; }
    const GapConfig& getGapConfig() const { return gap_config_; }
    
    void updateSpatialGrid();

private:
    struct WorkspaceData {
        std::unique_ptr<BSPNode> tree;
        std::unique_ptr<LayoutVisitor> layout;
    };
    
    std::vector<WorkspaceData> workspaces_;
    int current_workspace_{0};
    BSPNode* focused_node_{nullptr};
    Display* display_{nullptr};
    RenderPipeline* render_pipeline_{nullptr};  
    
    bool dwindle_mode_{true};
    int split_counter_{0};  
    double default_ratio_{0.5};
    int gap_size_{10};
    int outer_gap_{10};
    int top_gap_{-1};
    int bottom_gap_{-1};
    int left_gap_{-1};
    int right_gap_{-1};
    int border_width_{2};
    unsigned long focused_border_color_{0x89B4FA};  
    unsigned long unfocused_border_color_{0x45475A}; 
    
    bool resize_highlight_active_{false};
    Window resize_highlight_window_{None};
    unsigned long resize_border_color_{0x0000FF};  
    
    std::unordered_map<Window, Rect> window_bounds_;
    Rect screen_bounds_;
    
    std::unordered_map<Window, WindowStats> window_stats_;
    
    std::unordered_set<Window> floating_windows_;
    
    int viewport_x_{0};
    int viewport_y_{0};
    
    bool is_warping_{false};
    
    Camera camera_;
    
    SpatialGrid spatial_grid_;
    
    GapConfig gap_config_;
    
    FocusWrapMode focus_wrap_mode_{FocusWrapMode::Traditional};
    
    mutable std::vector<BSPNode*> cached_leaves_;
    mutable bool leaves_cache_valid_{false};
    
    std::vector<WorkspaceNode> workspace_nodes_;
    
    BSPNode* findNode(BSPNode* root, Window window);
    BSPNode* findParentNode(BSPNode* root, BSPNode* target);
    SplitType determineSplitType() const;
    
    void splitLeaf(BSPNode* leaf, Window new_window);
    
    BSPNode* removeLeafAndPromoteSibling(BSPNode* leaf);
    
    BSPNode* findSpatialNeighbor(BSPNode* from, const std::string& direction);
    
    void updateWindowBounds(BSPNode* node, const Rect& bounds);
    
    void calculateNodeBounds(BSPNode* node, const Rect& bounds,
                             std::unordered_map<Window, Rect>& bounds_map) const;
    
    void invalidateLeavesCache() { leaves_cache_valid_ = false; }
    
    void rebuildLeavesCache() const;
    
    void collectLeavesDFSHelper(BSPNode* node, std::vector<BSPNode*>& leaves) const;
};

} 