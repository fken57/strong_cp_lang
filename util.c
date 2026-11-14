#ifndef CALC_UTIL_C
#define CALC_UTIL_C
#include "typedef.c"
#include <stdarg.h>

_Noreturn void error_at(const Token *token, const char *format, ...) {
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

const char *token_type_name(TokenType type) {
    switch (type) {
        case TK_NUMBER: return "TK_NUMBER";
        case TK_IDENT:  return "TK_IDENT";
        case TK_LET:    return "TK_LET";
        case TK_IF:     return "TK_IF";
        case TK_ELSE:   return "TK_ELSE";
        case TK_WHILE:  return "TK_WHILE";
        case TK_PRINT:  return "TK_PRINT";
        case TK_PLUS:   return "TK_PLUS";
        case TK_MINUS:  return "TK_MINUS";
        case TK_STAR:   return "TK_STAR";
        case TK_SLASH:  return "TK_SLASH";
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
        case TK_SEMICOLON: return "TK_SEMICOLON";
        case TK_COMMA:  return "TK_COMMA";
        case TK_EOF:    return "TK_EOF";
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
