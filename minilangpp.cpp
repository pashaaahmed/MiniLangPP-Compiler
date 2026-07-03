// =====================================================================
//  MiniLang++ Compiler Front-End
//  Lexical Analyzer + Recursive-Descent Parser + Semantic Analyzer
//  Optional Advanced Tasks implemented:
//      - Uninitialized variable usage detection
//      - Constant folding (semantic analysis phase)
//      - Three-Address Code (TAC) generation
//      - Arrays with type-checked indexed access
//
//  Build:  g++ -std=c++17 -O2 -o minilangpp minilangpp.cpp
//  Run:    ./minilangpp source.mlpp
// =====================================================================

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <memory>
#include <cctype>
#include <variant>
#include <functional>
#include <optional>
#include <stdexcept>

// =====================================================================
// 1. TOKENS
// =====================================================================

enum class TokType {
    // literals / identifiers
    IDENT, INT_LIT, FLOAT_LIT, BOOL_LIT,
    // keywords
    KW_INT, KW_FLOAT, KW_BOOL, KW_IF, KW_ELSE, KW_WHILE, KW_RETURN,
    KW_TRUE, KW_FALSE,
    // operators
    PLUS, MINUS, STAR, SLASH, PERCENT,
    ASSIGN, EQ, NEQ, LT, GT, LE, GE, AND, OR, NOT,
    // delimiters
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET,
    COMMA, SEMI,
    // misc
    END_OF_FILE, INVALID
};

struct Token {
    TokType type;
    std::string lexeme;
    int line = 0;
    int col  = 0;
};

static const std::unordered_map<std::string, TokType> KEYWORDS = {
    {"int", TokType::KW_INT}, {"float", TokType::KW_FLOAT}, {"bool", TokType::KW_BOOL},
    {"if", TokType::KW_IF}, {"else", TokType::KW_ELSE}, {"while", TokType::KW_WHILE},
    {"return", TokType::KW_RETURN}, {"true", TokType::KW_TRUE}, {"false", TokType::KW_FALSE}
};

static std::string tokTypeName(TokType t) {
    switch (t) {
        case TokType::IDENT: return "IDENTIFIER";
        case TokType::INT_LIT: return "INT_LITERAL";
        case TokType::FLOAT_LIT: return "FLOAT_LITERAL";
        case TokType::BOOL_LIT: return "BOOL_LITERAL";
        case TokType::KW_INT: case TokType::KW_FLOAT: case TokType::KW_BOOL:
        case TokType::KW_IF: case TokType::KW_ELSE: case TokType::KW_WHILE:
        case TokType::KW_RETURN: case TokType::KW_TRUE: case TokType::KW_FALSE:
            return "KEYWORD";
        case TokType::PLUS: case TokType::MINUS: case TokType::STAR:
        case TokType::SLASH: case TokType::PERCENT: case TokType::ASSIGN:
        case TokType::EQ: case TokType::NEQ: case TokType::LT: case TokType::GT:
        case TokType::LE: case TokType::GE: case TokType::AND: case TokType::OR:
        case TokType::NOT:
            return "OPERATOR";
        case TokType::LPAREN: case TokType::RPAREN: case TokType::LBRACE:
        case TokType::RBRACE: case TokType::LBRACKET: case TokType::RBRACKET:
        case TokType::COMMA: case TokType::SEMI:
            return "DELIMITER";
        case TokType::END_OF_FILE: return "EOF";
        default: return "INVALID";
    }
}

// =====================================================================
// 2. ERROR REPORTING (shared)
// =====================================================================

struct CompilerError {
    std::string phase;   // "Lexical" | "Syntax" | "Semantic"
    std::string message;
    int line, col;
};

static std::vector<CompilerError> g_errors;

static void reportError(const std::string& phase, const std::string& msg, int line, int col) {
    g_errors.push_back({phase, msg, line, col});
}

// =====================================================================
// 3. LEXICAL ANALYZER
// =====================================================================

class Lexer {
public:
    explicit Lexer(const std::string& src) : src_(src) {}

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        for (;;) {
            Token t = nextToken();
            tokens.push_back(t);
            if (t.type == TokType::END_OF_FILE) break;
        }
        return tokens;
    }

private:
    const std::string& src_;
    size_t pos_ = 0;
    int line_ = 1, col_ = 1;

    char peek(int off = 0) const {
        size_t p = pos_ + off;
        return p < src_.size() ? src_[p] : '\0';
    }
    char advance() {
        char c = src_[pos_++];
        if (c == '\n') { line_++; col_ = 1; } else { col_++; }
        return c;
    }
    bool match(char expected) {
        if (peek() != expected) return false;
        advance();
        return true;
    }
    void skipWhitespaceAndComments() {
        for (;;) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(); continue; }
            if (c == '/' && peek(1) == '/') {
                while (peek() != '\n' && peek() != '\0') advance();
                continue;
            }
            if (c == '/' && peek(1) == '*') {
                advance(); advance();
                while (!(peek() == '*' && peek(1) == '/') && peek() != '\0') advance();
                if (peek() != '\0') { advance(); advance(); }
                continue;
            }
            break;
        }
    }

    Token make(TokType type, const std::string& lex, int line, int col) {
        return Token{type, lex, line, col};
    }

    Token nextToken() {
        skipWhitespaceAndComments();
        int startLine = line_, startCol = col_;
        if (pos_ >= src_.size()) return make(TokType::END_OF_FILE, "", startLine, startCol);

        char c = peek();

        // Identifiers / keywords
        if (std::isalpha((unsigned char)c) || c == '_') {
            std::string lex;
            while (std::isalnum((unsigned char)peek()) || peek() == '_') lex += advance();
            auto it = KEYWORDS.find(lex);
            if (it != KEYWORDS.end()) {
                TokType t = it->second;
                if (t == TokType::KW_TRUE || t == TokType::KW_FALSE)
                    return make(TokType::BOOL_LIT, lex, startLine, startCol);
                return make(t, lex, startLine, startCol);
            }
            return make(TokType::IDENT, lex, startLine, startCol);
        }

        // Numbers
        if (std::isdigit((unsigned char)c)) {
            std::string lex;
            bool isFloat = false;
            while (std::isdigit((unsigned char)peek())) lex += advance();
            if (peek() == '.' && std::isdigit((unsigned char)peek(1))) {
                isFloat = true;
                lex += advance(); // '.'
                while (std::isdigit((unsigned char)peek())) lex += advance();
            }
            // malformed number like "12.3.4" or trailing identifier chars -> invalid
            if (std::isalpha((unsigned char)peek()) || peek() == '_') {
                while (std::isalnum((unsigned char)peek()) || peek() == '_' || peek() == '.') lex += advance();
                reportError("Lexical", "Invalid numeric literal '" + lex + "'", startLine, startCol);
                return make(TokType::INVALID, lex, startLine, startCol);
            }
            return make(isFloat ? TokType::FLOAT_LIT : TokType::INT_LIT, lex, startLine, startCol);
        }

        advance(); // consume the character we are about to classify
        switch (c) {
            case '+': return make(TokType::PLUS, "+", startLine, startCol);
            case '-': return make(TokType::MINUS, "-", startLine, startCol);
            case '*': return make(TokType::STAR, "*", startLine, startCol);
            case '/': return make(TokType::SLASH, "/", startLine, startCol);
            case '%': return make(TokType::PERCENT, "%", startLine, startCol);
            case '(': return make(TokType::LPAREN, "(", startLine, startCol);
            case ')': return make(TokType::RPAREN, ")", startLine, startCol);
            case '{': return make(TokType::LBRACE, "{", startLine, startCol);
            case '}': return make(TokType::RBRACE, "}", startLine, startCol);
            case '[': return make(TokType::LBRACKET, "[", startLine, startCol);
            case ']': return make(TokType::RBRACKET, "]", startLine, startCol);
            case ',': return make(TokType::COMMA, ",", startLine, startCol);
            case ';': return make(TokType::SEMI, ";", startLine, startCol);
            case '=':
                if (match('=')) return make(TokType::EQ, "==", startLine, startCol);
                return make(TokType::ASSIGN, "=", startLine, startCol);
            case '!':
                if (match('=')) return make(TokType::NEQ, "!=", startLine, startCol);
                return make(TokType::NOT, "!", startLine, startCol);
            case '<':
                if (match('=')) return make(TokType::LE, "<=", startLine, startCol);
                return make(TokType::LT, "<", startLine, startCol);
            case '>':
                if (match('=')) return make(TokType::GE, ">=", startLine, startCol);
                return make(TokType::GT, ">", startLine, startCol);
            case '&':
                if (match('&')) return make(TokType::AND, "&&", startLine, startCol);
                reportError("Lexical", "Invalid token '&' (did you mean '&&'?)", startLine, startCol);
                return make(TokType::INVALID, "&", startLine, startCol);
            case '|':
                if (match('|')) return make(TokType::OR, "||", startLine, startCol);
                reportError("Lexical", "Invalid token '|' (did you mean '||'?)", startLine, startCol);
                return make(TokType::INVALID, "|", startLine, startCol);
            default: {
                std::string lex(1, c);
                reportError("Lexical", "Invalid character '" + lex + "'", startLine, startCol);
                return make(TokType::INVALID, lex, startLine, startCol);
            }
        }
    }
};

// =====================================================================
// 4. TYPES
// =====================================================================

enum class Type { INT, FLOAT, BOOL, VOID, INT_ARR, FLOAT_ARR, BOOL_ARR, UNKNOWN };

static std::string typeName(Type t) {
    switch (t) {
        case Type::INT: return "int";
        case Type::FLOAT: return "float";
        case Type::BOOL: return "bool";
        case Type::VOID: return "void";
        case Type::INT_ARR: return "int[]";
        case Type::FLOAT_ARR: return "float[]";
        case Type::BOOL_ARR: return "bool[]";
        default: return "unknown";
    }
}
static Type baseOfArray(Type t) {
    if (t == Type::INT_ARR) return Type::INT;
    if (t == Type::FLOAT_ARR) return Type::FLOAT;
    if (t == Type::BOOL_ARR) return Type::BOOL;
    return t;
}
static Type arrayOfBase(Type t) {
    if (t == Type::INT) return Type::INT_ARR;
    if (t == Type::FLOAT) return Type::FLOAT_ARR;
    if (t == Type::BOOL) return Type::BOOL_ARR;
    return t;
}

// =====================================================================
// 5. AST NODES
// =====================================================================

struct Expr {
    int line = 0, col = 0;
    Type resolvedType = Type::UNKNOWN;
    virtual ~Expr() = default;
};
using ExprPtr = std::shared_ptr<Expr>;

struct IntLiteral : Expr { long long value; };
struct FloatLiteral : Expr { double value; };
struct BoolLiteral : Expr { bool value; };

struct Identifier : Expr { std::string name; };

struct ArrayAccess : Expr { std::string name; ExprPtr index; };

struct BinaryExpr : Expr { std::string op; ExprPtr left, right; };
struct UnaryExpr : Expr { std::string op; ExprPtr operand; };

struct CallExpr : Expr { std::string callee; std::vector<ExprPtr> args; };

struct Stmt { int line = 0, col = 0; virtual ~Stmt() = default; };
using StmtPtr = std::shared_ptr<Stmt>;

struct VarDeclStmt : Stmt {
    Type declType;
    std::string name;
    bool isArray = false;
    int arraySize = 0;
    ExprPtr initExpr;
};

struct AssignStmt : Stmt {
    std::string name;
    ExprPtr indexExpr;
    ExprPtr value;
};

struct IfStmt : Stmt {
    ExprPtr cond;
    StmtPtr thenBlock;
    StmtPtr elseBlock;
};

struct WhileStmt : Stmt {
    ExprPtr cond;
    StmtPtr body;
};

struct ReturnStmt : Stmt {
    ExprPtr value;
};

struct Block : Stmt {
    std::vector<StmtPtr> stmts;
};

struct ExprStmt : Stmt {
    ExprPtr expr;
};

struct ErrorStmt : Stmt {   // placeholder inserted where a statement could not be parsed
    std::string note;
};

struct Param { Type type; std::string name; bool isArray = false; };

struct FuncDecl : Stmt {
    Type returnType;
    std::string name;
    std::vector<Param> params;
    StmtPtr body;
};

struct Program {
    std::vector<std::shared_ptr<FuncDecl>> functions;
};

// =====================================================================
// 6. PARSER  (Recursive Descent, with simple error recovery)
// =====================================================================

class ParseError : public std::runtime_error {
public: explicit ParseError(const std::string& m) : std::runtime_error(m) {}
};

class Parser {
public:
    explicit Parser(std::vector<Token> toks) : toks_(std::move(toks)) {}

    Program parseProgram() {
        Program prog;
        while (!check(TokType::END_OF_FILE)) {
            try {
                prog.functions.push_back(parseFunction());
            } catch (ParseError&) {
                synchronize();
            }
        }
        return prog;
    }

private:
    std::vector<Token> toks_;
    size_t pos_ = 0;

    const Token& peek(int off = 0) const {
        size_t p = pos_ + off;
        return p < toks_.size() ? toks_[p] : toks_.back();
    }
    const Token& cur() const { return peek(); }
    bool check(TokType t) const { return cur().type == t; }
    const Token& advance() { if (!check(TokType::END_OF_FILE)) pos_++; return toks_[pos_ - 1]; }
    bool match(TokType t) { if (check(t)) { advance(); return true; } return false; }

    const Token& expect(TokType t, const std::string& what) {
        if (check(t)) return advance();
        error(what + " expected but found '" + cur().lexeme + "'");
        throw ParseError(what);
    }

    void error(const std::string& msg) {
        reportError("Syntax", msg, cur().line, cur().col);
    }

    void synchronize() {
        while (!check(TokType::END_OF_FILE)) {
            if (cur().type == TokType::SEMI) { advance(); return; }
            if (cur().type == TokType::RBRACE) { advance(); return; }
            if (cur().type == TokType::KW_INT || cur().type == TokType::KW_FLOAT ||
                cur().type == TokType::KW_BOOL || cur().type == TokType::KW_IF ||
                cur().type == TokType::KW_WHILE || cur().type == TokType::KW_RETURN) return;
            advance();
        }
    }

    bool isTypeToken(TokType t) const {
        return t == TokType::KW_INT || t == TokType::KW_FLOAT || t == TokType::KW_BOOL;
    }
    Type tokenToType(TokType t) {
        if (t == TokType::KW_INT) return Type::INT;
        if (t == TokType::KW_FLOAT) return Type::FLOAT;
        if (t == TokType::KW_BOOL) return Type::BOOL;
        return Type::UNKNOWN;
    }

    std::shared_ptr<FuncDecl> parseFunction() {
        auto fn = std::make_shared<FuncDecl>();
        fn->line = cur().line; fn->col = cur().col;
        if (!isTypeToken(cur().type)) {
            error("Function return type (int/float/bool) expected");
            throw ParseError("return type");
        }
        fn->returnType = tokenToType(advance().type);
        Token nameTok = expect(TokType::IDENT, "Function name");
        fn->name = nameTok.lexeme;
        expect(TokType::LPAREN, "'('");
        if (!check(TokType::RPAREN)) {
            do {
                Param p;
                if (!isTypeToken(cur().type)) { error("Parameter type expected"); throw ParseError("param type"); }
                p.type = tokenToType(advance().type);
                p.name = expect(TokType::IDENT, "Parameter name").lexeme;
                if (match(TokType::LBRACKET)) { expect(TokType::RBRACKET, "']'"); p.isArray = true; p.type = arrayOfBase(p.type); }
                fn->params.push_back(p);
            } while (match(TokType::COMMA));
        }
        expect(TokType::RPAREN, "')'");
        fn->body = parseBlock();
        return fn;
    }

    StmtPtr parseBlock() {
        auto blk = std::make_shared<Block>();
        blk->line = cur().line; blk->col = cur().col;
        expect(TokType::LBRACE, "'{'");
        while (!check(TokType::RBRACE) && !check(TokType::END_OF_FILE)) {
            try {
                blk->stmts.push_back(parseStatement());
            } catch (ParseError& pe) {
                auto err = std::make_shared<ErrorStmt>();
                err->line = cur().line; err->col = cur().col;
                err->note = pe.what();
                blk->stmts.push_back(err);
                synchronize();
            }
        }
        expect(TokType::RBRACE, "'}'");
        return blk;
    }

    StmtPtr parseStatement() {
        if (isTypeToken(cur().type)) return parseVarDecl();
        if (check(TokType::KW_IF)) return parseIf();
        if (check(TokType::KW_WHILE)) return parseWhile();
        if (check(TokType::KW_RETURN)) return parseReturn();
        if (check(TokType::LBRACE)) return parseBlock();
        if (check(TokType::IDENT)) return parseIdentStatement();
        error("Statement expected, found '" + cur().lexeme + "'");
        throw ParseError("statement");
    }

    StmtPtr parseVarDecl() {
        auto v = std::make_shared<VarDeclStmt>();
        v->line = cur().line; v->col = cur().col;
        v->declType = tokenToType(advance().type);
        v->name = expect(TokType::IDENT, "Variable name").lexeme;
        if (match(TokType::LBRACKET)) {
            v->isArray = true;
            v->declType = arrayOfBase(v->declType);
            if (check(TokType::INT_LIT)) v->arraySize = std::stoi(advance().lexeme);
            expect(TokType::RBRACKET, "']'");
        }
        if (match(TokType::ASSIGN)) v->initExpr = parseExpression();
        expect(TokType::SEMI, "';'");
        return v;
    }

    StmtPtr parseIdentStatement() {
        int line = cur().line, col = cur().col;
        std::string name = advance().lexeme;
        if (check(TokType::LPAREN)) {
            auto call = parseCallTail(name, line, col);
            auto es = std::make_shared<ExprStmt>();
            es->line = line; es->col = col; es->expr = call;
            expect(TokType::SEMI, "';'");
            return es;
        }
        auto as = std::make_shared<AssignStmt>();
        as->line = line; as->col = col; as->name = name;
        if (match(TokType::LBRACKET)) {
            as->indexExpr = parseExpression();
            expect(TokType::RBRACKET, "']'");
        }
        expect(TokType::ASSIGN, "'='");
        as->value = parseExpression();
        expect(TokType::SEMI, "';'");
        return as;
    }

    StmtPtr parseIf() {
        auto s = std::make_shared<IfStmt>();
        s->line = cur().line; s->col = cur().col;
        advance();
        expect(TokType::LPAREN, "'('");
        s->cond = parseExpression();
        expect(TokType::RPAREN, "')'");
        s->thenBlock = parseBlock();
        if (match(TokType::KW_ELSE)) {
            if (check(TokType::KW_IF)) s->elseBlock = parseIf();
            else s->elseBlock = parseBlock();
        }
        return s;
    }

    StmtPtr parseWhile() {
        auto s = std::make_shared<WhileStmt>();
        s->line = cur().line; s->col = cur().col;
        advance();
        expect(TokType::LPAREN, "'('");
        s->cond = parseExpression();
        expect(TokType::RPAREN, "')'");
        s->body = parseBlock();
        return s;
    }

    StmtPtr parseReturn() {
        auto s = std::make_shared<ReturnStmt>();
        s->line = cur().line; s->col = cur().col;
        advance();
        if (!check(TokType::SEMI)) s->value = parseExpression();
        expect(TokType::SEMI, "';'");
        return s;
    }

    ExprPtr parseExpression() { return parseOr(); }

    ExprPtr parseOr() {
        auto e = parseAnd();
        while (check(TokType::OR)) {
            int l = cur().line, c = cur().col; std::string op = advance().lexeme;
            auto rhs = parseAnd();
            auto b = std::make_shared<BinaryExpr>(); b->op = op; b->left = e; b->right = rhs; b->line = l; b->col = c;
            e = b;
        }
        return e;
    }
    ExprPtr parseAnd() {
        auto e = parseEquality();
        while (check(TokType::AND)) {
            int l = cur().line, c = cur().col; std::string op = advance().lexeme;
            auto rhs = parseEquality();
            auto b = std::make_shared<BinaryExpr>(); b->op = op; b->left = e; b->right = rhs; b->line = l; b->col = c;
            e = b;
        }
        return e;
    }
    ExprPtr parseEquality() {
        auto e = parseRelational();
        while (check(TokType::EQ) || check(TokType::NEQ)) {
            int l = cur().line, c = cur().col; std::string op = advance().lexeme;
            auto rhs = parseRelational();
            auto b = std::make_shared<BinaryExpr>(); b->op = op; b->left = e; b->right = rhs; b->line = l; b->col = c;
            e = b;
        }
        return e;
    }
    ExprPtr parseRelational() {
        auto e = parseAdditive();
        while (check(TokType::LT) || check(TokType::GT) || check(TokType::LE) || check(TokType::GE)) {
            int l = cur().line, c = cur().col; std::string op = advance().lexeme;
            auto rhs = parseAdditive();
            auto b = std::make_shared<BinaryExpr>(); b->op = op; b->left = e; b->right = rhs; b->line = l; b->col = c;
            e = b;
        }
        return e;
    }
    ExprPtr parseAdditive() {
        auto e = parseMultiplicative();
        while (check(TokType::PLUS) || check(TokType::MINUS)) {
            int l = cur().line, c = cur().col; std::string op = advance().lexeme;
            auto rhs = parseMultiplicative();
            auto b = std::make_shared<BinaryExpr>(); b->op = op; b->left = e; b->right = rhs; b->line = l; b->col = c;
            e = b;
        }
        return e;
    }
    ExprPtr parseMultiplicative() {
        auto e = parseUnary();
        while (check(TokType::STAR) || check(TokType::SLASH) || check(TokType::PERCENT)) {
            int l = cur().line, c = cur().col; std::string op = advance().lexeme;
            auto rhs = parseUnary();
            auto b = std::make_shared<BinaryExpr>(); b->op = op; b->left = e; b->right = rhs; b->line = l; b->col = c;
            e = b;
        }
        return e;
    }
    ExprPtr parseUnary() {
        if (check(TokType::MINUS) || check(TokType::NOT)) {
            int l = cur().line, c = cur().col; std::string op = advance().lexeme;
            auto operand = parseUnary();
            auto u = std::make_shared<UnaryExpr>(); u->op = op; u->operand = operand; u->line = l; u->col = c;
            return u;
        }
        return parsePrimary();
    }

    ExprPtr parseCallTail(const std::string& name, int line, int col) {
        expect(TokType::LPAREN, "'('");
        auto call = std::make_shared<CallExpr>();
        call->callee = name; call->line = line; call->col = col;
        if (!check(TokType::RPAREN)) {
            do { call->args.push_back(parseExpression()); } while (match(TokType::COMMA));
        }
        expect(TokType::RPAREN, "')'");
        return call;
    }

    ExprPtr parsePrimary() {
        int l = cur().line, c = cur().col;
        if (check(TokType::INT_LIT)) {
            auto lit = std::make_shared<IntLiteral>(); lit->value = std::stoll(advance().lexeme);
            lit->line = l; lit->col = c; return lit;
        }
        if (check(TokType::FLOAT_LIT)) {
            auto lit = std::make_shared<FloatLiteral>(); lit->value = std::stod(advance().lexeme);
            lit->line = l; lit->col = c; return lit;
        }
        if (check(TokType::BOOL_LIT)) {
            auto lit = std::make_shared<BoolLiteral>(); lit->value = (advance().lexeme == "true");
            lit->line = l; lit->col = c; return lit;
        }
        if (check(TokType::IDENT)) {
            std::string name = advance().lexeme;
            if (check(TokType::LPAREN)) return parseCallTail(name, l, c);
            if (match(TokType::LBRACKET)) {
                auto idx = parseExpression();
                expect(TokType::RBRACKET, "']'");
                auto aa = std::make_shared<ArrayAccess>(); aa->name = name; aa->index = idx; aa->line = l; aa->col = c;
                return aa;
            }
            auto id = std::make_shared<Identifier>(); id->name = name; id->line = l; id->col = c;
            return id;
        }
        if (match(TokType::LPAREN)) {
            auto e = parseExpression();
            expect(TokType::RPAREN, "')'");
            return e;
        }
        error("Expression expected, found '" + cur().lexeme + "'");
        throw ParseError("expression");
    }
};

// =====================================================================
// 7. CONSTANT FOLDING  (Optional Advanced Task)
//    Recursively collapses constant sub-expressions, e.g. 2 + 3 * 4 -> 24
// =====================================================================

class ConstantFolder {
public:
    ExprPtr fold(ExprPtr e) {
        if (!e) return e;
        if (auto b = std::dynamic_pointer_cast<BinaryExpr>(e)) {
            b->left = fold(b->left);
            b->right = fold(b->right);
            return foldBinary(b);
        }
        if (auto u = std::dynamic_pointer_cast<UnaryExpr>(e)) {
            u->operand = fold(u->operand);
            return foldUnary(u);
        }
        if (auto aa = std::dynamic_pointer_cast<ArrayAccess>(e)) {
            aa->index = fold(aa->index);
            return aa;
        }
        if (auto call = std::dynamic_pointer_cast<CallExpr>(e)) {
            for (auto& a : call->args) a = fold(a);
            return call;
        }
        return e;
    }

private:
    bool asNumber(const ExprPtr& e, double& out, bool& isFloat) {
        if (auto i = std::dynamic_pointer_cast<IntLiteral>(e)) { out = (double)i->value; isFloat = false; return true; }
        if (auto f = std::dynamic_pointer_cast<FloatLiteral>(e)) { out = f->value; isFloat = true; return true; }
        return false;
    }
    bool asBool(const ExprPtr& e, bool& out) {
        if (auto b = std::dynamic_pointer_cast<BoolLiteral>(e)) { out = b->value; return true; }
        return false;
    }

    ExprPtr makeNum(double v, bool isFloat, int line, int col) {
        if (isFloat) { auto f = std::make_shared<FloatLiteral>(); f->value = v; f->line = line; f->col = col; return f; }
        auto i = std::make_shared<IntLiteral>(); i->value = (long long)v; i->line = line; i->col = col; return i;
    }
    ExprPtr makeBool(bool v, int line, int col) {
        auto b = std::make_shared<BoolLiteral>(); b->value = v; b->line = line; b->col = col; return b;
    }

    ExprPtr foldUnary(std::shared_ptr<UnaryExpr> u) {
        double v; bool isFloat;
        if (u->op == "-" && asNumber(u->operand, v, isFloat)) return makeNum(-v, isFloat, u->line, u->col);
        bool bv;
        if (u->op == "!" && asBool(u->operand, bv)) return makeBool(!bv, u->line, u->col);
        return u;
    }

    ExprPtr foldBinary(std::shared_ptr<BinaryExpr> b) {
        double lv, rv; bool lf, rf;
        bool lIsNum = asNumber(b->left, lv, lf);
        bool rIsNum = asNumber(b->right, rv, rf);
        const std::string& op = b->op;

        if (lIsNum && rIsNum) {
            bool resultFloat = lf || rf;
            if (op == "+") return makeNum(lv + rv, resultFloat, b->line, b->col);
            if (op == "-") return makeNum(lv - rv, resultFloat, b->line, b->col);
            if (op == "*") return makeNum(lv * rv, resultFloat, b->line, b->col);
            if (op == "/") { if (rv != 0) return makeNum(resultFloat ? (lv / rv) : (double)((long long)lv / (long long)rv), resultFloat, b->line, b->col); return b; }
            if (op == "%") { if (!resultFloat && (long long)rv != 0) return makeNum((double)((long long)lv % (long long)rv), false, b->line, b->col); return b; }
            if (op == "<")  return makeBool(lv < rv, b->line, b->col);
            if (op == ">")  return makeBool(lv > rv, b->line, b->col);
            if (op == "<=") return makeBool(lv <= rv, b->line, b->col);
            if (op == ">=") return makeBool(lv >= rv, b->line, b->col);
            if (op == "==") return makeBool(lv == rv, b->line, b->col);
            if (op == "!=") return makeBool(lv != rv, b->line, b->col);
        }
        bool lb, rb;
        bool lIsBool = asBool(b->left, lb);
        bool rIsBool = asBool(b->right, rb);
        if (lIsBool && rIsBool) {
            if (op == "&&") return makeBool(lb && rb, b->line, b->col);
            if (op == "||") return makeBool(lb || rb, b->line, b->col);
            if (op == "==") return makeBool(lb == rb, b->line, b->col);
            if (op == "!=") return makeBool(lb != rb, b->line, b->col);
        }
        return b;
    }
};

// =====================================================================
// 8. SYMBOL TABLE
// =====================================================================

struct Symbol {
    std::string name;
    Type type = Type::UNKNOWN;
    bool isArray = false;
    int arraySize = 0;
    bool initialized = false;
    bool isFunction = false;
    std::vector<Type> paramTypes;
    Type returnType = Type::VOID;
    int declLine = 0;
};

class SymbolTable {
public:
    void enterScope() { scopes_.emplace_back(); }
    void exitScope()  { scopes_.pop_back(); }

    // returns false if redeclared in the *current* scope
    bool declare(const Symbol& sym) {
        auto& top = scopes_.back();
        if (top.count(sym.name)) return false;
        top[sym.name] = sym;
        return true;
    }

    Symbol* lookup(const std::string& name) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) return &f->second;
        }
        return nullptr;
    }

    // Lookup restricted to global scope (scope 0) - used for functions
    Symbol* lookupGlobal(const std::string& name) {
        if (scopes_.empty()) return nullptr;
        auto f = scopes_.front().find(name);
        if (f != scopes_.front().end()) return &f->second;
        return nullptr;
    }

    void print(std::ostream& os) const {
        os << "\n=== Symbol Table (final global scope) ===\n";
        if (scopes_.empty()) return;
        for (auto& [name, sym] : scopes_.front()) {
            if (sym.isFunction) {
                os << "  function " << name << "(";
                for (size_t i = 0; i < sym.paramTypes.size(); ++i) {
                    os << typeName(sym.paramTypes[i]);
                    if (i + 1 < sym.paramTypes.size()) os << ", ";
                }
                os << ") -> " << typeName(sym.returnType) << "\n";
            }
        }
    }

private:
    std::vector<std::unordered_map<std::string, Symbol>> scopes_;
};

// =====================================================================
// 9. SEMANTIC ANALYZER
//    - scoped symbol tables & type checking
//    - function declaration / call correctness
//    - undeclared identifier / redeclaration detection
//    - uninitialized variable usage detection  (Advanced Task)
//    - constant folding integration            (Advanced Task)
//    - array type-checked indexed access       (Advanced Task)
// =====================================================================

class SemanticAnalyzer {
public:
    void analyze(Program& prog) {
        symtab_.enterScope(); // global scope holds function signatures

        // pass 1: register all function signatures first (allows forward calls / recursion)
        for (auto& fn : prog.functions) {
            Symbol s;
            s.name = fn->name; s.isFunction = true; s.returnType = fn->returnType;
            for (auto& p : fn->params) s.paramTypes.push_back(p.type);
            s.declLine = fn->line;
            if (!symtab_.declare(s)) {
                reportError("Semantic", "Function '" + fn->name + "' is already declared", fn->line, fn->col);
            }
        }

        // pass 2: analyze each function body
        for (auto& fn : prog.functions) analyzeFunction(*fn);

        // global scope intentionally left open so printSymbolTable() can show it
    }

    void printSymbolTable(std::ostream& os) { symtab_.print(os); }

private:
    SymbolTable symtab_;
    Type currentReturnType_ = Type::VOID;
    std::string currentFuncName_;
    ConstantFolder folder_;

    void analyzeFunction(FuncDecl& fn) {
        currentReturnType_ = fn.returnType;
        currentFuncName_ = fn.name;
        symtab_.enterScope();
        for (auto& p : fn.params) {
            Symbol s; s.name = p.name; s.type = p.type; s.isArray = p.isArray;
            s.initialized = true; // parameters are considered initialized
            s.declLine = fn.line;
            if (!symtab_.declare(s))
                reportError("Semantic", "Parameter '" + p.name + "' redeclared in function '" + fn.name + "'", fn.line, fn.col);
        }
        analyzeStmt(fn.body);
        symtab_.exitScope();
    }

    void analyzeStmt(const StmtPtr& stmt) {
        if (!stmt) return;
        if (auto blk = std::dynamic_pointer_cast<Block>(stmt)) {
            symtab_.enterScope();
            for (auto& s : blk->stmts) analyzeStmt(s);
            symtab_.exitScope();
            return;
        }
        if (auto vd = std::dynamic_pointer_cast<VarDeclStmt>(stmt)) { analyzeVarDecl(*vd); return; }
        if (auto as = std::dynamic_pointer_cast<AssignStmt>(stmt))  { analyzeAssign(*as);  return; }
        if (auto ifs = std::dynamic_pointer_cast<IfStmt>(stmt))     { analyzeIf(*ifs);     return; }
        if (auto ws = std::dynamic_pointer_cast<WhileStmt>(stmt))   { analyzeWhile(*ws);   return; }
        if (auto rs = std::dynamic_pointer_cast<ReturnStmt>(stmt))  { analyzeReturn(*rs);  return; }
        if (auto es = std::dynamic_pointer_cast<ExprStmt>(stmt))    { es->expr = folder_.fold(es->expr); analyzeExpr(es->expr); return; }
    }

    void analyzeVarDecl(VarDeclStmt& vd) {
        Symbol s;
        s.name = vd.name; s.type = vd.declType; s.isArray = vd.isArray; s.arraySize = vd.arraySize;
        s.declLine = vd.line;
        if (vd.initExpr) {
            vd.initExpr = folder_.fold(vd.initExpr);
            Type initT = analyzeExpr(vd.initExpr);
            checkAssignCompat(vd.declType, initT, vd.line, vd.col, vd.name);
            s.initialized = true;
        } else {
            s.initialized = false; // not yet initialized -> tracked for advanced task
        }
        if (!symtab_.declare(s)) {
            reportError("Semantic", "Variable '" + vd.name + "' redeclared in the same scope", vd.line, vd.col);
        }
    }

    void analyzeAssign(AssignStmt& as) {
        Symbol* sym = symtab_.lookup(as.name);
        if (!sym) {
            reportError("Semantic", "Undeclared identifier '" + as.name + "'", as.line, as.col);
            // still analyze RHS so further errors can surface
            as.value = folder_.fold(as.value);
            analyzeExpr(as.value);
            return;
        }
        as.value = folder_.fold(as.value);
        Type rhsT = analyzeExpr(as.value);

        if (as.indexExpr) {
            // Array element assignment - advanced task: type-checked indexed access
            if (!sym->isArray) {
                reportError("Semantic", "'" + as.name + "' is not an array", as.line, as.col);
            } else {
                as.indexExpr = folder_.fold(as.indexExpr);
                Type idxT = analyzeExpr(as.indexExpr);
                if (idxT != Type::INT && idxT != Type::UNKNOWN)
                    reportError("Semantic", "Array index for '" + as.name + "' must be of type int", as.line, as.col);
                checkAssignCompat(baseOfArray(sym->type), rhsT, as.line, as.col, as.name + "[]");
            }
        } else {
            if (sym->isFunction) {
                reportError("Semantic", "Cannot assign to function '" + as.name + "'", as.line, as.col);
            } else {
                checkAssignCompat(sym->type, rhsT, as.line, as.col, as.name);
            }
        }
        sym->initialized = true; // after assignment the variable becomes initialized
    }

    void analyzeIf(IfStmt& ifs) {
        ifs.cond = folder_.fold(ifs.cond);
        Type ct = analyzeExpr(ifs.cond);
        if (ct != Type::BOOL && ct != Type::UNKNOWN)
            reportError("Semantic", "'if' condition must be of type bool, got " + typeName(ct), ifs.line, ifs.col);
        analyzeStmt(ifs.thenBlock);
        if (ifs.elseBlock) analyzeStmt(ifs.elseBlock);
    }

    void analyzeWhile(WhileStmt& ws) {
        ws.cond = folder_.fold(ws.cond);
        Type ct = analyzeExpr(ws.cond);
        if (ct != Type::BOOL && ct != Type::UNKNOWN)
            reportError("Semantic", "'while' condition must be of type bool, got " + typeName(ct), ws.line, ws.col);
        analyzeStmt(ws.body);
    }

    void analyzeReturn(ReturnStmt& rs) {
        if (rs.value) {
            rs.value = folder_.fold(rs.value);
            Type t = analyzeExpr(rs.value);
            if (currentReturnType_ == Type::VOID) {
                reportError("Semantic", "Function '" + currentFuncName_ + "' is void but returns a value", rs.line, rs.col);
            } else if (!isAssignable(currentReturnType_, t) && t != Type::UNKNOWN) {
                reportError("Semantic", "Function '" + currentFuncName_ + "' must return " + typeName(currentReturnType_) +
                                          " but returns " + typeName(t), rs.line, rs.col);
            }
        } else {
            if (currentReturnType_ != Type::VOID)
                reportError("Semantic", "Function '" + currentFuncName_ + "' must return a value of type " +
                                          typeName(currentReturnType_), rs.line, rs.col);
        }
    }

    bool isAssignable(Type target, Type src) {
        if (target == src) return true;
        // implicit int -> float widening only
        if (target == Type::FLOAT && src == Type::INT) return true;
        return false;
    }

    void checkAssignCompat(Type target, Type src, int line, int col, const std::string& who) {
        if (src == Type::UNKNOWN) return;
        if (!isAssignable(target, src)) {
            reportError("Semantic", "Type mismatch assigning to '" + who + "': cannot assign " +
                                      typeName(src) + " to " + typeName(target) +
                                      (target == Type::INT && src == Type::FLOAT ? " (assigning float to int requires explicit cast)" : ""),
                        line, col);
        }
    }

    Type analyzeExpr(const ExprPtr& e) {
        if (!e) return Type::UNKNOWN;
        if (auto i = std::dynamic_pointer_cast<IntLiteral>(e))   { e->resolvedType = Type::INT; return Type::INT; }
        if (auto f = std::dynamic_pointer_cast<FloatLiteral>(e)) { e->resolvedType = Type::FLOAT; return Type::FLOAT; }
        if (auto b = std::dynamic_pointer_cast<BoolLiteral>(e))  { e->resolvedType = Type::BOOL; return Type::BOOL; }

        if (auto id = std::dynamic_pointer_cast<Identifier>(e)) {
            Symbol* sym = symtab_.lookup(id->name);
            if (!sym) {
                reportError("Semantic", "Undeclared identifier '" + id->name + "'", id->line, id->col);
                e->resolvedType = Type::UNKNOWN; return Type::UNKNOWN;
            }
            if (sym->isFunction) {
                reportError("Semantic", "'" + id->name + "' is a function, not a variable", id->line, id->col);
                return Type::UNKNOWN;
            }
            // Advanced task: uninitialized variable usage detection
            if (!sym->initialized) {
                reportError("Semantic", "Variable '" + id->name + "' may be used before being initialized", id->line, id->col);
            }
            e->resolvedType = sym->type;
            return sym->type;
        }

        if (auto aa = std::dynamic_pointer_cast<ArrayAccess>(e)) {
            Symbol* sym = symtab_.lookup(aa->name);
            Type idxT = analyzeExpr(aa->index);
            if (idxT != Type::INT && idxT != Type::UNKNOWN)
                reportError("Semantic", "Array index for '" + aa->name + "' must be of type int", aa->line, aa->col);
            if (!sym) {
                reportError("Semantic", "Undeclared identifier '" + aa->name + "'", aa->line, aa->col);
                return Type::UNKNOWN;
            }
            if (!sym->isArray) {
                reportError("Semantic", "'" + aa->name + "' is not an array", aa->line, aa->col);
                return Type::UNKNOWN;
            }
            e->resolvedType = baseOfArray(sym->type);
            return e->resolvedType;
        }

        if (auto u = std::dynamic_pointer_cast<UnaryExpr>(e)) {
            Type t = analyzeExpr(u->operand);
            if (u->op == "-") {
                if (t != Type::INT && t != Type::FLOAT && t != Type::UNKNOWN)
                    reportError("Semantic", "Unary '-' requires numeric operand, got " + typeName(t), u->line, u->col);
                e->resolvedType = t;
            } else if (u->op == "!") {
                if (t != Type::BOOL && t != Type::UNKNOWN)
                    reportError("Semantic", "Unary '!' requires bool operand, got " + typeName(t), u->line, u->col);
                e->resolvedType = Type::BOOL;
            }
            return e->resolvedType;
        }

        if (auto b = std::dynamic_pointer_cast<BinaryExpr>(e)) {
            Type lt = analyzeExpr(b->left);
            Type rt = analyzeExpr(b->right);
            const std::string& op = b->op;
            if (op == "&&" || op == "||") {
                if ((lt != Type::BOOL && lt != Type::UNKNOWN) || (rt != Type::BOOL && rt != Type::UNKNOWN))
                    reportError("Semantic", "Operator '" + op + "' requires bool operands, got " +
                                              typeName(lt) + " and " + typeName(rt), b->line, b->col);
                e->resolvedType = Type::BOOL;
                return Type::BOOL;
            }
            if (op == "==" || op == "!=") {
                if (lt != rt && lt != Type::UNKNOWN && rt != Type::UNKNOWN &&
                    !(isNumeric(lt) && isNumeric(rt)))
                    reportError("Semantic", "Cannot compare " + typeName(lt) + " with " + typeName(rt), b->line, b->col);
                e->resolvedType = Type::BOOL;
                return Type::BOOL;
            }
            if (op == "<" || op == ">" || op == "<=" || op == ">=") {
                if ((!isNumeric(lt) && lt != Type::UNKNOWN) || (!isNumeric(rt) && rt != Type::UNKNOWN))
                    reportError("Semantic", "Relational operator '" + op + "' requires numeric operands, got " +
                                              typeName(lt) + " and " + typeName(rt), b->line, b->col);
                e->resolvedType = Type::BOOL;
                return Type::BOOL;
            }
            // arithmetic: + - * / %
            if (!isNumeric(lt) && lt != Type::UNKNOWN)
                reportError("Semantic", "Operator '" + op + "' requires numeric left operand, got " + typeName(lt), b->line, b->col);
            if (!isNumeric(rt) && rt != Type::UNKNOWN)
                reportError("Semantic", "Operator '" + op + "' requires numeric right operand, got " + typeName(rt), b->line, b->col);
            Type result = (lt == Type::FLOAT || rt == Type::FLOAT) ? Type::FLOAT : Type::INT;
            e->resolvedType = result;
            return result;
        }

        if (auto call = std::dynamic_pointer_cast<CallExpr>(e)) {
            Symbol* sym = symtab_.lookupGlobal(call->callee);
            if (!sym || !sym->isFunction) {
                reportError("Semantic", "Call to undeclared function '" + call->callee + "'", call->line, call->col);
                for (auto& a : call->args) analyzeExpr(a);
                return Type::UNKNOWN;
            }
            if (call->args.size() != sym->paramTypes.size()) {
                reportError("Semantic", "Function '" + call->callee + "' expects " +
                                          std::to_string(sym->paramTypes.size()) + " argument(s) but got " +
                                          std::to_string(call->args.size()), call->line, call->col);
            }
            size_t n = std::min(call->args.size(), sym->paramTypes.size());
            for (size_t i = 0; i < n; ++i) {
                call->args[i] = folder_.fold(call->args[i]);
                Type at = analyzeExpr(call->args[i]);
                if (at != Type::UNKNOWN && !isAssignable(sym->paramTypes[i], at) && sym->paramTypes[i] != at)
                    reportError("Semantic", "Argument " + std::to_string(i + 1) + " to '" + call->callee +
                                              "' has type " + typeName(at) + ", expected " + typeName(sym->paramTypes[i]),
                                call->args[i]->line, call->args[i]->col);
            }
            for (size_t i = n; i < call->args.size(); ++i) analyzeExpr(call->args[i]);
            e->resolvedType = sym->returnType;
            return sym->returnType;
        }
        return Type::UNKNOWN;
    }

    bool isNumeric(Type t) { return t == Type::INT || t == Type::FLOAT; }
};

// =====================================================================
// 10. THREE-ADDRESS CODE GENERATOR  (Optional Advanced Task)
// =====================================================================

class TACGenerator {
public:
    std::vector<std::string> generate(Program& prog) {
        for (auto& fn : prog.functions) genFunction(*fn);
        return code_;
    }

private:
    std::vector<std::string> code_;
    int tempCount_ = 0;
    int labelCount_ = 0;

    std::string newTemp()  { return "t" + std::to_string(tempCount_++); }
    std::string newLabel() { return "L" + std::to_string(labelCount_++); }
    void emit(const std::string& s) { code_.push_back(s); }

    void genFunction(FuncDecl& fn) {
        std::string sig = "func " + fn.name + "(";
        for (size_t i = 0; i < fn.params.size(); ++i) {
            sig += typeName(fn.params[i].type) + " " + fn.params[i].name;
            if (i + 1 < fn.params.size()) sig += ", ";
        }
        sig += "):";
        emit(sig);
        genStmt(fn.body);
        emit("endfunc " + fn.name);
        emit("");
    }

    void genStmt(const StmtPtr& s) {
        if (!s) return;
        if (auto blk = std::dynamic_pointer_cast<Block>(s)) {
            for (auto& st : blk->stmts) genStmt(st);
            return;
        }
        if (auto vd = std::dynamic_pointer_cast<VarDeclStmt>(s)) {
            if (vd->initExpr) {
                std::string r = genExpr(vd->initExpr);
                emit(vd->name + " = " + r);
            }
            return;
        }
        if (auto as = std::dynamic_pointer_cast<AssignStmt>(s)) {
            std::string r = genExpr(as->value);
            if (as->indexExpr) {
                std::string idx = genExpr(as->indexExpr);
                emit(as->name + "[" + idx + "] = " + r);
            } else {
                emit(as->name + " = " + r);
            }
            return;
        }
        if (auto ifs = std::dynamic_pointer_cast<IfStmt>(s)) {
            std::string c = genExpr(ifs->cond);
            std::string elseLbl = newLabel();
            std::string endLbl  = newLabel();
            emit("ifFalse " + c + " goto " + elseLbl);
            genStmt(ifs->thenBlock);
            emit("goto " + endLbl);
            emit(elseLbl + ":");
            if (ifs->elseBlock) genStmt(ifs->elseBlock);
            emit(endLbl + ":");
            return;
        }
        if (auto ws = std::dynamic_pointer_cast<WhileStmt>(s)) {
            std::string startLbl = newLabel();
            std::string endLbl   = newLabel();
            emit(startLbl + ":");
            std::string c = genExpr(ws->cond);
            emit("ifFalse " + c + " goto " + endLbl);
            genStmt(ws->body);
            emit("goto " + startLbl);
            emit(endLbl + ":");
            return;
        }
        if (auto rs = std::dynamic_pointer_cast<ReturnStmt>(s)) {
            if (rs->value) emit("return " + genExpr(rs->value));
            else emit("return");
            return;
        }
        if (auto es = std::dynamic_pointer_cast<ExprStmt>(s)) {
            genExpr(es->expr);
            return;
        }
    }

    std::string genExpr(const ExprPtr& e) {
        if (!e) return "";
        if (auto i = std::dynamic_pointer_cast<IntLiteral>(e))   return std::to_string(i->value);
        if (auto f = std::dynamic_pointer_cast<FloatLiteral>(e)) return std::to_string(f->value);
        if (auto b = std::dynamic_pointer_cast<BoolLiteral>(e))  return b->value ? "true" : "false";
        if (auto id = std::dynamic_pointer_cast<Identifier>(e))  return id->name;
        if (auto aa = std::dynamic_pointer_cast<ArrayAccess>(e)) {
            std::string idx = genExpr(aa->index);
            std::string t = newTemp();
            emit(t + " = " + aa->name + "[" + idx + "]");
            return t;
        }
        if (auto u = std::dynamic_pointer_cast<UnaryExpr>(e)) {
            std::string r = genExpr(u->operand);
            std::string t = newTemp();
            emit(t + " = " + u->op + r);
            return t;
        }
        if (auto b = std::dynamic_pointer_cast<BinaryExpr>(e)) {
            std::string l = genExpr(b->left);
            std::string r = genExpr(b->right);
            std::string t = newTemp();
            emit(t + " = " + l + " " + b->op + " " + r);
            return t;
        }
        if (auto call = std::dynamic_pointer_cast<CallExpr>(e)) {
            std::vector<std::string> argRegs;
            for (auto& a : call->args) argRegs.push_back(genExpr(a));
            for (auto& ar : argRegs) emit("param " + ar);
            std::string t = newTemp();
            emit(t + " = call " + call->callee + ", " + std::to_string(call->args.size()));
            return t;
        }
        return "";
    }
};

// =====================================================================
// 11. AST PRINTER (Tokens / quick textual dump, useful for debugging)
// =====================================================================

static void printTokens(const std::vector<Token>& toks, std::ostream& os) {
    os << "\n=== Tokens ===\n";
    for (auto& t : toks) {
        if (t.type == TokType::END_OF_FILE) break;
        os << "  [" << t.line << ":" << t.col << "] " << tokTypeName(t.type)
           << " '" << t.lexeme << "'\n";
    }
}

// ---------------------------------------------------------------------
// Level-wise (indented) Abstract Syntax Tree / Parse Tree printer.
// Each nesting level is shown with deeper indentation, so the structure
// of the parsed program is visible without drawing an actual tree graph.
// ---------------------------------------------------------------------

static std::string exprToString(const ExprPtr& e) {
    if (!e) return "";
    if (auto i = std::dynamic_pointer_cast<IntLiteral>(e))   return std::to_string(i->value);
    if (auto f = std::dynamic_pointer_cast<FloatLiteral>(e)) return std::to_string(f->value);
    if (auto b = std::dynamic_pointer_cast<BoolLiteral>(e))  return b->value ? "true" : "false";
    if (auto id = std::dynamic_pointer_cast<Identifier>(e))  return id->name;
    if (auto aa = std::dynamic_pointer_cast<ArrayAccess>(e)) return aa->name + "[" + exprToString(aa->index) + "]";
    if (auto u = std::dynamic_pointer_cast<UnaryExpr>(e))    return u->op + exprToString(u->operand);
    if (auto b = std::dynamic_pointer_cast<BinaryExpr>(e))   return "(" + exprToString(b->left) + " " + b->op + " " + exprToString(b->right) + ")";
    if (auto c = std::dynamic_pointer_cast<CallExpr>(e)) {
        std::string s = c->callee + "(";
        for (size_t i = 0; i < c->args.size(); ++i) { s += exprToString(c->args[i]); if (i + 1 < c->args.size()) s += ", "; }
        return s + ")";
    }
    return "?";
}

static void printTreeLine(std::ostream& os, const std::string& prefix, bool isLast, const std::string& label) {
    os << prefix << (isLast ? "`-- " : "|-- ") << label << "\n";
}
static std::string childPrefixFor(const std::string& prefix, bool isLast) {
    return prefix + (isLast ? "    " : "|   ");
}

// Forward declaration so blocks/ifs/whiles can recurse into their children.
static void printASTStmt(const StmtPtr& s, std::ostream& os, const std::string& prefix, bool isLast);

// Prints a list of statements as siblings under the given prefix.
static void printStmtList(const std::vector<StmtPtr>& stmts, std::ostream& os, const std::string& prefix) {
    for (size_t i = 0; i < stmts.size(); ++i) {
        printASTStmt(stmts[i], os, prefix, i + 1 == stmts.size());
    }
}

static void printASTStmt(const StmtPtr& s, std::ostream& os, const std::string& prefix, bool isLast) {
    if (!s) return;

    if (auto blk = std::dynamic_pointer_cast<Block>(s)) {
        printTreeLine(os, prefix, isLast, "Block");
        printStmtList(blk->stmts, os, childPrefixFor(prefix, isLast));
        return;
    }
    if (auto vd = std::dynamic_pointer_cast<VarDeclStmt>(s)) {
        std::string label = "VarDecl: " + typeName(vd->declType) + " " + vd->name;
        if (vd->isArray) label += "[" + std::to_string(vd->arraySize) + "]";
        if (vd->initExpr) label += " = " + exprToString(vd->initExpr);
        label += "  (line " + std::to_string(vd->line) + ")";
        printTreeLine(os, prefix, isLast, label);
        return;
    }
    if (auto as = std::dynamic_pointer_cast<AssignStmt>(s)) {
        std::string label = "Assign: " + as->name;
        if (as->indexExpr) label += "[" + exprToString(as->indexExpr) + "]";
        label += " = " + exprToString(as->value) + "  (line " + std::to_string(as->line) + ")";
        printTreeLine(os, prefix, isLast, label);
        return;
    }
    if (auto ifs = std::dynamic_pointer_cast<IfStmt>(s)) {
        printTreeLine(os, prefix, isLast, "If (" + exprToString(ifs->cond) + ")  (line " + std::to_string(ifs->line) + ")");
        std::string cp = childPrefixFor(prefix, isLast);
        bool hasElse = (ifs->elseBlock != nullptr);
        printTreeLine(os, cp, !hasElse, "Then:");
        printASTStmt(ifs->thenBlock, os, childPrefixFor(cp, !hasElse), true);
        if (hasElse) {
            printTreeLine(os, cp, true, "Else:");
            printASTStmt(ifs->elseBlock, os, childPrefixFor(cp, true), true);
        }
        return;
    }
    if (auto ws = std::dynamic_pointer_cast<WhileStmt>(s)) {
        printTreeLine(os, prefix, isLast, "While (" + exprToString(ws->cond) + ")  (line " + std::to_string(ws->line) + ")");
        printASTStmt(ws->body, os, childPrefixFor(prefix, isLast), true);
        return;
    }
    if (auto rs = std::dynamic_pointer_cast<ReturnStmt>(s)) {
        std::string label = "Return";
        if (rs->value) label += " " + exprToString(rs->value);
        label += "  (line " + std::to_string(rs->line) + ")";
        printTreeLine(os, prefix, isLast, label);
        return;
    }
    if (auto es = std::dynamic_pointer_cast<ExprStmt>(s)) {
        printTreeLine(os, prefix, isLast, "ExprStmt: " + exprToString(es->expr) + "  (line " + std::to_string(es->line) + ")");
        return;
    }
    if (auto err = std::dynamic_pointer_cast<ErrorStmt>(s)) {
        printTreeLine(os, prefix, isLast, "<ERROR: statement could not be parsed, skipped>  (line " + std::to_string(err->line) + ")");
        return;
    }
}

static void printAST(Program& prog, std::ostream& os) {
    os << "\n=== Parse Tree / AST (tree view) ===\n";
    for (size_t fi = 0; fi < prog.functions.size(); ++fi) {
        auto& fn = prog.functions[fi];
        std::string sig = "Function " + typeName(fn->returnType) + " " + fn->name + "(";
        for (size_t i = 0; i < fn->params.size(); ++i) {
            sig += typeName(fn->params[i].type) + " " + fn->params[i].name;
            if (i + 1 < fn->params.size()) sig += ", ";
        }
        sig += ")  (line " + std::to_string(fn->line) + ")";
        os << sig << "\n";
        printASTStmt(fn->body, os, "", true);
        if (fi + 1 < prog.functions.size()) os << "\n";
    }
}

// =====================================================================
// 12. MAIN DRIVER
// =====================================================================

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open file: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void printErrorsForPhase(const std::string& phase, const std::string& heading) {
    bool any = false;
    for (auto& e : g_errors) {
        if (e.phase != phase) continue;
        if (!any) { std::cout << "\n" << heading << "\n"; any = true; }
        std::cout << "[" << e.phase << " Error] Line " << e.line << ", Col " << e.col
                   << ": " << e.message << "\n";
    }
    if (!any) std::cout << "\n" << heading << " None.\n";
}

static const char* SAMPLE_PROGRAM =
"int max(int a, int b) {\n"
"  if (a > b) {\n"
"    return a;\n"
"  } else {\n"
"    return b;\n"
"  }\n"
"}\n"
"\n"
"int main() {\n"
"  int x = 10;\n"
"  int y = 20;\n"
"  int z = max(x, y);\n"
"  float pi = 3 + 0.14;\n"
"  int arr[5];\n"
"  arr[0] = 42;\n"
"  int w;\n"
"  int bad = w + 1;\n"
"  return 0;\n"
"}\n";

int main(int argc, char** argv) {
    std::string source;
    std::string sourceName;

    if (argc >= 2) {
        // Mode 1: a filename was passed as a command-line argument
        try {
            source = readFile(argv[1]);
            sourceName = argv[1];
        } catch (std::exception& ex) {
            std::cerr << ex.what() << "\n";
            return 1;
        }
    } else {
        // Mode 2: no file given -> ask the user to type/paste MiniLang++ code
        // directly into the console (interactive input).
        std::cout << "==========================================\n";
        std::cout << " MiniLang++ Compiler Front-End\n";
        std::cout << "==========================================\n";
        std::cout << "No input file given.\n\n";
        std::cout << "Choose an option:\n";
        std::cout << "  1) Paste/type MiniLang++ source code now\n";
        std::cout << "  2) Run the built-in sample program instead\n";
        std::cout << "Enter choice (1 or 2): ";

        std::string choice;
        std::getline(std::cin, choice);

        if (!choice.empty() && choice[0] == '2') {
            sourceName = "<embedded sample>";
            source = SAMPLE_PROGRAM;
        } else {
            std::cout << "\nType or paste your MiniLang++ code below.\n"
                          "When finished, type a line containing only: END\n"
                          "------------------------------------------\n";
            std::ostringstream collected;
            std::string line;
            while (std::getline(std::cin, line)) {
                if (line == "END") break;
                collected << line << "\n";
            }
            source = collected.str();
            sourceName = "<console input>";

            if (source.empty()) {
                std::cout << "\nNo code entered - running the built-in sample program instead.\n";
                source = SAMPLE_PROGRAM;
                sourceName = "<embedded sample>";
            }
        }
    }

    std::cout << "==========================================\n";
    std::cout << " MiniLang++ Compiler Front-End\n";
    std::cout << " Source: " << sourceName << "\n";
    std::cout << "==========================================\n";

    // ---- 1. Lexical Analysis ----
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    printTokens(tokens, std::cout);
    printErrorsForPhase("Lexical", "=== Lexical Errors ===");

    // ---- 2. Syntax Analysis ----
    Parser parser(tokens);
    Program prog = parser.parseProgram();
    printAST(prog, std::cout);   // level-wise parse tree, as required
    printErrorsForPhase("Syntax", "=== Syntax Errors ===");

    // ---- 3. Semantic Analysis (includes constant folding & uninitialized-var checks) ----
    SemanticAnalyzer sema;
    sema.analyze(prog);
    sema.printSymbolTable(std::cout);
    printErrorsForPhase("Semantic", "=== Semantic Errors ===");

    // ---- 4. Intermediate Code Generation (optional) ----
    TACGenerator tacGen;
    auto tac = tacGen.generate(prog);
    std::cout << "\n=== Three-Address Code ===\n";
    for (auto& line : tac) std::cout << "  " << line << "\n";

    // ---- 5. Final Summary ----
    if (!g_errors.empty()) {
        std::cout << "\nCompilation finished with " << g_errors.size() << " error(s).\n";
        return 1;
    }
    std::cout << "\nCompilation finished successfully, no errors found.\n";
    return 0;
}
