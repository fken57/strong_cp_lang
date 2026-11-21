// Serialize the analyzed program for the browser worker. Identifiers use resolved slots.
static void plan_expr(const Expr *e);

static void plan_args(Expr **args, size_t count) {
    putchar('[');
    for (size_t i = 0; i < count; ++i) {
        if (i) putchar(',');
        plan_expr(args[i]);
    }
    putchar(']');
}

static void plan_expr(const Expr *e) {
    if (!e) { fputs("null", stdout); return; }
    switch (e->type) {
    case EXPR_TEXT:
        fputs("[\"text\",", stdout); json_string(stdout, e->data.text); break;
    case EXPR_NUMBER:
        printf("[\"int\",\"%s\"", e->data.number.literal); break;
    case EXPR_VARIABLE:
        printf("[\"var\",%d", -e->data.variable.offset); break;
    case EXPR_BINARY:
        printf("[\"%s\",", token_type_name(e->data.binary.op));
        plan_expr(e->data.binary.left); putchar(','); plan_expr(e->data.binary.right); break;
    case EXPR_UNARY:
        fputs("[\"neg\",", stdout); plan_expr(e->data.unary.operand); break;
    case EXPR_ARRAY:
        printf("[\"arrayLiteral\",%d,", e->value_type); plan_args(e->data.array.items, e->data.array.count); break;
    case EXPR_CONSTRUCT:
        printf("[\"construct\",%d,", e->value_type); plan_args(e->data.construct.sizes, e->data.construct.count); break;
    case EXPR_RECORD:
        printf("[\"record\",%d,[", e->value_type);
        for (size_t i = 0; i < e->data.record.count; ++i) {
            if (i) putchar(',');
            printf("[%zu,", e->data.record.indices[i]); plan_expr(e->data.record.values[i]); putchar(']');
        }
        putchar(']'); break;
    case EXPR_FIELD:
        printf("[\"field\",%zu,", e->data.field.index); plan_expr(e->data.field.owner); break;
    case EXPR_INDEX:
        fputs("[\"index\",", stdout); plan_expr(e->data.index.array); putchar(','); plan_expr(e->data.index.index); break;
    case EXPR_CALL:
        fputs("[\"call\",", stdout);
        if (e->data.call.function) printf("%zu", e->data.call.function->id);
        else json_string(stdout, e->data.call.name);
        putchar(','); plan_expr(e->data.call.receiver);
        putchar(','); plan_args(e->data.call.args, e->data.call.count);
        printf(",%d", e->data.call.receiver ? e->data.call.receiver->value_type : 0);
        break;
    }
    putchar(']');
}

static void plan_statement(const Statement *s);

static void plan_block(Statement **statements, size_t count) {
    fputs("[\"block\"", stdout);
    for (size_t i = 0; i < count; ++i) { putchar(','); plan_statement(statements[i]); }
    putchar(']');
}

static void plan_statement(const Statement *s) {
    if (!s) { fputs("null", stdout); return; }
    switch (s->type) {
    case STMT_DECLARATIONS:
    case STMT_BLOCK:
        plan_block(s->data.block.statements, s->data.block.count); return;
    case STMT_LET:
        printf("[\"let\",%d,", -s->data.let.offset); plan_expr(s->data.let.value); break;
    case STMT_ASSIGN:
        printf("[\"assign\",\"%s\",", token_type_name(s->data.assign.op));
        plan_expr(s->data.assign.target); putchar(','); plan_expr(s->data.assign.value); break;
    case STMT_WRITE:
        fputs("[\"write\",", stdout); plan_expr(s->data.write.value); break;
    case STMT_PRINT:
        fputs("[\"print\",", stdout); plan_expr(s->data.print.expression); break;
    case STMT_EXPR:
        fputs("[\"expr\",", stdout); plan_expr(s->data.expr.expression); break;
    case STMT_RETURN:
        fputs("[\"return\",", stdout); plan_expr(s->data.return_stmt.value); break;
    case STMT_BREAK:
        fputs("[\"break\"", stdout); break;
    case STMT_IF:
        fputs("[\"if\",", stdout); plan_expr(s->data.if_stmt.condition);
        putchar(','); plan_statement(s->data.if_stmt.then_branch);
        putchar(','); plan_statement(s->data.if_stmt.else_branch); break;
    case STMT_WHILE:
        fputs("[\"loop\",null,", stdout); plan_expr(s->data.while_stmt.condition);
        fputs(",null,", stdout); plan_statement(s->data.while_stmt.body); break;
    case STMT_EACH:
        printf("[\"each\",%d,", -s->data.each.offset); plan_expr(s->data.each.values);
        putchar(','); plan_statement(s->data.each.body); break;
    case STMT_FOR:
        fputs("[\"loop\",", stdout); plan_statement(s->data.for_stmt.init);
        putchar(','); plan_expr(s->data.for_stmt.condition);
        putchar(','); plan_statement(s->data.for_stmt.step);
        putchar(','); plan_statement(s->data.for_stmt.body); break;
    }
    putchar(']');
}

static void generate_plan(const Program *p) {
    fputs("{\"records\":[", stdout);
    for (size_t i = 0; i < p->record_count; ++i) {
        if (i) putchar(',');
        putchar('[');
        for (size_t j = 0; j < p->records[i].count; ++j) {
            if (j) putchar(',');
            printf("%d", p->records[i].fields[j].type);
        }
        putchar(']');
    }
    fputs("],\"functions\":[", stdout);
    for (size_t i = 0; i < p->function_count; i++) {
        Function *f = p->functions[i];
        if (i) putchar(',');
        printf("[%zu", f->id); fputs(",[", stdout);
        for (size_t j = 0; j < f->count; j++) {
            if (j) putchar(',');
            printf("[%d,%s]", -f->params[j].offset, f->params[j].by_ref ? "true" : "false");
        }
        fputs("],", stdout); plan_statement(f->body); putchar(']');
    }
    fputs("],\"main\":", stdout); plan_block(p->statesments, p->count); putchar('}');
}
