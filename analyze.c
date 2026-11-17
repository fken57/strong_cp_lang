#include "util.c"

static const struct {
    const char *name;
    size_t min_args, max_args;
    ValueType argument, result;
} builtins[] = {
    {"read_int", 0, 0, VALUE_INT, VALUE_INT},
    {"read_vec", 1, 1, VALUE_INT, VALUE_ARRAY},
    {"array", 1, 2, VALUE_INT, VALUE_ARRAY},
    {"len", 1, 1, VALUE_ARRAY, VALUE_INT},
};

static void analyze_expr(Expr *expr, SymbolTable *symbols);
static void analyze_expr_as(Expr *expr, SymbolTable *symbols, ValueType hint);
static void analyze_statement(Statement *stmt, int *offset, SymbolTable *symbols);

static Symbol *find_symbol(SymbolTable *symbols, const char *name, Token *token) {
    for (size_t i = symbols->count; i-- > 0;) {
        if (strcmp(symbols->symbols[i].name, name) == 0) return &symbols->symbols[i];
    }
    error_at(token, "undefined variable '%s'", name);
}

static void expect_expr(Expr *expr, SymbolTable *symbols, ValueType expected) {
    analyze_expr_as(expr, symbols, expected);
    if (expr->value_type != expected) {
        if (expected > VALUE_INT) error_at(expr->token, "expected array expression with %d dimension(s)", expected);
        error_at(expr->token, "expected integer expression");
    }
}

static void analyze_expr(Expr *expr, SymbolTable *symbols) {
    analyze_expr_as(expr, symbols, -2);
}

static void analyze_expr_as(Expr *expr, SymbolTable *symbols, ValueType hint) {
    expr->value_type = VALUE_INT;
    switch (expr->type) {
        case EXPR_NUMBER: break;
        case EXPR_VARIABLE: {
            Symbol *symbol = find_symbol(symbols, expr->data.variable.var_name, expr->token);
            expr->data.variable.offset = symbol->offset;
            expr->value_type = symbol->value_type;
            break;
        }
        case EXPR_BINARY:
            expect_expr(expr->data.binary.left, symbols, VALUE_INT);
            expect_expr(expr->data.binary.right, symbols, VALUE_INT);
            break;
        case EXPR_UNARY:
            expect_expr(expr->data.unary.operand, symbols, VALUE_INT);
            break;
        case EXPR_CONSTRUCT:
            expr->value_type = expr->data.construct.type;
            if (expr->data.construct.count && expr->data.construct.count != (size_t)expr->value_type) {
                error_at(expr->token, "constructor requires one size per vector dimension");
            }
            for (size_t i = 0; i < expr->data.construct.count; i++) {
                expect_expr(expr->data.construct.sizes[i], symbols, VALUE_INT);
            }
            break;
        case EXPR_ARRAY: {
            ValueType element = hint > VALUE_INT ? hint - 1 : VALUE_INT;
            if (hint <= VALUE_INT) {
                for (size_t i = 0; i < expr->data.array.count; i++) {
                    analyze_expr(expr->data.array.items[i], symbols);
                    if (expr->data.array.items[i]->value_type > element) element = expr->data.array.items[i]->value_type;
                }
            }
            expr->value_type = element + 1;
            // Empty literals take their dimension from their declaration, argument, or sibling.
            for (size_t i = 0; i < expr->data.array.count; i++) {
                expect_expr(expr->data.array.items[i], symbols, element);
            }
            break;
        }
        case EXPR_INDEX:
            analyze_expr(expr->data.index.array, symbols);
            if (expr->data.index.array->value_type <= VALUE_INT) error_at(expr->token, "cannot index a non-array value");
            expect_expr(expr->data.index.index, symbols, VALUE_INT);
            expr->value_type = expr->data.index.array->value_type - 1;
            break;
        case EXPR_CALL: {
            expr->data.call.function = NULL;
            if (expr->data.call.receiver) {
                Expr *receiver = expr->data.call.receiver;
                analyze_expr(receiver, symbols);
                if (receiver->value_type <= VALUE_INT) error_at(expr->token, "vector methods require an array receiver");
                const char *name = expr->data.call.name;
                size_t min_args = 0, max_args = 0;
                ValueType result = VALUE_VOID;
                bool mutates = true;
                if (strcmp(name, "push") == 0) min_args = max_args = 1;
                else if (strcmp(name, "resize") == 0) { min_args = 1; max_args = 2; }
                else if (strcmp(name, "pop") == 0) result = receiver->value_type - 1;
                else if (strcmp(name, "back") == 0) { result = receiver->value_type - 1; mutates = false; }
                else if (strcmp(name, "size") == 0) { result = VALUE_INT; mutates = false; }
                else if (strcmp(name, "clear") != 0) error_at(expr->token, "unknown vector method '%s'", name);
                if (expr->data.call.count < min_args || expr->data.call.count > max_args) {
                    error_at(expr->token, "wrong number of arguments to vector method '%s'", name);
                }
                if (mutates && !lvalue_root(receiver)) error_at(expr->token, "mutating method receiver must be a variable or indexed variable");
                for (size_t i = 0; i < expr->data.call.count; i++) {
                    ValueType expected = strcmp(name, "resize") == 0 && i == 0 ? VALUE_INT : receiver->value_type - 1;
                    expect_expr(expr->data.call.args[i], symbols, expected);
                }
                expr->value_type = result;
                return;
            }
            // ponytail: linear function lookup; index names if large files make analysis slow.
            for (size_t i = 0; i < symbols->program->function_count; i++) {
                Function *function = symbols->program->functions[i];
                if (strcmp(expr->data.call.name, function->name) != 0) continue;
                if (expr->data.call.count != function->count) {
                    error_at(expr->token, "wrong number of arguments to '%s'", function->name);
                }
                for (size_t j = 0; j < function->count; j++) {
                    Expr *arg = expr->data.call.args[j];
                    expect_expr(arg, symbols, function->params[j].type);
                    if (function->params[j].by_ref && arg->type != EXPR_VARIABLE) {
                        error_at(arg->token, "reference argument must be a variable");
                    }
                }
                expr->data.call.function = function;
                expr->value_type = function->return_type;
                return;
            }
            for (size_t i = 0; i < sizeof(builtins) / sizeof(*builtins); i++) {
                if (strcmp(expr->data.call.name, builtins[i].name) != 0) continue;
                size_t count = expr->data.call.count;
                if (count < builtins[i].min_args || count > builtins[i].max_args) {
                    error_at(expr->token, "wrong number of arguments to '%s'",
                             expr->data.call.name);
                }
                for (size_t j = 0; j < count; j++) {
                    if (strcmp(expr->data.call.name, "len") == 0) {
                        analyze_expr(expr->data.call.args[j], symbols);
                        if (expr->data.call.args[j]->value_type <= VALUE_INT) error_at(expr->token, "len requires an array");
                    } else expect_expr(expr->data.call.args[j], symbols, builtins[i].argument);
                }
                expr->value_type = builtins[i].result;
                return;
            }
            error_at(expr->token, "unknown function '%s'", expr->data.call.name);
        }
        default: error_at(expr->token, "unsupported expression");
    }
}

static int declare_symbol(SymbolTable *symbols, int *offset, char *name,
                          ValueType type, Token *token) {
    for (size_t i = symbols->scope_start; i < symbols->count; i++) {
        if (strcmp(symbols->symbols[i].name, name) == 0) {
            error_at(token, "variable '%s' is already declared in this scope", name);
        }
    }
    if (symbols->count == symbols->capacity) {
        symbols->capacity *= 2;
        Symbol *data = realloc(symbols->symbols, sizeof(Symbol) * symbols->capacity);
        if (!data) error_at(NULL, "out of memory");
        symbols->symbols = data;
    }
    *offset -= 8;
    symbols->symbols[symbols->count++] = (Symbol){name, *offset, type};
    return *offset;
}

static void analyze_block(Statement *stmt, int *offset, SymbolTable *symbols) {
    size_t saved_count = symbols->count, saved_scope = symbols->scope_start;
    symbols->scope_start = saved_count;
    for (size_t i = 0; i < stmt->data.block.count; i++) {
        analyze_statement(stmt->data.block.statements[i], offset, symbols);
    }
    symbols->count = saved_count;
    symbols->scope_start = saved_scope;
}

static void analyze_statement(Statement *stmt, int *offset, SymbolTable *symbols) {
    switch (stmt->type) {
        case STMT_LET:
            if (stmt->data.let.explicit_type) expect_expr(stmt->data.let.value, symbols, stmt->data.let.declared_type);
            else analyze_expr(stmt->data.let.value, symbols);
            if (stmt->data.let.value->value_type == VALUE_VOID) error_at(stmt->token, "cannot store a void result");
            stmt->data.let.offset = declare_symbol(
                symbols, offset, stmt->data.let.name, stmt->data.let.value->value_type,
                stmt->token);
            break;
        case STMT_ASSIGN: {
            Symbol *symbol = find_symbol(symbols, stmt->data.assign.name, stmt->token);
            stmt->data.assign.offset = symbol->offset;
            if (!stmt->data.assign.target) {
                Expr *target = calloc(1, sizeof(*target));
                if (!target) error_at(NULL, "out of memory");
                target->type = EXPR_VARIABLE;
                target->token = stmt->token;
                target->data.variable.var_name = stmt->data.assign.name;
                stmt->data.assign.target = target;
            }
            analyze_expr(stmt->data.assign.target, symbols);
            ValueType type = stmt->data.assign.target->value_type;
            TokenType op = stmt->data.assign.op;
            if (op != TK_ASSIGN && type != VALUE_INT) {
                error_at(stmt->token, "updates require an integer target");
            }
            if (op != TK_INC && op != TK_DEC) {
                expect_expr(stmt->data.assign.value, symbols, type);
            }
            break;
        }
        case STMT_EXPR:
            analyze_expr(stmt->data.expr.expression, symbols);
            break;
        case STMT_BREAK:
            if (!symbols->loop_depth) error_at(stmt->token, "break is only allowed inside a loop");
            break;
        case STMT_RETURN:
            if (!symbols->function) error_at(stmt->token, "return is only allowed inside a function");
            if (symbols->function->return_type == VALUE_VOID) {
                if (stmt->data.return_stmt.value) error_at(stmt->token, "void function cannot return a value");
            } else {
                if (!stmt->data.return_stmt.value) error_at(stmt->token, "expected return value");
                expect_expr(stmt->data.return_stmt.value, symbols, symbols->function->return_type);
            }
            break;
        case STMT_PRINT:
            analyze_expr(stmt->data.print.expression, symbols);
            if (stmt->data.print.expression->value_type == VALUE_VOID) error_at(stmt->token, "cannot print a void result");
            break;
        case STMT_BLOCK:
            analyze_block(stmt, offset, symbols);
            break;
        case STMT_IF:
            expect_expr(stmt->data.if_stmt.condition, symbols, VALUE_INT);
            analyze_block(stmt->data.if_stmt.then_branch, offset, symbols);
            if (stmt->data.if_stmt.else_branch) {
                analyze_block(stmt->data.if_stmt.else_branch, offset, symbols);
            }
            break;
        case STMT_WHILE:
            expect_expr(stmt->data.while_stmt.condition, symbols, VALUE_INT);
            symbols->loop_depth++;
            analyze_block(stmt->data.while_stmt.body, offset, symbols);
            symbols->loop_depth--;
            break;
        case STMT_FOR: {
            size_t saved_count = symbols->count, saved_scope = symbols->scope_start;
            symbols->scope_start = saved_count;
            if (stmt->data.for_stmt.init) analyze_statement(stmt->data.for_stmt.init, offset, symbols);
            if (stmt->data.for_stmt.condition) expect_expr(stmt->data.for_stmt.condition, symbols, VALUE_INT);
            if (stmt->data.for_stmt.step) analyze_statement(stmt->data.for_stmt.step, offset, symbols);
            symbols->loop_depth++;
            analyze_block(stmt->data.for_stmt.body, offset, symbols);
            symbols->loop_depth--;
            symbols->count = saved_count;
            symbols->scope_start = saved_scope;
            break;
        }
        default: error_at(stmt->token, "unsupported statement");
    }
}

/* Conservative return check: loops may run zero times or fail to terminate. */
static bool statement_returns(const Statement *stmt) {
    if (stmt->type == STMT_RETURN) return true;
    if (stmt->type == STMT_BLOCK) {
        for (size_t i = 0; i < stmt->data.block.count; i++) {
            if (statement_returns(stmt->data.block.statements[i])) return true;
        }
    }
    if (stmt->type == STMT_IF && stmt->data.if_stmt.else_branch) {
        return statement_returns(stmt->data.if_stmt.then_branch) &&
               statement_returns(stmt->data.if_stmt.else_branch);
    }
    return false;
}

static void analyze_function(Program *program, Function *function) {
    int offset = 0;
    SymbolTable symbols = {.capacity = 8, .program = program, .function = function};
    symbols.symbols = malloc(sizeof(Symbol) * symbols.capacity);
    if (!symbols.symbols) error_at(NULL, "out of memory");
    for (size_t i = 0; i < function->count; i++) {
        Parameter *param = &function->params[i];
        param->offset = declare_symbol(&symbols, &offset, param->name, param->type, param->token);
    }
    for (size_t i = 0; i < function->body->data.block.count; i++) {
        analyze_statement(function->body->data.block.statements[i], &offset, &symbols);
    }
    if (function->return_type != VALUE_VOID && !statement_returns(function->body)) {
        error_at(function->token, "function '%s' must return a value on every path", function->name);
    }
    free(symbols.symbols);
}

void analyze(Program *program) {
    /* All signatures are available before bodies, enabling forward and mutual recursion. */
    for (size_t i = 0; i < program->function_count; i++) {
        Function *function = program->functions[i];
        for (size_t j = 0; j < i; j++) {
            if (strcmp(function->name, program->functions[j]->name) == 0) {
                error_at(function->token, "duplicate function '%s'", function->name);
            }
        }
        for (size_t j = 0; j < sizeof(builtins) / sizeof(*builtins); j++) {
            if (strcmp(function->name, builtins[j].name) == 0) {
                error_at(function->token, "cannot redefine builtin '%s'", function->name);
            }
        }
    }
    for (size_t i = 0; i < program->function_count; i++) {
        analyze_function(program, program->functions[i]);
    }
    int offset = 0;
    SymbolTable symbols = {.capacity = 8, .program = program};
    symbols.symbols = malloc(sizeof(Symbol) * symbols.capacity);
    if (!symbols.symbols) error_at(NULL, "out of memory");
    for (size_t i = 0; i < program->count; i++) {
        analyze_statement(program->statesments[i], &offset, &symbols);
    }
    program->required_size = -offset;
    free(symbols.symbols);
}
