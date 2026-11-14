#ifndef CALC_TYPEDEF_C
#define CALC_TYPEDEF_C
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/*

<expr> ::= <equality>

<equality> ::= <equality> "==" <comparison>
             | <equality> "!=" <comparison>
             | <comparison>

<comparison> ::= <comparison> "<" <additive>
               | <comparison> "<=" <additive>
               | <comparison> ">" <additive>
               | <comparison> ">=" <additive>
               | <additive>

<additive> ::= <additive> "+" <multiplicative>
             | <additive> "-" <multiplicative>
             | <multiplicative>

<multiplicative> ::= <multiplicative> "*" <unary>
                   | <multiplicative> "/" <unary>
                   | <unary>

<unary> ::= "-" <unary>
          | <primary>

<primary> ::= <number>
            | <ident>
            | "(" <expr> ")"


<program> ::= <statement-list> EOF

<statement-list> ::= ε
                   | <statement> <statement-list>

<statement> ::= <let-statement>
              | <assign-statement>
              | <print-statement>
              | <if-statement>
              | <while-statement>
              | <block>
              | <expr-statement>

<let-statement> ::= "let" <ident> "=" <expr> ";"

<assign-statement> ::= <ident> "=" <expr> ";"

<print-statement> ::= "print" "(" <expr> ")" ";"

<expr-statement> ::= <expr> ";"

<block> ::= "{" <statement-list> "}"

<if-statement> ::= "if" "(" <expr> ")" <block> <else-opt>

<else-opt> ::= ε
             | "else" <block>

<while-statement> ::= "while" "(" <expr> ")" <block>
*/
typedef struct Expr Expr;
typedef struct Statement Statement;
typedef struct Token Token;

typedef struct 
{
    Statement **statesments;
    size_t count;
    size_t capacity;
    int offset;
    int required_size;
}Program;

typedef enum {
    STMT_LET,       // 変数宣言
    STMT_ASSIGN,    // 代入
    STMT_EXPR,      // 式文
    STMT_PRINT,     // 出力
    STMT_BLOCK,     // ブロック
    STMT_IF,        // 条件分岐
    STMT_WHILE,     // 繰り返し
    //STMT_RETURN,    // 関数の戻り値
    //STMT_FUNCTION   // 関数定義
} StatementType;


typedef enum {
    TK_NUMBER,
    TK_IDENT,
    TK_LET,
    TK_IF,
    TK_ELSE,
    TK_WHILE,
    //TK_RETURN,
    //TK_FN,
    TK_PRINT,
    TK_PLUS,
    TK_MINUS,
    TK_STAR,
    TK_SLASH,
    TK_ASSIGN,
    TK_EQ,
    TK_NE,
    TK_LT,
    TK_LE,
    TK_GT,
    TK_GE,
    TK_LPAREN,
    TK_RPAREN,
    TK_LBRACE,
    TK_RBRACE,
    TK_SEMICOLON,
    TK_COMMA,
    TK_EOF
} TokenType;


typedef enum {
    EXPR_NUMBER,
    EXPR_VARIABLE,
    EXPR_BINARY,
    EXPR_UNARY,
    //EXPR_CALL
} ExprType;


struct Expr{
    ExprType type;
    Token *token;

    union {
        struct
        {
            long long number;
            char *literal;
        } number;

        struct
        {
            char *var_name;
            int offset;
        } variable;
    

        struct {
            TokenType op;
            Expr *left;
            Expr *right;
        } binary;

        struct {
            TokenType op;
            Expr *operand;
        } unary;

    } data;
};

struct Statement
{
    StatementType type;
    Token *token;
    union {
        struct {
            char *name;
            Expr *value;
            int offset;
        } let;

        struct {
            Expr *expression;
        } print;

        struct {
            Expr *condition;
            Statement *body;
        } while_stmt;

        struct {
            Expr *condition;
            Statement *then_branch;
            Statement *else_branch;
        } if_stmt;

        struct {
            Statement **statements;
            size_t count;
            size_t capacity;
        } block;

        struct {
            char *name;
            Expr *value;
            int offset;
        } assign;
        
    } data;
};

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} Source;

struct Token {
    TokenType type;
    char *text;
    size_t line;
    size_t column;
};

typedef struct {
    Token *tokens;
    size_t count;
    size_t capacity;
} TokenArray;

typedef struct {
    char *name;
    int offset;
} Symbol;

typedef struct {
    Symbol *symbols;
    size_t count;
    size_t capacity;
    size_t scope_start;
} SymbolTable;

#endif
