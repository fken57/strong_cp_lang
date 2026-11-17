#include "typedef.c"
#include "util.c"
#include "lex.c"
#include "parser.c"
#include "analyze.c"
#include "generate_cpp.c"

static void destroy_expr(Expr *expr) {
    if (!expr) return;
    if (expr->type == EXPR_BINARY) {
        destroy_expr(expr->data.binary.left);
        destroy_expr(expr->data.binary.right);
    } else if (expr->type == EXPR_UNARY) {
        destroy_expr(expr->data.unary.operand);
    } else if (expr->type == EXPR_CALL) {
        destroy_expr(expr->data.call.receiver);
        for (size_t i = 0; i < expr->data.call.count; i++) destroy_expr(expr->data.call.args[i]);
        free(expr->data.call.args);
    } else if (expr->type == EXPR_ARRAY) {
        for (size_t i = 0; i < expr->data.array.count; i++) destroy_expr(expr->data.array.items[i]);
        free(expr->data.array.items);
    } else if (expr->type == EXPR_CONSTRUCT) {
        for (size_t i = 0; i < expr->data.construct.count; i++) destroy_expr(expr->data.construct.sizes[i]);
        free(expr->data.construct.sizes);
    } else if (expr->type == EXPR_INDEX) {
        destroy_expr(expr->data.index.array);
        destroy_expr(expr->data.index.index);
    }
    free(expr);
}

static void destroy_statement(Statement *stmt) {
    if (!stmt) return;
    switch (stmt->type) {
        case STMT_LET:
            free(stmt->data.let.name);
            destroy_expr(stmt->data.let.value);
            break;
        case STMT_ASSIGN:
            free(stmt->data.assign.name);
            destroy_expr(stmt->data.assign.target);
            destroy_expr(stmt->data.assign.value);
            break;
        case STMT_PRINT: destroy_expr(stmt->data.print.expression); break;
        case STMT_EXPR: destroy_expr(stmt->data.expr.expression); break;
        case STMT_RETURN: destroy_expr(stmt->data.return_stmt.value); break;
        case STMT_BLOCK:
            for (size_t i = 0; i < stmt->data.block.count; i++) {
                destroy_statement(stmt->data.block.statements[i]);
            }
            free(stmt->data.block.statements);
            break;
        case STMT_IF:
            destroy_expr(stmt->data.if_stmt.condition);
            destroy_statement(stmt->data.if_stmt.then_branch);
            if (stmt->data.if_stmt.else_branch) destroy_statement(stmt->data.if_stmt.else_branch);
            break;
        case STMT_WHILE:
            destroy_expr(stmt->data.while_stmt.condition);
            destroy_statement(stmt->data.while_stmt.body);
            break;
        case STMT_FOR:
            destroy_statement(stmt->data.for_stmt.init);
            destroy_expr(stmt->data.for_stmt.condition);
            destroy_statement(stmt->data.for_stmt.step);
            destroy_statement(stmt->data.for_stmt.body);
            break;
        default: break;
    }
    free(stmt);
}

static Source read_source(FILE *file) {
    Source source = {.capacity = 4096};
    source.data = malloc(source.capacity);
    if (!source.data) error_at(NULL, "out of memory");
    int c;
    while ((c = fgetc(file)) != EOF) {
        if (source.length + 1 >= source.capacity) {
            source.capacity *= 2;
            char *data = realloc(source.data, source.capacity);
            if (!data) error_at(NULL, "out of memory");
            source.data = data;
        }
        source.data[source.length++] = (char)c;
    }
    if (ferror(file)) error_at(NULL, "failed to read source");
    source.data[source.length] = '\0';
    return source;
}

int main(int argc, char **argv) {
    bool check = false, token_mode = false;
    const char *path = NULL, *code = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--check") == 0) check = true;
        else if (strcmp(argv[i], "--tokens") == 0) token_mode = true;
        else if (strcmp(argv[i], "--json-diagnostics") == 0) diagnostic_json = true;
        else if (strcmp(argv[i], "--code") == 0 && i + 1 < argc) code = argv[++i];
        else if (!path) path = argv[i];
        else { fputs("usage: compiler [--check|--tokens] [--json-diagnostics] FILE | --code SOURCE\n", stderr); return 2; }
    }
    if ((!path && !code) || (path && code)) {
        fputs("usage: compiler [--check|--tokens] [--json-diagnostics] FILE | --code SOURCE\n", stderr);
        return 2;
    }
    Source source;
    if (code) {
        source.length = strlen(code);
        source.capacity = source.length + 1;
        source.data = copy_text(code, source.length);
    } else {
        FILE *file = strcmp(path, "-") == 0 ? stdin : fopen(path, "rb");
        if (!file) { perror(path); return 1; }
        source = read_source(file);
        if (file != stdin) fclose(file);
    }
    lex_allow_invalid = token_mode;
    TokenArray *tokens = lex(&source);
    if (token_mode) {
        fputc('[', stdout);
        for (size_t i = 0; i < tokens->count; i++) {
            Token *token = &tokens->tokens[i];
            if (i) fputc(',', stdout);
            fputs("{\"kind\":", stdout);
            json_string(stdout, token_type_name(token->type));
            fputs(",\"text\":", stdout);
            json_string(stdout, token->text);
            printf(",\"offset\":%zu,\"length\":%zu,\"line\":%zu,\"column\":%zu}",
                   token->offset, token->length, token->line, token->column);
        }
        fputs("]\n", stdout);
        for (size_t i = 0; i < tokens->count; i++) free(tokens->tokens[i].text);
        free(tokens->tokens);
        free(tokens);
        free(source.data);
        return ferror(stdout) ? 1 : 0;
    }
    Program *program = parse(tokens);
    analyze(program);
    if (!check) generate_cpp(program);
    for (size_t i = 0; i < program->count; i++) destroy_statement(program->statesments[i]);
    free(program->statesments);
    for (size_t i = 0; i < program->function_count; i++) {
        Function *function = program->functions[i];
        destroy_statement(function->body);
        free(function->params);
        free(function);
    }
    free(program->functions);
    free(program);
    for (size_t i = 0; i < tokens->count; i++) free(tokens->tokens[i].text);
    free(tokens->tokens);
    free(tokens);
    free(source.data);
    return ferror(stdout) ? 1 : 0;
}
