#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <unordered_map>
#include <optional>
#include <sstream>
#include <set>

namespace pblank {

class Toaster;

/**
 * @brief Version manager for detecting and migrating between .wmi versions
 */
class VersionManager {
public:
    enum class Version {
        Unknown,
        V1,
        V2
    };

    static Version detectVersion(const std::string& source);
    static std::string getVersionString(Version v);
};

namespace astv2 {

struct IntLiteral { int value; };
struct FloatLiteral { double value; };
struct StringLiteral { std::string value; };
struct BoolLiteral { bool value; };
struct Identifier { std::string name; };

struct BinaryOp {
    enum class Op { Add, Sub, Mul, Div, Mod, And, Or, Eq, Ne, Lt, Gt, Le, Ge, Concat };
    Op op;
    std::unique_ptr<struct Expression> left;
    std::unique_ptr<struct Expression> right;
};

struct UnaryOp {
    enum class Op { Not, Neg, BitNot };
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

struct ObjectLiteral {
    std::unordered_map<std::string, std::unique_ptr<struct Expression>> properties;
};

struct PropertyBinding {
    std::string name;
    std::unique_ptr<Expression> value;
    bool is_binding;  
};

struct CallExpression {
    std::unique_ptr<struct Expression> callee;
    std::vector<std::unique_ptr<struct Expression>> arguments;
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
    ArrayLiteral,
    ObjectLiteral,
    CallExpression
>;

struct Expression {
    ExpressionValue value;
};

struct StructMember {
    std::string type;
    std::string name;
    std::optional<std::unique_ptr<Expression>> default_value;
};

struct StructDefinition {
    std::string name;
    std::vector<StructMember> members;
};

struct EnumValue {
    std::string name;
    std::optional<std::unique_ptr<Expression>> value;
    
    EnumValue() = default;
    EnumValue(EnumValue&&) = default;
    EnumValue& operator=(EnumValue&&) = default;
    
    EnumValue(const EnumValue&) = delete;
    EnumValue& operator=(const EnumValue&) = delete;
};

struct EnumDefinition {
    std::string name;
    std::vector<EnumValue> values;
};

struct TypedefDeclaration {
    std::string original_type;
    std::string new_type_name;
};

struct FunctionParameter {
    std::string type;
    std::string name;
};

struct FunctionPrototype {
    std::string return_type;
    std::string name;
    std::vector<FunctionParameter> parameters;
};

struct CodeBlock {
    std::string code;  
    std::vector<std::string> lines;
};

struct AnchorValue {
    std::optional<std::unique_ptr<Expression>> left;
    std::optional<std::unique_ptr<Expression>> right;
    std::optional<std::unique_ptr<Expression>> top;
    std::optional<std::unique_ptr<Expression>> bottom;
    std::optional<std::unique_ptr<Expression>> horizontalCenter;
    std::optional<std::unique_ptr<Expression>> verticalCenter;
    std::optional<std::unique_ptr<Expression>> fill;  
    std::optional<std::unique_ptr<Expression>> centerIn;  
};

struct PropertyDeclaration {
    std::string type;
    std::string name;
    std::unique_ptr<Expression> default_value;
    bool is_readonly{false};
};

struct AnchorsDeclaration {
    AnchorValue anchors;
    std::string target;  
};

struct Assignment {
    std::string name;
    std::unique_ptr<Expression> value;
};

struct PropertyAssignment {
    std::string name;
    std::unique_ptr<Expression> value;
    bool is_binding{false};  
};

struct ObjectDefinition {
    std::string name;
    std::string type;  
    
    std::vector<PropertyDeclaration> property_declarations;
    std::vector<PropertyAssignment> property_assignments;
    
    std::optional<AnchorsDeclaration> anchors;
    
    std::vector<std::unique_ptr<ObjectDefinition>> nested_objects;
    
    std::vector<CodeBlock> code_blocks;
};

struct IfStatement {
    std::unique_ptr<Expression> condition;
    std::vector<std::unique_ptr<struct Statement>> then_branch;
    std::vector<std::unique_ptr<struct Statement>> else_branch;
};

struct VariableDeclaration {
    std::string type;
    std::string name;
    std::unique_ptr<Expression> value;
};

struct ExecDirective {
    std::string command;
};

using StatementValue = std::variant<
    Assignment,
    PropertyAssignment,
    ObjectDefinition,
    IfStatement,
    VariableDeclaration,
    StructDefinition,
    EnumDefinition,
    TypedefDeclaration,
    FunctionPrototype,
    CodeBlock,
    ExecDirective
>;

struct Statement {
    StatementValue value;
};

struct ConfigFileV2 {
    std::string version{"1.0"};
    
    std::vector<StructDefinition> structs;
    std::vector<EnumDefinition> enums;
    std::vector<TypedefDeclaration> typedefs;
    std::vector<FunctionPrototype> function_prototypes;
    
    std::vector<std::unique_ptr<Statement>> statements;
    
    std::unordered_map<std::string, std::variant<int, double, std::string, bool>> variables;
    
    ConfigFileV2() = default;
    ConfigFileV2(ConfigFileV2&&) = default;
    ConfigFileV2& operator=(ConfigFileV2&&) = default;
    
    ConfigFileV2(const ConfigFileV2&) = delete;
    ConfigFileV2& operator=(const ConfigFileV2&) = delete;
};

} 

enum class TokenTypeV2 {
    
    Integer, Float, String, TokTrue, TokFalse, TokNull,
    
    Identifier, 
    Property, Object, Anchors, Fill, CenterIn,
    
    Struct, Typedef, Enum, Function,
    
    If, Else, For, While, Return,
    
    IntType, FloatType_, BoolType, StringType_, VoidType, AutoType,
    
    Plus, Minus, Star, Slash, Percent,
    Equals, NotEquals, Less, Greater, LessEqual, GreaterEqual,
    And, Or, Not,
    Assign, Colon, Semicolon, Comma, Dot, DoubleColon,
    
    LeftBrace, RightBrace, LeftParen, RightParen,
    LeftBracket, RightBracket,
    
    Arrow, 
    
    Import, Include,
    
    EndOfFile, Invalid
};

struct TokenV2 {
    TokenTypeV2 type;
    std::string lexeme;
    int line;
    int column;
    
    std::variant<std::monostate, int, double, std::string, bool> literal_value;
    
    TokenV2() : type(TokenTypeV2::Invalid), lexeme(""), line(0), column(0), literal_value(std::monostate{}) {}
    
    TokenV2(TokenTypeV2 t, std::string lex, int l, int c) 
        : type(t), lexeme(std::move(lex)), line(l), column(c), literal_value(std::monostate{}) {}
    
    template<typename T>
    TokenV2(TokenTypeV2 t, std::string lex, int l, int c, T lit)
        : type(t), lexeme(std::move(lex)), line(l), column(c), literal_value(std::move(lit)) {}
    
    constexpr std::string_view getLexemeView() const noexcept { 
        return std::string_view(lexeme); 
    }
    
    inline bool isKeyword(std::string_view kw) const noexcept { 
        return type == TokenTypeV2::Identifier && getLexemeView() == kw; 
    }
};

class LexerV2 {
public:
    explicit LexerV2(std::string source);
    
    std::vector<TokenV2> tokenize();
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
    
    TokenV2 makeToken(TokenTypeV2 type);
    TokenV2 number();
    TokenV2 string();
    TokenV2 identifier();
    TokenV2 preprocessor();
    
    void addError(const std::string& message);
};

class ParserV2 {
public:
    explicit ParserV2(std::vector<TokenV2> tokens);
    
    std::unique_ptr<astv2::ConfigFileV2> parse();
    const std::vector<std::string>& getErrors() const { return errors_; }
    
private:
    std::vector<TokenV2> tokens_;
    size_t current_{0};
    std::vector<std::string> errors_;
    
    std::unordered_map<std::string, std::variant<int, double, std::string, bool>> variables_;
    
    std::vector<astv2::StructDefinition> structs_;
    std::vector<astv2::EnumDefinition> enums_;
    std::vector<astv2::TypedefDeclaration> typedefs_;
    std::vector<astv2::FunctionPrototype> function_prototypes_;
    
    inline const TokenV2& peek() const;
    inline const TokenV2& previous() const;
    inline bool isAtEnd() const;
    inline const TokenV2& advance();
    inline bool check(TokenTypeV2 type) const;
    inline bool match(std::initializer_list<TokenTypeV2> types);
    const TokenV2& consume(TokenTypeV2 type, const std::string& message);
    
    std::unique_ptr<astv2::ConfigFileV2> configFile();
    
    std::unique_ptr<astv2::Statement> structDefinition();
    std::unique_ptr<astv2::Statement> enumDefinition();
    std::unique_ptr<astv2::Statement> typedefDeclaration();
    std::unique_ptr<astv2::Statement> functionPrototype();
    
    std::unique_ptr<astv2::Statement> statement();
    std::unique_ptr<astv2::Statement> ifStatement();
    std::unique_ptr<astv2::Statement> objectDefinition();
    std::unique_ptr<astv2::Statement> propertyDeclaration();
    std::unique_ptr<astv2::Statement> codeBlock();
    std::unique_ptr<astv2::Statement> execDirective();
    
    astv2::PropertyAssignment propertyAssignment();
    astv2::AnchorsDeclaration anchorsDeclaration();
    
    std::unique_ptr<astv2::Expression> expression();
    std::unique_ptr<astv2::Expression> assignment();
    std::unique_ptr<astv2::Expression> logicalOr();
    std::unique_ptr<astv2::Expression> logicalAnd();
    std::unique_ptr<astv2::Expression> equality();
    std::unique_ptr<astv2::Expression> comparison();
    std::unique_ptr<astv2::Expression> term();
    std::unique_ptr<astv2::Expression> factor();
    std::unique_ptr<astv2::Expression> unary();
    std::unique_ptr<astv2::Expression> primary();
    std::unique_ptr<astv2::Expression> arrayLiteral();
    std::unique_ptr<astv2::Expression> objectLiteral();
    
    bool isTypeName(const std::string& name) const;
    std::string getTypeForProperty(const std::string& property_name);
    
    void addError(const std::string& message);
    void synchronize();
};

class ConfigMigrator {
public:
    
    static std::string convertV1ToV2Source(const std::string& v1_source);
};

class ConfigParserV2 {
public:
    explicit ConfigParserV2(Toaster* toaster = nullptr);
    
    bool load(const std::filesystem::path& path);
    
    bool loadFromString(const std::string& source);
    
    const astv2::ConfigFileV2& getConfig() const { return config_; }
    
    bool isV2() const { return is_v2_; }
    
    const std::vector<std::string>& getErrors() const { return errors_; }
    
    bool migrateFromV1(const std::string& v1_source);
    
private:
    Toaster* toaster_;
    astv2::ConfigFileV2 config_;
    std::vector<std::string> errors_;
    bool is_v2_{false};
    
    void parseSource(const std::string& source);
};

} 
