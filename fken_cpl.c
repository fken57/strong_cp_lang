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
    }
    free(expr);
}

static void destroy_statement(Statement *stmt) {
    switch (stmt->type) {
        case STMT_LET:
            free(stmt->data.let.name);
            destroy_expr(stmt->data.let.value);
            break;
        case STMT_ASSIGN:
            free(stmt->data.assign.name);
            destroy_expr(stmt->data.assign.value);
            break;
        case STMT_PRINT: destroy_expr(stmt->data.print.expression); break;
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
    Source source;
    if (argc == 3 && strcmp(argv[1], "--code") == 0) {
        source.length = strlen(argv[2]);
        source.capacity = source.length + 1;
        source.data = copy_text(argv[2], source.length);
    } else if (argc == 2) {
        FILE *file = strcmp(argv[1], "-") == 0 ? stdin : fopen(argv[1], "rb");
        if (!file) { perror(argv[1]); return 1; }
        source = read_source(file);
        if (file != stdin) fclose(file);
    } else {
        fputs("usage: compiler FILE | --code SOURCE\n", stderr);
        return 2;
    }
    TokenArray *tokens = lex(&source);
    Program *program = parse(tokens);
    analyze(program);
    generate_cpp(program);
    for (size_t i = 0; i < program->count; i++) destroy_statement(program->statesments[i]);
    free(program->statesments);
    free(program);
    for (size_t i = 0; i < tokens->count; i++) free(tokens->tokens[i].text);
    free(tokens->tokens);
    free(tokens);
    free(source.data);
    return ferror(stdout) ? 1 : 0;
}
