#include "util.c"

static const struct {
    const char *name;
    size_t min_args, max_args;
    ValueType argument, result;
    bool can_shadow;
} builtins[] = {
    {"gcd", 2, 2, VALUE_INT, VALUE_INT, true},
    {"lcm", 2, 2, VALUE_INT, VALUE_INT, true},
    {"pow_mod", 3, 3, VALUE_INT, VALUE_INT, true},
    {"inv_mod", 2, 2, VALUE_INT, VALUE_INT, true},
    {"read_string", 0, 0, VALUE_INT, VALUE_STRING, false},
    {"read_char", 0, 0, VALUE_INT, VALUE_CHAR, false},
    {"read_int", 0, 0, VALUE_INT, VALUE_INT, false},
    {"read_vec", 1, 1, VALUE_INT, VALUE_ARRAY, false},
    {"array", 1, 2, VALUE_INT, VALUE_ARRAY, false},
    {"len", 1, 1, VALUE_ARRAY, VALUE_INT, false},
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

static void convert_residue(Expr *expr, ValueType type) {
    Expr *value = malloc(sizeof(*value));
    if (!value) error_at(NULL, "out of memory");
    *value = *expr;
    expr->type = EXPR_RESIDUE;
    expr->value_type = type;
    expr->data.residue.type = type;
    expr->data.residue.value = value;
}

static void require_type(Expr *expr, ValueType expected) {
    if (mint_type(expected) && expr->value_type == VALUE_INT) convert_residue(expr, expected);
    if (expr->value_type != expected) {
        if (type_base(expected) >= VALUE_EDGE || type_base(expr->value_type) >= VALUE_EDGE)
            error_at(expr->token, "incompatible value type");
        if (expected > VALUE_INT) error_at(expr->token, "expected array expression with %d dimension(s)", expected);
        error_at(expr->token, "expected integer expression");
    }
}

static void expect_expr(Expr *expr, SymbolTable *symbols, ValueType expected) {
    analyze_expr_as(expr, symbols, expected);
    require_type(expr, expected);
}

static void analyze_expr(Expr *expr, SymbolTable *symbols) {
    analyze_expr_as(expr, symbols, -2);
}

static bool writable(const Expr *expr, Program *program) {
    while (expr->type == EXPR_FIELD || expr->type == EXPR_INDEX) {
        if (expr->type == EXPR_FIELD) expr = expr->data.field.owner;
        else {
            expr = expr->data.index.array;
            if (graph_type(expr->value_type) ||
                (assoc_type(expr->value_type) && assoc_info(program, expr->value_type).kind >= 2)) return false;
        }
    }
    return expr->type == EXPR_VARIABLE;
}

static Record structure_fields(Program *program, ValueType type) {
    static Field edge[] = {{"to", VALUE_INT}, {"cost", VALUE_INT}, {"id", VALUE_INT}};
    static Field pair[] = {{"first", VALUE_INT}, {"second", VALUE_INT}};
    if (type == VALUE_EDGE) return (Record){.fields = edge, .count = 3};
    static Field diameter[] = {{"dist", VALUE_INT}, {"one", VALUE_INT}, {"two", VALUE_INT}};
    if (type == VALUE_DIAMETER) return (Record){.fields = diameter, .count = 3};
    if (type == VALUE_PAIR) return (Record){.fields = pair, .count = 2};
    if (type >= VALUE_RECORD && type < VALUE_MINT && !type_rank(type))
        return program->records[(type - VALUE_RECORD) / TYPE_STRIDE];
    return (Record){0};
}

static void analyze_expr_as(Expr *expr, SymbolTable *symbols, ValueType hint) {
    expr->value_type = VALUE_INT;
    switch (expr->type) {
        case EXPR_NUMBER: break;
        case EXPR_RESIDUE:
            expr->value_type = expr->data.residue.type;
            break;
        case EXPR_TEXT:
            expr->value_type = expr->token->type == TK_CHAR ? VALUE_CHAR : VALUE_STRING;
            break;
        case EXPR_VARIABLE: {
            Symbol *symbol = find_symbol(symbols, expr->data.variable.var_name, expr->token);
            expr->data.variable.offset = symbol->offset;
            expr->value_type = symbol->value_type;
            break;
        }
        case EXPR_BINARY: {
            Expr *left = expr->data.binary.left;
            analyze_expr(left, symbols);
            ValueType type = left->value_type;
            TokenType op = expr->data.binary.op;
            Expr *right = expr->data.binary.right;
            analyze_expr(right, symbols);
            if (mint_type(type) || mint_type(right->value_type)) {
                if (!mint_type(type)) type = right->value_type;
                require_type(left, type);
                require_type(right, type);
                if (op == TK_PLUS || op == TK_MINUS || op == TK_STAR || op == TK_SLASH)
                    expr->value_type = type;
                else if (op != TK_EQ && op != TK_NE)
                    error_at(expr->token, "modular values support arithmetic and equality");
                break;
            }
            bool compares = op == TK_EQ || op == TK_NE || op == TK_LT || op == TK_LE || op == TK_GT || op == TK_GE;
            if (type != VALUE_INT && !((type == VALUE_STRING || type == VALUE_CHAR) && compares) &&
                !(type == VALUE_STRING && op == TK_PLUS))
                error_at(expr->token, "operator does not support this operand type");
            require_type(right, type);
            if (type == VALUE_STRING && op == TK_PLUS) expr->value_type = VALUE_STRING;
            break;
        }
        case EXPR_UNARY:
            analyze_expr(expr->data.unary.operand, symbols);
            expr->value_type = expr->data.unary.operand->value_type;
            if (expr->value_type != VALUE_INT && !mint_type(expr->value_type))
                error_at(expr->token, "unary minus requires an integer or modular value");
            break;
        case EXPR_CONSTRUCT:
            expr->value_type = expr->data.construct.type;
            if (graph_type(expr->value_type) && expr->data.construct.count != 1)
                error_at(expr->token, "graph constructor requires a vertex count");
            if (!graph_type(expr->value_type) && expr->data.construct.count && expr->data.construct.count != (size_t)type_rank(expr->value_type)) {
                error_at(expr->token, "constructor requires one size per vector dimension");
            }
            for (size_t i = 0; i < expr->data.construct.count; i++) {
                expect_expr(expr->data.construct.sizes[i], symbols, VALUE_INT);
            }
            break;
        case EXPR_ARRAY: {
            ValueType element = type_rank(hint) ? hint - 1 : VALUE_INT;
            if (!type_rank(hint)) {
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
        case EXPR_RECORD: {
            expr->value_type = expr->data.record.type;
            Record *r = &symbols->program->records[(expr->value_type - VALUE_RECORD)/TYPE_STRIDE];
            for (size_t i = 0; i < expr->data.record.count; ++i)
                expect_expr(expr->data.record.values[i], symbols, r->fields[expr->data.record.indices[i]].type);
            break;
        }
        case EXPR_FIELD: {
            analyze_expr(expr->data.field.owner, symbols);
            Expr *receiver = expr->data.field.owner;
            ValueType owner = receiver->value_type;
            if (expr->data.field.name && !strcmp(expr->data.field.name, "diameter") && owner == VALUE_GRAPH) {
                expr->type = EXPR_CALL;
                memset(&expr->data.call, 0, sizeof(expr->data.call));
                expr->data.call.name = "diameter";
                expr->data.call.receiver = receiver;
                expr->value_type = VALUE_DIAMETER;
                return;
            }
            Record record = structure_fields(symbols->program, owner);
            if (!record.name && !record.fields) error_at(expr->token, "field access requires a structure");
            if (!expr->data.field.name) {
                const Field *field = &record.fields[expr->data.field.index];
                expr->data.field.name = field->name;
                expr->value_type = field->type;
                return;
            }
            for (size_t i = 0; i < record.count; ++i) {
                if (!strcmp(expr->data.field.name, record.fields[i].name)) {
                    expr->data.field.index = i;
                    expr->value_type = record.fields[i].type;
                    return;
                }
            }
            error_at(expr->token, "unknown field '%s'", expr->data.field.name);
        }
        case EXPR_INDEX:
            analyze_expr(expr->data.index.array, symbols);
            if (assoc_type(expr->data.index.array->value_type)) {
                AssocType a = assoc_info(symbols->program, expr->data.index.array->value_type);
                expect_expr(expr->data.index.index, symbols, a.key);
                expr->value_type = a.kind >= 4 ? VALUE_INT : a.value + (a.kind >= 2);
                break;
            }
            if (!type_rank(expr->data.index.array->value_type) && !graph_type(expr->data.index.array->value_type)) error_at(expr->token, "cannot index a non-array value");
            expect_expr(expr->data.index.index, symbols, VALUE_INT);
            expr->value_type = graph_type(expr->data.index.array->value_type) ? VALUE_EDGE + 1 : expr->data.index.array->value_type - 1;
            break;
        case EXPR_CALL: {
            expr->data.call.function = NULL;
            if (expr->data.call.receiver) {
                Expr *receiver = expr->data.call.receiver;
                analyze_expr(receiver, symbols);
                if (assoc_type(receiver->value_type)) {
                    AssocType a = assoc_info(symbols->program, receiver->value_type);
                    const char *name = expr->data.call.name;
                    size_t n = expr->data.call.count;
                    if (!strcmp(name, "size") && n == 0) return;
                    if (!strcmp(name, "keys") && n == 0) { expr->value_type = a.key + 1; return; }
                    if ((!strcmp(name, "count") || !strcmp(name, "contains") || !strcmp(name, "erase")) && n == 1) {
                        if (!strcmp(name, "erase") && !writable(receiver, symbols->program))
                            error_at(expr->token, "erase requires a writable container");
                        expect_expr(expr->data.call.args[0], symbols, a.key);
                        return;
                    }
                    if (!strcmp(name, "insert") && n == (a.kind < 4 ? 2u : 1u)) {
                        if (!writable(receiver, symbols->program)) error_at(expr->token, "insert requires a writable container");
                        expect_expr(expr->data.call.args[0], symbols, a.key);
                        if (a.kind < 4) expect_expr(expr->data.call.args[1], symbols, a.value);
                        expr->value_type = VALUE_VOID;
                        return;
                    }
                    error_at(expr->token, "invalid associative container method '%s'", name);
                }
                if (mint_type(receiver->value_type)) {
                    const char *name = expr->data.call.name;
                    if (!strcmp(name, "val") && !expr->data.call.count) return;
                    if (!strcmp(name, "pow") && expr->data.call.count == 1) {
                        expect_expr(expr->data.call.args[0], symbols, VALUE_INT);
                        expr->value_type = receiver->value_type;
                        return;
                    }
                    error_at(expr->token, "expected val() or pow(exponent)");
                }
                if (receiver->value_type >= VALUE_RECORD && receiver->value_type < VALUE_MINT && !type_rank(receiver->value_type)) {
                    for (size_t i = 0; i < symbols->program->function_count; ++i) {
                        Function *f = symbols->program->functions[i];
                        if (f->owner != receiver->value_type || strcmp(f->name, expr->data.call.name)) continue;
                        if (expr->data.call.count + 1 != f->count) error_at(expr->token, "wrong number of method arguments");
                        for (size_t j = 1; j < f->count; ++j) {
                            Expr *arg = expr->data.call.args[j-1];
                            expect_expr(arg, symbols, f->params[j].type);
                            if (f->params[j].by_ref && arg->type != EXPR_VARIABLE)
                                error_at(arg->token, "reference argument must be a variable");
                        }
                        expr->data.call.function = f;
                        expr->value_type = f->return_type;
                        return;
                    }
                    error_at(expr->token, "unknown structure method '%s'", expr->data.call.name);
                }
                if (graph_type(receiver->value_type)) {
                    const char *name = expr->data.call.name;
                    size_t n = expr->data.call.count;
                    if (!strcmp(name, "diameter") && receiver->value_type == VALUE_GRAPH && n == 0) {
                        expr->value_type = VALUE_DIAMETER;
                        return;
                    }
                    if ((!strcmp(name, "bfs") || !strcmp(name, "dijkstra")) && n == 1) {
                        expect_expr(expr->data.call.args[0], symbols, VALUE_INT);
                        expr->value_type = VALUE_ARRAY;
                        return;
                    }
                    if (!strcmp(name, "components") && receiver->value_type == VALUE_GRAPH && n == 0) {
                        expr->value_type = VALUE_ARRAY;
                        return;
                    }
                    if (!strcmp(name, "add")) {
                        if (!writable(receiver, symbols->program) || n < 2 || n > 3)
                            error_at(expr->token, "graph.add requires a writable graph and 2 or 3 arguments");
                        for (size_t i = 0; i < n; ++i) expect_expr(expr->data.call.args[i], symbols, VALUE_INT);
                    } else if (n || (strcmp(name, "size") && strcmp(name, "edge_count"))) {
                        error_at(expr->token, "invalid graph method '%s'", name);
                    }
                    expr->value_type = VALUE_INT;
                    return;
                }
                if (!type_rank(receiver->value_type)) error_at(expr->token, "vector methods require an array receiver");
                const char *name = expr->data.call.name;
                size_t min_args = 0, max_args = 0;
                ValueType result = VALUE_VOID;
                bool mutates = true;
                bool bound = !strcmp(name, "at_least") || !strcmp(name, "more_than") ||
                             !strcmp(name, "at_most") || !strcmp(name, "less_than");
                if (bound || !strcmp(name, "run_length") || !strcmp(name, "sort")) {
                    if (receiver->value_type != VALUE_ARRAY)
                        error_at(expr->token, "method '%s' requires int[]", name);
                    if (bound) { min_args = max_args = 1; result = VALUE_INT; }
                    if (!strcmp(name, "run_length")) result = VALUE_PAIR + 1;
                    mutates = result == VALUE_VOID;
                }
                else if (!strcmp(name, "push_back") && receiver->value_type == VALUE_STRING) min_args = max_args = 1;
                else if (strcmp(name, "push") == 0) min_args = max_args = 1;
                else if (strcmp(name, "resize") == 0) { min_args = 1; max_args = 2; }
                else if (strcmp(name, "pop") == 0) result = receiver->value_type - 1;
                else if (strcmp(name, "back") == 0) { result = receiver->value_type - 1; mutates = false; }
                else if (strcmp(name, "size") == 0) { result = VALUE_INT; mutates = false; }
                else if (strcmp(name, "clear") != 0) error_at(expr->token, "unknown vector method '%s'", name);
                if (expr->data.call.count < min_args || expr->data.call.count > max_args) {
                    error_at(expr->token, "wrong number of arguments to vector method '%s'", name);
                }
                if (mutates && !writable(receiver, symbols->program)) error_at(expr->token, "mutating method receiver must be a variable or indexed variable");
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
                if (function->owner || strcmp(expr->data.call.name, function->name) != 0) continue;
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
                        if (!type_rank(expr->data.call.args[j]->value_type)) error_at(expr->token, "len requires an array");
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
            if (stmt->data.let.binding_count) {
                Record record = structure_fields(symbols->program, stmt->data.let.value->value_type);
                if (!record.name && !record.fields)
                    error_at(stmt->token, "structured binding requires a structure or pair");
                if (stmt->data.let.binding_count != record.count)
                    error_at(stmt->token, "structured binding requires %zu names", record.count);
            }
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
            if (!writable(stmt->data.assign.target, symbols->program)) error_at(stmt->token, "assignment target is read-only");
            ValueType type = stmt->data.assign.target->value_type;
            TokenType op = stmt->data.assign.op;
            if (stmt->data.assign.stream_input) {
                Expr *read = stmt->data.assign.value;
                if (type == VALUE_STRING) read->data.call.name = "read_string";
                else if (type == VALUE_CHAR) read->data.call.name = "read_char";
                else if (type != VALUE_INT && !mint_type(type)) error_at(stmt->token, "cin requires an int, char or string destination");
            }
            if (op != TK_ASSIGN && type != VALUE_INT && !mint_type(type) && !(op == TK_PLUS_ASSIGN && type == VALUE_STRING)) {
                error_at(stmt->token, "updates require an integer target");
            }
            if (op == TK_PERCENT_ASSIGN && mint_type(type)) error_at(stmt->token, "remainder requires integers");
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
        case STMT_WRITE: {
            analyze_expr(stmt->data.write.value, symbols);
            ValueType type = stmt->data.write.value->value_type;
            if (type != VALUE_INT && type != VALUE_CHAR && type != VALUE_STRING && !mint_type(type))
                error_at(stmt->token, "cout requires an int, char or string");
            break;
        }
        case STMT_PRINT:
            analyze_expr(stmt->data.print.expression, symbols);
            if (stmt->data.print.expression->value_type == VALUE_VOID) error_at(stmt->token, "cannot print a void result");
            if (type_base(stmt->data.print.expression->value_type) != VALUE_INT &&
                type_base(stmt->data.print.expression->value_type) != VALUE_CHAR && !mint_type(stmt->data.print.expression->value_type))
                error_at(stmt->token, "print requires integers, text or arrays of those types");
            break;
        case STMT_DECLARATIONS:
            for (size_t i = 0; i < stmt->data.block.count; ++i) analyze_statement(stmt->data.block.statements[i], offset, symbols);
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
        case STMT_EACH: {
            size_t count = symbols->count, scope = symbols->scope_start;
            expect_expr(stmt->data.each.values, symbols, stmt->data.each.type + 1);
            symbols->scope_start = count;
            stmt->data.each.offset = declare_symbol(symbols, offset, stmt->data.each.name, stmt->data.each.type, stmt->token);
            ++symbols->loop_depth;
            analyze_block(stmt->data.each.body, offset, symbols);
            --symbols->loop_depth;
            symbols->count = count; symbols->scope_start = scope;
            break;
        }
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
            if (function->owner == program->functions[j]->owner && strcmp(function->name, program->functions[j]->name) == 0) {
                error_at(function->token, "duplicate function '%s'", function->name);
            }
        }
        for (size_t j = 0; j < sizeof(builtins) / sizeof(*builtins); j++) {
            if (!function->owner && !builtins[j].can_shadow && strcmp(function->name, builtins[j].name) == 0) {
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
