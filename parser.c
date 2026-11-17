
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


/* Return the matching ')' or ']' within the current expression. */
static int matching_close(TokenArray *tokens, int open, int end) {
    TokenType left = tokens->tokens[open].type;
    TokenType right = left == TK_LPAREN ? TK_RPAREN : TK_RBRACKET;
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
        if (kind == TK_LPAREN || kind == TK_LBRACKET) depth++;
        if (kind == TK_RPAREN || kind == TK_RBRACKET) depth--;
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
    } else if (token->type == TK_IDENT) {
        expr->type = EXPR_VARIABLE;
        expr->data.variable.var_name = token->text;
        if (pos < end && tokens->tokens[pos].type == TK_LPAREN) {
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
            if (pos + 2 >= end || tokens->tokens[pos + 1].type != TK_IDENT ||
                tokens->tokens[pos + 2].type != TK_LPAREN) {
                parse_error(tokens, pos, "expected method call after '.'");
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
        if(tokens->tokens[i].type == TK_LPAREN || tokens->tokens[i].type == TK_LBRACKET) {
            depth++;
        } else if(tokens->tokens[i].type == TK_RPAREN || tokens->tokens[i].type == TK_RBRACKET) {
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
        else if(tokens->tokens[i].type == TK_SLASH){
            expr->type = EXPR_BINARY;
            expr->token = &tokens->tokens[i];
            expr->data.binary.op = TK_SLASH;
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
        if(tokens->tokens[i].type == TK_LPAREN || tokens->tokens[i].type == TK_LBRACKET) {
            depth++;
        } else if(tokens->tokens[i].type == TK_RPAREN || tokens->tokens[i].type == TK_RBRACKET) {
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
        if(tokens->tokens[i].type == TK_LPAREN || tokens->tokens[i].type == TK_LBRACKET) {
            depth++;
        } else if(tokens->tokens[i].type == TK_RPAREN || tokens->tokens[i].type == TK_RBRACKET) {
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
        if(tokens->tokens[i].type == TK_LPAREN || tokens->tokens[i].type == TK_LBRACKET) {
            depth++;
        } else if(tokens->tokens[i].type == TK_RPAREN || tokens->tokens[i].type == TK_RBRACKET) {
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
    while (begin < end && tokens->tokens[begin].type == TK_LBRACKET) {
        begin = matching_close(tokens, begin, end) + 1;
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
        op != TK_STAR_ASSIGN && op != TK_SLASH_ASSIGN) {
        parse_error(tokens, begin - 1, "expected assignment operator");
    }
    statement->data.assign.value = parse_equality(tokens, begin, end);
    if (!statement->data.assign.value) parse_error(tokens, begin, "expected assignment value");
    return statement;
}

static Statement *parse_effect_range(TokenArray *tokens, int begin, int end) {
    int depth = 0;
    for (int i = begin; i < end; i++) {
        TokenType kind = tokens->tokens[i].type;
        if (kind == TK_LPAREN || kind == TK_LBRACKET) depth++;
        if (kind == TK_RPAREN || kind == TK_RBRACKET) depth--;
        if (depth == 0 && (kind == TK_ASSIGN || kind == TK_INC || kind == TK_DEC ||
            kind == TK_PLUS_ASSIGN || kind == TK_MINUS_ASSIGN ||
            kind == TK_STAR_ASSIGN || kind == TK_SLASH_ASSIGN)) {
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
        if (kind == TK_EOF || kind == TK_LBRACE || kind == TK_RBRACE) {
            parse_error(tokens, *pos, "expected ';' after assignment");
        }
        (*pos)++;
    }
    Statement *statement = parse_effect_range(tokens, begin, *pos);
    (*pos)++;
    return statement;
}

static ValueType parse_type(TokenArray *tokens, int *pos, int end, bool allow_void);

Statement* parse_let_statement(TokenArray* tokens, int *pos) {
    Statement *stmt = calloc(1, sizeof(*stmt));
    if (!stmt) error_at(NULL, "out of memory");
    stmt->type = STMT_LET;
    stmt->token = &tokens->tokens[*pos];
    stmt->data.let.explicit_type = stmt->token->type == TK_INT;
    if (stmt->data.let.explicit_type) {
        stmt->data.let.declared_type = parse_type(tokens, pos, (int)tokens->count, false);
    } else { (*pos)++; }
    if ((size_t)*pos >= tokens->count || tokens->tokens[*pos].type != TK_IDENT) {
        parse_error(tokens, *pos, "expected identifier in declaration");
    }
    stmt->data.let.name = copy_text(tokens->tokens[*pos].text, strlen(tokens->tokens[*pos].text));
    (*pos)++;
    int begin = *pos;
    while ((size_t)*pos < tokens->count && tokens->tokens[*pos].type != TK_SEMICOLON) {
        TokenType kind = tokens->tokens[*pos].type;
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
    (*pos)++;
    return stmt;
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
        if(tokens->tokens[*pos].type == TK_EOF ||
           tokens->tokens[*pos].type == TK_LBRACE ||
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
        if(tokens->tokens[*pos].type == TK_EOF ||
           tokens->tokens[*pos].type == TK_LBRACE ||
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
    if (tokens->tokens[*pos].type == TK_SEMICOLON) (*pos)++;
    else if ((tokens->tokens[*pos].type == TK_LET || tokens->tokens[*pos].type == TK_INT)) {
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
        if(tokens->tokens[*pos].type == TK_IDENT || tokens->tokens[*pos].type == TK_INC || tokens->tokens[*pos].type == TK_DEC){
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
        else if((tokens->tokens[*pos].type == TK_LET || tokens->tokens[*pos].type == TK_INT)){
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
            parse_error(tokens, *pos, "Expected ';' at the end of print statement");
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
    if (strcmp(token->text, "int") != 0) error_at(token, "unknown or invalid type '%s'", token->text);
    ValueType type = VALUE_INT;
    while (*pos < end && tokens->tokens[*pos].type == TK_LBRACKET) {
        (*pos)++;
        if (*pos >= end || tokens->tokens[*pos].type != TK_RBRACKET) {
            parse_error(tokens, *pos, "expected ']' in array type");
        }
        (*pos)++;
        type++;
    }
    return type;
}

static Function *parse_function(TokenArray *tokens, int *pos) {
    Function *function = calloc(1, sizeof(*function));
    if (!function) error_at(NULL, "out of memory");
    function->token = &tokens->tokens[*pos];
    function->return_type = VALUE_INT;
    (*pos)++;
    if ((size_t)*pos >= tokens->count || tokens->tokens[*pos].type != TK_IDENT) {
        parse_error(tokens, *pos, "expected function name");
    }
    function->name = tokens->tokens[(*pos)++].text;
    if ((size_t)*pos >= tokens->count || tokens->tokens[*pos].type != TK_LPAREN) {
        parse_error(tokens, *pos, "expected '(' after function name");
    }
    int close = matching_close(tokens, *pos, (int)tokens->count);
    (*pos)++;
    if (*pos < close) {
        function->params = calloc((size_t)(close - *pos), sizeof(Parameter));
        if (!function->params) error_at(NULL, "out of memory");
    }
    while (*pos < close) {
        if (tokens->tokens[*pos].type != TK_IDENT) parse_error(tokens, *pos, "expected parameter name");
        Parameter *param = &function->params[function->count++];
        param->token = &tokens->tokens[*pos];
        param->name = tokens->tokens[(*pos)++].text;
        param->type = VALUE_INT;
        if (*pos < close && tokens->tokens[*pos].type == TK_COLON) {
            (*pos)++;
            if (*pos < close && tokens->tokens[*pos].type == TK_AMP) {
                param->by_ref = true;
                (*pos)++;
            }
            param->type = parse_type(tokens, pos, close, false);
        }
        if (*pos == close) break;
        if (tokens->tokens[*pos].type != TK_COMMA) parse_error(tokens, *pos, "expected ',' between parameters");
        (*pos)++;
        if (*pos == close) parse_error(tokens, *pos, "expected parameter after ','");
    }
    *pos = close + 1;
    if ((size_t)*pos < tokens->count && tokens->tokens[*pos].type == TK_ARROW) {
        (*pos)++;
        function->return_type = parse_type(tokens, pos, (int)tokens->count, true);
    }
    function->body = parse_block(tokens, pos);
    return function;
}

Program* parse(TokenArray* tokens){
    Program* program = calloc(1, sizeof(Program));
    if (!program) error_at(NULL, "out of memory");
    program->statesments = malloc(sizeof(Statement*) * tokens->count);
    program->count = 0;
    program->capacity =tokens->count;

    int pos = 0;

    while((size_t)pos < tokens->count){
        if(tokens->tokens[pos].type == TK_FN){
            if (!program->functions) {
                program->functions = calloc(tokens->count, sizeof(Function *));
                if (!program->functions) error_at(NULL, "out of memory");
            }
            Function *function = parse_function(tokens, &pos);
            function->id = program->function_count;
            program->functions[program->function_count++] = function;
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
        else if(tokens->tokens[pos].type == TK_LET || tokens->tokens[pos].type == TK_INT){
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

