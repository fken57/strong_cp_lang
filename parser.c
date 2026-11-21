
#include "util.c"
#include <errno.h>

static _Noreturn void parse_error(TokenArray *tokens, int pos, const char *message) {
    size_t i = pos < 0 ? 0 : (size_t)pos;
    if (i >= tokens->count) i = tokens->count - 1;
    error_at(&tokens->tokens[i], "%s", message);
}


Expr* parse_primary(TokenArray* tokens, int begin,int end);
Expr* parse_unary(TokenArray* tokens, int begin, int end);
Expr* parse_multiplicative(TokenArray* tokens, int begin, int end);
Expr* parse_additive(TokenArray* tokens, int begin, int end);
Expr* parse_comparison(TokenArray* tokens, int begin, int end);
Expr* parse_equality(TokenArray* tokens, int begin, int end);

Statement* parse_assign_statement(TokenArray* tokens, int *pos);
Statement* parse_let_statement(TokenArray* tokens, int *pos);
Statement* parse_if_statement(TokenArray* tokens, int *pos);
Statement* parse_while_statement(TokenArray* tokens, int *pos);
Statement* parse_block(TokenArray* tokens, int *pos);
Statement* parse_print_statement(TokenArray* tokens, int *pos);
Program* parse(TokenArray* tokens);


static ValueType named_type(Program *program, const char *name);

/* Return the matching ')' or ']' within the current expression. */
static int matching_close(TokenArray *tokens, int open, int end) {
    TokenType left = tokens->tokens[open].type;
    TokenType right = left == TK_LPAREN ? TK_RPAREN : left == TK_LBRACE ? TK_RBRACE : TK_RBRACKET;
    int depth = 0;
    for (int i = open; i < end; i++) {
        if (tokens->tokens[i].type == left) depth++;
        if (tokens->tokens[i].type == right && --depth == 0) return i;
    }
    parse_error(tokens, open, "unclosed delimiter");
}

static Expr **parse_items(TokenArray *tokens, int begin, int end, size_t *count) {
    *count = 0;
    if (begin == end) return NULL;
    Expr **items = calloc((size_t)(end - begin), sizeof(*items));
    if (!items) error_at(NULL, "out of memory");
    int start = begin, depth = 0;
    for (int i = begin; i <= end; i++) {
        TokenType kind = i == end ? TK_COMMA : tokens->tokens[i].type;
        if (kind == TK_LPAREN || kind == TK_LBRACKET || kind == TK_LBRACE) depth++;
        if (kind == TK_RPAREN || kind == TK_RBRACKET || kind == TK_RBRACE) depth--;
        if (depth < 0) parse_error(tokens, i, "unexpected closing delimiter");
        if (i == end || (kind == TK_COMMA && depth == 0)) {
            Expr *item = parse_equality(tokens, start, i);
            if (!item) parse_error(tokens, start, "expected expression");
            items[(*count)++] = item;
            start = i + 1;
        }
    }
    return items;
}

Expr* parse_primary(TokenArray* tokens, int begin, int end) {
    if (begin >= end) return NULL;
    Token *token = &tokens->tokens[begin];
    Expr *expr = calloc(1, sizeof(*expr));
    if (!expr) error_at(NULL, "out of memory");
    expr->token = token;
    int pos = begin + 1;
    if (token->type == TK_NUMBER) {
        expr->type = EXPR_NUMBER;
        expr->data.number.literal = token->text;
        expr->data.number.number = strtoll(token->text, NULL, 10);
    } else if (token->type == TK_CHAR || token->type == TK_STRING) {
        expr->type = EXPR_TEXT;
        expr->data.text = malloc(token->length);
        if (!expr->data.text) error_at(NULL, "out of memory");
        char *out = expr->data.text;
        for (size_t i = 1; i + 1 < token->length; ++i) {
            char c = token->text[i];
            if (c == '\\') {
                c = token->text[++i];
                if (c == 'n') c = '\n';
                else if (c == 't') c = '\t';
                else if (c == 'r') c = '\r';
            }
            *out++ = c;
        }
        *out = '\0';
    } else if (token->type == TK_IDENT) {
        expr->type = EXPR_VARIABLE;
        expr->data.variable.var_name = token->text;
        if (pos < end && tokens->tokens[pos].type == TK_LBRACE) {
            ValueType type = named_type(tokens->program, token->text);
            if (type < VALUE_RECORD) parse_error(tokens, begin, "expected structure name");
            const Record *r = &tokens->program->records[(type-VALUE_RECORD)/TYPE_STRIDE];
            int close = matching_close(tokens, pos, end);
            expr->type = EXPR_RECORD;
            expr->data.record.type = type;
            expr->data.record.values = calloc(r->count, sizeof(Expr*));
            expr->data.record.indices = calloc(r->count, sizeof(size_t));
            if (r->count && (!expr->data.record.values || !expr->data.record.indices)) error_at(NULL, "out of memory");
            ++pos;
            while (pos < close) {
                Token *name = &tokens->tokens[pos++];
                size_t field = 0;
                while (field < r->count && strcmp(name->text, r->fields[field].name)) ++field;
                if (field == r->count) error_at(name, "unknown field '%s'", name->text);
                size_t n = expr->data.record.count;
                for (size_t i = 0; i < n; ++i)
                    if (expr->data.record.indices[i] == field) error_at(name, "duplicate initializer");
                if (tokens->tokens[pos++].type != TK_COLON) error_at(name, "expected ':' after field name");
                int start = pos;
                while (pos < close && tokens->tokens[pos].type != TK_COMMA) {
                    TokenType k = tokens->tokens[pos].type;
                    if (k == TK_LPAREN || k == TK_LBRACKET || k == TK_LBRACE) pos = matching_close(tokens, pos, close);
                    ++pos;
                }
                Expr *value = parse_equality(tokens, start, pos);
                if (!value) error_at(name, "expected field value");
                expr->data.record.indices[n] = field;
                expr->data.record.values[n] = value;
                expr->data.record.count++;
                if (pos < close) ++pos;
            }
            pos = close + 1;
        } else if (pos < end && tokens->tokens[pos].type == TK_LPAREN) {
            int close = matching_close(tokens, pos, end);
            expr->type = EXPR_CALL;
            expr->data.call.name = token->text;
            expr->data.call.args = parse_items(tokens, pos + 1, close, &expr->data.call.count);
            pos = close + 1;
        }
    } else if (token->type == TK_LBRACKET || token->type == TK_LPAREN) {
        int close = matching_close(tokens, begin, end);
        if (token->type == TK_LBRACKET) {
            expr->type = EXPR_ARRAY;
            expr->data.array.items = parse_items(tokens, begin + 1, close, &expr->data.array.count);
        } else {
            free(expr);
            expr = parse_equality(tokens, begin + 1, close);
            if (!expr) parse_error(tokens, begin, "expected expression");
        }
        pos = close + 1;
    } else { free(expr); return NULL; }
    while (pos < end) {
        Token *suffix = &tokens->tokens[pos];
        Expr *next = calloc(1, sizeof(*next));
        if (!next) error_at(NULL, "out of memory");
        next->token = suffix;
        if (suffix->type == TK_LBRACKET) {
            int close = matching_close(tokens, pos, end);
            next->type = EXPR_INDEX;
            next->data.index.array = expr;
            next->data.index.index = parse_equality(tokens, pos + 1, close);
            if (!next->data.index.index) parse_error(tokens, pos, "expected index");
            pos = close + 1;
        } else if (suffix->type == TK_DOT) {
            if (pos + 1 >= end || tokens->tokens[pos + 1].type != TK_IDENT)
                parse_error(tokens, pos, "expected member name");
            if (pos + 2 >= end || tokens->tokens[pos + 2].type != TK_LPAREN) {
                next->type = EXPR_FIELD;
                next->data.field.owner = expr;
                next->data.field.name = tokens->tokens[pos + 1].text;
                next->token = &tokens->tokens[pos + 1];
                expr = next;
                pos += 2;
                continue;
            }
            int close = matching_close(tokens, pos + 2, end);
            next->type = EXPR_CALL;
            next->token = &tokens->tokens[pos + 1];
            next->data.call.receiver = expr;
            next->data.call.name = tokens->tokens[pos + 1].text;
            next->data.call.args = parse_items(tokens, pos + 3, close, &next->data.call.count);
            pos = close + 1;
        } else {
            parse_error(tokens, pos, "unexpected token after expression");
        }
        expr = next;
    }
    return expr;
}

/** 単項演算子を解析する */
Expr* parse_unary(TokenArray* tokens, int begin, int end){
    if (begin >= end) return NULL;

    if (tokens->tokens[begin].type == TK_MINUS) {
        if (begin + 1 >= end) return NULL;

        Expr* expr = malloc(sizeof(*expr));
        expr->type = EXPR_UNARY;
        expr->token = &tokens->tokens[begin];
        expr->data.unary.op = tokens->tokens[begin].type;
        expr->data.unary.operand = parse_unary(tokens, begin + 1, end);
        if (expr->data.unary.operand == NULL) {
            free(expr);
            return NULL;
        }
        return expr;
    }

    return parse_primary(tokens, begin, end);
}

Expr* parse_multiplicative(TokenArray* tokens, int begin, int end){
    Expr* expr = malloc(sizeof(Expr));
    int depth=0;
    for(int i = end-1; i >= begin; i--){
        if(tokens->tokens[i].type == TK_LPAREN || tokens->tokens[i].type == TK_LBRACKET || tokens->tokens[i].type == TK_LBRACE) {
            depth++;
        } else if(tokens->tokens[i].type == TK_RPAREN || tokens->tokens[i].type == TK_RBRACKET || tokens->tokens[i].type == TK_RBRACE) {
            depth--;
        }

        if(depth != 0) continue;

        if(tokens->tokens[i].type == TK_STAR){
            expr->type = EXPR_BINARY;
            expr->token = &tokens->tokens[i];
            expr->data.binary.op = TK_STAR;
            expr->data.binary.left = parse_multiplicative(tokens, begin, i);
            expr->data.binary.right = parse_unary(tokens, i+1, end);
            if(expr->data.binary.left == NULL || expr->data.binary.right == NULL){
                parse_error(tokens, begin, "Expected expression in multiplication");
            }
            return expr;
        }
        else if(tokens->tokens[i].type == TK_SLASH || tokens->tokens[i].type == TK_PERCENT){
            expr->type = EXPR_BINARY;
            expr->token = &tokens->tokens[i];
            expr->data.binary.op = tokens->tokens[i].type;
            expr->data.binary.left = parse_multiplicative(tokens, begin, i);
            expr->data.binary.right = parse_unary(tokens, i+1, end);
            if(expr->data.binary.left == NULL || expr->data.binary.right == NULL){
                parse_error(tokens, begin, "Expected expression in division");
            }
            return expr;
        }
    }
    free(expr);
    return parse_unary(tokens, begin, end);
}

/*足し算と引き算を解析する*/
Expr* parse_additive(TokenArray* tokens, int begin, int end){
    Expr* expr = malloc(sizeof(Expr));
    int depth=0;
    // TODO: Implement additive parsing
    for(int i = end-1; i >= begin; i--){
        if(tokens->tokens[i].type == TK_LPAREN || tokens->tokens[i].type == TK_LBRACKET || tokens->tokens[i].type == TK_LBRACE) {
            depth++;
        } else if(tokens->tokens[i].type == TK_RPAREN || tokens->tokens[i].type == TK_RBRACKET || tokens->tokens[i].type == TK_RBRACE) {
            depth--;
        }

        if(depth != 0) continue;

        if(tokens->tokens[i].type == TK_PLUS){
            expr->type = EXPR_BINARY;
            expr->token = &tokens->tokens[i];
            expr->data.binary.op = TK_PLUS;
            expr->data.binary.left = parse_additive(tokens, begin, i);
            expr->data.binary.right = parse_multiplicative(tokens, i+1, end);
            if(expr->data.binary.left == NULL || expr->data.binary.right == NULL){
                parse_error(tokens, begin, "Expected expression in addition");
            }
            return expr;
        }
        else if(tokens->tokens[i].type == TK_MINUS){
            //先頭のマイナスは二項減算にはなり得ない
            if(i == begin) {
                // TODO: Implement unary minus
                continue;
            }
            // TODO: Implement subtraction
            if(tokens->tokens[i-1].type != TK_NUMBER && tokens->tokens[i-1].type != TK_IDENT && tokens->tokens[i-1].type != TK_RPAREN && tokens->tokens[i-1].type != TK_RBRACKET){
                continue;
            }
            expr->type = EXPR_BINARY;
            expr->token = &tokens->tokens[i];
            expr->data.binary.op = TK_MINUS;
            expr->data.binary.left = parse_additive(tokens, begin, i);
            expr->data.binary.right = parse_multiplicative(tokens, i+1, end);
            if(expr->data.binary.left == NULL || expr->data.binary.right == NULL){
                parse_error(tokens, begin, "Expected expression in subtraction");
            }
            return expr;    
        }
    }
    free(expr);
    return parse_multiplicative(tokens, begin, end);
}

Expr* parse_comparison(TokenArray* tokens, int begin, int end){
    Expr* expr = malloc(sizeof(Expr));
    int depth=0;
    // TODO: Implement comparison parsing
    for(int i = end-1; i >= begin; i--){
        if(tokens->tokens[i].type == TK_LPAREN || tokens->tokens[i].type == TK_LBRACKET || tokens->tokens[i].type == TK_LBRACE) {
            depth++;
        } else if(tokens->tokens[i].type == TK_RPAREN || tokens->tokens[i].type == TK_RBRACKET || tokens->tokens[i].type == TK_RBRACE) {
            depth--;
        }

        if(depth != 0) continue;

        if(tokens->tokens[i].type == TK_LE){
            expr->type = EXPR_BINARY;
            expr->token = &tokens->tokens[i];
            expr->data.binary.op = TK_LE;
            expr->data.binary.left = parse_comparison(tokens, begin, i);
            expr->data.binary.right = parse_additive(tokens, i+1, end);
            if(expr->data.binary.left == NULL || expr->data.binary.right == NULL){
                parse_error(tokens, begin, "Expected expression in less than or equal to");
            }
            return expr;
        }
        else if(tokens->tokens[i].type == TK_GE){
            // TODO: Implement greater than or equal to
            expr->type = EXPR_BINARY;
            expr->token = &tokens->tokens[i];
            expr->data.binary.op = TK_GE;
            expr->data.binary.left = parse_comparison(tokens, begin, i);
            expr->data.binary.right = parse_additive(tokens, i+1, end);
            if(expr->data.binary.left == NULL || expr->data.binary.right == NULL){
                parse_error(tokens, begin, "Expected expression in greater than or equal to");
            }
            return expr;
        }
        else if(tokens->tokens[i].type == TK_GT){
            // TODO: Implement greater than
            expr->type = EXPR_BINARY;
            expr->token = &tokens->tokens[i];
            expr->data.binary.op = TK_GT;
            expr->data.binary.left = parse_comparison(tokens, begin, i);
            expr->data.binary.right = parse_additive(tokens, i+1, end);
            if(expr->data.binary.left == NULL || expr->data.binary.right == NULL){
                parse_error(tokens, begin, "Expected expression in greater than");
            }
            return expr;    
        }
        else if(tokens->tokens[i].type == TK_LT){
            // TODO: Implement less than
            expr->type = EXPR_BINARY;
            expr->token = &tokens->tokens[i];
            expr->data.binary.op = TK_LT;
            expr->data.binary.left = parse_comparison(tokens, begin, i);
            expr->data.binary.right = parse_additive(tokens, i+1, end);
            if(expr->data.binary.left == NULL || expr->data.binary.right == NULL){
                parse_error(tokens, begin, "Expected expression in less than");
            }
            return expr;
        }
    }
    free(expr);
    return parse_additive(tokens, begin, end);
}

/*==と!=の比較*/
Expr* parse_equality(TokenArray* tokens, int begin, int end){
    Expr* expr = malloc(sizeof(Expr));
    int depth=0;
    // TODO: Implement equality parsing
    for(int i = end-1; i >= begin; i--){
        if(tokens->tokens[i].type == TK_LPAREN || tokens->tokens[i].type == TK_LBRACKET || tokens->tokens[i].type == TK_LBRACE) {
            depth++;
        } else if(tokens->tokens[i].type == TK_RPAREN || tokens->tokens[i].type == TK_RBRACKET || tokens->tokens[i].type == TK_RBRACE) {
            depth--;
        }
        if(depth != 0) continue;
        if(tokens->tokens[i].type == TK_EQ){
            expr->type = EXPR_BINARY;
            expr->token = &tokens->tokens[i];
            expr->data.binary.op = TK_EQ;
            expr->data.binary.left = parse_equality(tokens, begin, i);
            expr->data.binary.right = parse_comparison(tokens, i+1, end);
            if(expr->data.binary.left == NULL || expr->data.binary.right == NULL){
                parse_error(tokens, begin, "Expected expression in equality");
            }
            return expr;
        }
        else if(tokens->tokens[i].type == TK_NE){
            // TODO: Implement not equal to
            expr->type = EXPR_BINARY;
            expr->token = &tokens->tokens[i];
            expr->data.binary.op = TK_NE;
            expr->data.binary.left = parse_equality(tokens, begin, i);
            expr->data.binary.right = parse_comparison(tokens, i+1, end);
            if(expr->data.binary.left == NULL || expr->data.binary.right == NULL){
                parse_error(tokens, begin, "Expected expression in not equal to");
            }
            return expr;
        }
    }
    free(expr);
    return parse_comparison(tokens, begin, end);
}

static Statement *parse_assignment_range(TokenArray *tokens, int begin, int end) {
    if (begin >= end) parse_error(tokens, begin, "expected assignment");
    Statement *statement = calloc(1, sizeof(*statement));
    if (!statement) error_at(NULL, "out of memory");
    statement->token = &tokens->tokens[begin];
    statement->type = STMT_ASSIGN;
    TokenType prefix = tokens->tokens[begin].type;
    bool has_prefix = prefix == TK_INC || prefix == TK_DEC;
    if (has_prefix) begin++;
    if (begin >= end || tokens->tokens[begin].type != TK_IDENT) {
        parse_error(tokens, begin, "expected variable to update");
    }
    Token *name = &tokens->tokens[begin++];
    statement->data.assign.name = copy_text(name->text, strlen(name->text));
    int target_begin = begin - 1;
    while (begin < end) {
        if (tokens->tokens[begin].type == TK_LBRACKET) begin = matching_close(tokens, begin, end) + 1;
        else if (begin + 1 < end && tokens->tokens[begin].type == TK_DOT && tokens->tokens[begin+1].type == TK_IDENT) begin += 2;
        else break;
    }
    statement->data.assign.target = parse_primary(tokens, target_begin, begin);
    if (has_prefix) {
        if (begin != end) parse_error(tokens, begin, "unexpected token after update");
        statement->data.assign.op = prefix;
        return statement;
    }
    if (begin >= end) parse_error(tokens, begin, "expected assignment operator");
    TokenType op = tokens->tokens[begin++].type;
    statement->data.assign.op = op;
    if (op == TK_INC || op == TK_DEC) {
        if (begin != end) parse_error(tokens, begin, "unexpected token after update");
        return statement;
    }
    if (op != TK_ASSIGN && op != TK_PLUS_ASSIGN && op != TK_MINUS_ASSIGN &&
        op != TK_STAR_ASSIGN && op != TK_SLASH_ASSIGN && op != TK_PERCENT_ASSIGN) {
        parse_error(tokens, begin - 1, "expected assignment operator");
    }
    statement->data.assign.value = parse_equality(tokens, begin, end);
    if (!statement->data.assign.value) parse_error(tokens, begin, "expected assignment value");
    return statement;
}

static Statement *parse_stream(TokenArray *tokens, int begin, int end, bool input) {
    Statement *block = calloc(1, sizeof(*block));
    if (!block) error_at(NULL, "out of memory");
    block->type = STMT_BLOCK;
    block->token = &tokens->tokens[begin];
    block->data.block.statements = calloc(end - begin, sizeof(Statement*));
    if (!block->data.block.statements) error_at(NULL, "out of memory");
    TokenType delimiter = input ? TK_EXTRACT : TK_INSERT;
    int pos = begin + 1;
    while (pos < end) {
        if (tokens->tokens[pos++].type != delimiter) parse_error(tokens, pos-1, "unexpected stream operator");
        int start = pos;
        while (pos < end && tokens->tokens[pos].type != delimiter) {
            TokenType k = tokens->tokens[pos].type;
            if (k == TK_LPAREN || k == TK_LBRACKET || k == TK_LBRACE) pos = matching_close(tokens,pos,end);
            ++pos;
        }
        if (start == pos) parse_error(tokens, start, "expected stream operand");
        Statement *item = calloc(1, sizeof(*item));
        if (!item) error_at(NULL, "out of memory");
        item->token = &tokens->tokens[start];
        if (input) {
            Expr *target = parse_primary(tokens, start, pos);
            const Expr *root = lvalue_root(target);
            if (!root) parse_error(tokens, start, "expected input destination");
            item->type = STMT_ASSIGN;
            item->data.assign.target = target;
            item->data.assign.name = copy_text(root->data.variable.var_name, strlen(root->data.variable.var_name));
            item->data.assign.op = TK_ASSIGN;
            item->data.assign.stream_input = true;
            Expr *read = calloc(1, sizeof(*read));
            if (!read) error_at(NULL, "out of memory");
            read->type = EXPR_CALL; read->token = item->token; read->data.call.name = "read_int";
            item->data.assign.value = read;
        } else {
            item->type = STMT_WRITE;
            item->data.write.value = parse_equality(tokens, start, pos);
            if (!item->data.write.value) parse_error(tokens, start, "expected output value");
        }
        block->data.block.statements[block->data.block.count++] = item;
    }
    return block;
}

static Statement *parse_effect_range(TokenArray *tokens, int begin, int end) {
    if (begin + 1 < end && !strcmp(tokens->tokens[begin].text, "cin") && tokens->tokens[begin+1].type == TK_EXTRACT)
        return parse_stream(tokens, begin, end, true);
    if (begin + 1 < end && !strcmp(tokens->tokens[begin].text, "cout") && tokens->tokens[begin+1].type == TK_INSERT)
        return parse_stream(tokens, begin, end, false);
    int depth = 0;
    for (int i = begin; i < end; i++) {
        TokenType kind = tokens->tokens[i].type;
        if (kind == TK_LPAREN || kind == TK_LBRACKET || kind == TK_LBRACE) depth++;
        if (kind == TK_RPAREN || kind == TK_RBRACKET || kind == TK_RBRACE) depth--;
        if (depth == 0 && (kind == TK_ASSIGN || kind == TK_INC || kind == TK_DEC ||
            kind == TK_PLUS_ASSIGN || kind == TK_MINUS_ASSIGN ||
            kind == TK_STAR_ASSIGN || kind == TK_SLASH_ASSIGN || kind == TK_PERCENT_ASSIGN)) {
            return parse_assignment_range(tokens, begin, end);
        }
    }
    Expr *expr = parse_equality(tokens, begin, end);
    if (!expr || expr->type != EXPR_CALL) {
        parse_error(tokens, begin, "expected assignment or function call");
    }
    Statement *stmt = calloc(1, sizeof(*stmt));
    if (!stmt) error_at(NULL, "out of memory");
    stmt->type = STMT_EXPR;
    stmt->token = &tokens->tokens[begin];
    stmt->data.expr.expression = expr;
    return stmt;
}

Statement* parse_assign_statement(TokenArray* tokens, int *pos){
    int begin = *pos;
    while ((size_t)*pos < tokens->count && tokens->tokens[*pos].type != TK_SEMICOLON) {
        TokenType kind = tokens->tokens[*pos].type;
        if (kind == TK_LBRACE && *pos > begin && tokens->tokens[*pos-1].type == TK_IDENT) {
            *pos = matching_close(tokens, *pos, (int)tokens->count) + 1; continue;
        }
        if (kind == TK_EOF || kind == TK_LBRACE || kind == TK_RBRACE) {
            parse_error(tokens, *pos, "expected ';' after assignment");
        }
        (*pos)++;
    }
    Statement *statement = parse_effect_range(tokens, begin, *pos);
    (*pos)++;
    return statement;
}

static ValueType named_type(Program *program, const char *name) {
    if (!strcmp(name, "int")) return VALUE_INT;
    if (!strcmp(name, "char")) return VALUE_CHAR;
    if (!strcmp(name, "string")) return VALUE_STRING;
    if (!strcmp(name, "graph")) return VALUE_GRAPH;
    if (!strcmp(name, "digraph")) return VALUE_DIGRAPH;
    if (!strcmp(name, "edge")) return VALUE_EDGE;
    for (size_t i = 0; i < program->record_count; ++i) {
        if (!strcmp(name, program->records[i].name)) return VALUE_RECORD + (int)i * TYPE_STRIDE;
    }
    return VALUE_VOID;
}

static ValueType modular_type(Program *program, const char *value, Token *token) {
    while (*value == '0') ++value;
    if (!*value) error_at(token, "modulus must be positive");
    for (size_t i = 0; i < program->modulus_count; ++i)
        if (!strcmp(program->moduli[i], value)) return VALUE_MINT + (int)i * TYPE_STRIDE;
    if (program->modulus_count == 8192) error_at(token, "too many moduli");
    size_t i = program->modulus_count++;
    program->moduli = realloc(program->moduli, (i + 1) * sizeof(*program->moduli));
    if (!program->moduli) error_at(NULL, "out of memory");
    program->moduli[i] = value;
    return VALUE_MINT + (int)i * TYPE_STRIDE;
}

static ValueType associative_type(Program *p, AssocType type, Token *token) {
    for (size_t i = 0; i < p->assoc_count; ++i) {
        const AssocType a = p->assocs[i];
        if (a.kind == type.kind && a.key == type.key && a.value == type.value)
            return VALUE_ASSOC + (int)i * TYPE_STRIDE;
    }
    if (p->assoc_count == 8192) error_at(token, "too many associative container types");
    size_t i = p->assoc_count++;
    p->assocs = realloc(p->assocs, (i + 1) * sizeof(*p->assocs));
    if (!p->assocs) error_at(NULL, "out of memory");
    p->assocs[i] = type;
    return VALUE_ASSOC + (int)i * TYPE_STRIDE;
}

static bool declaration_start(TokenArray *tokens, int i) {
    Token *t = &tokens->tokens[i];
    return t->type == TK_LET || t->type == TK_AUTO || t->type == TK_INT ||
           (t->type == TK_IDENT && (named_type(tokens->program, t->text) != VALUE_VOID ||
            (!strcmp(t->text, "mint998") || !strcmp(t->text, "mint107")) ||
            ((!strcmp(t->text, "pair") || !strcmp(t->text, "mint") || assoc_kind(t->text) >= 0) && tokens->tokens[i+1].type == TK_LT)));
}

static ValueType parse_type(TokenArray *tokens, int *pos, int end, bool allow_void);

static Statement *parse_declarator(TokenArray *tokens, int *pos, Token *keyword, ValueType type, const char *name) {
    Statement *stmt = calloc(1, sizeof(*stmt));
    if (!stmt) error_at(NULL, "out of memory");
    stmt->type = STMT_LET;
    stmt->token = keyword;
    stmt->data.let.explicit_type = keyword->type != TK_LET && keyword->type != TK_AUTO;
    stmt->data.let.declared_type = type;
    if (!name) {
        if (tokens->tokens[*pos].type != TK_IDENT)
            parse_error(tokens, *pos, "expected identifier in declaration");
        name = tokens->tokens[(*pos)++].text;
    }
    stmt->data.let.name = copy_text(name, strlen(name));
    int begin = *pos;
    while ((size_t)*pos < tokens->count && tokens->tokens[*pos].type != TK_SEMICOLON) {
        TokenType kind = tokens->tokens[*pos].type;
        if (kind == TK_COMMA) break;
        if (kind == TK_LPAREN || kind == TK_LBRACKET || kind == TK_LBRACE) {
            *pos = matching_close(tokens, *pos, (int)tokens->count) + 1; continue;
        }
        if (kind == TK_EOF || kind == TK_LBRACE || kind == TK_RBRACE) {
            parse_error(tokens, *pos, "expected ';' after declaration");
        }
        (*pos)++;
    }
    int end = *pos;
    if (tokens->tokens[begin].type == TK_ASSIGN) {
        stmt->data.let.value = parse_equality(tokens, begin + 1, end);
        if (!stmt->data.let.value) parse_error(tokens, begin, "expected initializer");
    } else if (stmt->data.let.explicit_type) {
        Expr *value = calloc(1, sizeof(*value));
        if (!value) error_at(NULL, "out of memory");
        value->type = EXPR_CONSTRUCT;
        value->token = stmt->token;
        value->data.construct.type = stmt->data.let.declared_type;
        if (begin != end) {
            if (tokens->tokens[begin].type == TK_LBRACKET) {
                value->data.construct.sizes = calloc((size_t)(end - begin), sizeof(Expr *));
                if (!value->data.construct.sizes) error_at(NULL, "out of memory");
                while (begin < end && tokens->tokens[begin].type == TK_LBRACKET) {
                    int close = matching_close(tokens, begin, end);
                    Expr *size = parse_equality(tokens, begin + 1, close);
                    if (!size) parse_error(tokens, begin, "expected initial size");
                    value->data.construct.sizes[value->data.construct.count++] = size;
                    begin = close + 1;
                }
                if (begin != end) parse_error(tokens, begin, "unexpected token after initial sizes");
            } else {
                if (tokens->tokens[begin].type != TK_LPAREN || matching_close(tokens, begin, end) != end - 1) {
                    parse_error(tokens, begin, "expected '=', constructor sizes, or ';'");
                }
                value->data.construct.sizes = parse_items(tokens, begin + 1, end - 1, &value->data.construct.count);
            }
        }
        stmt->data.let.value = value;
    } else { parse_error(tokens, begin, "expected '=' in let declaration"); }
    return stmt;
}

static Statement *parse_bindings(TokenArray *tokens, int *pos, Token *keyword) {
    int opening = *pos;
    int close = matching_close(tokens, opening, (int)tokens->count);
    Statement *group = calloc(1, sizeof(*group));
    if (!group) error_at(NULL, "out of memory");
    group->type = STMT_DECLARATIONS;
    group->token = keyword;
    Statement **items = calloc((size_t)(close - opening + 1), sizeof(*items));
    if (!items) error_at(NULL, "out of memory");
    group->data.block.statements = items;
    size_t count = 1; // The hidden first declaration evaluates the initializer once.
    char hidden[64];
    snprintf(hidden, sizeof(hidden), "$binding%zu", keyword->offset);
    ++*pos;
    for (;;) {
        Token *name = &tokens->tokens[*pos];
        if (name->type != TK_IDENT) parse_error(tokens, *pos, "expected binding name");
        Statement *binding = calloc(1, sizeof(*binding));
        Expr *field = calloc(1, sizeof(*field)), *owner = calloc(1, sizeof(*owner));
        if (!binding || !field || !owner) error_at(NULL, "out of memory");
        binding->type = STMT_LET;
        binding->token = name;
        binding->data.let.name = copy_text(name->text, strlen(name->text));
        binding->data.let.value = field;
        field->type = EXPR_FIELD;
        field->token = name;
        field->data.field.owner = owner;
        field->data.field.index = count - 1;
        owner->type = EXPR_VARIABLE;
        owner->token = keyword;
        items[count++] = binding;
        if (++*pos == close) break;
        if (tokens->tokens[(*pos)++].type != TK_COMMA) parse_error(tokens, *pos - 1, "expected ',' between bindings");
    }
    ++*pos;
    items[0] = parse_declarator(tokens, pos, keyword, VALUE_INT, hidden);
    if (tokens->tokens[*pos].type != TK_SEMICOLON) parse_error(tokens, *pos, "expected ';' after bindings");
    ++*pos;
    items[0]->data.let.binding_count = count - 1;
    for (size_t i = 1; i < count; ++i) {
        items[i]->data.let.value->data.field.owner->data.variable.var_name = items[0]->data.let.name;
    }
    group->data.block.count = count;
    return group;
}

Statement* parse_let_statement(TokenArray* tokens, int *pos) {
    Token *keyword = &tokens->tokens[*pos];
    ValueType type = VALUE_INT;
    if (keyword->type == TK_AUTO && tokens->tokens[*pos+1].type == TK_LBRACKET) {
        ++*pos;
        return parse_bindings(tokens, pos, keyword);
    }
    if (keyword->type == TK_LET || keyword->type == TK_AUTO) ++*pos;
    else type = parse_type(tokens, pos, (int)tokens->count, false);
    Statement *first = parse_declarator(tokens, pos, keyword, type, NULL);
    if (tokens->tokens[*pos].type == TK_SEMICOLON) { ++*pos; return first; }
    Statement *group = calloc(1, sizeof(*group));
    if (!group) error_at(NULL, "out of memory");
    group->type = STMT_DECLARATIONS;
    group->token = keyword;
    Statement *item = first;
    for (;;) {
        size_t n = group->data.block.count++;
        group->data.block.statements = realloc(group->data.block.statements, (n+1) * sizeof(Statement*));
        if (!group->data.block.statements) error_at(NULL, "out of memory");
        group->data.block.statements[n] = item;
        if (tokens->tokens[(*pos)++].type == TK_SEMICOLON) break;
        item = parse_declarator(tokens, pos, keyword, type, NULL);
    }
    return group;
}

static Statement *parse_break_statement(TokenArray *tokens, int *pos) {
    Statement *stmt = calloc(1, sizeof(*stmt));
    if (!stmt) error_at(NULL, "out of memory");
    stmt->type = STMT_BREAK;
    stmt->token = &tokens->tokens[(*pos)++];
    if ((size_t)*pos >= tokens->count || tokens->tokens[*pos].type != TK_SEMICOLON) {
        parse_error(tokens, *pos, "expected ';' after break");
    }
    (*pos)++;
    return stmt;
}
/*<if-statement> ::= "if" "(" <expr> ")" <block> <else-opt>*/
Statement* parse_if_statement(TokenArray* tokens, int *pos){
    Statement* statement = malloc(sizeof(Statement));
    statement->token = &tokens->tokens[*pos];
    statement->type = STMT_IF;
    int depth=0;
    int begin= *pos;
    (*pos)++;
    if((size_t)*pos >= tokens->count || tokens->tokens[*pos].type != TK_LPAREN){
        parse_error(tokens, *pos, "Expected '(' after 'if'");
    }
    depth = 1;
    (*pos)++;

    while((size_t)*pos < tokens->count && depth != 0){
        if (tokens->tokens[*pos].type == TK_LBRACE && *pos > 0 && tokens->tokens[*pos-1].type == TK_IDENT) {
            *pos = matching_close(tokens, *pos, (int)tokens->count) + 1;
            continue;
        }
        if(tokens->tokens[*pos].type == TK_EOF || tokens->tokens[*pos].type == TK_LBRACE ||
           tokens->tokens[*pos].type == TK_RBRACE){
            break;
        }
        if(tokens->tokens[*pos].type == TK_LPAREN){
            depth++;
        }
        if(tokens->tokens[*pos].type == TK_RPAREN){
            depth--;
        }
        (*pos)++;
    }
    if(depth != 0 || *pos - 1 <= begin + 2){
        parse_error(tokens, *pos, "Expected condition and closing ')' after 'if'");
    }
    statement->data.if_stmt.condition = parse_equality(tokens, begin + 2, *pos - 1);
    if(statement->data.if_stmt.condition == NULL){
        parse_error(tokens, *pos, "Expected condition after 'if'");
    }

    statement->data.if_stmt.then_branch = parse_block(tokens, pos);

    if((size_t)*pos < tokens->count && tokens->tokens[*pos].type == TK_ELSE){
        (*pos)++;
        statement->data.if_stmt.else_branch = parse_block(tokens, pos);
    }
    else{
        statement->data.if_stmt.else_branch = NULL;
    }
    return statement;
}

/*
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
Statement* parse_while_statement(TokenArray* tokens, int *pos){
    // TODO: Implement while statement parsing
    Statement* statement = malloc(sizeof(*statement));
    statement->token = &tokens->tokens[*pos];
    statement->type = STMT_WHILE;
    int paren_depth=0;

    int start = *pos;
    (*pos)++;
    if((size_t)*pos >= tokens->count || tokens->tokens[*pos].type != TK_LPAREN){
        parse_error(tokens, *pos, "Expected '(' after 'while'");
    }
    paren_depth = 1;
    (*pos)++;
    while((size_t)*pos < tokens->count && paren_depth != 0){
        if (tokens->tokens[*pos].type == TK_LBRACE && *pos > 0 && tokens->tokens[*pos-1].type == TK_IDENT) {
            *pos = matching_close(tokens, *pos, (int)tokens->count) + 1;
            continue;
        }
        if(tokens->tokens[*pos].type == TK_EOF || tokens->tokens[*pos].type == TK_LBRACE ||
           tokens->tokens[*pos].type == TK_RBRACE){
            break;
        }
        if(tokens->tokens[*pos].type == TK_LPAREN){
            paren_depth++;
        }
        if(tokens->tokens[*pos].type == TK_RPAREN){
            paren_depth--;
        }
        (*pos)++;
    }
    if(paren_depth != 0 || *pos - 1 <= start + 2){
        parse_error(tokens, *pos, "Expected condition and closing ')' after 'while'");
    }
    statement->data.while_stmt.condition=parse_equality(tokens, start + 2, *pos - 1);
    if(statement->data.while_stmt.condition == NULL){
        parse_error(tokens, *pos, "Expected condition after 'while'");
    }

    statement->data.while_stmt.body = parse_block(tokens, pos);
    return statement;
}

Statement* parse_for_statement(TokenArray *tokens, int *pos) {
    Statement *statement = calloc(1, sizeof(*statement));
    if (!statement) error_at(NULL, "out of memory");
    statement->token = &tokens->tokens[*pos];
    statement->type = STMT_FOR;
    (*pos)++;
    if ((size_t)*pos >= tokens->count || tokens->tokens[*pos].type != TK_LPAREN) {
        parse_error(tokens, *pos, "expected '(' after 'for'");
    }
    int close = matching_close(tokens, *pos, (int)tokens->count);
    (*pos)++;
    if (declaration_start(tokens, *pos) && tokens->tokens[*pos].type != TK_LET && tokens->tokens[*pos].type != TK_AUTO) {
        int look = *pos;
        ValueType type = parse_type(tokens, &look, close, false);
        if (look + 1 < close && tokens->tokens[look].type == TK_IDENT && tokens->tokens[look+1].type == TK_COLON) {
            statement->type = STMT_EACH;
            statement->data.each.type = type;
            statement->data.each.name = tokens->tokens[look].text;
            statement->data.each.values = parse_equality(tokens, look+2, close);
            if (!statement->data.each.values) parse_error(tokens, look+2, "expected range expression");
            *pos = close + 1;
            statement->data.each.body = parse_block(tokens, pos);
            return statement;
        }
    }
    if (tokens->tokens[*pos].type == TK_SEMICOLON) (*pos)++;
    else if (declaration_start(tokens, *pos)) {
        statement->data.for_stmt.init = parse_let_statement(tokens, pos);
    } else {
        statement->data.for_stmt.init = parse_assign_statement(tokens, pos);
    }
    int begin = *pos;
    while (*pos < close && tokens->tokens[*pos].type != TK_SEMICOLON) (*pos)++;
    if (*pos >= close) parse_error(tokens, *pos, "expected ';' after for condition");
    if (begin != *pos) {
        statement->data.for_stmt.condition = parse_equality(tokens, begin, *pos);
        if (!statement->data.for_stmt.condition) parse_error(tokens, begin, "expected for condition");
    }
    (*pos)++;
    if (*pos < close) {
        statement->data.for_stmt.step = parse_effect_range(tokens, *pos, close);
    }
    *pos = close + 1;
    statement->data.for_stmt.body = parse_block(tokens, pos);
    return statement;
}

/* rep(i, start, end) lowers to for (let i = start; i < end; i++). */
static Statement *parse_rep_statement(TokenArray *tokens, int *pos) {
    Token *token = &tokens->tokens[*pos];
    (*pos)++;
    if ((size_t)*pos >= tokens->count || tokens->tokens[*pos].type != TK_LPAREN) {
        parse_error(tokens, *pos, "expected '(' after 'rep'");
    }
    int close = matching_close(tokens, *pos, (int)tokens->count);
    size_t count;
    Expr **args = parse_items(tokens, *pos + 1, close, &count);
    if (count != 3 || args[0]->type != EXPR_VARIABLE) {
        error_at(token, "expected rep(variable, start, end)");
    }
    char *name = args[0]->data.variable.var_name;
    Statement *stmt = calloc(1, sizeof(*stmt));
    Statement *init = calloc(1, sizeof(*init));
    Statement *step = calloc(1, sizeof(*step));
    Expr *condition = calloc(1, sizeof(*condition));
    if (!stmt || !init || !step || !condition) error_at(NULL, "out of memory");
    init->type = STMT_LET;
    init->token = args[0]->token;
    init->data.let.name = copy_text(name, strlen(name));
    init->data.let.value = args[1];
    condition->type = EXPR_BINARY;
    condition->token = token;
    condition->data.binary.op = TK_LT;
    condition->data.binary.left = args[0];
    condition->data.binary.right = args[2];
    step->type = STMT_ASSIGN;
    step->token = args[0]->token;
    step->data.assign.name = copy_text(name, strlen(name));
    step->data.assign.op = TK_INC;
    stmt->type = STMT_FOR;
    stmt->token = token;
    stmt->data.for_stmt.init = init;
    stmt->data.for_stmt.condition = condition;
    stmt->data.for_stmt.step = step;
    *pos = close + 1;
    stmt->data.for_stmt.body = parse_block(tokens, pos);
    free(args);
    return stmt;
}

static Statement *parse_return_statement(TokenArray *tokens, int *pos) {
    Statement *stmt = calloc(1, sizeof(*stmt));
    if (!stmt) error_at(NULL, "out of memory");
    stmt->type = STMT_RETURN;
    stmt->token = &tokens->tokens[*pos];
    int begin = ++(*pos);
    while ((size_t)*pos < tokens->count && tokens->tokens[*pos].type != TK_SEMICOLON) {
        TokenType kind = tokens->tokens[*pos].type;
        if (kind == TK_LBRACE && *pos > begin && tokens->tokens[*pos-1].type == TK_IDENT) {
            *pos = matching_close(tokens, *pos, (int)tokens->count) + 1; continue;
        }
        if (kind == TK_EOF || kind == TK_LBRACE || kind == TK_RBRACE) {
            parse_error(tokens, *pos, "expected ';' after return");
        }
        (*pos)++;
    }
    if (begin != *pos) {
        stmt->data.return_stmt.value = parse_equality(tokens, begin, *pos);
        if (!stmt->data.return_stmt.value) parse_error(tokens, begin, "expected return value");
    }
    (*pos)++;
    return stmt;
}

Statement* parse_block(TokenArray* tokens, int *pos){
    // TODO: Implement block parsing
    if((size_t)*pos >= tokens->count || tokens->tokens[*pos].type != TK_LBRACE){
        parse_error(tokens, *pos, "Expected '{'");
    }
    Statement *statement = malloc(sizeof(*statement));
    statement->token = &tokens->tokens[*pos];
    statement->type = STMT_BLOCK;
    statement->data.block.count = 0;
    statement->data.block.capacity = tokens->count;
    statement->data.block.statements = malloc(sizeof(Statement*) * tokens->count);
    (*pos)++;// Skip the opening brace
    while(((size_t)*pos < tokens->count) && (tokens->tokens[*pos].type != TK_RBRACE)){
        if(!declaration_start(tokens, *pos) && (tokens->tokens[*pos].type == TK_IDENT || tokens->tokens[*pos].type == TK_INC || tokens->tokens[*pos].type == TK_DEC)){
            statement->data.block.statements[statement->data.block.count++] = parse_assign_statement(tokens, pos);
        }
        else if(tokens->tokens[*pos].type == TK_BREAK){
            statement->data.block.statements[statement->data.block.count++] = parse_break_statement(tokens, pos);
        }
        else if(tokens->tokens[*pos].type == TK_RETURN){
            statement->data.block.statements[statement->data.block.count++] = parse_return_statement(tokens, pos);
        }
        else if(tokens->tokens[*pos].type == TK_FN){
            parse_error(tokens, *pos, "function definitions must be top-level");
        }
        else if(declaration_start(tokens, *pos)){
            statement->data.block.statements[statement->data.block.count++] = parse_let_statement(tokens, pos);
        }
        else if(tokens->tokens[*pos].type == TK_PRINT){
            statement->data.block.statements[statement->data.block.count++] = parse_print_statement(tokens, pos);
        }
        else if(tokens->tokens[*pos].type == TK_IF){
            statement->data.block.statements[statement->data.block.count++] = parse_if_statement(tokens, pos);
        }
        else if(tokens->tokens[*pos].type == TK_REP){
            statement->data.block.statements[statement->data.block.count++] = parse_rep_statement(tokens, pos);
        }
        else if(tokens->tokens[*pos].type == TK_FOR){
            statement->data.block.statements[statement->data.block.count++] = parse_for_statement(tokens, pos);
        }
        else if(tokens->tokens[*pos].type == TK_WHILE){
            statement->data.block.statements[statement->data.block.count++] = parse_while_statement(tokens, pos);
        }
        else if(tokens->tokens[*pos].type == TK_LBRACE){
            statement->data.block.statements[statement->data.block.count++] = parse_block(tokens, pos);
        }
        else if(tokens->tokens[*pos].type == TK_EOF){
            parse_error(tokens, *pos, "Unexpected end of file");
        }
        else{
            parse_error(tokens, *pos, "Unexpected token");
        }

    }
    if((size_t)*pos >= tokens->count){
        parse_error(tokens, *pos, "Expected '}'");
    }
    (*pos)++;

    return statement;
}

Statement* parse_print_statement(TokenArray* tokens, int *pos){
    // TODO: Implement print statement parsing
    Statement* statement = malloc(sizeof(*statement));
    statement->token = &tokens->tokens[*pos];
    statement->type = STMT_PRINT;
    int start = *pos;
    while(((size_t)*pos < tokens->count) && (tokens->tokens[*pos].type != TK_SEMICOLON))
    {
        if(tokens->tokens[*pos].type == TK_EOF){
            parse_error(tokens, *pos, "Expected ';' at the end of print statement");
        }
        else if(tokens->tokens[*pos].type == TK_RBRACE){
            parse_error(tokens, *pos, "Expected ';' at the end of print statement");
        }
        else if(tokens->tokens[*pos].type == TK_LBRACE){
            *pos = matching_close(tokens, *pos, (int)tokens->count) + 1; continue;
        }
        (*pos)++;
    }
    int end = *pos;
    if(tokens->tokens[start + 1].type == TK_LPAREN && tokens->tokens[end - 1].type == TK_RPAREN){
        statement->data.print.expression = parse_equality(tokens, start + 2, end - 1);
        if(statement->data.print.expression == NULL){
            parse_error(tokens, *pos, "Expected expression in print statement");
        }
            (*pos)++;
            return statement;
    }
    else{
        parse_error(tokens, *pos, "Unexpected token");
    }
}

static ValueType parse_type(TokenArray *tokens, int *pos, int end, bool allow_void) {
    if (*pos >= end || (tokens->tokens[*pos].type != TK_IDENT && tokens->tokens[*pos].type != TK_INT)) {
        parse_error(tokens, *pos, "expected type 'int', 'int[]', or 'void'");
    }
    Token *token = &tokens->tokens[(*pos)++];
    if (strcmp(token->text, "void") == 0 && allow_void) return VALUE_VOID;
    ValueType type = named_type(tokens->program, token->text);
    if (!strcmp(token->text, "mint998")) type = modular_type(tokens->program, "998244353", token);
    else if (!strcmp(token->text, "mint107")) type = modular_type(tokens->program, "1000000007", token);
    else if (!strcmp(token->text, "mint")) {
        if (*pos + 2 >= end || tokens->tokens[*pos].type != TK_LT ||
            tokens->tokens[*pos+1].type != TK_NUMBER || tokens->tokens[*pos+2].type != TK_GT)
            parse_error(tokens, *pos, "expected mint<positive integer>");
        type = modular_type(tokens->program, tokens->tokens[*pos+1].text, token);
        *pos += 3;
    }
    if (!strcmp(token->text, "pair")) {
        const TokenType syntax[] = {TK_LT, TK_INT, TK_COMMA, TK_INT, TK_GT};
        for (size_t i = 0; i < sizeof(syntax)/sizeof(*syntax); ++i) {
            if (*pos >= end || tokens->tokens[*pos].type != syntax[i])
                parse_error(tokens, *pos, "expected pair<int,int>");
            ++*pos;
        }
        type = VALUE_PAIR;
    }
    int kind = assoc_kind(token->text);
    if (kind >= 0) {
        if (*pos >= end || tokens->tokens[(*pos)++].type != TK_LT)
            parse_error(tokens, *pos - 1, "expected '<' after container type");
        ValueType key = parse_type(tokens, pos, end, false);
        if (key != VALUE_INT && key != VALUE_CHAR && key != VALUE_STRING)
            error_at(token, "container keys must be int, char or string");
        ValueType value = key;
        if (kind < 4) {
            if (*pos >= end || tokens->tokens[(*pos)++].type != TK_COMMA)
                parse_error(tokens, *pos - 1, "map requires key and value types");
            value = parse_type(tokens, pos, end, false);
        }
        if (*pos >= end || tokens->tokens[(*pos)++].type != TK_GT)
            parse_error(tokens, *pos - 1, "expected '>' after container type");
        type = associative_type(tokens->program, (AssocType){kind, key, value}, token);
    }
    if (type == VALUE_VOID) error_at(token, "unknown or invalid type '%s'", token->text);
    while (*pos < end && tokens->tokens[*pos].type == TK_LBRACKET) {
        (*pos)++;
        if (*pos >= end || tokens->tokens[*pos].type != TK_RBRACKET) {
            parse_error(tokens, *pos, "expected ']' in array type");
        }
        (*pos)++;
        if (type_rank(type) == TYPE_STRIDE - 1) parse_error(tokens, *pos, "too many vector dimensions");
        type++;
    }
    return type;
}

static Function *parse_function(TokenArray *tokens, int *pos, ValueType owner) {
    Function *function = calloc(1, sizeof(*function));
    if (!function) error_at(NULL, "out of memory");
    function->token = &tokens->tokens[*pos];
    function->owner = owner;
    const bool legacy = function->token->type == TK_FN;
    if (legacy) {
        function->return_type = owner ? VALUE_VOID : VALUE_INT;
        ++*pos;
    } else {
        function->return_type = parse_type(tokens, pos, (int)tokens->count, true);
    }
    if ((size_t)*pos >= tokens->count || tokens->tokens[*pos].type != TK_IDENT) {
        parse_error(tokens, *pos, "expected function name");
    }
    function->name = tokens->tokens[(*pos)++].text;
    if ((size_t)*pos >= tokens->count || tokens->tokens[*pos].type != TK_LPAREN) {
        parse_error(tokens, *pos, "expected '(' after function name");
    }
    int close = matching_close(tokens, *pos, (int)tokens->count);
    (*pos)++;
    if (*pos < close || owner) {
        function->params = calloc((size_t)(close - *pos) + 1, sizeof(Parameter));
        if (!function->params) error_at(NULL, "out of memory");
    }
    if (owner) function->params[function->count++] = (Parameter){.name="self", .token=function->token, .type=owner, .by_ref=true};
    while (*pos < close) {
        Parameter *param = &function->params[function->count++];
        bool type_first = !legacy || (declaration_start(tokens, *pos) && *pos + 1 < close &&
                                      tokens->tokens[*pos+1].type != TK_COLON && tokens->tokens[*pos+1].type != TK_COMMA);
        param->type = VALUE_INT;
        if (type_first) {
            param->type = parse_type(tokens, pos, close, false);
            if (*pos < close && tokens->tokens[*pos].type == TK_AMP) {
                param->by_ref = true;
                ++*pos;
            }
        }
        if (*pos >= close || tokens->tokens[*pos].type != TK_IDENT)
            parse_error(tokens, *pos, "expected parameter name");
        param->token = &tokens->tokens[*pos];
        param->name = tokens->tokens[(*pos)++].text;
        if (!type_first && *pos < close && tokens->tokens[*pos].type == TK_COLON) {
            ++*pos;
            if (*pos < close && tokens->tokens[*pos].type == TK_AMP) {
                param->by_ref = true;
                ++*pos;
            }
            param->type = parse_type(tokens, pos, close, false);
        }
        if (*pos == close) break;
        if (tokens->tokens[*pos].type != TK_COMMA) parse_error(tokens, *pos, "expected ',' between parameters");
        (*pos)++;
        if (*pos == close) parse_error(tokens, *pos, "expected parameter after ','");
    }
    *pos = close + 1;
    if (legacy && (size_t)*pos < tokens->count && tokens->tokens[*pos].type == TK_ARROW) {
        (*pos)++;
        function->return_type = parse_type(tokens, pos, (int)tokens->count, true);
    }
    function->body = parse_block(tokens, pos);
    return function;
}

static bool function_start(TokenArray *tokens, int pos) {
    Token *t = &tokens->tokens[pos];
    if (t->type == TK_FN) return true;
    if (!declaration_start(tokens, pos) && strcmp(t->text, "void")) return false;
    if (t->type == TK_LET || t->type == TK_AUTO) return false;
    parse_type(tokens, &pos, (int)tokens->count, true);
    if (tokens->tokens[pos].type != TK_IDENT || tokens->tokens[pos+1].type != TK_LPAREN) return false;
    int close = matching_close(tokens, pos+1, (int)tokens->count);
    return tokens->tokens[close+1].type == TK_LBRACE;
}

static void add_function(TokenArray *tokens, int *pos, ValueType owner) {
    Program *p = tokens->program;
    if (!p->functions) {
        p->functions = calloc(tokens->count, sizeof(Function *));
        if (!p->functions) error_at(NULL, "out of memory");
    }
    Function *f = parse_function(tokens, pos, owner);
    f->id = p->function_count;
    p->functions[p->function_count++] = f;
}

static void parse_record(TokenArray *tokens, int *pos) {
    Program *p = tokens->program;
    ++*pos;
    Token *name = &tokens->tokens[*pos];
    if (name->type != TK_IDENT || !strcmp(name->text, "void") || !strcmp(name->text, "pair") || !strcmp(name->text, "mint") || assoc_kind(name->text) >= 0 ||
        !strcmp(name->text, "mint998") || !strcmp(name->text, "mint107") || !strcmp(name->text, "struct") || named_type(p, name->text) != VALUE_VOID)
        parse_error(tokens, *pos, "expected a new structure name");
    if (p->record_count >= 16384 - 7) parse_error(tokens, *pos, "too many structure types");
    ++*pos;
    if (tokens->tokens[*pos].type != TK_LBRACE) parse_error(tokens, *pos, "expected '{'");
    ++*pos;
    size_t id = p->record_count++;
    p->records = realloc(p->records, p->record_count * sizeof(Record));
    if (!p->records) error_at(NULL, "out of memory");
    Record *record = &p->records[id];
    *record = (Record){.name = name->text};
    ValueType owner = VALUE_RECORD + (int)id * TYPE_STRIDE;
    while (tokens->tokens[*pos].type != TK_RBRACE) {
        if (function_start(tokens, *pos)) {
            add_function(tokens, pos, owner);
            continue;
        }
        ValueType type = parse_type(tokens, pos, (int)tokens->count, false);
        if (type == owner) parse_error(tokens, *pos, "recursive field requires a vector");
        Token *field = &tokens->tokens[*pos];
        if (field->type != TK_IDENT) parse_error(tokens, *pos, "expected field name");
        for (size_t i = 0; i < record->count; ++i)
            if (!strcmp(field->text, record->fields[i].name)) parse_error(tokens, *pos, "duplicate field");
        ++*pos;
        if (tokens->tokens[*pos].type != TK_SEMICOLON) parse_error(tokens, *pos, "expected ';' after field");
        ++*pos;
        record->fields = realloc(record->fields, (record->count + 1) * sizeof(Field));
        if (!record->fields) error_at(NULL, "out of memory");
        record->fields[record->count++] = (Field){field->text, type};
    }
    ++*pos;
    if (tokens->tokens[*pos].type == TK_SEMICOLON) ++*pos;

}

Program* parse(TokenArray* tokens){
    Program* program = calloc(1, sizeof(Program));
    if (!program) error_at(NULL, "out of memory");
    tokens->program = program;
    program->statesments = malloc(sizeof(Statement*) * tokens->count);
    program->count = 0;
    program->capacity =tokens->count;

    int pos = 0;

    while((size_t)pos < tokens->count){
        if(tokens->tokens[pos].type == TK_IDENT && !strcmp(tokens->tokens[pos].text, "struct")) {
            parse_record(tokens, &pos);
        } else if(function_start(tokens, pos)){
            add_function(tokens, &pos, 0);
        }
        else if(tokens->tokens[pos].type == TK_RETURN){
            program->statesments[program->count++] = parse_return_statement(tokens, &pos);
        }
        else if(tokens->tokens[pos].type == TK_REP){
            program->statesments[program->count++] = parse_rep_statement(tokens, &pos);
        }
        else if(tokens->tokens[pos].type == TK_FOR){
            program->statesments[program->count++] = parse_for_statement(tokens, &pos);
        }
        else if(tokens->tokens[pos].type == TK_WHILE){
            program->statesments[program->count++] = parse_while_statement(tokens, &pos);
        }
        else if(tokens->tokens[pos].type == TK_LBRACE){
            program->statesments[program->count++] = parse_block(tokens, &pos);
        }
        else if(tokens->tokens[pos].type == TK_PRINT){
            program->statesments[program->count++] = parse_print_statement(tokens, &pos);
        }
        else if(tokens->tokens[pos].type == TK_BREAK){
            program->statesments[program->count++] = parse_break_statement(tokens, &pos);
        }
        else if(declaration_start(tokens, pos)){
            program->statesments[program->count++] = parse_let_statement(tokens, &pos);
        }
        else if(tokens->tokens[pos].type == TK_IDENT || tokens->tokens[pos].type == TK_INC || tokens->tokens[pos].type == TK_DEC){
            program->statesments[program->count++] = parse_assign_statement(tokens, &pos);
        }
        else if(tokens->tokens[pos].type == TK_IF){
            program->statesments[program->count++] = parse_if_statement(tokens, &pos);
        }
        else if(tokens->tokens[pos].type == TK_EOF){
            break;
        }
        else{
            parse_error(tokens, pos, "unexpected token");
        }
    }
    return program;
} 

