#include "util.c"
#include <ctype.h>

TokenArray *lex(Source *source) {
    TokenArray *tokens = malloc(sizeof(*tokens));
    if (!tokens) error_at(NULL, "out of memory");
    tokens->capacity = source->length + 1;
    tokens->tokens = calloc(tokens->capacity, sizeof(Token));
    if (!tokens->tokens) error_at(NULL, "out of memory");
    tokens->count = 0;
    size_t i = 0, line = 1, column = 1;
    while (i < source->length) {
        unsigned char c = (unsigned char)source->data[i];
        if (isspace(c)) {
            if (c == '\n') { line++; column = 1; }
            else column++;
            i++;
            continue;
        }
        if (c == '/' && i + 1 < source->length && source->data[i + 1] == '/') {
            while (i < source->length && source->data[i] != '\n') { i++; column++; }
            continue;
        }
        Token token = {.line = line, .column = column};
        size_t start = i;
        if (isdigit(c)) {
            do { i++; column++; }
            while (i < source->length && isdigit((unsigned char)source->data[i]));
            token.type = TK_NUMBER;
        } else if (isalpha(c) || c == '_') {
            do { i++; column++; }
            while (i < source->length &&
                   (isalnum((unsigned char)source->data[i]) || source->data[i] == '_'));
            token.type = TK_IDENT;
        } else {
            i++; column++;
            switch (c) {
                case '+': token.type = TK_PLUS; break;
                case '-': token.type = TK_MINUS; break;
                case '*': token.type = TK_STAR; break;
                case '/': token.type = TK_SLASH; break;
                case '(': token.type = TK_LPAREN; break;
                case ')': token.type = TK_RPAREN; break;
                case '{': token.type = TK_LBRACE; break;
                case '}': token.type = TK_RBRACE; break;
                case ';': token.type = TK_SEMICOLON; break;
                case ',': token.type = TK_COMMA; break;
                case '=': case '!': case '<': case '>':
                    if (i < source->length && source->data[i] == '=') {
                        token.type = c == '=' ? TK_EQ : c == '!' ? TK_NE :
                                     c == '<' ? TK_LE : TK_GE;
                        i++; column++;
                    } else if (c == '=') token.type = TK_ASSIGN;
                    else if (c == '<') token.type = TK_LT;
                    else if (c == '>') token.type = TK_GT;
                    else error_at(&token, "unexpected character '!'");
                    break;
                default: error_at(&token, "unexpected character (byte 0x%02x)", c);
            }
        }
        token.text = copy_text(source->data + start, i - start);
        if (token.type == TK_IDENT) {
            if (strcmp(token.text, "let") == 0) token.type = TK_LET;
            else if (strcmp(token.text, "if") == 0) token.type = TK_IF;
            else if (strcmp(token.text, "else") == 0) token.type = TK_ELSE;
            else if (strcmp(token.text, "while") == 0) token.type = TK_WHILE;
            else if (strcmp(token.text, "print") == 0) token.type = TK_PRINT;
        }
        tokens->tokens[tokens->count++] = token;
    }
    tokens->tokens[tokens->count++] = (Token){
        .type = TK_EOF, .text = copy_text("EOF", 3), .line = line, .column = column
    };
    return tokens;
}


