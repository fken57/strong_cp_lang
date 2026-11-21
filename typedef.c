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
typedef struct {
    char *name;
    int type;
} Field;
typedef struct {
    char *name;
    Field *fields;
    size_t count;
} Record;

typedef struct { int kind, key, value; } AssocType;

typedef struct
{
    Statement **statesments;
    size_t count;
    size_t capacity;
    int offset;
    int required_size;
    Function **functions;
    size_t function_count;
    Record *records;
    size_t record_count;
    const char **moduli;
    size_t modulus_count;
    AssocType *assocs;
    size_t assoc_count;
}Program;

typedef enum {
    STMT_DECLARATIONS,
    STMT_WRITE,
    STMT_EACH,
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
    TK_INSERT, TK_EXTRACT, TK_CHAR, TK_STRING,
    TK_NUMBER,
    TK_IDENT,
    TK_LET,
    TK_AUTO,
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
    TK_PERCENT,
    TK_INC,
    TK_DEC,
    TK_PLUS_ASSIGN,
    TK_MINUS_ASSIGN,
    TK_STAR_ASSIGN,
    TK_SLASH_ASSIGN,
    TK_PERCENT_ASSIGN,
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
    EXPR_TEXT,
    EXPR_RESIDUE,
    EXPR_VARIABLE,
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_CALL,
    EXPR_ARRAY,
    EXPR_INDEX,
    EXPR_CONSTRUCT,
    EXPR_FIELD,
    EXPR_RECORD,
} ExprType;

// A nonnegative type is the number of vector dimensions; -1 denotes void.
typedef int ValueType;
enum { VALUE_VOID = -1, VALUE_INT = 0, VALUE_ARRAY = 1, TYPE_STRIDE = 65536,
       VALUE_EDGE = TYPE_STRIDE, VALUE_GRAPH = 2 * TYPE_STRIDE,
       VALUE_DIGRAPH = 3 * TYPE_STRIDE, VALUE_PAIR = 4 * TYPE_STRIDE, VALUE_DIAMETER = 5 * TYPE_STRIDE, VALUE_CHAR = 6 * TYPE_STRIDE, VALUE_STRING = VALUE_CHAR + 1, VALUE_RECORD = 7 * TYPE_STRIDE, VALUE_MINT = 16384 * TYPE_STRIDE, VALUE_ASSOC = 24576 * TYPE_STRIDE };
static inline int type_rank(ValueType t) { return t < 0 ? 0 : t % TYPE_STRIDE; }
static inline int type_base(ValueType t) { return t - type_rank(t); }
static inline bool mint_type(ValueType t) { return t >= VALUE_MINT && t < VALUE_ASSOC && !type_rank(t); }
static inline bool assoc_type(ValueType t) { return t >= VALUE_ASSOC && !type_rank(t); }
static inline AssocType assoc_info(Program *p, ValueType t) { return p->assocs[(t - VALUE_ASSOC)/TYPE_STRIDE]; }
static inline bool graph_type(ValueType t) { return t == VALUE_GRAPH || t == VALUE_DIGRAPH; }


struct Expr{
    ExprType type;
    Token *token;
    ValueType value_type;

    union {
        char *text;
        struct { ValueType type; Expr *value; } residue;
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

        struct { ValueType type; size_t *indices; Expr **values; size_t count; } record;
        struct { Expr *owner; char *name; size_t index; } field;

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
            size_t binding_count;
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

        struct { Expr *value; } write;
        struct { ValueType type; char *name; int offset; Expr *values; Statement *body; } each;
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
            bool stream_input;
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
    ValueType owner;
    size_t id;
};

typedef struct {
    Token *tokens;
    size_t count;
    size_t capacity;
    Program *program;
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
