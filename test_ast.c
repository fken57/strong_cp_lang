
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>

// parser.c が typedef.c / util.c を読み込む前提
#include "parser.c"

#define T(kind, str) ((Token){.type = (kind), .text = (str)})
#define BUF_SIZE 4096

static int total = 0;
static int passed = 0;

static const char *op_name(TokenType op) {
    switch (op) {
        case TK_PLUS: return "+";
        case TK_MINUS: return "-";
        case TK_STAR: return "*";
        case TK_SLASH: return "/";
        case TK_EQ: return "==";
        case TK_NE: return "!=";
        case TK_LT: return "<";
        case TK_LE: return "<=";
        case TK_GT: return ">";
        case TK_GE: return ">=";
        default: return "?";
    }
}

static void append(char *buf, size_t *used, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf + *used, BUF_SIZE - *used, fmt, args);
    va_end(args);

    if (n < 0 || (size_t)n >= BUF_SIZE - *used) {
        fprintf(stderr, "AST dump overflow\n");
        exit(2);
    }
    *used += (size_t)n;
}

static void dump_expr(Expr *e, char *buf, size_t *used) {
    if (!e) {
        append(buf, used, "NULL");
        return;
    }

    switch (e->type) {
        case EXPR_NUMBER:
            append(buf, used, "N(%lld)",
                   (long long)e->data.number.number);
            break;

        case EXPR_VARIABLE:
            append(buf, used, "V(%s)",
                   e->data.variable.var_name);
            break;

        case EXPR_UNARY:
            append(buf, used, "(%s ",
                   e->data.unary.op == TK_MINUS
                   ? "neg" : "unknown-unary");
            dump_expr(e->data.unary.operand, buf, used);
            append(buf, used, ")");
            break;

        case EXPR_BINARY:
            append(buf, used, "(%s ",
                   op_name(e->data.binary.op));
            dump_expr(e->data.binary.left, buf, used);
            append(buf, used, " ");
            dump_expr(e->data.binary.right, buf, used);
            append(buf, used, ")");
            break;

        default:
            append(buf, used, "UNKNOWN");
    }
}

static void free_expr(Expr *e) {
    if (!e) return;

    switch (e->type) {
        case EXPR_BINARY:
            free_expr(e->data.binary.left);
            free_expr(e->data.binary.right);
            break;
        case EXPR_UNARY:
            free_expr(e->data.unary.operand);
            break;
        default:
            break;
    }
    free(e);
}

static void report(const char *name,
                   const char *actual,
                   const char *expected) {
    total++;

    if (strcmp(actual, expected) == 0) {
        passed++;
        printf("[AC] %s\n", name);
    } else {
        printf("[WA] %s\n", name);
        printf("  Expected: %s\n", expected);
        printf("  Actual:   %s\n", actual);
    }
}

static void check_expr(const char *name,
                       Token *items,
                       int count,
                       const char *expected) {
    TokenArray tokens = {
        .tokens = items,
        .count = count,
        .capacity = count
    };

    Expr *root = parse_equality(&tokens, 0, count);

    char actual[BUF_SIZE] = {0};
    size_t used = 0;
    dump_expr(root, actual, &used);

    report(name, actual, expected);
    free_expr(root);
}

#define TEST(name, expected, ...) do {                    \
    Token items[] = {__VA_ARGS__};                        \
    check_expr(name, items,                             \
        (int)(sizeof(items) / sizeof(items[0])),          \
        expected);                                        \
} while (0)

/* 文の種類・順序・入れ子も文字列にして比較する。 */
static void dump_statement(Statement *s, char *buf, size_t *used) {
    if (!s) {
        append(buf, used, "NULL");
        return;
    }
    switch (s->type) {
        case STMT_LET:
            append(buf, used, "(let %s ", s->data.let.name);
            dump_expr(s->data.let.value, buf, used);
            break;
        case STMT_ASSIGN:
            append(buf, used, "(assign %s ", s->data.assign.name);
            dump_expr(s->data.assign.value, buf, used);
            break;
        case STMT_PRINT:
            append(buf, used, "(print ");
            dump_expr(s->data.print.expression, buf, used);
            break;
        case STMT_BLOCK:
            append(buf, used, "(block");
            for (size_t i = 0; i < s->data.block.count; i++) {
                append(buf, used, " ");
                dump_statement(s->data.block.statements[i], buf, used);
            }
            break;
        case STMT_IF:
            append(buf, used, "(if ");
            dump_expr(s->data.if_stmt.condition, buf, used);
            append(buf, used, " ");
            dump_statement(s->data.if_stmt.then_branch, buf, used);
            append(buf, used, " ");
            dump_statement(s->data.if_stmt.else_branch, buf, used);
            break;
        case STMT_WHILE:
            append(buf, used, "(while ");
            dump_expr(s->data.while_stmt.condition, buf, used);
            append(buf, used, " ");
            dump_statement(s->data.while_stmt.body, buf, used);
            break;
        default:
            append(buf, used, "UNKNOWN_STATEMENT");
            return;
    }
    append(buf, used, ")");
}

static void free_statement(Statement *s) {
    if (!s) return;
    switch (s->type) {
        case STMT_LET:
            free(s->data.let.name);
            free_expr(s->data.let.value);
            break;
        case STMT_ASSIGN:
            free(s->data.assign.name);
            free_expr(s->data.assign.target);
            free_expr(s->data.assign.value);
            break;
        case STMT_PRINT:
            free_expr(s->data.print.expression);
            break;
        case STMT_BLOCK:
            for (size_t i = 0; i < s->data.block.count; i++)
                free_statement(s->data.block.statements[i]);
            free(s->data.block.statements);
            break;
        case STMT_IF:
            free_expr(s->data.if_stmt.condition);
            free_statement(s->data.if_stmt.then_branch);
            free_statement(s->data.if_stmt.else_branch);
            break;
        case STMT_WHILE:
            free_expr(s->data.while_stmt.condition);
            free_statement(s->data.while_stmt.body);
            break;
        default:
            break;
    }
    free(s);
}

static void free_program(Program *p) {
    for (size_t i = 0; i < p->count; i++) free_statement(p->statesments[i]);
    free(p->statesments);
    free(p);
}

static void check_program(const char *name, Token *items, size_t count,
                          const char *expected) {
    TokenArray tokens = {items, count, count};
    Program *p = parse(&tokens);
    char actual[BUF_SIZE] = {0};
    size_t used = 0;
    append(actual, &used, "(program");
    for (size_t i = 0; i < p->count; i++) {
        append(actual, &used, " ");
        dump_statement(p->statesments[i], actual, &used);
    }
    append(actual, &used, ")");
    report(name, actual, expected);
    free_program(p);
}

/* parser が exit(1) しても次のテストを実行する。クラッシュは成功扱いしない。 */
static void check_error(const char *name, Token *items, size_t count) {
    fflush(NULL);
    pid_t child = fork();
    if (child == -1) {
        perror("fork");
        exit(2);
    }
    if (child == 0) {
        alarm(3);
        if (!freopen("/dev/null", "w", stderr)) _exit(2);
        TokenArray tokens = {items, count, count};
        Program *p = parse(&tokens);
        free_program(p);
        _exit(0);
    }
    int status;
    pid_t result;
    do {
        result = waitpid(child, &status, 0);
    } while (result == -1 && errno == EINTR);
    if (result == -1) {
        perror("waitpid");
        exit(2);
    }
    char actual[64];
    if (WIFEXITED(status))
        snprintf(actual, sizeof(actual), "exit(%d)", WEXITSTATUS(status));
    else
        snprintf(actual, sizeof(actual), "signal(%d)", WTERMSIG(status));
    report(name, actual, "exit(1)");
}

/* EOF は各テストの末尾に自動で付ける。 */
#define PROGRAM(name, expected, ...) do {                 \
    Token items[] = {__VA_ARGS__, T(TK_EOF, "EOF")};       \
    check_program(name, items, sizeof(items)/sizeof(*items), expected); \
} while (0)
#define ERROR(name, ...) do {                            \
    Token items[] = {__VA_ARGS__, T(TK_EOF, "EOF")};       \
    check_error(name, items, sizeof(items)/sizeof(*items)); \
} while (0)

static void test_statements(void) {
    Token eof = T(TK_EOF, "EOF");
    check_program("empty program", &eof, 1, "(program)");
    PROGRAM("let a = 42;", "(program (let a N(42)))",
        T(TK_LET,"let"), T(TK_IDENT,"a"), T(TK_ASSIGN,"="), T(TK_NUMBER,"42"), T(TK_SEMICOLON,";"));
    PROGRAM("a = a + 1;", "(program (assign a (+ V(a) N(1))))",
        T(TK_IDENT,"a"), T(TK_ASSIGN,"="), T(TK_IDENT,"a"), T(TK_PLUS,"+"), T(TK_NUMBER,"1"), T(TK_SEMICOLON,";"));
    PROGRAM("print(2 * 3);", "(program (print (* N(2) N(3))))",
        T(TK_PRINT,"print"), T(TK_LPAREN,"("), T(TK_NUMBER,"2"), T(TK_STAR,"*"), T(TK_NUMBER,"3"), T(TK_RPAREN,")"), T(TK_SEMICOLON,";"));
    PROGRAM("empty block", "(program (block))", T(TK_LBRACE,"{"), T(TK_RBRACE,"}"));
    PROGRAM("nested blocks", "(program (block (block) (block)))",
        T(TK_LBRACE,"{"), T(TK_LBRACE,"{"), T(TK_RBRACE,"}"), T(TK_LBRACE,"{"), T(TK_RBRACE,"}"), T(TK_RBRACE,"}"));
    PROGRAM("block statements and following statement",
        "(program (block (let a N(1)) (assign a N(2))) (print V(a)))",
        T(TK_LBRACE,"{"), T(TK_LET,"let"), T(TK_IDENT,"a"), T(TK_ASSIGN,"="), T(TK_NUMBER,"1"), T(TK_SEMICOLON,";"),
        T(TK_IDENT,"a"), T(TK_ASSIGN,"="), T(TK_NUMBER,"2"), T(TK_SEMICOLON,";"), T(TK_RBRACE,"}"),
        T(TK_PRINT,"print"), T(TK_LPAREN,"("), T(TK_IDENT,"a"), T(TK_RPAREN,")"), T(TK_SEMICOLON,";"));
    PROGRAM("if without else followed by print", "(program (if N(1) (block) NULL) (print N(2)))",
        T(TK_IF,"if"), T(TK_LPAREN,"("), T(TK_NUMBER,"1"), T(TK_RPAREN,")"), T(TK_LBRACE,"{"), T(TK_RBRACE,"}"),
        T(TK_PRINT,"print"), T(TK_LPAREN,"("), T(TK_NUMBER,"2"), T(TK_RPAREN,")"), T(TK_SEMICOLON,";"));
    PROGRAM("if with else", "(program (if N(1) (block (print N(2))) (block (print N(3)))))",
        T(TK_IF,"if"), T(TK_LPAREN,"("), T(TK_NUMBER,"1"), T(TK_RPAREN,")"), T(TK_LBRACE,"{"),
        T(TK_PRINT,"print"), T(TK_LPAREN,"("), T(TK_NUMBER,"2"), T(TK_RPAREN,")"), T(TK_SEMICOLON,";"), T(TK_RBRACE,"}"),
        T(TK_ELSE,"else"), T(TK_LBRACE,"{"), T(TK_PRINT,"print"), T(TK_LPAREN,"("), T(TK_NUMBER,"3"), T(TK_RPAREN,")"), T(TK_SEMICOLON,";"), T(TK_RBRACE,"}"));
    PROGRAM("while with nested condition and following block",
        "(program (while (< V(a) (+ N(2) N(3))) (block (assign a (+ V(a) N(1))))) (block))",
        T(TK_WHILE,"while"), T(TK_LPAREN,"("), T(TK_IDENT,"a"), T(TK_LT,"<"), T(TK_LPAREN,"("), T(TK_NUMBER,"2"), T(TK_PLUS,"+"), T(TK_NUMBER,"3"), T(TK_RPAREN,")"), T(TK_RPAREN,")"),
        T(TK_LBRACE,"{"), T(TK_IDENT,"a"), T(TK_ASSIGN,"="), T(TK_IDENT,"a"), T(TK_PLUS,"+"), T(TK_NUMBER,"1"), T(TK_SEMICOLON,";"), T(TK_RBRACE,"}"), T(TK_LBRACE,"{"), T(TK_RBRACE,"}"));
    PROGRAM("if inside while: else belongs to inner if",
        "(program (while N(1) (block (if N(0) (block) (block)))))",
        T(TK_WHILE,"while"), T(TK_LPAREN,"("), T(TK_NUMBER,"1"), T(TK_RPAREN,")"), T(TK_LBRACE,"{"),
        T(TK_IF,"if"), T(TK_LPAREN,"("), T(TK_NUMBER,"0"), T(TK_RPAREN,")"), T(TK_LBRACE,"{"), T(TK_RBRACE,"}"), T(TK_ELSE,"else"), T(TK_LBRACE,"{"), T(TK_RBRACE,"}"), T(TK_RBRACE,"}"));
}

static void test_errors(void) {
    ERROR("let: missing identifier", T(TK_LET,"let"), T(TK_NUMBER,"1"), T(TK_ASSIGN,"="), T(TK_NUMBER,"2"), T(TK_SEMICOLON,";"));
    ERROR("let: == instead of =", T(TK_LET,"let"), T(TK_IDENT,"a"), T(TK_EQ,"=="), T(TK_NUMBER,"1"), T(TK_SEMICOLON,";"));
    ERROR("assignment: == instead of =", T(TK_IDENT,"a"), T(TK_EQ,"=="), T(TK_NUMBER,"1"), T(TK_SEMICOLON,";"));
    ERROR("let: empty value", T(TK_LET,"let"), T(TK_IDENT,"a"), T(TK_ASSIGN,"="), T(TK_SEMICOLON,";"));
    ERROR("assignment: empty value", T(TK_IDENT,"a"), T(TK_ASSIGN,"="), T(TK_SEMICOLON,";"));
    ERROR("print: empty expression", T(TK_PRINT,"print"), T(TK_LPAREN,"("), T(TK_RPAREN,")"), T(TK_SEMICOLON,";"));
    ERROR("let: missing semicolon at EOF", T(TK_LET,"let"), T(TK_IDENT,"a"), T(TK_ASSIGN,"="), T(TK_NUMBER,"1"));
    ERROR("assignment: missing semicolon at EOF", T(TK_IDENT,"a"), T(TK_ASSIGN,"="), T(TK_NUMBER,"1"));
    ERROR("print: missing semicolon at EOF", T(TK_PRINT,"print"), T(TK_LPAREN,"("), T(TK_NUMBER,"1"), T(TK_RPAREN,")"));
    ERROR("let: missing semicolon before }", T(TK_LBRACE,"{"), T(TK_LET,"let"), T(TK_IDENT,"a"), T(TK_ASSIGN,"="), T(TK_NUMBER,"1"), T(TK_RBRACE,"}"));
    ERROR("assignment: missing semicolon before }", T(TK_LBRACE,"{"), T(TK_IDENT,"a"), T(TK_ASSIGN,"="), T(TK_NUMBER,"1"), T(TK_RBRACE,"}"));
    ERROR("print: missing semicolon before }", T(TK_LBRACE,"{"), T(TK_PRINT,"print"), T(TK_LPAREN,"("), T(TK_NUMBER,"1"), T(TK_RPAREN,")"), T(TK_RBRACE,"}"));
    ERROR("block: missing }", T(TK_LBRACE,"{"));
    ERROR("block: extra }", T(TK_RBRACE,"}"));
    ERROR("if: missing (", T(TK_IF,"if"), T(TK_NUMBER,"1"), T(TK_LBRACE,"{"), T(TK_RBRACE,"}"));
    ERROR("while: missing (", T(TK_WHILE,"while"), T(TK_NUMBER,"1"), T(TK_LBRACE,"{"), T(TK_RBRACE,"}"));
    ERROR("if: missing )", T(TK_IF,"if"), T(TK_LPAREN,"("), T(TK_NUMBER,"1"), T(TK_LBRACE,"{"), T(TK_RBRACE,"}"));
    ERROR("while: missing )", T(TK_WHILE,"while"), T(TK_LPAREN,"("), T(TK_NUMBER,"1"), T(TK_LBRACE,"{"), T(TK_RBRACE,"}"));
    ERROR("if: empty condition", T(TK_IF,"if"), T(TK_LPAREN,"("), T(TK_RPAREN,")"), T(TK_LBRACE,"{"), T(TK_RBRACE,"}"));
    ERROR("while: empty condition", T(TK_WHILE,"while"), T(TK_LPAREN,"("), T(TK_RPAREN,")"), T(TK_LBRACE,"{"), T(TK_RBRACE,"}"));
    ERROR("if: missing block", T(TK_IF,"if"), T(TK_LPAREN,"("), T(TK_NUMBER,"1"), T(TK_RPAREN,")"));
    ERROR("else: missing block", T(TK_IF,"if"), T(TK_LPAREN,"("), T(TK_NUMBER,"1"), T(TK_RPAREN,")"), T(TK_LBRACE,"{"), T(TK_RBRACE,"}"), T(TK_ELSE,"else"));
    ERROR("if: missing right operand", T(TK_IF,"if"), T(TK_LPAREN,"("), T(TK_NUMBER,"1"), T(TK_PLUS,"+"), T(TK_RPAREN,")"), T(TK_LBRACE,"{"), T(TK_RBRACE,"}"));
    ERROR("let: missing left operand", T(TK_LET,"let"), T(TK_IDENT,"a"), T(TK_ASSIGN,"="), T(TK_STAR,"*"), T(TK_NUMBER,"1"), T(TK_SEMICOLON,";"));
    ERROR("print: missing unary operand", T(TK_PRINT,"print"), T(TK_LPAREN,"("), T(TK_MINUS,"-"), T(TK_RPAREN,")"), T(TK_SEMICOLON,";"));
}

int main(void) {
    // 1. 基本要素
    TEST("number", "N(42)",
        T(TK_NUMBER, "42"));

    TEST("variable", "V(a)",
        T(TK_IDENT, "a"));

    // 2. 四則演算と優先順位
    TEST("addition", "(+ N(1) N(2))",
        T(TK_NUMBER, "1"),
        T(TK_PLUS, "+"),
        T(TK_NUMBER, "2"));

    TEST("precedence", "(+ N(1) (* N(2) N(3)))",
        T(TK_NUMBER, "1"),
        T(TK_PLUS, "+"),
        T(TK_NUMBER, "2"),
        T(TK_STAR, "*"),
        T(TK_NUMBER, "3"));

    TEST("parentheses", "(* (+ N(1) N(2)) N(3))",
        T(TK_LPAREN, "("),
        T(TK_NUMBER, "1"),
        T(TK_PLUS, "+"),
        T(TK_NUMBER, "2"),
        T(TK_RPAREN, ")"),
        T(TK_STAR, "*"),
        T(TK_NUMBER, "3"));

    // 3. 左結合
    TEST("left associativity -",
        "(- (- N(10) N(3)) N(2))",
        T(TK_NUMBER, "10"),
        T(TK_MINUS, "-"),
        T(TK_NUMBER, "3"),
        T(TK_MINUS, "-"),
        T(TK_NUMBER, "2"));

    TEST("left associativity /",
        "(/ (/ N(8) N(2)) N(2))",
        T(TK_NUMBER, "8"),
        T(TK_SLASH, "/"),
        T(TK_NUMBER, "2"),
        T(TK_SLASH, "/"),
        T(TK_NUMBER, "2"));

    // 4. 単項マイナス
    TEST("unary minus", "(neg N(3))",
        T(TK_MINUS, "-"),
        T(TK_NUMBER, "3"));

    TEST("multiply negative",
        "(* N(2) (neg N(3)))",
        T(TK_NUMBER, "2"),
        T(TK_STAR, "*"),
        T(TK_MINUS, "-"),
        T(TK_NUMBER, "3"));

    TEST("double minus",
        "(- N(1) (neg N(2)))",
        T(TK_NUMBER, "1"),
        T(TK_MINUS, "-"),
        T(TK_MINUS, "-"),
        T(TK_NUMBER, "2"));

    // 5. 比較演算
    TEST("comparison",
        "(< V(a) (+ V(b) N(1)))",
        T(TK_IDENT, "a"),
        T(TK_LT, "<"),
        T(TK_IDENT, "b"),
        T(TK_PLUS, "+"),
        T(TK_NUMBER, "1"));

    TEST("equality",
        "(== (>= V(a) N(2)) V(b))",
        T(TK_IDENT, "a"),
        T(TK_GE, ">="),
        T(TK_NUMBER, "2"),
        T(TK_EQ, "=="),
        T(TK_IDENT, "b"));

    TEST("not equal", "(!= V(a) V(b))",
        T(TK_IDENT, "a"),
        T(TK_NE, "!="),
        T(TK_IDENT, "b"));

    // 6. 入れ子構造
    TEST("nested unary",
        "(neg (+ N(1) N(2)))",
        T(TK_MINUS, "-"),
        T(TK_LPAREN, "("),
        T(TK_NUMBER, "1"),
        T(TK_PLUS, "+"),
        T(TK_NUMBER, "2"),
        T(TK_RPAREN, ")"));

    TEST("nested parentheses", "N(5)",
        T(TK_LPAREN, "("),
        T(TK_LPAREN, "("),
        T(TK_NUMBER, "5"),
        T(TK_RPAREN, ")"),
        T(TK_RPAREN, ")"));

    TEST("consecutive unary minus", "(neg (neg N(3)))",
        T(TK_MINUS, "-"), T(TK_MINUS, "-"), T(TK_NUMBER, "3"));

    // 7. Statement全体
    test_statements();
    test_errors();

    printf("\nResult: %d/%d AC\n", passed, total);
    return passed == total ? 0 : 1;
}

