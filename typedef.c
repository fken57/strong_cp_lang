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
typedef struct Function Function;

typedef struct 
{
    Statement **statesments;
    size_t count;
    size_t capacity;
    int offset;
    int required_size;
    Function **functions;
    size_t function_count;
}Program;

typedef enum {
    STMT_LET,       // 変数宣言
    STMT_ASSIGN,    // 代入
    STMT_EXPR,      // 式文
    STMT_PRINT,     // 出力
    STMT_BLOCK,     // ブロック
    STMT_IF,        // 条件分岐
    STMT_WHILE,     // 繰り返し
    STMT_FOR,
    STMT_RETURN,
    STMT_BREAK,
} StatementType;


typedef enum {
    TK_NUMBER,
    TK_IDENT,
    TK_LET,
    TK_INT,
    TK_BREAK,
    TK_DOT,
    TK_IF,
    TK_ELSE,
    TK_WHILE,
    TK_FOR,
    TK_REP,
    TK_RETURN,
    TK_FN,
    TK_PRINT,
    TK_PLUS,
    TK_MINUS,
    TK_STAR,
    TK_SLASH,
    TK_INC,
    TK_DEC,
    TK_PLUS_ASSIGN,
    TK_MINUS_ASSIGN,
    TK_STAR_ASSIGN,
    TK_SLASH_ASSIGN,
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
    TK_LBRACKET,
    TK_RBRACKET,
    TK_SEMICOLON,
    TK_COMMA,
    TK_COLON,
    TK_ARROW,
    TK_AMP,
    TK_EOF,
    TK_INVALID
} TokenType;


typedef enum {
    EXPR_NUMBER,
    EXPR_VARIABLE,
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_CALL,
    EXPR_ARRAY,
    EXPR_INDEX,
    EXPR_CONSTRUCT,
} ExprType;

// A nonnegative type is the number of vector dimensions; -1 denotes void.
typedef int ValueType;
enum { VALUE_VOID = -1, VALUE_INT = 0, VALUE_ARRAY = 1 };


struct Expr{
    ExprType type;
    Token *token;
    ValueType value_type;

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

        struct {
            char *name;
            Expr **args;
            size_t count;
            Function *function;
            Expr *receiver;
        } call;

        struct {
            Expr **items;
            size_t count;
        } array;

        struct {
            ValueType type;
            Expr **sizes;
            size_t count;
        } construct;

        struct {
            Expr *array;
            Expr *index;
        } index;

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
            bool explicit_type;
            ValueType declared_type;
        } let;

        struct {
            Expr *expression;
        } print;

        struct {
            Expr *expression;
        } expr;

        struct {
            Expr *value;
        } return_stmt;

        struct {
            Expr *condition;
            Statement *body;
        } while_stmt;

        struct {
            Statement *init;
            Expr *condition;
            Statement *step;
            Statement *body;
        } for_stmt;

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
            Expr *target;
            TokenType op;
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
    size_t offset;
    size_t length;
};

typedef struct {
    char *name;
    Token *token;
    ValueType type;
    bool by_ref;
    int offset;
} Parameter;

struct Function {
    char *name;
    Token *token;
    Parameter *params;
    size_t count;
    ValueType return_type;
    Statement *body;
    size_t id;
};

typedef struct {
    Token *tokens;
    size_t count;
    size_t capacity;
} TokenArray;

typedef struct {
    char *name;
    int offset;
    ValueType value_type;
} Symbol;

typedef struct {
    Symbol *symbols;
    size_t count;
    size_t capacity;
    size_t scope_start;
    Program *program;
    Function *function;
    size_t loop_depth;
} SymbolTable;

#endif
