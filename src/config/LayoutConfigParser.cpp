#include "pointblank/config/LayoutConfigParser.hpp"
#include "pointblank/layout/LayoutEngine.hpp"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <regex>

namespace pblank {

// ============================================================================
// LayoutLexer Implementation
// ============================================================================

LayoutLexer::LayoutLexer(std::string source)
    : source_(std::move(source)) {}

std::vector<LayoutToken> LayoutLexer::tokenize() {
    std::vector<LayoutToken> tokens;
    
    while (!isAtEnd()) {
        skipWhitespace();
        
        if (isAtEnd()) break;
        
        char c = peek();
        
        // Skip comments
        if (c == '/' && peekNext() == '/') {
            skipComment();
            continue;
        }
        
        if (c == '#') {
            tokens.push_back(preprocessor());
            continue;
        }
        
        // String literals
        if (c == '"') {
            tokens.push_back(stringLiteral());
            continue;
        }
        
        // Numbers
        if (std::isdigit(c) || (c == '-' && std::isdigit(peekNext()))) {
            tokens.push_back(number());
            continue;
        }
        
        // Identifiers and keywords
        if (std::isalpha(c) || c == '_') {
            tokens.push_back(identifier());
            continue;
        }
        
        // Operators and delimiters
        switch (c) {
            case '+': tokens.push_back(makeToken(LayoutTokenType::Plus)); advance(); break;
            case '-': 
                advance();
                if (match('>')) {
                    tokens.push_back(makeToken(LayoutTokenType::Arrow));
                } else {
                    tokens.push_back(LayoutToken(LayoutTokenType::Minus, "-", line_, column_ - 1));
                }
                break;
            case '*': tokens.push_back(makeToken(LayoutTokenType::Star)); advance(); break;
            case '/': tokens.push_back(makeToken(LayoutTokenType::Slash)); advance(); break;
            case '=': 
                advance();
                if (match('=')) {
                    tokens.push_back(makeToken(LayoutTokenType::Equals));
                } else {
                    tokens.push_back(makeToken(LayoutTokenType::Assign));
                }
                break;
            case '!':
                advance();
                if (match('=')) {
                    tokens.push_back(makeToken(LayoutTokenType::NotEquals));
                } else {
                    tokens.push_back(makeToken(LayoutTokenType::Not));
                }
                break;
            case '<':
                advance();
                if (match('=')) {
                    tokens.push_back(makeToken(LayoutTokenType::LessEqual));
                } else {
                    tokens.push_back(makeToken(LayoutTokenType::Less));
                }
                break;
            case '>':
                advance();
                if (match('=')) {
                    tokens.push_back(makeToken(LayoutTokenType::GreaterEqual));
                } else {
                    tokens.push_back(makeToken(LayoutTokenType::Greater));
                }
                break;
            case '&':
                advance();
                if (match('&')) {
                    tokens.push_back(makeToken(LayoutTokenType::And));
                } else {
                    addError("Unexpected character '&'");
                }
                break;
            case '|':
                advance();
                if (match('|')) {
                    tokens.push_back(makeToken(LayoutTokenType::Or));
                } else {
                    addError("Unexpected character '|'");
                }
                break;
            case ':': tokens.push_back(makeToken(LayoutTokenType::Colon)); advance(); break;
            case ';': tokens.push_back(makeToken(LayoutTokenType::Semicolon)); advance(); break;
            case ',': tokens.push_back(makeToken(LayoutTokenType::Comma)); advance(); break;
            case '.': tokens.push_back(makeToken(LayoutTokenType::Dot)); advance(); break;
            case '{': tokens.push_back(makeToken(LayoutTokenType::LeftBrace)); advance(); break;
            case '}': tokens.push_back(makeToken(LayoutTokenType::RightBrace)); advance(); break;
            case '(': tokens.push_back(makeToken(LayoutTokenType::LeftParen)); advance(); break;
            case ')': tokens.push_back(makeToken(LayoutTokenType::RightParen)); advance(); break;
            case '[': tokens.push_back(makeToken(LayoutTokenType::LeftBracket)); advance(); break;
            case ']': tokens.push_back(makeToken(LayoutTokenType::RightBracket)); advance(); break;
            default:
                addError("Unexpected character '" + std::string(1, c) + "'");
                advance();
                break;
        }
    }
    
    tokens.push_back(LayoutToken(LayoutTokenType::EndOfFile, "", line_, column_));
    return tokens;
}

char LayoutLexer::peek() const {
    if (isAtEnd()) return '\0';
    return source_[current_];
}

char LayoutLexer::peekNext() const {
    if (current_ + 1 >= source_.size()) return '\0';
    return source_[current_ + 1];
}

char LayoutLexer::advance() {
    char c = source_[current_++];
    if (c == '\n') {
        line_++;
        column_ = 1;
    } else {
        column_++;
    }
    return c;
}

bool LayoutLexer::match(char expected) {
    if (isAtEnd() || source_[current_] != expected) return false;
    advance();
    return true;
}

bool LayoutLexer::isAtEnd() const {
    return current_ >= source_.size();
}

void LayoutLexer::skipWhitespace() {
    while (!isAtEnd()) {
        char c = peek();
        if (std::isspace(c)) {
            advance();
        } else {
            break;
        }
    }
}

void LayoutLexer::skipComment() {
    while (!isAtEnd() && peek() != '\n') {
        advance();
    }
}

LayoutToken LayoutLexer::makeToken(LayoutTokenType type) {
    return LayoutToken(type, std::string(1, source_[current_ - 1]), line_, column_ - 1);
}

LayoutToken LayoutLexer::number() {
    int start_col = column_;
    std::string num_str;
    
    // Handle negative sign
    if (peek() == '-') {
        num_str += advance();
    }
    
    while (!isAtEnd() && std::isdigit(peek())) {
        num_str += advance();
    }
    
    // Check for decimal
    if (peek() == '.' && std::isdigit(peekNext())) {
        num_str += advance(); // consume '.'
        while (!isAtEnd() && std::isdigit(peek())) {
            num_str += advance();
        }
        return LayoutToken(LayoutTokenType::Float, num_str, line_, start_col, 
                          std::stod(num_str));
    }
    
    return LayoutToken(LayoutTokenType::Integer, num_str, line_, start_col, 
                      std::stoi(num_str));
}

LayoutToken LayoutLexer::stringLiteral() {
    int start_line = line_;
    int start_col = column_;
    advance(); // consume opening quote
    
    std::string value;
    while (!isAtEnd() && peek() != '"') {
        if (peek() == '\n') {
            addError("Unterminated string");
            break;
        }
        if (peek() == '\\') {
            advance();
            if (!isAtEnd()) {
                char escaped = advance();
                switch (escaped) {
                    case 'n': value += '\n'; break;
                    case 't': value += '\t'; break;
                    case 'r': value += '\r'; break;
                    case '\\': value += '\\'; break;
                    case '"': value += '"'; break;
                    default: value += escaped; break;
                }
            }
        } else {
            value += advance();
        }
    }
    
    if (isAtEnd()) {
        addError("Unterminated string");
    } else {
        advance(); // consume closing quote
    }
    
    return LayoutToken(LayoutTokenType::String, value, start_line, start_col, value);
}

LayoutToken LayoutLexer::identifier() {
    int start_col = column_;
    std::string ident;
    
    while (!isAtEnd() && (std::isalnum(peek()) || peek() == '_')) {
        ident += advance();
    }
    
    // Check for keywords
    static const std::unordered_map<std::string, LayoutTokenType> keywords = {
        {"let", LayoutTokenType::Let},
        {"layout", LayoutTokenType::Layout},
        {"workspace", LayoutTokenType::Workspace},
        {"mode", LayoutTokenType::Mode},
        {"rule", LayoutTokenType::Rule},
        {"true", LayoutTokenType::TokTrue},
        {"false", LayoutTokenType::TokFalse}
    };
    
    auto it = keywords.find(ident);
    if (it != keywords.end()) {
        LayoutToken token(it->second, ident, line_, start_col);
        if (ident == "true") {
            token.literal_value = true;
        } else if (ident == "false") {
            token.literal_value = false;
        }
        return token;
    }
    
    return LayoutToken(LayoutTokenType::Identifier, ident, line_, start_col);
}

LayoutToken LayoutLexer::preprocessor() {
    int start_line = line_;
    int start_col = column_;
    advance(); // consume '#'
    
    std::string directive;
    while (!isAtEnd() && (std::isalpha(peek()) || peek() == '_' || peek() == '.')) {
        directive += advance();
    }
    
    // Check for specific preprocessor directives
    if (directive == "include") {
        // Check for "layout" keyword
        skipWhitespace();
        std::string next_word;
        while (!isAtEnd() && (std::isalpha(peek()) || peek() == '_')) {
            next_word += advance();
        }
        
        if (next_word == "layout") {
            return LayoutToken(LayoutTokenType::IncludeLayout, "#include layout", 
                             start_line, start_col);
        }
        return LayoutToken(LayoutTokenType::Include, "#include", start_line, start_col);
    }
    
    if (directive == "included.layout") {
        // Check for "user" keyword
        skipWhitespace();
        std::string next_word;
        while (!isAtEnd() && (std::isalpha(peek()) || peek() == '_')) {
            next_word += advance();
        }
        
        if (next_word == "user") {
            return LayoutToken(LayoutTokenType::IncludeLayoutUser, "#included.layout user", 
                             start_line, start_col);
        }
    }
    
    addError("Unknown preprocessor directive: #" + directive);
    return LayoutToken(LayoutTokenType::Invalid, "#" + directive, start_line, start_col);
}

void LayoutLexer::addError(const std::string& message) {
    errors_.push_back("Line " + std::to_string(line_) + ", Column " + 
                     std::to_string(column_) + ": " + message);
}

// ============================================================================
// LayoutParser Implementation
// ============================================================================

LayoutParser::LayoutParser(std::vector<LayoutToken> tokens)
    : tokens_(std::move(tokens)) {}

std::unique_ptr<layout_ast::LayoutConfigFile> LayoutParser::parse() {
    return configFile();
}

const LayoutToken& LayoutParser::peek() const {
    return tokens_[current_];
}

const LayoutToken& LayoutParser::previous() const {
    return tokens_[current_ - 1];
}

bool LayoutParser::isAtEnd() const {
    return peek().type == LayoutTokenType::EndOfFile;
}

const LayoutToken& LayoutParser::advance() {
    if (!isAtEnd()) current_++;
    return previous();
}

bool LayoutParser::check(LayoutTokenType type) const {
    if (isAtEnd()) return false;
    return peek().type == type;
}

bool LayoutParser::match(std::initializer_list<LayoutTokenType> types) {
    for (auto type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

const LayoutToken& LayoutParser::consume(LayoutTokenType type, const std::string& message) {
    if (check(type)) return advance();
    addError(message);
    throw std::runtime_error(message);
}

std::unique_ptr<layout_ast::LayoutConfigFile> LayoutParser::configFile() {
    auto config = std::make_unique<layout_ast::LayoutConfigFile>();
    
    // Parse includes
    config->includes = includes();
    
    // Parse root block
    if (!isAtEnd()) {
        config->root = block();
    }
    
    return config;
}

std::vector<layout_ast::LayoutIncludeDirective> LayoutParser::includes() {
    std::vector<layout_ast::LayoutIncludeDirective> directives;
    
    while (match({LayoutTokenType::IncludeLayout, LayoutTokenType::IncludeLayoutUser})) {
        layout_ast::LayoutIncludeDirective directive;
        directive.is_user_layout = (previous().type == LayoutTokenType::IncludeLayoutUser);
        
        // Expect layout name (string or identifier)
        if (match({LayoutTokenType::String})) {
            directive.layout_name = std::get<std::string>(previous().literal_value);
        } else if (match({LayoutTokenType::Identifier})) {
            directive.layout_name = previous().lexeme;
        } else {
            addError("Expected layout name after include directive");
            continue;
        }
        
        directives.push_back(directive);
    }
    
    return directives;
}

std::unique_ptr<layout_ast::LayoutBlock> LayoutParser::block() {
    auto blk = std::make_unique<layout_ast::LayoutBlock>();
    
    // Optional block name
    if (match({LayoutTokenType::Identifier})) {
        blk->name = previous().lexeme;
    }
    
    // Expect opening brace
    if (!match({LayoutTokenType::LeftBrace})) {
        addError("Expected '{' to start block");
        return blk;
    }
    
    // Parse statements
    while (!check(LayoutTokenType::RightBrace) && !isAtEnd()) {
        auto stmt = statement();
        if (stmt) {
            blk->statements.push_back(std::move(stmt));
        }
    }
    
    // Expect closing brace
    if (!match({LayoutTokenType::RightBrace})) {
        addError("Expected '}' to close block");
    }
    
    return blk;
}

std::unique_ptr<layout_ast::LayoutStatement> LayoutParser::statement() {
    // Layout rule: layout "workspace_pattern" -> mode { params }
    if (match({LayoutTokenType::Layout})) {
        return layoutRule();
    }
    
    // Assignment: let name = value;
    if (match({LayoutTokenType::Let})) {
        return assignment();
    }
    
    // Named block
    if (check(LayoutTokenType::Identifier)) {
        auto next = tokens_[current_ + 1];
        if (next.type == LayoutTokenType::LeftBrace) {
            return std::make_unique<layout_ast::LayoutStatement>(
                layout_ast::LayoutStatementValue{block()}
            );
        }
    }
    
    addError("Expected statement");
    advance();
    return nullptr;
}

std::unique_ptr<layout_ast::LayoutStatement> LayoutParser::layoutRule() {
    layout_ast::LayoutRule rule;
    
    // Get workspace pattern
    if (match({LayoutTokenType::String})) {
        rule.workspace_pattern = std::get<std::string>(previous().literal_value);
    } else if (match({LayoutTokenType::Identifier})) {
        rule.workspace_pattern = previous().lexeme;
    } else if (match({LayoutTokenType::Integer})) {
        rule.workspace_pattern = std::to_string(std::get<int>(previous().literal_value));
    } else {
        addError("Expected workspace pattern in layout rule");
        return nullptr;
    }
    
    // Expect arrow
    if (!match({LayoutTokenType::Arrow})) {
        addError("Expected '->' after workspace pattern");
        return nullptr;
    }
    
    // Get layout mode
    if (!match({LayoutTokenType::Identifier})) {
        addError("Expected layout mode name");
        return nullptr;
    }
    
    auto mode_opt = layoutModeFromString(previous().lexeme);
    if (!mode_opt) {
        addError("Unknown layout mode: " + previous().lexeme);
        return nullptr;
    }
    rule.mode = *mode_opt;
    
    // Optional parameter block
    if (match({LayoutTokenType::LeftBrace})) {
        while (!check(LayoutTokenType::RightBrace) && !isAtEnd()) {
            if (match({LayoutTokenType::Identifier})) {
                std::string param_name = previous().lexeme;
                
                if (!match({LayoutTokenType::Assign})) {
                    addError("Expected '=' after parameter name");
                    continue;
                }
                
                auto value_expr = expression();
                if (value_expr) {
                    // Evaluate simple expressions
                    try {
                        auto val = std::make_unique<layout_ast::LayoutExpression>(
                            layout_ast::LayoutExpressionValue{
                                layout_ast::IntLiteral{0}
                            }
                        );
                        
                        if (std::holds_alternative<layout_ast::IntLiteral>(value_expr->value)) {
                            rule.parameters[param_name] = 
                                std::get<layout_ast::IntLiteral>(value_expr->value).value;
                        } else if (std::holds_alternative<layout_ast::FloatLiteral>(value_expr->value)) {
                            rule.parameters[param_name] = 
                                std::get<layout_ast::FloatLiteral>(value_expr->value).value;
                        } else if (std::holds_alternative<layout_ast::StringLiteral>(value_expr->value)) {
                            rule.parameters[param_name] = 
                                std::get<layout_ast::StringLiteral>(value_expr->value).value;
                        } else if (std::holds_alternative<layout_ast::BoolLiteral>(value_expr->value)) {
                            rule.parameters[param_name] = 
                                std::get<layout_ast::BoolLiteral>(value_expr->value).value;
                        }
                    } catch (...) {}
                }
                
                match({LayoutTokenType::Semicolon});
            } else {
                advance();
            }
        }
        
        if (!match({LayoutTokenType::RightBrace})) {
            addError("Expected '}' to close layout parameters");
        }
    }
    
    match({LayoutTokenType::Semicolon});
    
    return std::make_unique<layout_ast::LayoutStatement>(
        layout_ast::LayoutStatementValue{std::move(rule)}
    );
}

std::unique_ptr<layout_ast::LayoutStatement> LayoutParser::assignment() {
    layout_ast::LayoutAssignment assignment;
    
    // Get variable name
    if (!match({LayoutTokenType::Identifier})) {
        addError("Expected variable name after 'let'");
        return nullptr;
    }
    assignment.name = previous().lexeme;
    
    // Expect '='
    if (!match({LayoutTokenType::Assign})) {
        addError("Expected '=' after variable name");
        return nullptr;
    }
    
    // Parse value expression
    assignment.value = expression();
    
    match({LayoutTokenType::Semicolon});
    
    return std::make_unique<layout_ast::LayoutStatement>(
        layout_ast::LayoutStatementValue{std::move(assignment)}
    );
}

std::unique_ptr<layout_ast::LayoutExpression> LayoutParser::expression() {
    return logicalOr();
}

std::unique_ptr<layout_ast::LayoutExpression> LayoutParser::logicalOr() {
    auto left = logicalAnd();
    
    while (match({LayoutTokenType::Or})) {
        auto right = logicalAnd();
        auto bin = layout_ast::BinaryOp{
            layout_ast::BinaryOp::Op::Or,
            std::move(left),
            std::move(right)
        };
        left = std::make_unique<layout_ast::LayoutExpression>(
            layout_ast::LayoutExpressionValue{std::move(bin)}
        );
    }
    
    return left;
}

std::unique_ptr<layout_ast::LayoutExpression> LayoutParser::logicalAnd() {
    auto left = equality();
    
    while (match({LayoutTokenType::And})) {
        auto right = equality();
        auto bin = layout_ast::BinaryOp{
            layout_ast::BinaryOp::Op::And,
            std::move(left),
            std::move(right)
        };
        left = std::make_unique<layout_ast::LayoutExpression>(
            layout_ast::LayoutExpressionValue{std::move(bin)}
        );
    }
    
    return left;
}

std::unique_ptr<layout_ast::LayoutExpression> LayoutParser::equality() {
    auto left = comparison();
    
    while (match({LayoutTokenType::Equals, LayoutTokenType::NotEquals})) {
        auto op = (previous().type == LayoutTokenType::Equals) 
            ? layout_ast::BinaryOp::Op::Eq 
            : layout_ast::BinaryOp::Op::Ne;
        auto right = comparison();
        auto bin = layout_ast::BinaryOp{op, std::move(left), std::move(right)};
        left = std::make_unique<layout_ast::LayoutExpression>(
            layout_ast::LayoutExpressionValue{std::move(bin)}
        );
    }
    
    return left;
}

std::unique_ptr<layout_ast::LayoutExpression> LayoutParser::comparison() {
    auto left = term();
    
    while (match({LayoutTokenType::Less, LayoutTokenType::Greater, 
                  LayoutTokenType::LessEqual, LayoutTokenType::GreaterEqual})) {
        layout_ast::BinaryOp::Op op;
        switch (previous().type) {
            case LayoutTokenType::Less: op = layout_ast::BinaryOp::Op::Lt; break;
            case LayoutTokenType::Greater: op = layout_ast::BinaryOp::Op::Gt; break;
            case LayoutTokenType::LessEqual: op = layout_ast::BinaryOp::Op::Le; break;
            case LayoutTokenType::GreaterEqual: op = layout_ast::BinaryOp::Op::Ge; break;
            default: op = layout_ast::BinaryOp::Op::Lt; break;
        }
        auto right = term();
        auto bin = layout_ast::BinaryOp{op, std::move(left), std::move(right)};
        left = std::make_unique<layout_ast::LayoutExpression>(
            layout_ast::LayoutExpressionValue{std::move(bin)}
        );
    }
    
    return left;
}

std::unique_ptr<layout_ast::LayoutExpression> LayoutParser::term() {
    auto left = factor();
    
    while (match({LayoutTokenType::Plus, LayoutTokenType::Minus})) {
        auto op = (previous().type == LayoutTokenType::Plus) 
            ? layout_ast::BinaryOp::Op::Add 
            : layout_ast::BinaryOp::Op::Sub;
        auto right = factor();
        auto bin = layout_ast::BinaryOp{op, std::move(left), std::move(right)};
        left = std::make_unique<layout_ast::LayoutExpression>(
            layout_ast::LayoutExpressionValue{std::move(bin)}
        );
    }
    
    return left;
}

std::unique_ptr<layout_ast::LayoutExpression> LayoutParser::factor() {
    auto left = unary();
    
    while (match({LayoutTokenType::Star, LayoutTokenType::Slash})) {
        auto op = (previous().type == LayoutTokenType::Star) 
            ? layout_ast::BinaryOp::Op::Mul 
            : layout_ast::BinaryOp::Op::Div;
        auto right = unary();
        auto bin = layout_ast::BinaryOp{op, std::move(left), std::move(right)};
        left = std::make_unique<layout_ast::LayoutExpression>(
            layout_ast::LayoutExpressionValue{std::move(bin)}
        );
    }
    
    return left;
}

std::unique_ptr<layout_ast::LayoutExpression> LayoutParser::unary() {
    if (match({LayoutTokenType::Not, LayoutTokenType::Minus})) {
        auto op = (previous().type == LayoutTokenType::Not) 
            ? layout_ast::UnaryOp::Op::Not 
            : layout_ast::UnaryOp::Op::Neg;
        auto operand = unary();
        auto un = layout_ast::UnaryOp{op, std::move(operand)};
        return std::make_unique<layout_ast::LayoutExpression>(
            layout_ast::LayoutExpressionValue{std::move(un)}
        );
    }
    
    return primary();
}

std::unique_ptr<layout_ast::LayoutExpression> LayoutParser::primary() {
    // Integer literal
    if (match({LayoutTokenType::Integer})) {
        return std::make_unique<layout_ast::LayoutExpression>(
            layout_ast::LayoutExpressionValue{
                layout_ast::IntLiteral{std::get<int>(previous().literal_value)}
            }
        );
    }
    
    // Float literal
    if (match({LayoutTokenType::Float})) {
        return std::make_unique<layout_ast::LayoutExpression>(
            layout_ast::LayoutExpressionValue{
                layout_ast::FloatLiteral{std::get<double>(previous().literal_value)}
            }
        );
    }
    
    // String literal
    if (match({LayoutTokenType::String})) {
        return std::make_unique<layout_ast::LayoutExpression>(
            layout_ast::LayoutExpressionValue{
                layout_ast::StringLiteral{std::get<std::string>(previous().literal_value)}
            }
        );
    }
    
    // Boolean literals
    if (match({LayoutTokenType::TokTrue})) {
        return std::make_unique<layout_ast::LayoutExpression>(
            layout_ast::LayoutExpressionValue{layout_ast::BoolLiteral{true}}
        );
    }
    
    if (match({LayoutTokenType::TokFalse})) {
        return std::make_unique<layout_ast::LayoutExpression>(
            layout_ast::LayoutExpressionValue{layout_ast::BoolLiteral{false}}
        );
    }
    
    // Identifier
    if (match({LayoutTokenType::Identifier})) {
        return std::make_unique<layout_ast::LayoutExpression>(
            layout_ast::LayoutExpressionValue{
                layout_ast::Identifier{previous().lexeme}
            }
        );
    }
    
    // Parenthesized expression
    if (match({LayoutTokenType::LeftParen})) {
        auto expr = expression();
        if (!match({LayoutTokenType::RightParen})) {
            addError("Expected ')' after expression");
        }
        return expr;
    }
    
    // Array literal
    if (match({LayoutTokenType::LeftBracket})) {
        layout_ast::ArrayLiteral arr;
        while (!check(LayoutTokenType::RightBracket) && !isAtEnd()) {
            arr.elements.push_back(expression());
            if (!match({LayoutTokenType::Comma})) break;
        }
        if (!match({LayoutTokenType::RightBracket})) {
            addError("Expected ']' after array elements");
        }
        return std::make_unique<layout_ast::LayoutExpression>(
            layout_ast::LayoutExpressionValue{std::move(arr)}
        );
    }
    
    addError("Expected expression");
    return std::make_unique<layout_ast::LayoutExpression>(
        layout_ast::LayoutExpressionValue{layout_ast::IntLiteral{0}}
    );
}

void LayoutParser::addError(const std::string& message) {
    errors_.push_back("Line " + std::to_string(peek().line) + ": " + message);
}

void LayoutParser::synchronize() {
    advance();
    while (!isAtEnd()) {
        if (previous().type == LayoutTokenType::Semicolon) return;
        
        switch (peek().type) {
            case LayoutTokenType::Layout:
            case LayoutTokenType::Let:
                return;
            default:
                advance();
                break;
        }
    }
}

// ============================================================================
// LayoutConfigParser Implementation
// ============================================================================

LayoutConfigParser::LayoutConfigParser(LayoutEngine* engine)
    : engine_(engine) {}

std::filesystem::path LayoutConfigParser::getDefaultLayoutPath() {
    const char* home = std::getenv("HOME");
    if (home) {
        return std::filesystem::path(home) / ".config" / "pblank" / "layout";
    }
    return std::filesystem::path("/etc") / "pblank" / "layout";
}

std::filesystem::path LayoutConfigParser::getSystemLayoutPath() {
    return std::filesystem::path("/etc") / "pblank" / "layout";
}

std::filesystem::path LayoutConfigParser::getUserLayoutPath() {
    const char* home = std::getenv("HOME");
    if (home) {
        return std::filesystem::path(home) / ".config" / "pblank" / "layout";
    }
    return std::filesystem::path(".") / "layout";
}

std::vector<std::string> LayoutConfigParser::getAvailableLayouts() {
    std::vector<std::string> layouts;
    
    // Check user layouts
    auto user_path = getUserLayoutPath();
    if (std::filesystem::exists(user_path)) {
        for (const auto& entry : std::filesystem::directory_iterator(user_path)) {
            if (entry.path().extension() == ".wmi") {
                layouts.push_back(entry.path().stem().string());
            }
        }
    }
    
    // Check system layouts
    auto system_path = getSystemLayoutPath();
    if (std::filesystem::exists(system_path)) {
        for (const auto& entry : std::filesystem::directory_iterator(system_path)) {
            if (entry.path().extension() == ".wmi") {
                std::string name = entry.path().stem().string();
                if (std::find(layouts.begin(), layouts.end(), name) == layouts.end()) {
                    layouts.push_back(name);
                }
            }
        }
    }
    
    std::sort(layouts.begin(), layouts.end());
    return layouts;
}

bool LayoutConfigParser::load(const std::filesystem::path& path) {
    // Check if path exists
    if (!std::filesystem::exists(path)) {
        // Try to create the directory
        try {
            std::filesystem::create_directories(path);
        } catch (const std::exception& e) {
            reportError("Failed to create layout directory: " + std::string(e.what()));
        }
        return true; // No layouts to load, but not an error
    }
    
    // Look for default.wmi or main.wmi
    std::vector<std::string> default_names = {"default", "main", "init"};
    
    for (const auto& name : default_names) {
        auto file_path = path / (name + ".wmi");
        if (std::filesystem::exists(file_path)) {
            return loadLayout(name, false);
        }
    }
    
    // Load all .wmi files in the directory
    bool success = true;
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        if (entry.path().extension() == ".wmi") {
            std::string name = entry.path().stem().string();
            if (!loadLayout(name, false)) {
                success = false;
            }
        }
    }
    
    return success;
}

bool LayoutConfigParser::loadLayout(const std::string& filename, bool is_user) {
    // Check for circular includes
    if (std::find(include_stack_.begin(), include_stack_.end(), filename) != include_stack_.end()) {
        reportError("Circular include detected: " + filename);
        return false;
    }
    
    // Check if already parsed
    if (parsed_layouts_.find(filename) != parsed_layouts_.end()) {
        return true;
    }
    
    // Find the file
    auto file_path = findLayoutFile(filename, is_user);
    if (!file_path) {
        reportError("Layout file not found: " + filename);
        return false;
    }
    
    // Read file contents
    auto content = readFile(*file_path);
    if (!content) {
        reportError("Failed to read layout file: " + filename);
        return false;
    }
    
    // Tokenize
    LayoutLexer lexer(*content);
    auto tokens = lexer.tokenize();
    if (!lexer.getErrors().empty()) {
        reportErrors(lexer.getErrors());
        return false;
    }
    
    // Parse
    LayoutParser parser(std::move(tokens));
    auto ast = parser.parse();
    if (!parser.getErrors().empty()) {
        reportErrors(parser.getErrors());
        return false;
    }
    
    // Add to include stack
    include_stack_.push_back(filename);
    
    // Process includes first
    for (const auto& include : ast->includes) {
        if (!resolveInclude(include)) {
            reportError("Failed to resolve include: " + include.layout_name);
        }
    }
    
    // Remove from include stack
    include_stack_.pop_back();
    
    // Store parsed AST
    parsed_layouts_[filename] = std::move(ast);
    
    // Interpret the AST
    return interpret(*parsed_layouts_[filename]);
}

std::optional<std::filesystem::path> LayoutConfigParser::findLayoutFile(
    const std::string& name, bool is_user) {
    
    std::vector<std::filesystem::path> search_paths;
    
    if (is_user) {
        search_paths.push_back(getUserLayoutPath());
    } else {
        search_paths.push_back(getUserLayoutPath());
        search_paths.push_back(getSystemLayoutPath());
    }
    
    for (const auto& path : search_paths) {
        auto full_path = path / (name + ".wmi");
        if (std::filesystem::exists(full_path)) {
            return full_path;
        }
    }
    
    return std::nullopt;
}

std::optional<std::string> LayoutConfigParser::readFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool LayoutConfigParser::resolveInclude(const layout_ast::LayoutIncludeDirective& include) {
    return loadLayout(include.layout_name, include.is_user_layout);
}

bool LayoutConfigParser::interpret(const layout_ast::LayoutConfigFile& ast) {
    if (ast.root) {
        evaluateBlock(*ast.root);
    }
    return true;
}

void LayoutConfigParser::evaluateBlock(const layout_ast::LayoutBlock& block) {
    for (const auto& stmt : block.statements) {
        evaluateStatement(*stmt);
    }
}

void LayoutConfigParser::evaluateStatement(const layout_ast::LayoutStatement& stmt) {
    std::visit([this](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        
        if constexpr (std::is_same_v<T, layout_ast::LayoutAssignment>) {
            // Handle assignment
            auto value = evaluateExpression(*arg.value);
            
            if (arg.name == "default_mode") {
                if (std::holds_alternative<std::string>(value)) {
                    auto mode = layoutModeFromString(std::get<std::string>(value));
                    if (mode) {
                        config_.default_mode = *mode;
                    }
                }
            } else if (arg.name == "gap_size") {
                if (std::holds_alternative<int>(value)) {
                    config_.bsp_params.gap_size = std::get<int>(value);
                    config_.master_stack_params.gap_size = std::get<int>(value);
                    config_.centered_master_params.gap_size = std::get<int>(value);
                    config_.dynamic_grid_params.gap_size = std::get<int>(value);
                    config_.dwindle_spiral_params.gap_size = std::get<int>(value);
                }
            } else if (arg.name == "border_width") {
                if (std::holds_alternative<int>(value)) {
                    config_.bsp_params.border_width = std::get<int>(value);
                }
            } else if (arg.name == "padding") {
                if (std::holds_alternative<int>(value)) {
                    config_.bsp_params.padding = std::get<int>(value);
                }
            } else if (arg.name == "dwindle") {
                if (std::holds_alternative<bool>(value)) {
                    config_.bsp_params.dwindle = std::get<bool>(value);
                }
            } else if (arg.name == "master_ratio") {
                if (std::holds_alternative<double>(value)) {
                    config_.master_stack_params.master_ratio = std::get<double>(value);
                }
            } else if (arg.name == "max_master") {
                if (std::holds_alternative<int>(value)) {
                    config_.master_stack_params.max_master = std::get<int>(value);
                }
            } else if (arg.name == "center_ratio") {
                if (std::holds_alternative<double>(value)) {
                    config_.centered_master_params.center_ratio = std::get<double>(value);
                }
            } else if (arg.name == "max_center") {
                if (std::holds_alternative<int>(value)) {
                    config_.centered_master_params.max_center = std::get<int>(value);
                }
            } else if (arg.name == "center_on_focus") {
                if (std::holds_alternative<bool>(value)) {
                    config_.centered_master_params.center_on_focus = std::get<bool>(value);
                }
            } else if (arg.name == "prefer_horizontal") {
                if (std::holds_alternative<bool>(value)) {
                    config_.dynamic_grid_params.prefer_horizontal = std::get<bool>(value);
                }
            } else if (arg.name == "min_cell_width") {
                if (std::holds_alternative<int>(value)) {
                    config_.dynamic_grid_params.min_cell_width = std::get<int>(value);
                }
            } else if (arg.name == "min_cell_height") {
                if (std::holds_alternative<int>(value)) {
                    config_.dynamic_grid_params.min_cell_height = std::get<int>(value);
                }
            } else if (arg.name == "initial_ratio") {
                if (std::holds_alternative<double>(value)) {
                    config_.dwindle_spiral_params.initial_ratio = std::get<double>(value);
                }
            } else if (arg.name == "ratio_increment") {
                if (std::holds_alternative<double>(value)) {
                    config_.dwindle_spiral_params.ratio_increment = std::get<double>(value);
                }
            } else if (arg.name == "shift_by_focus") {
                if (std::holds_alternative<bool>(value)) {
                    config_.dwindle_spiral_params.shift_by_focus = std::get<bool>(value);
                }
            } else if (arg.name == "tab_height") {
                if (std::holds_alternative<int>(value)) {
                    config_.tabbed_stacked_params.tab_height = std::get<int>(value);
                }
            } else if (arg.name == "tab_min_width") {
                if (std::holds_alternative<int>(value)) {
                    config_.tabbed_stacked_params.tab_min_width = std::get<int>(value);
                }
            } else if (arg.name == "show_focused_only") {
                if (std::holds_alternative<bool>(value)) {
                    config_.tabbed_stacked_params.show_focused_only = std::get<bool>(value);
                }
            } else if (arg.name == "tab_at_top") {
                if (std::holds_alternative<bool>(value)) {
                    config_.tabbed_stacked_params.tab_at_top = std::get<bool>(value);
                }
            } else if (arg.name == "focused_border_color") {
                if (std::holds_alternative<std::string>(value)) {
                    std::string color = std::get<std::string>(value);
                    if (color.substr(0, 2) == "0x") {
                        config_.focused_border_color = std::stoul(color, nullptr, 16);
                    } else {
                        config_.focused_border_color = std::stoul(color, nullptr, 16);
                    }
                } else if (std::holds_alternative<int>(value)) {
                    config_.focused_border_color = static_cast<unsigned long>(std::get<int>(value));
                }
            } else if (arg.name == "unfocused_border_color") {
                if (std::holds_alternative<std::string>(value)) {
                    std::string color = std::get<std::string>(value);
                    if (color.substr(0, 2) == "0x") {
                        config_.unfocused_border_color = std::stoul(color, nullptr, 16);
                    } else {
                        config_.unfocused_border_color = std::stoul(color, nullptr, 16);
                    }
                } else if (std::holds_alternative<int>(value)) {
                    config_.unfocused_border_color = static_cast<unsigned long>(std::get<int>(value));
                }
            } else if (arg.name == "cycle_direction") {
                if (std::holds_alternative<std::string>(value)) {
                    auto dir = cycleDirectionFromString(std::get<std::string>(value));
                    if (dir) {
                        config_.cycle_direction = *dir;
                    }
                }
            } else if (arg.name == "wrap_cycle") {
                if (std::holds_alternative<bool>(value)) {
                    config_.wrap_cycle = std::get<bool>(value);
                }
            }
        } else if constexpr (std::is_same_v<T, std::unique_ptr<layout_ast::LayoutBlock>>) {
            if (arg) {
                evaluateBlock(*arg);
            }
        } else if constexpr (std::is_same_v<T, layout_ast::LayoutRule>) {
            // Parse workspace pattern and apply layout rule
            auto workspaces = parseWorkspacePattern(arg.workspace_pattern);
            for (int ws : workspaces) {
                config_.workspace_modes[ws] = arg.mode;
            }
            config_.layout_rules.push_back(arg);
        }
    }, stmt.value);
}

std::variant<int, double, std::string, bool, std::vector<std::string>>
LayoutConfigParser::evaluateExpression(const layout_ast::LayoutExpression& expr) {
    return std::visit([this](auto&& arg) -> 
        std::variant<int, double, std::string, bool, std::vector<std::string>> {
        using T = std::decay_t<decltype(arg)>;
        
        if constexpr (std::is_same_v<T, layout_ast::IntLiteral>) {
            return arg.value;
        } else if constexpr (std::is_same_v<T, layout_ast::FloatLiteral>) {
            return arg.value;
        } else if constexpr (std::is_same_v<T, layout_ast::StringLiteral>) {
            return arg.value;
        } else if constexpr (std::is_same_v<T, layout_ast::BoolLiteral>) {
            return arg.value;
        } else if constexpr (std::is_same_v<T, layout_ast::Identifier>) {
            // Look up variable
            return std::string(arg.name);
        } else if constexpr (std::is_same_v<T, layout_ast::BinaryOp>) {
            auto left = evaluateExpression(*arg.left);
            auto right = evaluateExpression(*arg.right);
            
            // Simple arithmetic for numeric types
            if (std::holds_alternative<int>(left) && std::holds_alternative<int>(right)) {
                int l = std::get<int>(left);
                int r = std::get<int>(right);
                switch (arg.op) {
                    case layout_ast::BinaryOp::Op::Add: return l + r;
                    case layout_ast::BinaryOp::Op::Sub: return l - r;
                    case layout_ast::BinaryOp::Op::Mul: return l * r;
                    case layout_ast::BinaryOp::Op::Div: return r != 0 ? l / r : 0;
                    case layout_ast::BinaryOp::Op::Lt: return l < r;
                    case layout_ast::BinaryOp::Op::Gt: return l > r;
                    case layout_ast::BinaryOp::Op::Le: return l <= r;
                    case layout_ast::BinaryOp::Op::Ge: return l >= r;
                    case layout_ast::BinaryOp::Op::Eq: return l == r;
                    case layout_ast::BinaryOp::Op::Ne: return l != r;
                    default: break;
                }
            }
            
            if (std::holds_alternative<double>(left) && std::holds_alternative<double>(right)) {
                double l = std::get<double>(left);
                double r = std::get<double>(right);
                switch (arg.op) {
                    case layout_ast::BinaryOp::Op::Add: return l + r;
                    case layout_ast::BinaryOp::Op::Sub: return l - r;
                    case layout_ast::BinaryOp::Op::Mul: return l * r;
                    case layout_ast::BinaryOp::Op::Div: return r != 0 ? l / r : 0.0;
                    case layout_ast::BinaryOp::Op::Lt: return l < r;
                    case layout_ast::BinaryOp::Op::Gt: return l > r;
                    case layout_ast::BinaryOp::Op::Le: return l <= r;
                    case layout_ast::BinaryOp::Op::Ge: return l >= r;
                    case layout_ast::BinaryOp::Op::Eq: return l == r;
                    case layout_ast::BinaryOp::Op::Ne: return l != r;
                    default: break;
                }
            }
            
            return 0;
        } else if constexpr (std::is_same_v<T, layout_ast::UnaryOp>) {
            auto operand = evaluateExpression(*arg.operand);
            if (arg.op == layout_ast::UnaryOp::Op::Neg) {
                if (std::holds_alternative<int>(operand)) {
                    return -std::get<int>(operand);
                } else if (std::holds_alternative<double>(operand)) {
                    return -std::get<double>(operand);
                }
            } else if (arg.op == layout_ast::UnaryOp::Op::Not) {
                if (std::holds_alternative<bool>(operand)) {
                    return !std::get<bool>(operand);
                }
            }
            return 0;
        } else if constexpr (std::is_same_v<T, layout_ast::MemberAccess>) {
            // Handle member access (e.g., config.gap_size)
            return std::string("");
        } else if constexpr (std::is_same_v<T, layout_ast::ArrayLiteral>) {
            std::vector<std::string> result;
            for (const auto& elem : arg.elements) {
                auto val = evaluateExpression(*elem);
                if (std::holds_alternative<std::string>(val)) {
                    result.push_back(std::get<std::string>(val));
                }
            }
            return result;
        }
        
        return 0;
    }, expr.value);
}

std::vector<int> LayoutConfigParser::parseWorkspacePattern(const std::string& pattern) {
    std::vector<int> workspaces;
    
    if (pattern == "*" || pattern == "all") {
        // All workspaces (1-12)
        for (int i = 1; i <= 12; ++i) {
            workspaces.push_back(i);
        }
        return workspaces;
    }
    
    // Check for range pattern (e.g., "1-5")
    std::regex range_regex(R"((\d+)-(\d+))");
    std::smatch range_match;
    if (std::regex_match(pattern, range_match, range_regex)) {
        int start = std::stoi(range_match[1].str());
        int end = std::stoi(range_match[2].str());
        for (int i = start; i <= end; ++i) {
            if (i >= 1 && i <= 12) {
                workspaces.push_back(i);
            }
        }
        return workspaces;
    }
    
    // Check for comma-separated list (e.g., "1,3,5,7")
    std::regex list_regex(R"((\d+)(?:,(\d+))*)");
    std::smatch list_match;
    if (std::regex_match(pattern, list_match, list_regex)) {
        std::stringstream ss(pattern);
        std::string token;
        while (std::getline(ss, token, ',')) {
            int ws = std::stoi(token);
            if (ws >= 1 && ws <= 12) {
                workspaces.push_back(ws);
            }
        }
        return workspaces;
    }
    
    // Single workspace number
    try {
        int ws = std::stoi(pattern);
        if (ws >= 1 && ws <= 12) {
            workspaces.push_back(ws);
        }
    } catch (...) {
        // Invalid pattern
    }
    
    return workspaces;
}

void LayoutConfigParser::applyToEngine() {
    if (!engine_) return;
    
    // Apply global settings
    engine_->setGapSize(config_.bsp_params.gap_size);
    engine_->setBorderWidth(config_.bsp_params.border_width);
    engine_->setBorderColors(config_.focused_border_color, config_.unfocused_border_color);
    engine_->setDwindleMode(config_.bsp_params.dwindle);
}

void LayoutConfigParser::reportError(const std::string& message) {
    std::cerr << "[LayoutConfigParser Error] " << message << std::endl;
}

void LayoutConfigParser::reportErrors(const std::vector<std::string>& errors) {
    for (const auto& error : errors) {
        std::cerr << "[LayoutConfigParser] " << error << std::endl;
    }
}

} // namespace pblank