/**
 * @file LayoutProvider.hpp
 * @brief Layout Provider Interface for Custom Layout Implementations
 * 
 * Provides an interface for implementing custom tiling layouts:
 * - Built-in layouts (BSP, Horizontal, Vertical, Grid)
 * - Custom layout plugins
 * - Lua-scripted layouts
 * - Dynamic layout switching
 * 
 * Phase 11 of Enhanced TWM Features
 */

#ifndef LAYOUTPROVIDER_HPP
#define LAYOUTPROVIDER_HPP

#include <X11/Xlib.h>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <functional>

namespace pblank {

class GapConfig;

struct LayoutRect {
    int x{0};
    int y{0};
    int width{0};
    int height{0};
    
    bool operator==(const LayoutRect& other) const {
        return x == other.x && y == other.y && 
               width == other.width && height == other.height;
    }
    
    bool operator!=(const LayoutRect& other) const {
        return !(*this == other);
    }
    
    bool isValid() const {
        return width > 0 && height > 0;
    }
    
    int area() const {
        return width * height;
    }
    
    bool contains(int px, int py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
    
    LayoutRect withGaps(int outer_gap, int inner_gap) const;
};

struct LayoutNode {
    enum class SplitType {
        Horizontal,
        Vertical,
        NoSplit
    };
    
    Window window{0};               
    LayoutRect rect;                
    SplitType split{SplitType::NoSplit};
    double ratio{0.5};              
    std::shared_ptr<LayoutNode> first;
    std::shared_ptr<LayoutNode> second;
    std::shared_ptr<LayoutNode> parent;
    
    bool isLeaf() const { return window != 0; }
    bool isInternal() const { return window == 0 && (first || second); }
};

struct LayoutContext {
    Display* display{nullptr};
    int workspace_id{0};
    int monitor_id{0};
    LayoutRect available_area;
    const GapConfig* gaps{nullptr};
    std::vector<Window> windows;
    Window focused_window{0};
};

struct LayoutResult {
    std::vector<std::pair<Window, LayoutRect>> placements;
    bool success{true};
    std::string error_message;
};

class ILayoutProvider {
public:
    virtual ~ILayoutProvider() = default;
    
    virtual std::string getName() const = 0;
    
    virtual std::string getDescription() const {
        return "";
    }
    
    virtual LayoutResult calculate(const LayoutContext& context) = 0;
    
    virtual void onWindowAdded(Window window, const LayoutContext& context) {
        (void)window; (void)context;
    }
    
    virtual void onWindowRemoved(Window window, const LayoutContext& context) {
        (void)window; (void)context;
    }
    
    virtual void onFocusChanged(Window window, const LayoutContext& context) {
        (void)window; (void)context;
    }
    
    virtual bool supportsRotation() const { return false; }
    
    virtual void rotate(bool clockwise = true) {
        (void)clockwise;
    }
    
    virtual bool supportsFlip() const { return false; }
    
    virtual void flip(bool horizontal = true) {
        (void)horizontal;
    }
    
    virtual Window getNextWindow(Window current, const LayoutContext& context) = 0;
    
    virtual Window getPrevWindow(Window current, const LayoutContext& context) = 0;
    
    virtual bool swapWindows(Window window1, Window window2, 
                            const LayoutContext& context) = 0;
    
    virtual bool moveWindow(Window window, int direction, 
                           const LayoutContext& context) {
        (void)window; (void)direction; (void)context;
        return false;
    }
    
    virtual bool resizeWindow(Window window, int delta_x, int delta_y,
                             const LayoutContext& context) {
        (void)window; (void)delta_x; (void)delta_y; (void)context;
        return false;
    }
    
    virtual void setSplitRatio(Window window, double ratio) {
        (void)window; (void)ratio;
    }
    
    virtual double getSplitRatio(Window window) const {
        (void)window;
        return 0.5;
    }
    
    virtual bool canHandleWindowCount(size_t count) const {
        (void)count;
        return true;
    }
    
    virtual size_t getPreferredWindowCount() const {
        return 0;
    }
    
    virtual std::unique_ptr<ILayoutProvider> clone() const = 0;
};

class BSPLayoutProvider : public ILayoutProvider {
public:
    std::string getName() const override { return "bsp"; }
    std::string getDescription() const override { 
        return "Binary Space Partitioning - recursive splits"; 
    }
    
    LayoutResult calculate(const LayoutContext& context) override;
    
    void onWindowAdded(Window window, const LayoutContext& context) override;
    void onWindowRemoved(Window window, const LayoutContext& context) override;
    
    bool supportsRotation() const override { return true; }
    void rotate(bool clockwise = true) override;
    
    bool supportsFlip() const override { return true; }
    void flip(bool horizontal = true) override;
    
    Window getNextWindow(Window current, const LayoutContext& context) override;
    Window getPrevWindow(Window current, const LayoutContext& context) override;
    bool swapWindows(Window window1, Window window2, 
                    const LayoutContext& context) override;
    bool moveWindow(Window window, int direction, 
                   const LayoutContext& context) override;
    bool resizeWindow(Window window, int delta_x, int delta_y,
                     const LayoutContext& context) override;
    
    void setSplitRatio(Window window, double ratio) override;
    double getSplitRatio(Window window) const override;
    
    std::unique_ptr<ILayoutProvider> clone() const override;
    
private:
    std::shared_ptr<LayoutNode> root_;
    std::unordered_map<Window, std::shared_ptr<LayoutNode>> window_nodes_;
    bool split_horizontal_{true};  
    
    void buildTree(const std::vector<Window>& windows, 
                   const LayoutRect& rect,
                   std::shared_ptr<LayoutNode> node);
    void collectWindows(std::shared_ptr<LayoutNode> node, 
                       std::vector<Window>& windows) const;
    LayoutNode* findNode(Window window) const;
};

class HorizontalLayoutProvider : public ILayoutProvider {
public:
    std::string getName() const override { return "horizontal"; }
    std::string getDescription() const override { 
        return "Horizontal stack - windows side by side"; 
    }
    
    LayoutResult calculate(const LayoutContext& context) override;
    
    Window getNextWindow(Window current, const LayoutContext& context) override;
    Window getPrevWindow(Window current, const LayoutContext& context) override;
    bool swapWindows(Window window1, Window window2, 
                    const LayoutContext& context) override;
    bool resizeWindow(Window window, int delta_x, int delta_y,
                     const LayoutContext& context) override;
    
    void setSplitRatio(Window window, double ratio) override;
    double getSplitRatio(Window window) const override;
    
    std::unique_ptr<ILayoutProvider> clone() const override;
    
private:
    std::unordered_map<Window, double> window_ratios_;
};

class VerticalLayoutProvider : public ILayoutProvider {
public:
    std::string getName() const override { return "vertical"; }
    std::string getDescription() const override { 
        return "Vertical stack - windows stacked top to bottom"; 
    }
    
    LayoutResult calculate(const LayoutContext& context) override;
    
    Window getNextWindow(Window current, const LayoutContext& context) override;
    Window getPrevWindow(Window current, const LayoutContext& context) override;
    bool swapWindows(Window window1, Window window2, 
                    const LayoutContext& context) override;
    bool resizeWindow(Window window, int delta_x, int delta_y,
                     const LayoutContext& context) override;
    
    void setSplitRatio(Window window, double ratio) override;
    double getSplitRatio(Window window) const override;
    
    std::unique_ptr<ILayoutProvider> clone() const override;
    
private:
    std::unordered_map<Window, double> window_ratios_;
};

class GridLayoutProvider : public ILayoutProvider {
public:
    std::string getName() const override { return "grid"; }
    std::string getDescription() const override { 
        return "Grid - windows arranged in a grid"; 
    }
    
    LayoutResult calculate(const LayoutContext& context) override;
    
    Window getNextWindow(Window current, const LayoutContext& context) override;
    Window getPrevWindow(Window current, const LayoutContext& context) override;
    bool swapWindows(Window window1, Window window2, 
                    const LayoutContext& context) override;
    
    std::unique_ptr<ILayoutProvider> clone() const override;
    
private:
    static std::pair<int, int> getGridDimensions(size_t count);
};

class LayoutProviderFactory {
public:
    
    static LayoutProviderFactory& instance();
    
    void registerLayout(const std::string& name,
                       std::function<std::unique_ptr<ILayoutProvider>()> creator);
    
    std::unique_ptr<ILayoutProvider> create(const std::string& name) const;
    
    std::vector<std::string> getAvailableLayouts() const;
    
    bool hasLayout(const std::string& name) const;
    
private:
    LayoutProviderFactory();
    LayoutProviderFactory(const LayoutProviderFactory&) = delete;
    LayoutProviderFactory& operator=(const LayoutProviderFactory&) = delete;
    
    std::unordered_map<std::string, 
                       std::function<std::unique_ptr<ILayoutProvider>()>> creators_;
};

} 

#endif 
