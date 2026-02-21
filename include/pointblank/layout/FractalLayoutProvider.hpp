/**
 * @file FractalLayoutProvider.hpp
 * @brief Fractal Layout Provider - recursive window tiling
 * 
 * Implements fractal tiling patterns based on the Point:Blank philosophy:
 * "If there is a limit, we break through it. If there is no limit, 
 * we become the limit. Repeat until the resolution fails."
 * 
 * Supported fractal patterns:
 * - Sierpinski: Recursive triangle subdivision
 * - Fibonacci: Golden ratio spiral tiling  
 * - Cantor: Middle-third elimination pattern
 * - Vicsek: Cross-shaped fractal subdivision
 * - Tree: Binary tree fractal structure
 */

#ifndef FRACTAL_LAYOUT_PROVIDER_HPP
#define FRACTAL_LAYOUT_PROVIDER_HPP

#include "pointblank/layout/LayoutProvider.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cmath>

namespace pblank {

enum class FractalPattern {
    Sierpinski,   
    Fibonacci,    
    Cantor,       
    Vicsek,       
    Tree,         
    Spiral        
};

struct FractalConfig {
    FractalPattern pattern{FractalPattern::Sierpinski};
    int max_depth{4};                    
    int min_window_width{100};           
    int min_window_height{80};          
    double split_ratio{0.5};            
    bool preserve_aspect{true};         
    bool adaptive_depth{true};           
    
    double golden_ratio{1.618033988749}; 
    double fractal_dimension{0.0};       
};

class FractalLayoutProvider : public ILayoutProvider {
public:
    FractalLayoutProvider();
    explicit FractalLayoutProvider(const FractalConfig& config);
    
    std::string getName() const override { return "fractal"; }
    std::string getDescription() const override { 
        return "Fractal - recursive subdivision patterns"; 
    }
    
    LayoutResult calculate(const LayoutContext& context) override;
    
    bool supportsRotation() const override { return true; }
    void rotate(bool clockwise = true) override;
    
    bool supportsFlip() const override { return true; }
    void flip(bool horizontal = true) override;
    
    Window getNextWindow(Window current, const LayoutContext& context) override;
    Window getPrevWindow(Window current, const LayoutContext& context) override;
    bool swapWindows(Window window1, Window window2, 
                    const LayoutContext& context) override;
    bool resizeWindow(Window window, int delta_x, int delta_y,
                     const LayoutContext& context) override;
    
    void setSplitRatio(Window window, double ratio) override;
    double getSplitRatio(Window window) const override;
    
    std::unique_ptr<ILayoutProvider> clone() const override;
    
    void setPattern(FractalPattern pattern);
    FractalPattern getPattern() const { return config_.pattern; }
    
    void setMaxDepth(int depth);
    int getMaxDepth() const { return config_.max_depth; }
    
    void setMinWindowSize(int width, int height);
    int getMinWindowWidth() const { return config_.min_window_width; }
    int getMinWindowHeight() const { return config_.min_window_height; }
    
    void setConfig(const FractalConfig& config);
    const FractalConfig& getConfig() const { return config_; }
    
    static std::vector<std::string> getAvailablePatterns();
    
    static FractalPattern patternFromName(const std::string& name);

private:
    FractalConfig config_;
    std::unordered_map<Window, double> window_ratios_;
    std::vector<Window> window_order_;
    int rotation_state_{0};  
    bool flip_horizontal_{false};
    
    int calculateEffectiveDepth(size_t window_count) const;
    
    LayoutResult calculateSierpinski(const LayoutContext& context);
    LayoutResult calculateFibonacci(const LayoutContext& context);
    LayoutResult calculateCantor(const LayoutContext& context);
    LayoutResult calculateVicsek(const LayoutContext& context);
    LayoutResult calculateTree(const LayoutContext& context);
    LayoutResult calculateSpiral(const LayoutContext& context);
    
    void sierpinskiRecursive(const LayoutRect& rect, int depth,
                            std::vector<LayoutRect>& output);
    void cantorRecursive(const LayoutRect& rect, int depth,
                        std::vector<LayoutRect>& output);
    void vicsekRecursive(const LayoutRect& rect, int depth,
                        std::vector<LayoutRect>& output);
    void treeRecursive(const LayoutRect& rect, int depth, bool horizontal,
                      std::vector<LayoutRect>& output);
    
    LayoutRect applyConstraints(const LayoutRect& rect) const;
    
    bool isValidSize(const LayoutRect& rect) const;
    
    LayoutResult distributeWindows(const LayoutContext& context,
                                  const std::vector<LayoutRect>& fractalRects);
};

class SierpinskiLayoutProvider : public FractalLayoutProvider {
public:
    SierpinskiLayoutProvider() : FractalLayoutProvider() {
        FractalConfig cfg = getConfig();
        cfg.pattern = FractalPattern::Sierpinski;
        setConfig(cfg);
    }
    std::string getName() const override { return "sierpinski"; }
    std::string getDescription() const override { 
        return "Sierpinski carpet - recursive square subdivision"; 
    }
    std::unique_ptr<ILayoutProvider> clone() const override;
};

class FibonacciLayoutProvider : public FractalLayoutProvider {
public:
    FibonacciLayoutProvider() : FractalLayoutProvider() {
        FractalConfig cfg = getConfig();
        cfg.pattern = FractalPattern::Fibonacci;
        setConfig(cfg);
    }
    std::string getName() const override { return "fibonacci"; }
    std::string getDescription() const override { 
        return "Fibonacci spiral - golden ratio window arrangement"; 
    }
    std::unique_ptr<ILayoutProvider> clone() const override;
};

class CantorLayoutProvider : public FractalLayoutProvider {
public:
    CantorLayoutProvider() : FractalLayoutProvider() {
        FractalConfig cfg = getConfig();
        cfg.pattern = FractalPattern::Cantor;
        setConfig(cfg);
    }
    std::string getName() const override { return "cantorfractal"; }
    std::string getDescription() const override { 
        return "Cantor dust - middle-third elimination pattern"; 
    }
    std::unique_ptr<ILayoutProvider> clone() const override;
};

class VicsekLayoutProvider : public FractalLayoutProvider {
public:
    VicsekLayoutProvider() : FractalLayoutProvider() {
        FractalConfig cfg = getConfig();
        cfg.pattern = FractalPattern::Vicsek;
        setConfig(cfg);
    }
    std::string getName() const override { return "vicsek"; }
    std::string getDescription() const override { 
        return "Vicsek fractal - cross-shaped subdivision"; 
    }
    std::unique_ptr<ILayoutProvider> clone() const override;
};

class TreeLayoutProvider : public FractalLayoutProvider {
public:
    TreeLayoutProvider() : FractalLayoutProvider() {
        FractalConfig cfg = getConfig();
        cfg.pattern = FractalPattern::Tree;
        setConfig(cfg);
    }
    std::string getName() const override { return "treefractal"; }
    std::string getDescription() const override { 
        return "Tree fractal - binary tree window structure"; 
    }
    std::unique_ptr<ILayoutProvider> clone() const override;
};

class SpiralLayoutProvider : public FractalLayoutProvider {
public:
    SpiralLayoutProvider() : FractalLayoutProvider() {
        FractalConfig cfg = getConfig();
        cfg.pattern = FractalPattern::Spiral;
        setConfig(cfg);
    }
    std::string getName() const override { return "spiral"; }
    std::string getDescription() const override { 
        return "Spiral - Archimedean spiral window arrangement"; 
    }
    std::unique_ptr<ILayoutProvider> clone() const override;
};

} 

#endif 
