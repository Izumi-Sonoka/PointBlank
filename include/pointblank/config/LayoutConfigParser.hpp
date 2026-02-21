#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>
#include <unordered_map>
#include <functional>
#include <optional>
#include <filesystem>
#include <X11/Xlib.h>

namespace pblank {

class BSPNode;
class LayoutEngine;
struct Rect;

/**
 * @brief Layout mode identifiers for the tiling engine
 */
enum class LayoutMode {
    BSP,            
    Monocle,        
    MasterStack,    
    CenteredMaster, 
    DynamicGrid,    
    DwindleSpiral,  
    GoldenRatio,    
    TabbedStacked,  
    InfiniteCanvas  
};

inline std::optional<LayoutMode> layoutModeFromString(const std::string& str) {
    static const std::unordered_map<std::string, LayoutMode> mode_map = {
        {"bsp", LayoutMode::BSP},
        {"monocle", LayoutMode::Monocle},
        {"master_stack", LayoutMode::MasterStack},
        {"master-stack", LayoutMode::MasterStack},
        {"centered_master", LayoutMode::CenteredMaster},
        {"centered-master", LayoutMode::CenteredMaster},
        {"dynamic_grid", LayoutMode::DynamicGrid},
        {"dynamic-grid", LayoutMode::DynamicGrid},
        {"dwindle_spiral", LayoutMode::DwindleSpiral},
        {"dwindle-spiral", LayoutMode::DwindleSpiral},
        {"golden_ratio", LayoutMode::GoldenRatio},
        {"golden-ratio", LayoutMode::GoldenRatio},
        {"tabbed_stacked", LayoutMode::TabbedStacked},
        {"tabbed-stacked", LayoutMode::TabbedStacked},
        {"tabbed", LayoutMode::TabbedStacked},
        {"stacked", LayoutMode::TabbedStacked}
    };
    
    auto it = mode_map.find(str);
    if (it != mode_map.end()) {
        return it->second;
    }
    return std::nullopt;
}

inline std::string layoutModeToString(LayoutMode mode) {
    switch (mode) {
        case LayoutMode::BSP: return "bsp";
        case LayoutMode::Monocle: return "monocle";
        case LayoutMode::MasterStack: return "master_stack";
        case LayoutMode::CenteredMaster: return "centered_master";
        case LayoutMode::DynamicGrid: return "dynamic_grid";
        case LayoutMode::DwindleSpiral: return "dwindle_spiral";
        case LayoutMode::GoldenRatio: return "golden_ratio";
        case LayoutMode::TabbedStacked: return "tabbed_stacked";
        case LayoutMode::InfiniteCanvas: return "infinite_canvas";
        default: return "unknown";
    }
}

enum class LayoutCycleDirection {
    Forward,    
    Backward    
};

inline std::optional<LayoutCycleDirection> cycleDirectionFromString(const std::string& str) {
    if (str == "forward" || str == "front-to-back" || str == "front_to_back") {
        return LayoutCycleDirection::Forward;
    }
    if (str == "backward" || str == "back-to-front" || str == "back_to_front") {
        return LayoutCycleDirection::Backward;
    }
    return std::nullopt;
}

inline std::string cycleDirectionToString(LayoutCycleDirection dir) {
    return (dir == LayoutCycleDirection::Forward) ? "forward" : "backward";
}

namespace layout_ast {

struct IntLiteral { int value; };
struct FloatLiteral { double value; };
struct StringLiteral { std::string value; };
struct BoolLiteral { bool value; };
struct Identifier { std::string name; };

struct BinaryOp {
    enum class Op { Add, Sub, Mul, Div, And, Or, Eq, Ne, Lt, Gt, Le, Ge };
    Op op;
    std::unique_ptr<struct LayoutExpression> left;
    std::unique_ptr<struct LayoutExpression> right;
};

struct UnaryOp {
    enum class Op { Not, Neg };
    Op op;
    std::unique_ptr<struct LayoutExpression> operand;
};

struct MemberAccess {
    std::unique_ptr<struct LayoutExpression> object;
    std::string member;
};

struct ArrayLiteral {
    std::vector<std::unique_ptr<struct LayoutExpression>> elements;
};

using LayoutExpressionValue = std::variant<
    IntLiteral,
    FloatLiteral,
    StringLiteral,
    BoolLiteral,
    Identifier,
    BinaryOp,
    UnaryOp,
    MemberAccess,
    ArrayLiteral
>;

struct LayoutExpression {
    LayoutExpressionValue value;
};

struct LayoutAssignment {
    std::string name;
    std::unique_ptr<LayoutExpression> value;
};

struct LayoutBlock {
    std::string name;
    std::vector<std::unique_ptr<struct LayoutStatement>> statements;
};

struct LayoutRule {
    std::string workspace_pattern;  
    LayoutMode mode;
    std::unordered_map<std::string, std::variant<int, double, std::string, bool>> parameters;
};

using LayoutStatementValue = std::variant<
    LayoutAssignment,
    std::unique_ptr<LayoutBlock>,
    LayoutRule
>;

struct LayoutStatement {
    LayoutStatementValue value;
};

struct LayoutIncludeDirective {
    std::string layout_name;
    bool is_user_layout;  
};

struct LayoutConfigFile {
    std::vector<LayoutIncludeDirective> includes;
    std::unique_ptr<LayoutBlock> root;
};

} 

enum class LayoutTokenType {
    
    Integer, Float, String, TokTrue, TokFalse,
    
    Identifier, Let, Layout, Workspace, Mode, Rule,
    
    Plus, Minus, Star, Slash,
    Equals, NotEquals, Less, Greater, LessEqual, GreaterEqual,
    And, Or, Not,
    Assign, Colon, Semicolon, Comma, Dot, Arrow,
    
    LeftBrace, RightBrace, LeftParen, RightParen,
    LeftBracket, RightBracket,
    
    Include, IncludeLayout, IncludeLayoutUser,
    
    EndOfFile, Invalid
};

struct LayoutToken {
    LayoutTokenType type;
    std::string_view lexeme;  
    int line;
    int column;
    
    std::variant<std::monostate, int, double, std::string, bool> literal_value;
    
    LayoutToken() : type(LayoutTokenType::Invalid), lexeme(""), line(0), column(0), 
                   literal_value(std::monostate{}) {}
    
    LayoutToken(LayoutTokenType t, std::string_view lex, int l, int c) 
        : type(t), lexeme(lex), line(l), column(c), 
          literal_value(std::monostate{}) {}
    
    LayoutToken(LayoutTokenType t, std::string_view lex, int l, int c, const std::string& lit)
        : type(t), lexeme(lex), line(l), column(c), 
          literal_value(lit) {}
    
    LayoutToken(LayoutTokenType t, std::string_view lex, int l, int c, int lit)
        : type(t), lexeme(lex), line(l), column(c), 
          literal_value(lit) {}
    
    LayoutToken(LayoutTokenType t, std::string_view lex, int l, int c, double lit)
        : type(t), lexeme(lex), line(l), column(c), 
          literal_value(lit) {}
    
    LayoutToken(LayoutTokenType t, std::string_view lex, int l, int c, bool lit)
        : type(t), lexeme(lex), line(l), column(c), 
          literal_value(lit) {}
};

class LayoutLexer {
public:
    explicit LayoutLexer(std::string source);
    
    std::vector<LayoutToken> tokenize();
    const std::vector<std::string>& getErrors() const { return errors_; }
    
private:
    std::string source_;
    size_t current_{0};
    int line_{1};
    int column_{1};
    std::vector<std::string> errors_;
    
    char peek() const;
    char peekNext() const;
    char advance();
    bool match(char expected);
    bool isAtEnd() const;
    
    void skipWhitespace();
    void skipComment();
    
    LayoutToken makeToken(LayoutTokenType type);
    LayoutToken number();
    LayoutToken stringLiteral();
    LayoutToken identifier();
    LayoutToken preprocessor();
    
    void addError(const std::string& message);
};

class LayoutParser {
public:
    explicit LayoutParser(std::vector<LayoutToken> tokens);
    
    std::unique_ptr<layout_ast::LayoutConfigFile> parse();
    const std::vector<std::string>& getErrors() const { return errors_; }
    
private:
    std::vector<LayoutToken> tokens_;
    size_t current_{0};
    std::vector<std::string> errors_;
    
    const LayoutToken& peek() const;
    const LayoutToken& previous() const;
    bool isAtEnd() const;
    const LayoutToken& advance();
    bool check(LayoutTokenType type) const;
    bool match(std::initializer_list<LayoutTokenType> types);
    const LayoutToken& consume(LayoutTokenType type, const std::string& message);
    
    std::unique_ptr<layout_ast::LayoutConfigFile> configFile();
    std::vector<layout_ast::LayoutIncludeDirective> includes();
    std::unique_ptr<layout_ast::LayoutBlock> block();
    std::unique_ptr<layout_ast::LayoutStatement> statement();
    std::unique_ptr<layout_ast::LayoutStatement> layoutRule();
    std::unique_ptr<layout_ast::LayoutStatement> assignment();
    std::unique_ptr<layout_ast::LayoutExpression> expression();
    std::unique_ptr<layout_ast::LayoutExpression> logicalOr();
    std::unique_ptr<layout_ast::LayoutExpression> logicalAnd();
    std::unique_ptr<layout_ast::LayoutExpression> equality();
    std::unique_ptr<layout_ast::LayoutExpression> comparison();
    std::unique_ptr<layout_ast::LayoutExpression> term();
    std::unique_ptr<layout_ast::LayoutExpression> factor();
    std::unique_ptr<layout_ast::LayoutExpression> unary();
    std::unique_ptr<layout_ast::LayoutExpression> primary();
    
    void addError(const std::string& message);
    void synchronize();
};

struct LayoutConfig {
    
    LayoutMode default_mode{LayoutMode::BSP};
    
    std::unordered_map<int, LayoutMode> workspace_modes;
    
    struct BSPParams {
        int gap_size{10};
        int border_width{2};
        int padding{5};
        bool dwindle{true};
    } bsp_params;
    
    struct MasterStackParams {
        double master_ratio{0.55};
        int max_master{1};
        int gap_size{10};
    } master_stack_params;
    
    struct CenteredMasterParams {
        double center_ratio{0.5};      
        int max_center{1};             
        int gap_size{10};
        bool center_on_focus{true};    
    } centered_master_params;
    
    struct DynamicGridParams {
        bool prefer_horizontal{false}; 
        int min_cell_width{200};
        int min_cell_height{150};
        int gap_size{10};
    } dynamic_grid_params;
    
    struct DwindleSpiralParams {
        double initial_ratio{0.55};
        double ratio_increment{0.02};  
        int gap_size{10};
        bool shift_by_focus{true};     
    } dwindle_spiral_params;
    
    struct TabbedStackedParams {
        int tab_height{25};
        int tab_min_width{100};
        int gap_size{0};
        bool show_focused_only{true};  
        bool tab_at_top{true};         
    } tabbed_stacked_params;
    
    unsigned long focused_border_color{0x89B4FA};   
    unsigned long unfocused_border_color{0x45475A}; 
    
    LayoutCycleDirection cycle_direction{LayoutCycleDirection::Forward};
    bool wrap_cycle{true};  
    
    std::vector<layout_ast::LayoutRule> layout_rules;
};

class LayoutConfigParser {
public:
    explicit LayoutConfigParser(LayoutEngine* engine);
    
    bool load(const std::filesystem::path& path = getDefaultLayoutPath());
    
    bool loadLayout(const std::string& filename, bool is_user = false);
    
    const LayoutConfig& getConfig() const { return config_; }
    
    LayoutConfig& getConfigMutable() { return config_; }
    
    static std::filesystem::path getDefaultLayoutPath();
    
    static std::filesystem::path getSystemLayoutPath();
    
    static std::filesystem::path getUserLayoutPath();
    
    static std::vector<std::string> getAvailableLayouts();
    
    void applyToEngine();
    
private:
    LayoutEngine* engine_;
    LayoutConfig config_;
    
    std::unordered_map<std::string, std::unique_ptr<layout_ast::LayoutConfigFile>> parsed_layouts_;
    std::vector<std::string> include_stack_;  
    
    bool interpret(const layout_ast::LayoutConfigFile& ast);
    void evaluateBlock(const layout_ast::LayoutBlock& block);
    void evaluateStatement(const layout_ast::LayoutStatement& stmt);
    std::variant<int, double, std::string, bool, std::vector<std::string>> 
        evaluateExpression(const layout_ast::LayoutExpression& expr);
    
    bool resolveInclude(const layout_ast::LayoutIncludeDirective& include);
    std::optional<std::filesystem::path> findLayoutFile(const std::string& name, bool is_user);
    
    std::vector<int> parseWorkspacePattern(const std::string& pattern);
    
    void reportError(const std::string& message);
    void reportErrors(const std::vector<std::string>& errors);
    
    std::optional<std::string> readFile(const std::filesystem::path& path);
};

} 