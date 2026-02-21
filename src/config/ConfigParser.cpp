#include "pointblank/config/ConfigParser.hpp"
#include "pointblank/core/Toaster.hpp"
#include <X11/Xlib.h>
#include <fstream>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <iostream>

namespace pblank {





Lexer::Lexer(std::string source) : source_(std::move(source)) {}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    
    std::cerr << "[Lexer] tokenize() source starts with: '" << source_.substr(0, 50) << "'" << std::endl;
    
    
    tokens.reserve(source_.size() / 8);
    
    while (!isAtEnd()) {
        skipWhitespace();
        if (isAtEnd()) break;
        
        char c = peek();
        
        
        if (c == '/' && peekNext() == '/') {
            skipComment();
            continue;
        }
        
        if (c == '/' && peekNext() == '*') {
            
            advance(); advance(); 
            while (!isAtEnd() && !(peek() == '*' && peekNext() == '/')) {
                advance();
            }
            if (!isAtEnd()) {
                advance(); advance(); 
            }
            continue;
        }
        
        
        if (c == '#') {
            tokens.emplace_back(preprocessor());
            continue;
        }
        
        
        if (std::isdigit(c)) {
            tokens.emplace_back(number());
            continue;
        }
        
        
        if (c == '"') {
            tokens.emplace_back(string());
            continue;
        }
        
        
        if (std::isalpha(c) || c == '_') {
            tokens.emplace_back(identifier());
            continue;
        }
        
        
        switch (c) {
            case '(': tokens.emplace_back(makeToken(TokenType::LeftParen)); advance(); break;
            case ')': tokens.emplace_back(makeToken(TokenType::RightParen)); advance(); break;
            case '{': tokens.emplace_back(makeToken(TokenType::LeftBrace)); advance(); break;
            case '}': tokens.emplace_back(makeToken(TokenType::RightBrace)); advance(); break;
            case '[': tokens.emplace_back(makeToken(TokenType::LeftBracket)); advance(); break;
            case ']': tokens.emplace_back(makeToken(TokenType::RightBracket)); advance(); break;
            case ':': tokens.emplace_back(makeToken(TokenType::Colon)); advance(); break;
            case ';': tokens.emplace_back(makeToken(TokenType::Semicolon)); advance(); break;
            case ',': tokens.emplace_back(makeToken(TokenType::Comma)); advance(); break;
            case '.': tokens.emplace_back(makeToken(TokenType::Dot)); advance(); break;
            case '+': tokens.emplace_back(makeToken(TokenType::Plus)); advance(); break;
            case '-': tokens.emplace_back(makeToken(TokenType::Minus)); advance(); break;
            case '*': tokens.emplace_back(makeToken(TokenType::Star)); advance(); break;
            case '/': tokens.emplace_back(makeToken(TokenType::Slash)); advance(); break;
            
            case '=':
                advance();
                if (peek() == '=') {
                    advance();
                    tokens.emplace_back(makeToken(TokenType::Equals));
                } else {
                    tokens.emplace_back(makeToken(TokenType::Assign));
                }
                break;
            
            case '!':
                advance();
                if (peek() == '=') {
                    advance();
                    tokens.emplace_back(makeToken(TokenType::NotEquals));
                } else {
                    tokens.emplace_back(makeToken(TokenType::Not));
                }
                break;
            
            case '<':
                advance();
                if (peek() == '=') {
                    advance();
                    tokens.emplace_back(makeToken(TokenType::LessEqual));
                } else {
                    tokens.emplace_back(makeToken(TokenType::Less));
                }
                break;
            
            case '>':
                advance();
                if (peek() == '=') {
                    advance();
                    tokens.emplace_back(makeToken(TokenType::GreaterEqual));
                } else {
                    tokens.emplace_back(makeToken(TokenType::Greater));
                }
                break;
            
            case '&':
                advance();
                if (peek() == '&') {
                    advance();
                    tokens.emplace_back(makeToken(TokenType::And));
                } else {
                    addError("Expected '&' after '&'");
                }
                break;
            
            case '|':
                advance();
                if (peek() == '|') {
                    advance();
                    tokens.emplace_back(makeToken(TokenType::Or));
                } else {
                    addError("Expected '|' after '|'");
                }
                break;
            
            default:
                addError(std::string("Unexpected character: ") + c);
                advance();
                break;
        }
    }
    
    tokens.emplace_back(Token(TokenType::EndOfFile, "", line_, column_));
    return tokens;
}

char Lexer::peek() const {
    if (isAtEnd()) return '\0';
    return source_[current_];
}

inline char Lexer::peekNext() const {
    if (current_ + 1 >= source_.length()) return '\0';
    return source_[current_ + 1];
}

inline char Lexer::advance() {
    char c = source_[current_++];
    column_++;
    if (c == '\n') {
        line_++;
        column_ = 1;
    }
    return c;
}

inline bool Lexer::match(char expected) {
    if (isAtEnd() || peek() != expected) return false;
    advance();
    return true;
}

inline bool Lexer::isAtEnd() const {
    return current_ >= source_.length();
}

inline void Lexer::skipWhitespace() {
    while (!isAtEnd() && std::isspace(peek())) {
        advance();
    }
}

inline void Lexer::skipComment() {
    while (!isAtEnd() && peek() != '\n') {
        advance();
    }
}

Token Lexer::makeToken(TokenType type) {
    
    
    
    return Token(type, std::string{}, line_, column_);
}

Token Lexer::number() {
    int start_line = line_;
    int start_col = column_;
    size_t start = current_;
    
    while (!isAtEnd() && std::isdigit(peek())) {
        advance();
    }
    
    
    if (!isAtEnd() && peek() == '.' && std::isdigit(peekNext())) {
        advance(); 
        while (!isAtEnd() && std::isdigit(peek())) {
            advance();
        }
        std::string num(source_.data() + start, current_ - start);
        return Token(TokenType::Float, num, start_line, start_col, std::stod(num));
    }
    
    std::string num(source_.data() + start, current_ - start);
    return Token(TokenType::Integer, num, start_line, start_col, std::stoi(num));
}

Token Lexer::string() {
    int start_line = line_;
    int start_col = column_;
    
    advance(); 
    
    std::string str;
    str.reserve(32);  
    while (!isAtEnd() && peek() != '"') {
        if (peek() == '\\') {
            advance();
            if (!isAtEnd()) {
                char c = advance();
                switch (c) {
                    case 'n': str += '\n'; break;
                    case 't': str += '\t'; break;
                    case '"': str += '"'; break;
                    case '\\': str += '\\'; break;
                    default: str += c; break;
                }
            }
        } else {
            str += advance();
        }
    }
    
    if (isAtEnd()) {
        addError("Unterminated string");
        return Token(TokenType::Invalid, str, start_line, start_col);
    }
    
    advance(); 
    
    return Token(TokenType::String, str, start_line, start_col, str);
}

Token Lexer::identifier() {
    int start_line = line_;
    int start_col = column_;
    size_t start = current_;
    
    while (!isAtEnd() && (std::isalnum(peek()) || peek() == '_')) {
        advance();
    }
    
    
    std::string text(source_.data() + start, current_ - start);
    
    std::string_view text_view(text);
    TokenType type = TokenType::Identifier;
    if (text_view == "let") type = TokenType::Let;
    else if (text_view == "if") type = TokenType::If;
    else if (text_view == "else") type = TokenType::Else;
    else if (text_view == "exec") type = TokenType::Exec;
    else if (text_view == "true") {
        return Token(TokenType::TokTrue, text, start_line, start_col, true);
    }
    else if (text_view == "false") {
        return Token(TokenType::TokFalse, text, start_line, start_col, false);
    }
    
    return Token(type, std::move(text), start_line, start_col);
}

Token Lexer::preprocessor() {
    int start_line = line_;
    int start_col = column_;
    
    advance(); 
    
    size_t dir_start = current_;
    while (!isAtEnd() && std::isalpha(peek())) {
        advance();
    }
    std::string directive(source_.data() + dir_start, current_ - dir_start);
    
    skipWhitespace();
    
    size_t name_start = current_;
    while (!isAtEnd() && !std::isspace(peek())) {
        advance();
    }
    std::string name(source_.data() + name_start, current_ - name_start);
    
    std::string_view dir_view(directive);
    TokenType type = TokenType::Invalid;
    if (dir_view == "import") {
        type = TokenType::Import;
    } else if (dir_view == "include") {
        type = TokenType::Include;
    } else {
        addError("Unknown preprocessor directive: #" + directive);
    }
    
    return Token(type, name, start_line, start_col, name);
}

void Lexer::addError(const std::string& message) {
    std::ostringstream oss;
    oss << "Line " << line_ << ", Col " << column_ << ": " << message;
    errors_.push_back(oss.str());
}





Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

std::unique_ptr<ast::ConfigFile> Parser::parse() {
    return configFile();
}

const Token& Parser::peek() const {
    return tokens_[current_];
}

inline const Token& Parser::previous() const {
    return tokens_[current_ - 1];
}

inline bool Parser::isAtEnd() const {
    return peek().type == TokenType::EndOfFile;
}

inline const Token& Parser::advance() {
    if (!isAtEnd()) current_++;
    return previous();
}

inline bool Parser::check(TokenType type) const {
    if (isAtEnd()) return false;
    return peek().type == type;
}

inline bool Parser::match(std::initializer_list<TokenType> types) {
    for (auto type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

const Token& Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    
    addError(message);
    return peek();
}

std::unique_ptr<ast::ConfigFile> Parser::configFile() {
    auto config = std::make_unique<ast::ConfigFile>();
    
    
    auto toplevel_block = std::make_unique<ast::Block>();
    toplevel_block->name = "toplevel";
    
    std::cerr << "[Parser] configFile() starting, current token: " << peek().lexeme << std::endl;
    
    
    while (!isAtEnd()) {
        std::cerr << "[Parser] configFile loop, token type: " << (int)peek().type << " value: " << peek().lexeme << std::endl;
        
        
        if (check(TokenType::Import) || check(TokenType::Include)) {
            auto importList = imports();
            for (auto& imp : importList) {
                config->imports.emplace_back(std::move(imp));
            }
            continue;
        }
        
        
        if (check(TokenType::Identifier)) {
            std::cerr << "[Parser] Found Identifier: " << peek().lexeme << std::endl;
            size_t saved = current_;
            advance(); 
            
            if (check(TokenType::Colon)) {
                advance(); 
                if (check(TokenType::LeftBrace)) {
                    
                    std::cerr << "[Parser] Found block pattern, calling block()" << std::endl;
                    current_ = saved;
                    auto blk = block();
                    if (blk) {
                        std::cerr << "[Parser] Block parsed, name: " << blk->name << std::endl;
                        
                        if (!config->root) {
                            config->root = std::move(blk);
                        } else {
                            config->blocks.emplace_back(std::move(blk));
                        }
                    }
                    continue;
                } else {
                    std::cerr << "[Parser] Colon but no LeftBrace, not a block" << std::endl;
                }
            }
            
            current_ = saved;
        }
        
        
        auto stmt = statement();
        if (stmt) {
            
            toplevel_block->statements.emplace_back(std::move(stmt));
        } else {
            break;
        }
    }
    
    
    if (!toplevel_block->statements.empty()) {
        config->blocks.emplace_back(std::move(toplevel_block));
    }
    
    return config;
}

std::vector<ast::ImportDirective> Parser::imports() {
    std::vector<ast::ImportDirective> result;
    
    
    
    while (match({TokenType::Import, TokenType::Include})) {
        TokenType type = previous().type;
        std::string name;
        
        
        if (auto* val = std::get_if<std::string>(&previous().literal_value)) {
            name = *val;
        }
        
        
        result.push_back(ast::ImportDirective{
            name,
            type == TokenType::Import
        });
    }
    
    return result;
}

std::unique_ptr<ast::Block> Parser::block() {
    std::cerr << "[Parser] block() called, current token: " << peek().lexeme << std::endl;
    if (!check(TokenType::Identifier)) {
        addError("Expected block name, got: " + std::string(peek().lexeme));
        return nullptr;
    }
    
    std::string name{advance().lexeme};
    std::cerr << "[Parser] block name: " << name << std::endl;
    
    consume(TokenType::Colon, "Expected ':' after block name");
    consume(TokenType::LeftBrace, "Expected '{' to start block");
    
    auto blk = std::make_unique<ast::Block>();
    blk->name = name;
    
    while (!check(TokenType::RightBrace) && !isAtEnd()) {
        auto stmt = statement();
        if (stmt) {
            blk->statements.push_back(std::move(stmt));
        }
    }
    
    consume(TokenType::RightBrace, "Expected '}' to close block");
    
    
    match({TokenType::Semicolon});
    
    return blk;
}

std::unique_ptr<ast::Statement> Parser::statement() {
    
    if (match({TokenType::Let})) {
        return letStatement();
    }
    
    
    if (match({TokenType::If})) {
        return ifStatement();
    }
    
    
    if (match({TokenType::Exec})) {
        consume(TokenType::Colon, "Expected ':' after exec");
        if (!check(TokenType::String)) {
            addError("Expected string after exec:");
            return nullptr;
        }
        
        std::string command = std::get<std::string>(advance().literal_value);
        
        auto stmt = std::make_unique<ast::Statement>();
        stmt->value = ast::ExecDirective{command};
        return stmt;
    }
    
    
    
    if (check(TokenType::String)) {
        
        size_t saved = current_;
        advance(); 
        
        if (check(TokenType::Colon)) {
            
            current_ = saved;
        } else {
            
            std::string command = std::get<std::string>(previous().literal_value);
            
            auto stmt = std::make_unique<ast::Statement>();
            stmt->value = ast::ExecDirective{command};
            return stmt;
        }
    }
    
    
    
    if (check(TokenType::Identifier)) {
        
        size_t saved = current_;
        std::string first_token{advance().lexeme};
        
        if (check(TokenType::Colon)) {
            
            current_ = saved;
        } else {
            
            std::string command = first_token;
            
            
            
            
            while (!check(TokenType::Semicolon) && !check(TokenType::RightBrace) && !isAtEnd()) {
                Token tok = advance();
                
                
                if (tok.type == TokenType::Minus) {
                    command += std::string{tok.lexeme};  
                } else {
                    command += std::string{" "} + std::string{tok.lexeme};
                }
            }
            
            
            
            match({TokenType::Semicolon});
            
            auto stmt = std::make_unique<ast::Statement>();
            stmt->value = ast::ExecDirective{command};
            return stmt;
        }
    }
    
    
    if (check(TokenType::Identifier) || check(TokenType::String)) {
        
        size_t saved = current_;
        advance(); 
        
        if (check(TokenType::Colon)) {
            advance(); 
            if (check(TokenType::LeftBrace)) {
                
                current_ = saved;
                
                
                if (!check(TokenType::Identifier)) {
                    addError("Block name must be an identifier");
                    return nullptr;
                }
                auto blk = block();
                auto stmt = std::make_unique<ast::Statement>();
                stmt->value = std::move(*blk);
                return stmt;
            }
        }
        
        
        current_ = saved;
        return assignment();
    }
    
    addError("Expected statement");
    synchronize();
    return nullptr;
}

std::unique_ptr<ast::Statement> Parser::ifStatement() {
    consume(TokenType::LeftParen, "Expected '(' after if");
    auto condition = expression();
    consume(TokenType::RightParen, "Expected ')' after condition");
    
    consume(TokenType::LeftBrace, "Expected '{' after if condition");
    
    std::vector<std::unique_ptr<ast::Statement>> then_branch;
    while (!check(TokenType::RightBrace) && !isAtEnd()) {
        auto stmt = statement();
        if (stmt) {
            then_branch.push_back(std::move(stmt));
        }
    }
    
    consume(TokenType::RightBrace, "Expected '}' to close if block");
    
    std::vector<std::unique_ptr<ast::Statement>> else_branch;
    if (match({TokenType::Else})) {
        consume(TokenType::LeftBrace, "Expected '{' after else");
        
        while (!check(TokenType::RightBrace) && !isAtEnd()) {
            auto stmt = statement();
            if (stmt) {
                else_branch.push_back(std::move(stmt));
            }
        }
        
        consume(TokenType::RightBrace, "Expected '}' to close else block");
    }
    
    
    match({TokenType::Semicolon});
    
    auto stmt = std::make_unique<ast::Statement>();
    stmt->value = ast::IfStatement{
        std::move(condition),
        std::move(then_branch),
        std::move(else_branch)
    };
    
    return stmt;
}

std::unique_ptr<ast::Statement> Parser::assignment() {
    std::string name;
    
    
    
    if (match({TokenType::Identifier})) {
        name = previous().lexeme;
    } else if (match({TokenType::String})) {
        if (auto* val = std::get_if<std::string>(&previous().literal_value)) {
            name = *val;
        }
    } else {
        addError("Expected identifier or string for assignment name");
        return nullptr;
    }
    
    consume(TokenType::Colon, "Expected ':' after identifier");
    
    
    if (match({TokenType::Exec})) {
        consume(TokenType::Colon, "Expected ':' after exec");
        if (!check(TokenType::String)) {
            addError("Expected string after exec:");
            return nullptr;
        }
        
        std::string command = std::get<std::string>(advance().literal_value);
        
        
        auto stmt = std::make_unique<ast::Statement>();
        auto expr = std::make_unique<ast::Expression>();
        expr->value = ast::StringLiteral{"exec: " + command};
        stmt->value = ast::Assignment{name, std::move(expr)};
        return stmt;
    }
    
    auto value = expression();
    
    
    match({TokenType::Semicolon});
    
    auto stmt = std::make_unique<ast::Statement>();
    stmt->value = ast::Assignment{name, std::move(value)};
    
    return stmt;
}

std::unique_ptr<ast::Statement> Parser::letStatement() {
    
    if (!check(TokenType::Identifier)) {
        addError("Expected identifier after 'let'");
        return nullptr;
    }
    
    std::string name{advance().lexeme};
    
    consume(TokenType::Assign, "Expected '=' after identifier");
    
    auto value = expression();
    
    
    match({TokenType::Semicolon});
    
    auto stmt = std::make_unique<ast::Statement>();
    stmt->value = ast::VariableDeclaration{name, std::move(value)};
    
    
    
    
    return stmt;
}

std::unique_ptr<ast::Expression> Parser::expression() {
    return logicalOr();
}

std::unique_ptr<ast::Expression> Parser::logicalOr() {
    auto left = logicalAnd();
    
    while (match({TokenType::Or})) {
        auto right = logicalAnd();
        auto expr = std::make_unique<ast::Expression>();
        expr->value = ast::BinaryOp{
            ast::BinaryOp::Op::Or,
            std::move(left),
            std::move(right)
        };
        left = std::move(expr);
    }
    
    return left;
}

std::unique_ptr<ast::Expression> Parser::logicalAnd() {
    auto left = equality();
    
    while (match({TokenType::And})) {
        auto right = equality();
        auto expr = std::make_unique<ast::Expression>();
        expr->value = ast::BinaryOp{
            ast::BinaryOp::Op::And,
            std::move(left),
            std::move(right)
        };
        left = std::move(expr);
    }
    
    return left;
}

std::unique_ptr<ast::Expression> Parser::equality() {
    auto left = comparison();
    
    while (match({TokenType::Equals, TokenType::NotEquals})) {
        auto op = previous().type == TokenType::Equals ? 
            ast::BinaryOp::Op::Eq : ast::BinaryOp::Op::Ne;
        
        auto right = comparison();
        auto expr = std::make_unique<ast::Expression>();
        expr->value = ast::BinaryOp{op, std::move(left), std::move(right)};
        left = std::move(expr);
    }
    
    return left;
}

std::unique_ptr<ast::Expression> Parser::comparison() {
    auto left = term();
    
    while (match({TokenType::Less, TokenType::Greater, 
                  TokenType::LessEqual, TokenType::GreaterEqual})) {
        ast::BinaryOp::Op op;
        switch (previous().type) {
            case TokenType::Less: op = ast::BinaryOp::Op::Lt; break;
            case TokenType::Greater: op = ast::BinaryOp::Op::Gt; break;
            case TokenType::LessEqual: op = ast::BinaryOp::Op::Le; break;
            case TokenType::GreaterEqual: op = ast::BinaryOp::Op::Ge; break;
            default: op = ast::BinaryOp::Op::Eq; break;
        }
        
        auto right = term();
        auto expr = std::make_unique<ast::Expression>();
        expr->value = ast::BinaryOp{op, std::move(left), std::move(right)};
        left = std::move(expr);
    }
    
    return left;
}

std::unique_ptr<ast::Expression> Parser::term() {
    auto left = factor();
    
    while (match({TokenType::Plus, TokenType::Minus})) {
        auto op = previous().type == TokenType::Plus ? 
            ast::BinaryOp::Op::Add : ast::BinaryOp::Op::Sub;
        
        auto right = factor();
        auto expr = std::make_unique<ast::Expression>();
        expr->value = ast::BinaryOp{op, std::move(left), std::move(right)};
        left = std::move(expr);
    }
    
    return left;
}

std::unique_ptr<ast::Expression> Parser::factor() {
    auto left = unary();
    
    while (match({TokenType::Star, TokenType::Slash})) {
        auto op = previous().type == TokenType::Star ? 
            ast::BinaryOp::Op::Mul : ast::BinaryOp::Op::Div;
        
        auto right = unary();
        auto expr = std::make_unique<ast::Expression>();
        expr->value = ast::BinaryOp{op, std::move(left), std::move(right)};
        left = std::move(expr);
    }
    
    return left;
}

std::unique_ptr<ast::Expression> Parser::unary() {
    if (match({TokenType::Not, TokenType::Minus})) {
        auto op = previous().type == TokenType::Not ? 
            ast::UnaryOp::Op::Not : ast::UnaryOp::Op::Neg;
        
        auto operand = unary();
        auto expr = std::make_unique<ast::Expression>();
        expr->value = ast::UnaryOp{op, std::move(operand)};
        return expr;
    }
    
    return primary();
}

std::unique_ptr<ast::Expression> Parser::primary() {
    
    if (match({TokenType::Integer})) {
        auto expr = std::make_unique<ast::Expression>();
        if (auto* val = std::get_if<int>(&previous().literal_value)) {
            expr->value = ast::IntLiteral{*val};
        }
        return expr;
    }
    
    if (match({TokenType::Float})) {
        auto expr = std::make_unique<ast::Expression>();
        if (auto* val = std::get_if<double>(&previous().literal_value)) {
            expr->value = ast::FloatLiteral{*val};
        }
        return expr;
    }
    
    if (match({TokenType::String})) {
        auto expr = std::make_unique<ast::Expression>();
        if (auto* val = std::get_if<std::string>(&previous().literal_value)) {
            expr->value = ast::StringLiteral{*val};
        }
        return expr;
    }
    
    if (match({TokenType::TokTrue, TokenType::TokFalse})) {
        auto expr = std::make_unique<ast::Expression>();
        if (auto* val = std::get_if<bool>(&previous().literal_value)) {
            expr->value = ast::BoolLiteral{*val};
        }
        return expr;
    }
    
    
    if (match({TokenType::Identifier})) {
        std::string name{previous().lexeme};
        
        
        if (match({TokenType::Dot})) {
            auto expr = std::make_unique<ast::Expression>();
            expr->value = ast::Identifier{name};
            
            while (true) {
                std::string member{consume(TokenType::Identifier, 
                    "Expected identifier after '.'").lexeme};
                
                auto member_expr = std::make_unique<ast::Expression>();
                member_expr->value = ast::MemberAccess{std::move(expr), member};
                expr = std::move(member_expr);
                
                if (!match({TokenType::Dot})) break;
            }
            
            return expr;
        }
        
        auto expr = std::make_unique<ast::Expression>();
        expr->value = ast::Identifier{name};
        return expr;
    }
    
    
    if (match({TokenType::LeftParen})) {
        auto expr = expression();
        consume(TokenType::RightParen, "Expected ')' after expression");
        return expr;
    }
    
    
    if (match({TokenType::LeftBracket})) {
        ast::ArrayLiteral array;
        
        
        if (!check(TokenType::RightBracket)) {
            do {
                auto elem = expression();
                if (elem) {
                    array.elements.push_back(std::move(elem));
                }
            } while (match({TokenType::Comma}));
        }
        
        consume(TokenType::RightBracket, "Expected ']' after array elements");
        
        auto expr = std::make_unique<ast::Expression>();
        expr->value = std::move(array);
        return expr;
    }
    
    addError("Expected expression");
    return nullptr;
}

void Parser::addError(const std::string& message) {
    std::ostringstream oss;
    oss << "Line " << peek().line << ": " << message;
    errors_.push_back(oss.str());
}

void Parser::synchronize() {
    while (!isAtEnd()) {
        if (previous().type == TokenType::Semicolon) return;
        if (peek().type == TokenType::RightBrace) return;
        
        advance();
    }
}





ConfigParser::ConfigParser(Toaster* toaster) : toaster_(toaster) {}

bool ConfigParser::load(const std::filesystem::path& path) {
    
    if (!std::filesystem::exists(path)) {
        reportError("Config file not found: " + path.string());
        return false;
    }
    
    
    std::ifstream file(path);
    if (!file.is_open()) {
        reportError("Failed to open config file: " + path.string());
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    
    
    std::cerr << "[ConfigParser] SOURCE FIRST 200 CHARS: '" << source.substr(0, 200) << "'" << std::endl;
    
    
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    
    if (!lexer.getErrors().empty()) {
        reportErrors(lexer.getErrors());
        return false;
    }
    
    
    std::cerr << "[ConfigParser] First 20 tokens:" << std::endl;
    for (size_t i = 0; i < std::min(tokens.size(), size_t(20)); i++) {
        std::cerr << "  " << i << ": type=" << (int)tokens[i].type << " lexeme='" << tokens[i].lexeme << "'" << std::endl;
    }
    
    
    Parser parser(tokens);
    auto ast = parser.parse();
    
    if (!parser.getErrors().empty()) {
        reportErrors(parser.getErrors());
        return false;
    }
    
    
    bool result = interpret(*ast);
    return result;
}

bool ConfigParser::loadFromString(const std::string& source) {
    
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    
    if (!lexer.getErrors().empty()) {
        reportErrors(lexer.getErrors());
        return false;
    }
    
    
    Parser parser(tokens);
    auto ast = parser.parse();
    
    if (!parser.getErrors().empty()) {
        reportErrors(parser.getErrors());
        return false;
    }
    
    
    return interpret(*ast);
}

std::string ConfigParser::getEmbeddedConfig() {
    
    
    return R"(
pointblank: {
    
    
    
    
    
    
    
    performance: {
        target_fps: 60
        vsync: true
    }
    
    
    window_rules: {
        opacity: 1.0
        border_width: 2
    }
    
    
    workspaces: {
        infinite: true
        max_workspaces: 12
        initial_count: 1
        dynamic_creation: true
        auto_remove: true
        min_persist: 1
    }
    
    
    binds: {
        
        "SUPER, Q": "killactive"
        "SUPER, RETURN": exec: "alacritty"
        "SUPER, F": "fullscreen"
        "SUPER, T": "togglefloating"
        
        
        "SUPER, B": "layout bsp"
        "SUPER, M": "layout monocle"
        "SUPER, COMMA": "cyclelayoutprev"
        "SUPER, PERIOD": "cyclelayoutnext"
        
        
        "SUPER, LEFT": "focusleft"
        "SUPER, RIGHT": "focusright"
        "SUPER, UP": "focusup"
        "SUPER, DOWN": "focusdown"
        
        
        "SUPER_SHIFT, H": "swapleft"
        "SUPER_SHIFT, L": "swapright"
        "SUPER_SHIFT, K": "swapup"
        "SUPER_SHIFT, J": "swapdown"
        
        
        "SUPER, H": "resizeleft"
        "SUPER, L": "resizeright"
        "SUPER, K": "resizeup"
        "SUPER, J": "resizedown"
        
        
        "SUPER, 1": "workspace 1"
        "SUPER, 2": "workspace 2"
        "SUPER, 3": "workspace 3"
        "SUPER, 4": "workspace 4"
        "SUPER, 5": "workspace 5"
        "SUPER, 6": "workspace 6"
        "SUPER, 7": "workspace 7"
        "SUPER, 8": "workspace 8"
        "SUPER, 9": "workspace 9"
        "SUPER, 0": "workspace 10"
        
        
        "SUPER_SHIFT, 1": "movetoworkspace 1"
        "SUPER_SHIFT, 2": "movetoworkspace 2"
        "SUPER_SHIFT, 3": "movetoworkspace 3"
        "SUPER_SHIFT, 4": "movetoworkspace 4"
        "SUPER_SHIFT, 5": "movetoworkspace 5"
        "SUPER_SHIFT, 6": "movetoworkspace 6"
        "SUPER_SHIFT, 7": "movetoworkspace 7"
        "SUPER_SHIFT, 8": "movetoworkspace 8"
        "SUPER_SHIFT, 9": "movetoworkspace 9"
        "SUPER_SHIFT, 0": "movetoworkspace 10"
        
        
        "SUPER_SHIFT, R": "reload"
        "SUPER_SHIFT, Q": "exit"
    }
    
    
    mouse: {
        focus_follows_mouse: false
    }
    
    
    drag: {
        swap_on_drag: true
        threshold: 5
        swap_threshold: 20
        visual_feedback: true
    }
    
    
    borders: {
        focused_color: "#00FF00"
        unfocused_color: "#808080"
    }
    
    
    gaps: {
        inner_gap: 10
        outer_gap: 10
    }
    
    
    status_bar: {
        enabled: false
    }
    
    
    extensions: {
        enabled: false
    }
}
)";
}

bool ConfigParser::interpret(const ast::ConfigFile& ast) {
    
    for (const auto& import : ast.imports) {
        if (!resolveImport(import)) {
            
        }
    }
    
    
    if (ast.root) {
        std::cerr << "[ConfigParser] ROOT BLOCK NAME: '" << ast.root->name << "'" << std::endl;
        evaluateBlock(*ast.root);
    }
    
    
    for (const auto& blk : ast.blocks) {
        if (blk) {
            evaluateBlock(*blk);
        }
    }
    
    return true;
}

void ConfigParser::evaluateBlock(const ast::Block& block) {
    
    if (block.name == "window_rules") {
        for (const auto& stmt : block.statements) {
            evaluateStatement(*stmt);
        }
    } else if (block.name == "workspaces") {
        for (const auto& stmt : block.statements) {
            std::visit([this](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ast::Assignment>) {
                    auto result = evaluateExpression(*value.value);
                    
                    if (value.name == "infinite") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.workspaces.infinite = *b;
                        }
                    } else if (value.name == "max_workspace" || value.name == "max_workspaces") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.workspaces.max_workspaces = *i;
                        }
                    } else if (value.name == "initial_count") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.workspaces.initial_count = *i;
                        }
                    } else if (value.name == "dynamic_creation") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.workspaces.dynamic_creation = *b;
                        }
                    } else if (value.name == "auto_remove") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.workspaces.auto_remove = *b;
                        }
                    } else if (value.name == "min_persist") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.workspaces.min_persist = *i;
                        }
                    }
                }
            }, stmt->value);
        }
    } else if (block.name == "binds") {
        std::cerr << "[CONFIG] Parsing binds block, statements count: " << block.statements.size() << std::endl;
        
        for (const auto& stmt : block.statements) {
            
            if (auto* var_decl = std::get_if<ast::VariableDeclaration>(&stmt->value)) {
                auto result = evaluateExpression(*var_decl->value);
                config_.variables[var_decl->name] = result;
                continue;
            }
            
            if (auto* assign = std::get_if<ast::Assignment>(&stmt->value)) {
                std::string keybind_str = assign->name;
                std::cerr << "[CONFIG] Found keybind assignment: " << keybind_str << std::endl;
                auto action_value = evaluateExpression(*assign->value);
                
                Config::Keybind bind;
                
                auto last_comma = keybind_str.rfind(',');
                if (last_comma != std::string::npos) {
                    bind.modifiers = keybind_str.substr(0, last_comma);
                    bind.key = keybind_str.substr(last_comma + 1);
                    
                    
                    bind.modifiers.erase(0, bind.modifiers.find_first_not_of(" \t"));
                    bind.modifiers.erase(bind.modifiers.find_last_not_of(" \t") + 1);
                    bind.key.erase(0, bind.key.find_first_not_of(" \t"));
                    bind.key.erase(bind.key.find_last_not_of(" \t") + 1);
                } else {
                    bind.key = keybind_str;
                }
                
                if (auto* str = std::get_if<std::string>(&action_value)) {
                    bind.action = *str;
                }
                
                
                config_.keybinds.push_back(bind);
            }
        }
        
    } else if (block.name == "pointblank") {
    
    for (const auto& stmt : block.statements) {
        evaluateStatement(*stmt);
    }
    } else if (block.name == "mouse") {
        
        for (const auto& stmt : block.statements) {
            evaluateStatement(*stmt);
        }
    } else if (block.name == "drag") {
        
        for (const auto& stmt : block.statements) {
            std::visit([this](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ast::Assignment>) {
                    auto result = evaluateExpression(*value.value);
                    if (auto* b = std::get_if<bool>(&result)) {
                    } else if (auto* i = std::get_if<int>(&result)) {
                    } else if (auto* d = std::get_if<double>(&result)) {
                    } else if (auto* s = std::get_if<std::string>(&result)) {
                    }
                    
                    if (value.name == "swap_on_drag") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.drag.swap_on_drag = *b;
                        }
                    } else if (value.name == "threshold") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.drag.threshold = *i;
                        }
                    } else if (value.name == "swap_threshold") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.drag.swap_threshold = *i;
                        }
                    } else if (value.name == "visual_feedback") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.drag.visual_feedback = *b;
                        }
                    } else {
                    }
                }
            }, stmt->value);
        }
    } else if (block.name == "borders") {
        
        for (const auto& stmt : block.statements) {
            std::visit([this](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ast::Assignment>) {
                    auto result = evaluateExpression(*value.value);
                    
                    
                    if (value.name == "width") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.borders.width = *i;
                        }
                        return;
                    }
                    
                    
                    if (auto* s = std::get_if<std::string>(&result)) {
                        
                        if (value.name == "focused" || value.name == "focused_color") {
                            try {
                                std::string hex = *s;
                                if (!hex.empty() && hex[0] == '#') {
                                    hex = hex.substr(1);
                                }
                                config_.borders.focused_color = hex;  
                            } catch (const std::exception& e) {
                            }
                        } else if (value.name == "unfocused" || value.name == "unfocused_color") {
                            try {
                                std::string hex = *s;
                                if (!hex.empty() && hex[0] == '#') {
                                    hex = hex.substr(1);
                                }
                                config_.borders.unfocused_color = hex;  
                            } catch (const std::exception& e) {
                            }
                        } else if (value.name == "urgent" || value.name == "urgent_color") {
                            try {
                                std::string hex = *s;
                                if (!hex.empty() && hex[0] == '#') {
                                    hex = hex.substr(1);
                                }
                                config_.borders.urgent_color = hex;  
                            } catch (const std::exception& e) {
                            }
                        }
                    }
                }
            }, stmt->value);
        }
    } else if (block.name == "mouse") {
        
        for (const auto& stmt : block.statements) {
            std::visit([this](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ast::Assignment>) {
                    auto result = evaluateExpression(*value.value);
                    
                    if (value.name == "focus_follows_mouse") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.mouse.focus_follows_mouse = *b;
                        }
                    } else if (value.name == "mouse_warping") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.mouse.mouse_warping = *b;
                        }
                    } else if (value.name == "cursor_speed") {
                        if (auto* d = std::get_if<double>(&result)) {
                            config_.mouse.cursor_speed = *d;
                        } else if (auto* i = std::get_if<int>(&result)) {
                            config_.mouse.cursor_speed = static_cast<double>(*i);
                        }
                    } else {
                    }
                }
            }, stmt->value);
        }
    } else if (block.name == "animations") {
        
        for (const auto& stmt : block.statements) {
            std::visit([this](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ast::Assignment>) {
                    auto result = evaluateExpression(*value.value);
                    
                    if (value.name == "enabled") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.animations.enabled = *b;
                        }
                    } else if (value.name == "curve") {
                        if (auto* s = std::get_if<std::string>(&result)) {
                            config_.animations.curve = *s;
                        }
                    } else if (value.name == "duration") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.animations.duration = *i;
                        }
                    } else {
                    }
                }
            }, stmt->value);
        }
    } else if (block.name == "performance") {
        
        for (const auto& stmt : block.statements) {
            std::visit([this](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ast::Assignment>) {
                    auto result = evaluateExpression(*value.value);
                    
                    
                    if (value.name == "scheduler_policy") {
                        if (auto* s = std::get_if<std::string>(&result)) {
                            config_.performance.scheduler_policy = *s;
                        }
                    } else if (value.name == "scheduler_priority") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.performance.scheduler_priority = *i;
                        }
                    } else if (value.name == "cpu_cores") {
                        if (auto* s = std::get_if<std::string>(&result)) {
                            config_.performance.cpu_cores = *s;
                        }
                    } else if (value.name == "cpu_exclusive") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.performance.cpu_exclusive = *b;
                        }
                    } else if (value.name == "hyperthreading_aware") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.performance.hyperthreading_aware = *b;
                        }
                    } else if (value.name == "realtime_mode") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.performance.realtime_mode = *b;
                        }
                    } else if (value.name == "realtime_priority") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.performance.realtime_priority = *i;
                        }
                    } else if (value.name == "lock_memory") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.performance.lock_memory = *b;
                        }
                    } else if (value.name == "locked_memory_mb") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.performance.locked_memory_mb = *i;
                        }
                    } else if (value.name == "target_fps") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.performance.target_fps = *i;
                        }
                    } else if (value.name == "min_fps") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.performance.min_fps = *i;
                        }
                    } else if (value.name == "max_fps") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.performance.max_fps = *i;
                        }
                    } else if (value.name == "vsync") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.performance.vsync = *b;
                        }
                    } else if (value.name == "adaptive_sync") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.performance.adaptive_sync = *b;
                        }
                    } else if (value.name == "throttle_threshold_us") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.performance.throttle_threshold_us = *i;
                        }
                    } else if (value.name == "throttle_delay_us") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.performance.throttle_delay_us = *i;
                        }
                    } else if (value.name == "throttle_on_battery") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.performance.throttle_on_battery = *b;
                        }
                    } else if (value.name == "max_batch_size") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.performance.max_batch_size = *i;
                        }
                    } else if (value.name == "batch_timeout_us") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.performance.batch_timeout_us = *i;
                        }
                    } else if (value.name == "dirty_rectangles_only") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.performance.dirty_rectangles_only = *b;
                        }
                    } else if (value.name == "double_buffer") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.performance.double_buffer = *b;
                        }
                    } else if (value.name == "triple_buffer") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.performance.triple_buffer = *b;
                        }
                    } else if (value.name == "metrics_enabled") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.performance.metrics_enabled = *b;
                        }
                    } else if (value.name == "metrics_interval_ms") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.performance.metrics_interval_ms = *i;
                        }
                    } else if (value.name == "latency_tracking") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.performance.latency_tracking = *b;
                        }
                    } else {
                    }
                }
            }, stmt->value);
        }
    } else if (block.name == "extensions") {
        
        for (const auto& stmt : block.statements) {
            std::visit([this](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ast::Assignment>) {
                    auto result = evaluateExpression(*value.value);
                    
                    if (value.name == "enabled") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.extensions.enabled = *b;
                        }
                    } else if (value.name == "strict_validation") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.extensions.strict_validation = *b;
                        }
                    } else if (value.name == "health_check_interval_s") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.extensions.health_check_interval_s = *i;
                        }
                    } else if (value.name == "builtin_extension_dir") {
                        if (auto* s = std::get_if<std::string>(&result)) {
                            config_.extensions.builtin_extension_dir = *s;
                        }
                    } else if (value.name == "user_extension_dir") {
                        if (auto* s = std::get_if<std::string>(&result)) {
                            config_.extensions.user_extension_dir = *s;
                        }
                    } else if (value.name == "init_timeout_ms") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.extensions.init_timeout_ms = *i;
                        }
                    } else if (value.name == "max_extensions") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.extensions.max_extensions = *i;
                        }
                    } else if (value.name == "allow_event_blocking") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.extensions.allow_event_blocking = *b;
                        }
                    } else if (value.name == "system_paths") {
                        if (auto* arr = std::get_if<std::vector<std::string>>(&result)) {
                            config_.system_paths = *arr;
                        }
                    } else {
                    }
                }
            }, stmt->value);
        }
    } else if (block.name == "status_bar") {
        
        for (const auto& stmt : block.statements) {
            std::visit([this](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ast::Assignment>) {
                    auto result = evaluateExpression(*value.value);
                    
                    if (value.name == "height") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.status_bar.height = *i;
                        }
                    } else if (value.name == "padding_x") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.status_bar.padding_x = *i;
                        }
                    } else if (value.name == "padding_y") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.status_bar.padding_y = *i;
                        }
                    } else if (value.name == "bg_color" || value.name == "background") {
                        if (auto* s = std::get_if<std::string>(&result)) {
                            config_.status_bar.bg_color = *s;
                        }
                    } else if (value.name == "fg_color" || value.name == "foreground") {
                        if (auto* s = std::get_if<std::string>(&result)) {
                            config_.status_bar.fg_color = *s;
                        }
                    } else if (value.name == "accent_color") {
                        if (auto* s = std::get_if<std::string>(&result)) {
                            config_.status_bar.accent_color = *s;
                        }
                    } else if (value.name == "urgent_color") {
                        if (auto* s = std::get_if<std::string>(&result)) {
                            config_.status_bar.urgent_color = *s;
                        }
                    } else if (value.name == "inactive_bg") {
                        if (auto* s = std::get_if<std::string>(&result)) {
                            config_.status_bar.inactive_bg = *s;
                        }
                    } else if (value.name == "font_family") {
                        if (auto* s = std::get_if<std::string>(&result)) {
                            config_.status_bar.font_family = *s;
                        }
                    } else if (value.name == "font_size") {
                        if (auto* d = std::get_if<double>(&result)) {
                            config_.status_bar.font_size = *d;
                        } else if (auto* i = std::get_if<int>(&result)) {
                            config_.status_bar.font_size = static_cast<double>(*i);
                        }
                    } else if (value.name == "show_workspace_icons") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.status_bar.show_workspace_icons = *b;
                        }
                    } else if (value.name == "show_layout_mode") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.status_bar.show_layout_mode = *b;
                        }
                    } else if (value.name == "show_window_title") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.status_bar.show_window_title = *b;
                        }
                    } else if (value.name == "workspace_clickable") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.status_bar.workspace_clickable = *b;
                        }
                    } else if (value.name == "enabled") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.status_bar.enabled = *b;
                        }
                    } else if (value.name == "position") {
                        if (auto* s = std::get_if<std::string>(&result)) {
                            config_.status_bar.position = *s;
                        }
                    } else if (value.name == "workspace_icons") {
                        if (auto* arr = std::get_if<std::vector<std::string>>(&result)) {
                            config_.status_bar.workspace_icons = *arr;
                        }
                    } else {
                    }
                }
            }, stmt->value);
        }
    } else if (block.name == "windows") {
        
        for (const auto& stmt : block.statements) {
            std::visit([this](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ast::Assignment>) {
                    auto result = evaluateExpression(*value.value);
                    
                    if (value.name == "auto_resize_non_docks") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.windows.auto_resize_non_docks = *b;
                        }
                    } else if (value.name == "floating_resize_enabled") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.windows.floating_resize_enabled = *b;
                        }
                    } else if (value.name == "floating_resize_edge_size") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.windows.floating_resize_edge_size = *i;
                        }
                    } else if (value.name == "smart_gaps") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.windows.smart_gaps = *b;
                        }
                    } else if (value.name == "smart_borders") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.windows.smart_borders = *b;
                        }
                    } else if (value.name == "focus_new_windows") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.windows.focus_new_windows = *b;
                        }
                    } else if (value.name == "focus_urgent_windows") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.windows.focus_urgent_windows = *b;
                        }
                    } else if (value.name == "default_floating_width") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.windows.default_floating_width = *i;
                        }
                    } else if (value.name == "default_floating_height") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.windows.default_floating_height = *i;
                        }
                    } else if (value.name == "center_floating_windows") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.windows.center_floating_windows = *b;
                        }
                    } else {
                    }
                }
            }, stmt->value);
        }
    } else if (block.name == "window_rules") {
        
        for (const auto& stmt : block.statements) {
            std::visit([this](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ast::Assignment>) {
                    auto result = evaluateExpression(*value.value);
                    
                    if (value.name == "opacity") {
                        if (auto* d = std::get_if<double>(&result)) {
                            config_.window_rules.opacity = *d;
                        } else if (auto* i = std::get_if<int>(&result)) {
                            config_.window_rules.opacity = static_cast<double>(*i);
                        }
                    } else if (value.name == "blur") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.window_rules.blur = *b;
                        }
                    } else if (value.name == "border_width") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.window_rules.border_width = *i;
                        }
                    } else if (value.name == "gap_size") {
                        if (auto* i = std::get_if<int>(&result)) {
                            config_.window_rules.gap_size = *i;
                        }
                    } else {
                    }
                }
            }, stmt->value);
        }
    } else if (block.name == "layout") {
        
        for (const auto& stmt : block.statements) {
            std::visit([this](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ast::Assignment>) {
                    auto result = evaluateExpression(*value.value);
                    
                    if (value.name == "cycle_direction") {
                        if (auto* str = std::get_if<std::string>(&result)) {
                            config_.layout.cycle_direction = *str;
                        }
                    } else if (value.name == "wrap_cycle") {
                        if (auto* b = std::get_if<bool>(&result)) {
                            config_.layout.wrap_cycle = *b;
                        }
                    }
                } else if constexpr (std::is_same_v<T, ast::Block>) {
                    
                    const auto& nested_block = value;
                    if (nested_block.name == "global") {
                        
                        for (const auto& nested_stmt : nested_block.statements) {
                            std::visit([this](auto&& nested_value) {
                                using NT = std::decay_t<decltype(nested_value)>;
                                if constexpr (std::is_same_v<NT, ast::Assignment>) {
                                    auto nested_result = evaluateExpression(*nested_value.value);
                                    
                                    if (nested_value.name == "inner_gap") {
                                        if (auto* i = std::get_if<int>(&nested_result)) {
                                            if (*i >= 0) {
                                                config_.layout_gaps.inner_gap = *i;
                                            } else {
                                            }
                                        }
                                    } else if (nested_value.name == "outer_gap") {
                                        if (auto* i = std::get_if<int>(&nested_result)) {
                                            if (*i >= 0) {
                                                config_.layout_gaps.outer_gap = *i;
                                            } else {
                                            }
                                        }
                                    } else if (nested_value.name == "top_gap") {
                                        if (auto* i = std::get_if<int>(&nested_result)) {
                                            if (*i >= -1) {
                                                config_.layout_gaps.top_gap = *i;
                                            } else {
                                            }
                                        }
                                    } else if (nested_value.name == "bottom_gap") {
                                        if (auto* i = std::get_if<int>(&nested_result)) {
                                            if (*i >= -1) {
                                                config_.layout_gaps.bottom_gap = *i;
                                            } else {
                                            }
                                        }
                                    } else if (nested_value.name == "left_gap") {
                                        if (auto* i = std::get_if<int>(&nested_result)) {
                                            if (*i >= -1) {
                                                config_.layout_gaps.left_gap = *i;
                                            } else {
                                            }
                                        }
                                    } else if (nested_value.name == "right_gap") {
                                        if (auto* i = std::get_if<int>(&nested_result)) {
                                            if (*i >= -1) {
                                                config_.layout_gaps.right_gap = *i;
                                            } else {
                                            }
                                        }
                                    } else {
                                    }
                                }
                            }, nested_stmt->value);
                        }
                    }
                    
                }
            }, stmt->value);
        }
    } else if (block.name == "layout_gaps" || block.name == "gaps") {
        
        for (const auto& stmt : block.statements) {
            std::visit([this](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ast::Assignment>) {
                    auto result = evaluateExpression(*value.value);
                    
                    if (value.name == "inner_gap") {
                        if (auto* i = std::get_if<int>(&result)) {
                            if (*i >= 0) {
                                config_.layout_gaps.inner_gap = *i;
                            } else {
                            }
                        }
                    } else if (value.name == "outer_gap") {
                        if (auto* i = std::get_if<int>(&result)) {
                            if (*i >= 0) {
                                config_.layout_gaps.outer_gap = *i;
                            } else {
                            }
                        }
                    } else if (value.name == "top_gap") {
                        if (auto* i = std::get_if<int>(&result)) {
                            if (*i >= -1) {
                                config_.layout_gaps.top_gap = *i;
                            } else {
                            }
                        }
                    } else if (value.name == "bottom_gap") {
                        if (auto* i = std::get_if<int>(&result)) {
                            if (*i >= -1) {
                                config_.layout_gaps.bottom_gap = *i;
                            } else {
                            }
                        }
                    } else if (value.name == "left_gap") {
                        if (auto* i = std::get_if<int>(&result)) {
                            if (*i >= -1) {
                                config_.layout_gaps.left_gap = *i;
                            } else {
                            }
                        }
                    } else if (value.name == "right_gap") {
                        if (auto* i = std::get_if<int>(&result)) {
                            if (*i >= -1) {
                                config_.layout_gaps.right_gap = *i;
                            } else {
                            }
                        }
                    } else {
                    }
                }
            }, stmt->value);
        }
    } else if (block.name == "autostart") {
        
        
        for (const auto& stmt : block.statements) {
            std::visit([this](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ast::Assignment>) {
                    
                    auto result = evaluateExpression(*value.value);
                    
                    if (auto* str = std::get_if<std::string>(&result)) {
                        std::string cmd = *str;
                        
                        
                        cmd.erase(0, cmd.find_first_not_of(" \t"));
                        cmd.erase(cmd.find_last_not_of(" \t") + 1);
                        
                        if (!cmd.empty()) {
                            config_.autostart.commands.push_back(cmd);
                        }
                    } else if (auto* arr = std::get_if<std::vector<std::string>>(&result)) {
                        
                        for (const auto& arr_cmd : *arr) {
                            std::string trimmed = arr_cmd;
                            trimmed.erase(0, trimmed.find_first_not_of(" \t"));
                            trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
                            if (!trimmed.empty()) {
                                config_.autostart.commands.push_back(trimmed);
                            }
                        }
                    }
                } else if constexpr (std::is_same_v<T, ast::ExecDirective>) {
                    
                    std::string cmd = value.command;
                    
                    
                    
                    cmd.erase(0, cmd.find_first_not_of(" \t"));
                    cmd.erase(cmd.find_last_not_of(" \t") + 1);
                    
                    if (!cmd.empty()) {
                        config_.autostart.commands.push_back(cmd);
                    }
                } else {
                }
            }, stmt->value);
        }
        
    } else if (block.name == "toplevel") {
        
        for (const auto& stmt : block.statements) {
            evaluateStatement(*stmt);
        }
    } else {
        
        for (const auto& stmt : block.statements) {
            evaluateStatement(*stmt);
        }
    }
}

void ConfigParser::evaluateStatement(const ast::Statement& stmt) {
    std::visit([this](auto&& value) {
        using T = std::decay_t<decltype(value)>;
        
        if constexpr (std::is_same_v<T, ast::Assignment>) {
            auto result = evaluateExpression(*value.value);
            config_.variables[value.name] = result;
            
            
            if (auto* i = std::get_if<int>(&result)) {
            } else if (auto* d = std::get_if<double>(&result)) {
            } else if (auto* s = std::get_if<std::string>(&result)) {
            } else if (auto* b = std::get_if<bool>(&result)) {
            }
            
            
            if (value.name == "opacity") {
                if (auto* d = std::get_if<double>(&result)) {
                    config_.window_rules.opacity = *d;
                } else if (auto* i = std::get_if<int>(&result)) {
                    config_.window_rules.opacity = static_cast<double>(*i);
                }
            } else if (value.name == "blur") {
                if (auto* b = std::get_if<bool>(&result)) {
                    config_.window_rules.blur = *b;
                }
            } else if (value.name == "focus_follows_mouse") {
                if (auto* b = std::get_if<bool>(&result)) {
                    config_.focus_follows_mouse = *b;
                }
            } else if (value.name == "monitor_focus_follows_mouse") {
                if (auto* b = std::get_if<bool>(&result)) {
                    config_.monitor_focus_follows_mouse = *b;
                }
            } else if (value.name == "system_paths") {
                if (auto* arr = std::get_if<std::vector<std::string>>(&result)) {
                    config_.system_paths = *arr;
                }
            } else {
            }
        } else if constexpr (std::is_same_v<T, ast::VariableDeclaration>) {
            auto result = evaluateExpression(*value.value);
            config_.variables[value.name] = result;
            

            if (auto* i = std::get_if<int>(&result)) {
            } else if (auto* d = std::get_if<double>(&result)) {
            } else if (auto* s = std::get_if<std::string>(&result)) {
            } else if (auto* b = std::get_if<bool>(&result)) {
            }
        } else if constexpr (std::is_same_v<T, ast::Block>) {
            evaluateBlock(value);
        } else if constexpr (std::is_same_v<T, ast::IfStatement>) {
            auto condition = evaluateExpression(*value.condition);
            bool cond_value = false;
            
            if (auto* b = std::get_if<bool>(&condition)) {
                cond_value = *b;
            }
            
            if (cond_value) {
                for (const auto& s : value.then_branch) {
                    evaluateStatement(*s);
                }
            } else {
                for (const auto& s : value.else_branch) {
                    evaluateStatement(*s);
                }
            }
        } else if constexpr (std::is_same_v<T, ast::ExecDirective>) {
            
        }
    }, stmt.value);
}

std::variant<int, double, std::string, bool, std::vector<std::string>> 
ConfigParser::evaluateExpression(const ast::Expression& expr) {
    
    
    return evaluateExpression(expr, None, nullptr);
}

std::variant<int, double, std::string, bool, std::vector<std::string>> 
ConfigParser::evaluateExpression(const ast::Expression& expr, Window window, Display* display) {
    return std::visit([this, window, display](auto&& value) -> std::variant<int, double, std::string, bool, std::vector<std::string>> {
        using T = std::decay_t<decltype(value)>;
        
        if constexpr (std::is_same_v<T, ast::IntLiteral>) {
            return value.value;
        } else if constexpr (std::is_same_v<T, ast::FloatLiteral>) {
            return value.value;
        } else if constexpr (std::is_same_v<T, ast::StringLiteral>) {
            return value.value;
        } else if constexpr (std::is_same_v<T, ast::BoolLiteral>) {
            return value.value;
        } else if constexpr (std::is_same_v<T, ast::Identifier>) {
            
            auto it = config_.variables.find(value.name);
            if (it != config_.variables.end()) {
                return std::visit([](auto&& v) -> std::variant<int, double, std::string, bool, std::vector<std::string>> {
                    return v;
                }, it->second);
            }
            return 0; 
        } else if constexpr (std::is_same_v<T, ast::BinaryOp>) {
            auto left = evaluateExpression(*value.left, window, display);
            auto right = evaluateExpression(*value.right, window, display);
            
            
            switch (value.op) {
                case ast::BinaryOp::Op::Add: {
                    if (auto* l = std::get_if<int>(&left)) {
                        if (auto* r = std::get_if<int>(&right)) {
                            return *l + *r;
                        }
                    }
                    
                    if (auto* l = std::get_if<std::string>(&left)) {
                        if (auto* r = std::get_if<std::string>(&right)) {
                            return *l + *r;
                        }
                    }
                    return 0;
                }
                case ast::BinaryOp::Op::Sub: {
                    if (auto* l = std::get_if<int>(&left)) {
                        if (auto* r = std::get_if<int>(&right)) {
                            return *l - *r;
                        }
                    }
                    return 0;
                }
                case ast::BinaryOp::Op::Mul: {
                    if (auto* l = std::get_if<int>(&left)) {
                        if (auto* r = std::get_if<int>(&right)) {
                            return *l * *r;
                        }
                    }
                    return 0;
                }
                case ast::BinaryOp::Op::Div: {
                    if (auto* l = std::get_if<int>(&left)) {
                        if (auto* r = std::get_if<int>(&right)) {
                            if (*r != 0) return *l / *r;
                        }
                    }
                    return 0;
                }
                case ast::BinaryOp::Op::And: {
                    bool l = false, r = false;
                    if (auto* b = std::get_if<bool>(&left)) l = *b;
                    else if (auto* i = std::get_if<int>(&left)) l = *i != 0;
                    if (auto* b = std::get_if<bool>(&right)) r = *b;
                    else if (auto* i = std::get_if<int>(&right)) r = *i != 0;
                    return l && r;
                }
                case ast::BinaryOp::Op::Or: {
                    bool l = false, r = false;
                    if (auto* b = std::get_if<bool>(&left)) l = *b;
                    else if (auto* i = std::get_if<int>(&left)) l = *i != 0;
                    if (auto* b = std::get_if<bool>(&right)) r = *b;
                    else if (auto* i = std::get_if<int>(&right)) r = *i != 0;
                    return l || r;
                }
                case ast::BinaryOp::Op::Eq: {
                    
                    if (auto* l = std::get_if<std::string>(&left)) {
                        if (auto* r = std::get_if<std::string>(&right)) {
                            return *l == *r;
                        }
                    }
                    
                    if (auto* l = std::get_if<int>(&left)) {
                        if (auto* r = std::get_if<int>(&right)) {
                            return *l == *r;
                        }
                    }
                    
                    if (auto* l = std::get_if<bool>(&left)) {
                        if (auto* r = std::get_if<bool>(&right)) {
                            return *l == *r;
                        }
                    }
                    return false;
                }
                case ast::BinaryOp::Op::Ne: {
                    
                    if (auto* l = std::get_if<std::string>(&left)) {
                        if (auto* r = std::get_if<std::string>(&right)) {
                            return *l != *r;
                        }
                    }
                    
                    if (auto* l = std::get_if<int>(&left)) {
                        if (auto* r = std::get_if<int>(&right)) {
                            return *l != *r;
                        }
                    }
                    
                    if (auto* l = std::get_if<bool>(&left)) {
                        if (auto* r = std::get_if<bool>(&right)) {
                            return *l != *r;
                        }
                    }
                    return true;
                }
                case ast::BinaryOp::Op::Lt: {
                    if (auto* l = std::get_if<int>(&left)) {
                        if (auto* r = std::get_if<int>(&right)) {
                            return *l < *r;
                        }
                    }
                    return false;
                }
                case ast::BinaryOp::Op::Gt: {
                    if (auto* l = std::get_if<int>(&left)) {
                        if (auto* r = std::get_if<int>(&right)) {
                            return *l > *r;
                        }
                    }
                    return false;
                }
                case ast::BinaryOp::Op::Le: {
                    if (auto* l = std::get_if<int>(&left)) {
                        if (auto* r = std::get_if<int>(&right)) {
                            return *l <= *r;
                        }
                    }
                    return false;
                }
                case ast::BinaryOp::Op::Ge: {
                    if (auto* l = std::get_if<int>(&left)) {
                        if (auto* r = std::get_if<int>(&right)) {
                            return *l >= *r;
                        }
                    }
                    return false;
                }
            }
            return 0;
        } else if constexpr (std::is_same_v<T, ast::MemberAccess>) {
            
            
            auto obj_result = evaluateExpression(*value.object, window, display);
            
            std::string object_name;
            if (auto* s = std::get_if<std::string>(&obj_result)) {
                object_name = *s;
            } else if (auto* i = std::get_if<int>(&obj_result)) {
                object_name = std::to_string(*i);
            }
            
            
            if (object_name == "window") {
                if (!window || !display) {
                    
                    return std::string("");
                }
                
                
                if (value.member == "class") {
                    XClassHint class_hint;
                    if (XGetClassHint(display, window, &class_hint)) {
                        std::string result = class_hint.res_class ? class_hint.res_class : "";
                        if (class_hint.res_name) XFree(class_hint.res_name);
                        if (class_hint.res_class) XFree(class_hint.res_class);
                        return result;
                    }
                    return std::string("");
                } else if (value.member == "title") {
                    char* name = nullptr;
                    if (XFetchName(display, window, &name)) {
                        std::string result = name ? name : "";
                        if (name) XFree(name);
                        return result;
                    }
                    return std::string("");
                } else if (value.member == "workspace") {
                    
                    
                    return 0;
                }
            }
            
            return std::string("");
        } else if constexpr (std::is_same_v<T, ast::ArrayLiteral>) {
            std::vector<std::string> result;
            for (const auto& elem : value.elements) {
                auto elem_val = evaluateExpression(*elem, window, display);
                if (auto* s = std::get_if<std::string>(&elem_val)) {
                    result.push_back(*s);
                } else if (auto* i = std::get_if<int>(&elem_val)) {
                    result.push_back(std::to_string(*i));
                } else if (auto* d = std::get_if<double>(&elem_val)) {
                    result.push_back(std::to_string(*d));
                } else if (auto* b = std::get_if<bool>(&elem_val)) {
                    result.push_back(*b ? "true" : "false");
                }
            }
            return result;
        } else {
            return 0;
        }
    }, expr.value);
}

bool ConfigParser::resolveImport(const ast::ImportDirective& import) {
    
    if (imported_modules_.find(import.module_name) != imported_modules_.end()) {
        return true; 
    }
    
    
    auto file_path = findImportFile(import.module_name, import.is_user_extension);
    if (!file_path) {
        reportError("Could not find import file: " + import.module_name);
        return false;
    }
    
    
    std::ifstream file(*file_path);
    if (!file.is_open()) {
        reportError("Could not open import file: " + file_path->string());
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    file.close();
    
    
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    
    
    Parser parser(std::move(tokens));
    auto ast = parser.parse();
    
    
    const auto& errors = parser.getErrors();
    if (!errors.empty()) {
        reportErrors(errors);
        return false;
    }
    
    if (!ast) {
        reportError("Failed to parse import: " + import.module_name);
        return false;
    }
    
    
    for (auto& block : ast->blocks) {
        if (block) {
            evaluateBlock(*block);
        }
    }
    
    
    imported_modules_[import.module_name] = std::move(ast);
    
    return true;
}

std::optional<std::filesystem::path> 
ConfigParser::findImportFile(const std::string& name, bool is_user) {
    
    
    auto base_path = is_user ? getUserExtensionPath() : getPBExtensionPath();
    auto full_path = base_path / (name + ".wmi");
    
    if (std::filesystem::exists(full_path)) {
        return full_path;
    }
    
    return std::nullopt;
}

void ConfigParser::reportError(const std::string& message) {
    if (toaster_) {
        toaster_->error(message);
    }
}

void ConfigParser::reportErrors(const std::vector<std::string>& errors) {
    for (const auto& error : errors) {
        reportError(error);
    }
}

std::filesystem::path ConfigParser::getDefaultConfigPath() {
    auto home = std::getenv("HOME");
    if (!home) return "/etc/pointblank/pointblank.wmi";
    
    return std::filesystem::path(home) / ".config" / "pblank" / "pointblank.wmi";
}

std::filesystem::path ConfigParser::getPBExtensionPath() {
    auto home = std::getenv("HOME");
    if (!home) return "/etc/pointblank/extensions/pb";
    
    return std::filesystem::path(home) / ".config" / "pblank" / "extensions" / "pb";
}

std::filesystem::path ConfigParser::getUserExtensionPath() {
    auto home = std::getenv("HOME");
    if (!home) return "/etc/pointblank/extensions/user";
    
    return std::filesystem::path(home) / ".config" / "pblank" / "extensions" / "user";
}

} 
