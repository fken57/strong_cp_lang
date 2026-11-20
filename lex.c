#include "util.c"
#include <ctype.h>

// The language service also needs tokens while a document is incomplete.
static bool lex_allow_invalid = false;

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
        Token token = {.line = line, .column = column, .offset = i, .length = 1};
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
                case '+': case '-': case '*': case '/':
                    token.type = c == '+' ? TK_PLUS : c == '-' ? TK_MINUS :
                                 c == '*' ? TK_STAR : TK_SLASH;
                    if (c == '-' && i < source->length && source->data[i] == '>') {
                        token.type = TK_ARROW;
                        i++; column++;
                    } else if (i < source->length && source->data[i] == '=') {
                        token.type = c == '+' ? TK_PLUS_ASSIGN : c == '-' ? TK_MINUS_ASSIGN :
                                     c == '*' ? TK_STAR_ASSIGN : TK_SLASH_ASSIGN;
                        i++; column++;
                    } else if (i < source->length && source->data[i] == c &&
                               (c == '+' || c == '-')) {
                        token.type = c == '+' ? TK_INC : TK_DEC;
                        i++; column++;
                    }
                    break;
                case '\'':
                    if (i < source->length && source->data[i] == '\\') {
                        if (++i == source->length || (source->data[i] != 'n' && source->data[i] != 't'))
                            error_at(&token, "expected a space, newline or tab separator");
                    } else if (i == source->length || source->data[i] != ' ') {
                        error_at(&token, "expected a space, newline or tab separator");
                    }
                    if (++i == source->length || source->data[i++] != '\'') error_at(&token, "expected closing quote");
                    column += i - start - 1;
                    token.type = TK_CHAR;
                    break;
                case '(': token.type = TK_LPAREN; break;
                case ')': token.type = TK_RPAREN; break;
                case '{': token.type = TK_LBRACE; break;
                case '}': token.type = TK_RBRACE; break;
                case '[': token.type = TK_LBRACKET; break;
                case ']': token.type = TK_RBRACKET; break;
                case ';': token.type = TK_SEMICOLON; break;
                case ',': token.type = TK_COMMA; break;
                case '.': token.type = TK_DOT; break;
                case ':': token.type = TK_COLON; break;
                case '&': token.type = TK_AMP; break;
                case '=': case '!': case '<': case '>':
                    if (i < source->length && source->data[i] == '=') {
                        token.type = c == '=' ? TK_EQ : c == '!' ? TK_NE :
                                     c == '<' ? TK_LE : TK_GE;
                        i++; column++;
                    } else if ((c == '<' || c == '>') && i < source->length && source->data[i] == c) {
                        token.type = c == '<' ? TK_INSERT : TK_EXTRACT;
                        ++i; ++column;
                    } else if (c == '=') token.type = TK_ASSIGN;
                    else if (c == '<') token.type = TK_LT;
                    else if (c == '>') token.type = TK_GT;
                    else if (lex_allow_invalid) token.type = TK_INVALID;
                    else error_at(&token, "unexpected character '!'");
                    break;
                default:
                    if (lex_allow_invalid) token.type = TK_INVALID;
                    else error_at(&token, "unexpected character (byte 0x%02x)", c);
            }
        }
        token.text = copy_text(source->data + start, i - start);
        token.offset = start;
        token.length = i - start;
        if (token.type == TK_IDENT) {
            if (strcmp(token.text, "let") == 0) token.type = TK_LET;
            else if (strcmp(token.text, "int") == 0) token.type = TK_INT;
            else if (strcmp(token.text, "break") == 0) token.type = TK_BREAK;
            else if (strcmp(token.text, "if") == 0) token.type = TK_IF;
            else if (strcmp(token.text, "else") == 0) token.type = TK_ELSE;
            else if (strcmp(token.text, "while") == 0) token.type = TK_WHILE;
            else if (strcmp(token.text, "for") == 0) token.type = TK_FOR;
            else if (strcmp(token.text, "rep") == 0) token.type = TK_REP;
            else if (strcmp(token.text, "fn") == 0) token.type = TK_FN;
            else if (strcmp(token.text, "return") == 0) token.type = TK_RETURN;
            else if (strcmp(token.text, "print") == 0) token.type = TK_PRINT;
        }
        tokens->tokens[tokens->count++] = token;
    }
    tokens->tokens[tokens->count++] = (Token){
        .type = TK_EOF, .text = copy_text("EOF", 3), .line = line, .column = column,
        .offset = source->length
    };
    return tokens;
}


