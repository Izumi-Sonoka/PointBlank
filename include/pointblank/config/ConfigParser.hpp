#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <unordered_map>
#include <functional>
#include <optional>
#include <filesystem>

#include <X11/Xlib.h>

#include "ConfigParserV2.hpp"

namespace pblank {

class Toaster;

/**
 * @brief AST Node types for .wmi files
 */
namespace ast {

struct IntLiteral { int value; };
struct FloatLiteral { double value; };
struct StringLiteral { std::string value; };
struct BoolLiteral { bool value; };
struct Identifier { std::string name; };

struct BinaryOp {
    enum class Op { Add, Sub, Mul, Div, And, Or, Eq, Ne, Lt, Gt, Le, Ge };
    Op op;
    std::unique_ptr<struct Expression> left;
    std::unique_ptr<struct Expression> right;
};

struct UnaryOp {
    enum class Op { Not, Neg };
    Op op;
    std::unique_ptr<struct Expression> operand;
};

struct MemberAccess {
    std::unique_ptr<struct Expression> object;
    std::string member;
};

struct ArrayLiteral {
    std::vector<std::unique_ptr<struct Expression>> elements;
};

using ExpressionValue = std::variant<
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

struct Expression {
    ExpressionValue value;
};

struct Assignment {
    std::string name;
    std::unique_ptr<Expression> value;
};

struct VariableDeclaration {
    std::string name;
    std::unique_ptr<Expression> value;
};

struct IfStatement {
    std::unique_ptr<Expression> condition;
    std::vector<std::unique_ptr<struct Statement>> then_branch;
    std::vector<std::unique_ptr<struct Statement>> else_branch;
};

struct Block {
    std::string name;
    std::vector<std::unique_ptr<struct Statement>> statements;
};

struct ExecDirective {
    std::string command;
};

using StatementValue = std::variant<
    Assignment,
    VariableDeclaration,
    IfStatement,
    Block,
    ExecDirective
>;

struct Statement {
    StatementValue value;
};

struct ImportDirective {
    std::string module_name;
    bool is_user_extension; 
};

struct ConfigFile {
    std::vector<ImportDirective> imports;
    std::vector<std::unique_ptr<Block>> blocks;  
    std::unique_ptr<Block> root;  
    
    std::unordered_map<std::string, std::variant<int, double, std::string, bool>> variables;
};

} 

enum class TokenType {
    
    Integer, Float, String, TokTrue, TokFalse,
    
    Identifier, Let, If, Else, Exec,
    
    Plus, Minus, Star, Slash,
    Equals, NotEquals, Less, Greater, LessEqual, GreaterEqual,
    And, Or, Not,
    Assign, Colon, Semicolon, Comma, Dot,
    
    LeftBrace, RightBrace, LeftParen, RightParen,
    LeftBracket, RightBracket,
    
    Import, Include,
    
    EndOfFile, Invalid
};

struct Token {
    TokenType type;
    std::string lexeme;  
    int line;
    int column;
    
    std::variant<std::monostate, int, double, std::string, bool> literal_value;
    
    Token() : type(TokenType::Invalid), lexeme(""), line(0), column(0), literal_value(std::monostate{}) {}
    
    Token(TokenType t, std::string lex, int l, int c) 
        : type(t), lexeme(std::move(lex)), line(l), column(c), literal_value(std::monostate{}) {}
    
    Token(TokenType t, std::string lex, int l, int c, const std::string& lit)
        : type(t), lexeme(std::move(lex)), line(l), column(c), literal_value(lit) {}
    
    Token(TokenType t, std::string lex, int l, int c, int lit)
        : type(t), lexeme(std::move(lex)), line(l), column(c), literal_value(lit) {}
    
    Token(TokenType t, std::string lex, int l, int c, double lit)
        : type(t), lexeme(std::move(lex)), line(l), column(c), literal_value(lit) {}
    
    Token(TokenType t, std::string lex, int l, int c, bool lit)
        : type(t), lexeme(std::move(lex)), line(l), column(c), literal_value(lit) {}
    
    std::string_view getLexemeView() const noexcept { return lexeme; }
    
    inline bool isKeyword(std::string_view kw) const noexcept { return type == TokenType::Identifier && lexeme == kw; }
};

class Lexer {
public:
    explicit Lexer(std::string source);
    
    std::vector<Token> tokenize();
    const std::vector<std::string>& getErrors() const { return errors_; }
    
private:
    std::string source_;
    size_t current_{0};
    int line_{1};
    int column_{1};
    std::vector<std::string> errors_;
    
    inline char peek() const;
    inline char peekNext() const;
    inline char advance();
    inline bool match(char expected);
    inline bool isAtEnd() const;
    inline void skipWhitespace();
    inline void skipComment();
    
    Token makeToken(TokenType type);
    Token number();
    Token string();
    Token identifier();
    Token preprocessor();
    
    void addError(const std::string& message);
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    
    std::unique_ptr<ast::ConfigFile> parse();
    const std::vector<std::string>& getErrors() const { return errors_; }
    
private:
    std::vector<Token> tokens_;
    size_t current_{0};
    std::vector<std::string> errors_;
    
    std::unordered_map<std::string, std::variant<int, double, std::string, bool>> variables_;
    
    inline const Token& peek() const;
    inline const Token& previous() const;
    inline bool isAtEnd() const;
    inline const Token& advance();
    inline bool check(TokenType type) const;
    inline bool match(std::initializer_list<TokenType> types);
    const Token& consume(TokenType type, const std::string& message);
    
    std::unique_ptr<ast::ConfigFile> configFile();
    std::vector<ast::ImportDirective> imports();
    std::unique_ptr<ast::Block> block();
    std::unique_ptr<ast::Statement> statement();
    std::unique_ptr<ast::Statement> ifStatement();
    std::unique_ptr<ast::Statement> assignment();
    std::unique_ptr<ast::Statement> letStatement();
    std::unique_ptr<ast::Expression> expression();
    std::unique_ptr<ast::Expression> logicalOr();
    std::unique_ptr<ast::Expression> logicalAnd();
    std::unique_ptr<ast::Expression> equality();
    std::unique_ptr<ast::Expression> comparison();
    std::unique_ptr<ast::Expression> term();
    std::unique_ptr<ast::Expression> factor();
    std::unique_ptr<ast::Expression> unary();
    std::unique_ptr<ast::Expression> primary();
    
    void addError(const std::string& message);
    void synchronize();
};

class Config {
public:
    struct WindowRules {
        std::optional<double> opacity;
        std::optional<bool> blur;
        int border_width{2};              
        int gap_size{10};                 
    };
    
    struct Keybind {
        std::string modifiers;
        std::string key;
        std::string action;
        std::optional<std::string> exec_command;
    };
    
    struct DragConfig {
        bool swap_on_drag{true};
        int threshold{5};
        int swap_threshold{20};
        bool visual_feedback{true};
    };
    
    struct BordersConfig {
        std::string focused_color{"#89B4FA"};     
        std::string unfocused_color{"#45475A"};   
        std::string urgent_color{"#F38BA8"};      
        int width{2};                     
    };

    struct MouseConfig {
        bool focus_follows_mouse{true};    
        bool mouse_warping{false};        
        double cursor_speed{1.0};         
    };

    struct AnimationsConfig {
        bool enabled{true};               
        std::string curve{"ease-in-out"}; 
        int duration{200};                
    };

    struct PerformanceConfig {
        
        std::string scheduler_policy{"other"};  
        int scheduler_priority{0};
        
        std::string cpu_cores{""};       
        bool cpu_exclusive{false};
        bool hyperthreading_aware{true};
        
        bool realtime_mode{false};
        int realtime_priority{50};
        bool lock_memory{false};
        int locked_memory_mb{64};
        
        int target_fps{60};
        int min_fps{30};
        int max_fps{144};
        bool vsync{true};
        bool adaptive_sync{true};
        
        int throttle_threshold_us{1000};
        int throttle_delay_us{100};
        bool throttle_on_battery{true};
        
        int max_batch_size{16};
        int batch_timeout_us{100};
        
        bool dirty_rectangles_only{true};
        bool double_buffer{true};
        bool triple_buffer{false};
        
        bool metrics_enabled{true};
        int metrics_interval_ms{1000};
        bool latency_tracking{true};
    };

    struct ExtensionsConfig {
        bool enabled{true};               
        bool strict_validation{true};      
        int health_check_interval_s{30};
        std::string builtin_extension_dir{"./extensions/build"};
        std::string user_extension_dir{"~/.config/pblank/extensions/user"};
        int init_timeout_ms{5000};
        int max_extensions{32};
        bool allow_event_blocking{true};
    };
    
    struct WorkspaceConfig {
        bool infinite{false};           
        int max_workspaces{12};         
        int initial_count{1};          
        bool dynamic_creation{true};    
        bool auto_remove{true};         
        int min_persist{1};             
        
        bool per_monitor{false};        
        bool virtual_mapping{false};    
        std::unordered_map<int, int> workspace_to_monitor;  
    };
    
    struct StatusBarConfig {
        int height{24};                 
        int padding_x{8};               
        int padding_y{4};               
        std::string position{"top"};    
        
        std::string bg_color{"#1E1E2E"};    
        std::string fg_color{"#CDD6F4"};    
        std::string accent_color{"#89B4FA"}; 
        std::string urgent_color{"#F38BA8"}; 
        std::string inactive_bg{"#45475A"};  
        
        std::string font_family{"Sans"};
        double font_size{12.0};
        
        bool show_workspace_icons{true};
        bool show_layout_mode{true};
        bool show_window_title{true};
        bool workspace_clickable{true};  
        bool enabled{true};              
        
        std::vector<std::string> workspace_icons;
    };
    
    struct WindowsConfig {
        bool auto_resize_non_docks{true};     
        bool floating_resize_enabled{true};   
        int floating_resize_edge_size{8};     
        bool smart_gaps{false};              
        bool smart_borders{false};           
        bool focus_new_windows{true};        
        bool focus_urgent_windows{true};     
        int default_floating_width{800};     
        int default_floating_height{600};   
        bool center_floating_windows{true}; 
    };
    
    struct LayoutGapConfig {
        int inner_gap{10};        
        int outer_gap{10};        
        
        int top_gap{-1};
        int bottom_gap{-1};
        int left_gap{-1};
        int right_gap{-1};
    };
    
    struct AutostartConfig {
        std::vector<std::string> commands;  
    };
    
    struct LayoutConfig {
        std::string cycle_direction{"forward"};
        bool wrap_cycle{true};
    };
    
    bool focus_follows_mouse{false};
    bool monitor_focus_follows_mouse{false};  
    WindowRules window_rules;
    std::vector<Keybind> keybinds;
    DragConfig drag;
    BordersConfig borders;
    WorkspaceConfig workspaces;
    StatusBarConfig status_bar;
    LayoutConfig layout;
    WindowsConfig windows;
    LayoutGapConfig layout_gaps;
    AutostartConfig autostart;
    
    MouseConfig mouse;                    
    AnimationsConfig animations;          
    PerformanceConfig performance;        
    ExtensionsConfig extensions;          
    
    std::vector<std::string> system_paths;
    
    std::unordered_map<std::string, std::variant<int, double, std::string, bool, std::vector<std::string>>> variables;
    
    std::string config_version{"1.0"};
    bool is_v2_format{false};
};

class ConfigParser {
public:
    explicit ConfigParser(Toaster* toaster);
    
    bool load(const std::filesystem::path& path = getDefaultConfigPath());
    
    bool loadFromString(const std::string& source);
    
    static std::string getEmbeddedConfig();
    
    const Config& getConfig() const { return config_; }
    
    static std::filesystem::path getDefaultConfigPath();
    
    static std::filesystem::path getPBExtensionPath();
    static std::filesystem::path getUserExtensionPath();
    
    VersionManager::Version detectConfigVersion(const std::string& source);
    
    std::unordered_map<std::string, std::unique_ptr<ast::ConfigFile>> imported_modules_;
    
    bool interpret(const ast::ConfigFile& ast);
    void evaluateBlock(const ast::Block& block);
    void evaluateStatement(const ast::Statement& stmt);
    std::variant<int, double, std::string, bool, std::vector<std::string>> evaluateExpression(const ast::Expression& expr);
    
    std::variant<int, double, std::string, bool, std::vector<std::string>> 
    evaluateExpression(const ast::Expression& expr, Window window, Display* display);
    
    bool resolveImport(const ast::ImportDirective& import);
    std::optional<std::filesystem::path> findImportFile(const std::string& name, bool is_user);
    
    void reportError(const std::string& message);
    void reportErrors(const std::vector<std::string>& errors);

private:
    
    Toaster* toaster_{nullptr};
    Config config_;
    
    std::unique_ptr<ConfigParserV2> v2_parser_;
    std::unique_ptr<astv2::ConfigFileV2> v2_config_;
    
    bool loadV1(const std::string& source);
    bool loadV2(const std::string& source);
    bool interpretV2(const astv2::ConfigFileV2& v2_config);
};

} 
