/**
 * @file LayoutProvider.cpp
 * @brief Layout Provider Interface implementation
 * 
 * Phase 11 of Enhanced TWM Features
 */

#include "pointblank/layout/LayoutProvider.hpp"
#include "pointblank/utils/GapConfig.hpp"
#include <algorithm>
#include <cmath>

namespace pblank {

LayoutRect LayoutRect::withGaps(int outer_gap, int inner_gap) const {
    LayoutRect result = *this;
    result.x += outer_gap;
    result.y += outer_gap;
    result.width -= 2 * outer_gap;
    result.height -= 2 * outer_gap;
    (void)inner_gap; 
    return result;
}

LayoutResult BSPLayoutProvider::calculate(const LayoutContext& context) {
    LayoutResult result;
    
    if (context.windows.empty()) {
        return result;
    }
    
    
    if (!root_ || window_nodes_.size() != context.windows.size()) {
        root_ = std::make_shared<LayoutNode>();
        window_nodes_.clear();
        buildTree(context.windows, context.available_area, root_);
    }
    
    
    std::function<void(std::shared_ptr<LayoutNode>)> collect;
    collect = [&](std::shared_ptr<LayoutNode> node) {
        if (!node) return;
        
        if (node->isLeaf()) {
            result.placements.push_back({node->window, node->rect});
        } else {
            collect(node->first);
            collect(node->second);
        }
    };
    
    collect(root_);
    return result;
}

void BSPLayoutProvider::buildTree(const std::vector<Window>& windows,
                                   const LayoutRect& rect,
                                   std::shared_ptr<LayoutNode> node) {
    node->rect = rect;
    
    if (windows.size() == 1) {
        node->window = windows[0];
        window_nodes_[windows[0]] = node;
        return;
    }
    
    
    size_t mid = windows.size() / 2;
    std::vector<Window> first_windows(windows.begin(), windows.begin() + mid);
    std::vector<Window> second_windows(windows.begin() + mid, windows.end());
    
    LayoutRect first_rect = rect;
    LayoutRect second_rect = rect;
    
    node->split = split_horizontal_ ? LayoutNode::SplitType::Horizontal 
                                    : LayoutNode::SplitType::Vertical;
    
    if (split_horizontal_) {
        int split_pos = static_cast<int>(rect.width * node->ratio);
        first_rect.width = split_pos;
        second_rect.x = rect.x + split_pos;
        second_rect.width = rect.width - split_pos;
    } else {
        int split_pos = static_cast<int>(rect.height * node->ratio);
        first_rect.height = split_pos;
        second_rect.y = rect.y + split_pos;
        second_rect.height = rect.height - split_pos;
    }
    
    node->first = std::make_shared<LayoutNode>();
    node->first->parent = node;
    node->first->ratio = 0.5;
    
    node->second = std::make_shared<LayoutNode>();
    node->second->parent = node;
    node->second->ratio = 0.5;
    
    
    split_horizontal_ = !split_horizontal_;
    buildTree(first_windows, first_rect, node->first);
    buildTree(second_windows, second_rect, node->second);
    split_horizontal_ = !split_horizontal_;
}

void BSPLayoutProvider::collectWindows(std::shared_ptr<LayoutNode> node,
                                        std::vector<Window>& windows) const {
    if (!node) return;
    
    if (node->isLeaf()) {
        windows.push_back(node->window);
    } else {
        collectWindows(node->first, windows);
        collectWindows(node->second, windows);
    }
}

LayoutNode* BSPLayoutProvider::findNode(Window window) const {
    auto it = window_nodes_.find(window);
    return it != window_nodes_.end() ? it->second.get() : nullptr;
}

void BSPLayoutProvider::onWindowAdded(Window window, const LayoutContext& context) {
    (void)window; (void)context;
    
    root_.reset();
    window_nodes_.clear();
}

void BSPLayoutProvider::onWindowRemoved(Window window, const LayoutContext& context) {
    (void)context;
    window_nodes_.erase(window);
    root_.reset();
}

void BSPLayoutProvider::rotate(bool clockwise) {
    (void)clockwise;
    split_horizontal_ = !split_horizontal_;
    root_.reset();
    window_nodes_.clear();
}

void BSPLayoutProvider::flip(bool horizontal) {
    (void)horizontal;
    
    root_.reset();
    window_nodes_.clear();
}

Window BSPLayoutProvider::getNextWindow(Window current, const LayoutContext& context) {
    if (context.windows.empty()) return 0;
    
    auto it = std::find(context.windows.begin(), context.windows.end(), current);
    if (it == context.windows.end()) {
        return context.windows.empty() ? 0 : context.windows.front();
    }
    
    ++it;
    return it != context.windows.end() ? *it : context.windows.front();
}

Window BSPLayoutProvider::getPrevWindow(Window current, const LayoutContext& context) {
    if (context.windows.empty()) return 0;
    
    auto it = std::find(context.windows.begin(), context.windows.end(), current);
    if (it == context.windows.end()) {
        return context.windows.empty() ? 0 : context.windows.back();
    }
    
    if (it == context.windows.begin()) {
        return context.windows.back();
    }
    return *(--it);
}

bool BSPLayoutProvider::swapWindows(Window window1, Window window2,
                                    const LayoutContext& context) {
    (void)context;
    
    auto node1 = findNode(window1);
    auto node2 = findNode(window2);
    
    if (!node1 || !node2) return false;
    
    std::swap(node1->window, node2->window);
    window_nodes_[window1] = std::shared_ptr<LayoutNode>(node2, [](LayoutNode*){});
    window_nodes_[window2] = std::shared_ptr<LayoutNode>(node1, [](LayoutNode*){});
    
    return true;
}

bool BSPLayoutProvider::moveWindow(Window window, int direction,
                                   const LayoutContext& context) {
    Window target = direction > 0 ? 
        getNextWindow(window, context) : getPrevWindow(window, context);
    
    if (target == 0 || target == window) return false;
    
    return swapWindows(window, target, context);
}

bool BSPLayoutProvider::resizeWindow(Window window, int delta_x, int delta_y,
                                     const LayoutContext& context) {
    (void)context;
    
    LayoutNode* node = findNode(window);
    if (!node || !node->parent) return false;
    
    
    double delta = 0.0;
    if (node->parent->split == LayoutNode::SplitType::Horizontal) {
        delta = static_cast<double>(delta_x) / node->parent->rect.width;
    } else {
        delta = static_cast<double>(delta_y) / node->parent->rect.height;
    }
    
    node->parent->ratio = std::clamp(node->parent->ratio + delta, 0.1, 0.9);
    return true;
}

void BSPLayoutProvider::setSplitRatio(Window window, double ratio) {
    LayoutNode* node = findNode(window);
    if (node && node->parent) {
        node->parent->ratio = std::clamp(ratio, 0.1, 0.9);
    }
}

double BSPLayoutProvider::getSplitRatio(Window window) const {
    LayoutNode* node = findNode(window);
    if (node && node->parent) {
        return node->parent->ratio;
    }
    return 0.5;
}

std::unique_ptr<ILayoutProvider> BSPLayoutProvider::clone() const {
    return std::make_unique<BSPLayoutProvider>(*this);
}

LayoutResult HorizontalLayoutProvider::calculate(const LayoutContext& context) {
    LayoutResult result;
    
    if (context.windows.empty()) {
        return result;
    }
    
    size_t count = context.windows.size();
    int total_width = context.available_area.width;
    int default_width = total_width / static_cast<int>(count);
    
    int x = context.available_area.x;
    
    for (size_t i = 0; i < count; ++i) {
        Window win = context.windows[i];
        
        
        int width = default_width;
        auto ratio_it = window_ratios_.find(win);
        if (ratio_it != window_ratios_.end()) {
            width = static_cast<int>(total_width * ratio_it->second);
        }
        
        
        if (i == count - 1) {
            width = context.available_area.x + total_width - x;
        }
        
        LayoutRect rect;
        rect.x = x;
        rect.y = context.available_area.y;
        rect.width = width;
        rect.height = context.available_area.height;
        
        result.placements.push_back({win, rect});
        x += width;
    }
    
    return result;
}

Window HorizontalLayoutProvider::getNextWindow(Window current, 
                                               const LayoutContext& context) {
    if (context.windows.empty()) return 0;
    
    auto it = std::find(context.windows.begin(), context.windows.end(), current);
    if (it == context.windows.end()) {
        return context.windows.front();
    }
    
    ++it;
    return it != context.windows.end() ? *it : context.windows.front();
}

Window HorizontalLayoutProvider::getPrevWindow(Window current,
                                                const LayoutContext& context) {
    if (context.windows.empty()) return 0;
    
    auto it = std::find(context.windows.begin(), context.windows.end(), current);
    if (it == context.windows.end()) {
        return context.windows.back();
    }
    
    if (it == context.windows.begin()) {
        return context.windows.back();
    }
    return *(--it);
}

bool HorizontalLayoutProvider::swapWindows(Window window1, Window window2,
                                           const LayoutContext& context) {
    (void)context;
    
    auto it1 = window_ratios_.find(window1);
    auto it2 = window_ratios_.find(window2);
    
    if (it1 != window_ratios_.end() && it2 != window_ratios_.end()) {
        std::swap(it1->second, it2->second);
        return true;
    }
    return false;
}

bool HorizontalLayoutProvider::resizeWindow(Window window, int delta_x, int delta_y,
                                            const LayoutContext& context) {
    (void)delta_y; (void)context;
    
    auto it = window_ratios_.find(window);
    double current_ratio = it != window_ratios_.end() ? it->second : 
                          (1.0 / context.windows.size());
    
    double delta = static_cast<double>(delta_x) / context.available_area.width;
    double new_ratio = std::clamp(current_ratio + delta, 0.1, 0.9);
    
    window_ratios_[window] = new_ratio;
    return true;
}

void HorizontalLayoutProvider::setSplitRatio(Window window, double ratio) {
    window_ratios_[window] = std::clamp(ratio, 0.1, 0.9);
}

double HorizontalLayoutProvider::getSplitRatio(Window window) const {
    auto it = window_ratios_.find(window);
    return it != window_ratios_.end() ? it->second : 0.5;
}

std::unique_ptr<ILayoutProvider> HorizontalLayoutProvider::clone() const {
    return std::make_unique<HorizontalLayoutProvider>(*this);
}

LayoutResult VerticalLayoutProvider::calculate(const LayoutContext& context) {
    LayoutResult result;
    
    if (context.windows.empty()) {
        return result;
    }
    
    size_t count = context.windows.size();
    int total_height = context.available_area.height;
    int default_height = total_height / static_cast<int>(count);
    
    int y = context.available_area.y;
    
    for (size_t i = 0; i < count; ++i) {
        Window win = context.windows[i];
        
        
        int height = default_height;
        auto ratio_it = window_ratios_.find(win);
        if (ratio_it != window_ratios_.end()) {
            height = static_cast<int>(total_height * ratio_it->second);
        }
        
        
        if (i == count - 1) {
            height = context.available_area.y + total_height - y;
        }
        
        LayoutRect rect;
        rect.x = context.available_area.x;
        rect.y = y;
        rect.width = context.available_area.width;
        rect.height = height;
        
        result.placements.push_back({win, rect});
        y += height;
    }
    
    return result;
}

Window VerticalLayoutProvider::getNextWindow(Window current,
                                             const LayoutContext& context) {
    if (context.windows.empty()) return 0;
    
    auto it = std::find(context.windows.begin(), context.windows.end(), current);
    if (it == context.windows.end()) {
        return context.windows.front();
    }
    
    ++it;
    return it != context.windows.end() ? *it : context.windows.front();
}

Window VerticalLayoutProvider::getPrevWindow(Window current,
                                              const LayoutContext& context) {
    if (context.windows.empty()) return 0;
    
    auto it = std::find(context.windows.begin(), context.windows.end(), current);
    if (it == context.windows.end()) {
        return context.windows.back();
    }
    
    if (it == context.windows.begin()) {
        return context.windows.back();
    }
    return *(--it);
}

bool VerticalLayoutProvider::swapWindows(Window window1, Window window2,
                                         const LayoutContext& context) {
    (void)context;
    
    auto it1 = window_ratios_.find(window1);
    auto it2 = window_ratios_.find(window2);
    
    if (it1 != window_ratios_.end() && it2 != window_ratios_.end()) {
        std::swap(it1->second, it2->second);
        return true;
    }
    return false;
}

bool VerticalLayoutProvider::resizeWindow(Window window, int delta_x, int delta_y,
                                          const LayoutContext& context) {
    (void)delta_x; (void)context;
    
    auto it = window_ratios_.find(window);
    double current_ratio = it != window_ratios_.end() ? it->second : 
                          (1.0 / context.windows.size());
    
    double delta = static_cast<double>(delta_y) / context.available_area.height;
    double new_ratio = std::clamp(current_ratio + delta, 0.1, 0.9);
    
    window_ratios_[window] = new_ratio;
    return true;
}

void VerticalLayoutProvider::setSplitRatio(Window window, double ratio) {
    window_ratios_[window] = std::clamp(ratio, 0.1, 0.9);
}

double VerticalLayoutProvider::getSplitRatio(Window window) const {
    auto it = window_ratios_.find(window);
    return it != window_ratios_.end() ? it->second : 0.5;
}

std::unique_ptr<ILayoutProvider> VerticalLayoutProvider::clone() const {
    return std::make_unique<VerticalLayoutProvider>(*this);
}

std::pair<int, int> GridLayoutProvider::getGridDimensions(size_t count) {
    if (count == 0) return {0, 0};
    if (count == 1) return {1, 1};
    if (count == 2) return {2, 1};
    if (count <= 4) return {2, 2};
    if (count <= 6) return {3, 2};
    if (count <= 9) return {3, 3};
    if (count <= 12) return {4, 3};
    
    int cols = static_cast<int>(std::ceil(std::sqrt(count)));
    int rows = static_cast<int>(std::ceil(static_cast<double>(count) / cols));
    return {cols, rows};
}

LayoutResult GridLayoutProvider::calculate(const LayoutContext& context) {
    LayoutResult result;
    
    if (context.windows.empty()) {
        return result;
    }
    
    auto [cols, rows] = getGridDimensions(context.windows.size());
    
    int cell_width = context.available_area.width / cols;
    int cell_height = context.available_area.height / rows;
    
    size_t index = 0;
    for (int row = 0; row < rows && index < context.windows.size(); ++row) {
        for (int col = 0; col < cols && index < context.windows.size(); ++col) {
            LayoutRect rect;
            rect.x = context.available_area.x + col * cell_width;
            rect.y = context.available_area.y + row * cell_height;
            rect.width = cell_width;
            rect.height = cell_height;
            
            result.placements.push_back({context.windows[index], rect});
            ++index;
        }
    }
    
    return result;
}

Window GridLayoutProvider::getNextWindow(Window current,
                                         const LayoutContext& context) {
    if (context.windows.empty()) return 0;
    
    auto it = std::find(context.windows.begin(), context.windows.end(), current);
    if (it == context.windows.end()) {
        return context.windows.front();
    }
    
    ++it;
    return it != context.windows.end() ? *it : context.windows.front();
}

Window GridLayoutProvider::getPrevWindow(Window current,
                                          const LayoutContext& context) {
    if (context.windows.empty()) return 0;
    
    auto it = std::find(context.windows.begin(), context.windows.end(), current);
    if (it == context.windows.end()) {
        return context.windows.back();
    }
    
    if (it == context.windows.begin()) {
        return context.windows.back();
    }
    return *(--it);
}

bool GridLayoutProvider::swapWindows(Window window1, Window window2,
                                     const LayoutContext& context) {
    (void)context; (void)window1; (void)window2;
    
    return false;
}

std::unique_ptr<ILayoutProvider> GridLayoutProvider::clone() const {
    return std::make_unique<GridLayoutProvider>(*this);
}

LayoutProviderFactory::LayoutProviderFactory() {
    
    registerLayout("bsp", []() {
        return std::make_unique<BSPLayoutProvider>();
    });
    
    registerLayout("horizontal", []() {
        return std::make_unique<HorizontalLayoutProvider>();
    });
    
    registerLayout("vertical", []() {
        return std::make_unique<VerticalLayoutProvider>();
    });
    
    registerLayout("grid", []() {
        return std::make_unique<GridLayoutProvider>();
    });
}

LayoutProviderFactory& LayoutProviderFactory::instance() {
    static LayoutProviderFactory factory;
    return factory;
}

void LayoutProviderFactory::registerLayout(
    const std::string& name,
    std::function<std::unique_ptr<ILayoutProvider>()> creator
) {
    creators_[name] = std::move(creator);
}

std::unique_ptr<ILayoutProvider> LayoutProviderFactory::create(
    const std::string& name
) const {
    auto it = creators_.find(name);
    if (it != creators_.end()) {
        return it->second();
    }
    return nullptr;
}

std::vector<std::string> LayoutProviderFactory::getAvailableLayouts() const {
    std::vector<std::string> names;
    for (const auto& pair : creators_) {
        names.push_back(pair.first);
    }
    return names;
}

bool LayoutProviderFactory::hasLayout(const std::string& name) const {
    return creators_.find(name) != creators_.end();
}

} 
