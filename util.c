#ifndef CALC_UTIL_C
#define CALC_UTIL_C
#include "typedef.c"
#include <stdarg.h>

static bool diagnostic_json = false;

void json_string(FILE *out, const char *text) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p == '"' || *p == '\\') fprintf(out, "\\%c", *p);
        else if (*p < 32 || *p >= 127) fprintf(out, "\\u%04x", *p);
        else fputc(*p, out);
    }
    fputc('"', out);
}

_Noreturn void error_at(const Token *token, const char *format, ...) {
    if (diagnostic_json) {
        va_list args, copy;
        va_start(args, format);
        va_copy(copy, args);
        int size = vsnprintf(NULL, 0, format, copy);
        va_end(copy);
        char *message = size < 0 ? NULL : malloc((size_t)size + 1);
        if (!message) exit(1);
        vsnprintf(message, (size_t)size + 1, format, args);
        va_end(args);
        fprintf(stderr, "{\"line\":%zu,\"column\":%zu,\"offset\":%zu,\"length\":%zu,\"message\":",
                token && token->line ? token->line : 1, token && token->column ? token->column : 1,
                token ? token->offset : 0, token ? token->length : 0);
        json_string(stderr, message);
        fputs("}\n", stderr);
        free(message);
        exit(1);
    }
    if (token && token->line) {
        fprintf(stderr, "%zu:%zu: error: ", token->line, token->column);
    } else {
        fputs("error: ", stderr);
    }
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    exit(1);
}

static const Expr *lvalue_root(const Expr *expr) {
    while (expr && (expr->type == EXPR_INDEX || expr->type == EXPR_FIELD))
        expr = expr->type == EXPR_FIELD ? expr->data.field.owner : expr->data.index.array;
    return expr && expr->type == EXPR_VARIABLE ? expr : NULL;
}

static int assoc_kind(const char *name) {
    static const char *names[] = {"map", "unordered_map", "multi_map", "unordered_multi_map",
                                  "set", "unordered_set", "multi_set", "unordered_multi_set"};
    for (int i = 0; i < 8; ++i) if (!strcmp(name, names[i])) return i;
    if (!strcmp(name, "multimap")) return 2;
    if (!strcmp(name, "unordered_multimap")) return 3;
    if (!strcmp(name, "multiset")) return 6;
    if (!strcmp(name, "unordered_multiset")) return 7;
    return -1;
}

const char *token_type_name(TokenType type) {
    switch (type) {
        case TK_INSERT: return "TK_INSERT";
        case TK_EXTRACT: return "TK_EXTRACT";
        case TK_STRING: return "TK_STRING";
        case TK_CHAR: return "TK_CHAR";
        case TK_NUMBER: return "TK_NUMBER";
        case TK_IDENT:  return "TK_IDENT";
        case TK_INT: return "TK_INT";
        case TK_DOT: return "TK_DOT";
        case TK_BREAK: return "TK_BREAK";
        case TK_AUTO: return "TK_AUTO";
        case TK_LET:    return "TK_LET";
        case TK_IF:     return "TK_IF";
        case TK_ELSE:   return "TK_ELSE";
        case TK_WHILE:  return "TK_WHILE";
        case TK_FOR:    return "TK_FOR";
        case TK_REP:    return "TK_REP";
        case TK_FN: return "TK_FN";
        case TK_RETURN: return "TK_RETURN";
        case TK_PRINT:  return "TK_PRINT";
        case TK_PLUS:   return "TK_PLUS";
        case TK_MINUS:  return "TK_MINUS";
        case TK_STAR:   return "TK_STAR";
        case TK_PERCENT: return "TK_PERCENT";
        case TK_PERCENT_ASSIGN: return "TK_PERCENT_ASSIGN";
        case TK_SLASH:  return "TK_SLASH";
        case TK_INC: return "TK_INC";
        case TK_DEC: return "TK_DEC";
        case TK_PLUS_ASSIGN: return "TK_PLUS_ASSIGN";
        case TK_MINUS_ASSIGN: return "TK_MINUS_ASSIGN";
        case TK_STAR_ASSIGN: return "TK_STAR_ASSIGN";
        case TK_SLASH_ASSIGN: return "TK_SLASH_ASSIGN";
        case TK_ASSIGN: return "TK_ASSIGN";
        case TK_EQ:     return "TK_EQ";
        case TK_NE:     return "TK_NE";
        case TK_LT:     return "TK_LT";
        case TK_LE:     return "TK_LE";
        case TK_GT:     return "TK_GT";
        case TK_GE:     return "TK_GE";
        case TK_LPAREN: return "TK_LPAREN";
        case TK_RPAREN: return "TK_RPAREN";
        case TK_LBRACE: return "TK_LBRACE";
        case TK_RBRACE: return "TK_RBRACE";
        case TK_LBRACKET: return "TK_LBRACKET";
        case TK_RBRACKET: return "TK_RBRACKET";
        case TK_SEMICOLON: return "TK_SEMICOLON";
        case TK_COMMA:  return "TK_COMMA";
        case TK_COLON: return "TK_COLON";
        case TK_ARROW: return "TK_ARROW";
        case TK_AMP: return "TK_AMP";
        case TK_EOF:    return "TK_EOF";
        case TK_INVALID: return "TK_INVALID";
        default:        return "OTHER";
    }
}

char *copy_text(const char *src, size_t len) {
    char *s = malloc(len + 1);
    if (s == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    memcpy(s, src, len);
    s[len] = '\0';
    return s;
}

#endif
