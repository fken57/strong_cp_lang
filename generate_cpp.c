#include "util.c"
#include <errno.h>

static const char cpp_runtime[] =
#include "runtime.inc"
;

static const char *cpp_type(ValueType type) {
    if (type == VALUE_VOID) return "void";
    if (type == VALUE_CHAR) return "char";
    if (type == VALUE_STRING) return "std::string";
    if (type == VALUE_INT) return "fken::Int";
    if (type == VALUE_ARRAY) return "fken::Array";
    static char name[64];
    char element[40];
    switch (type_base(type)) {
        case VALUE_CHAR: strcpy(element, "char"); break;
        case VALUE_INT: strcpy(element, "fken::Int"); break;
        case VALUE_DIAMETER: strcpy(element, "fken::Diameter"); break;
        case VALUE_PAIR: strcpy(element, "fken::IntPair"); break;
        case VALUE_EDGE: strcpy(element, "fken::Edge"); break;
        case VALUE_GRAPH: strcpy(element, "fken::Graph<false>"); break;
        case VALUE_DIGRAPH: strcpy(element, "fken::Graph<true>"); break;
        default: snprintf(element, sizeof(element), "fken_record%d", (type_base(type)-VALUE_RECORD)/TYPE_STRIDE);
    }
    if (type_rank(type)) snprintf(name, sizeof(name), "fken::Vec<%d,%s>", type_rank(type), element);
    else snprintf(name, sizeof(name), "%s", element);
    return name;
}

static void cpp_expr(const Expr *expr);

static const Function *current_function;

static void cpp_variable(const Expr *target) {
    printf("fken_v%d", -target->data.variable.offset);
    if (current_function) {
        for (size_t i = 0; i < current_function->count; ++i) {
            const Parameter *p = &current_function->params[i];
            if (p->by_ref && p->offset == target->data.variable.offset) { fputs("()", stdout); break; }
        }
    }
}

static void cpp_path(const Expr *target, size_t count) {
    if (target->type == EXPR_VARIABLE) { cpp_variable(target); return; }
    if (target->type == EXPR_FIELD) {
        cpp_path(target->data.field.owner, count);
        if (target->data.field.owner->value_type == VALUE_EDGE || target->data.field.owner->value_type == VALUE_PAIR || target->data.field.owner->value_type == VALUE_DIAMETER) printf(".%s", target->data.field.name);
        else printf(".f%zu", target->data.field.index);
        return;
    }
    fputs("fken::at(", stdout);
    cpp_path(target->data.index.array, count - 1);
    printf(", fken_index%zu)", count - 1);
}

static size_t cpp_capture_path(const Expr *target) {
    if (target->type == EXPR_VARIABLE) return 0;
    if (target->type == EXPR_FIELD) return cpp_capture_path(target->data.field.owner);
    size_t count = cpp_capture_path(target->data.index.array);
    printf("fken::Int fken_index%zu = ", count);
    cpp_expr(target->data.index.index);
    fputs("; ", stdout);
    // Recheck from the root: an index expression may resize an outer vector.
    cpp_path(target, count + 1);
    fputs("; ", stdout);
    return count + 1;
}

// Reference parameters may forward self, whose containing vector can reallocate.
static void cpp_argument(const Expr *arg, bool reference) {
    if (reference) printf("[&]() -> %s& { return ", cpp_type(arg->value_type));
    cpp_expr(arg);
    if (reference) fputs("; }", stdout);
}

static void cpp_capture_arg(const Expr *arg, size_t i, bool reference) {
    printf("%s fken_arg%zu = ", reference ? "auto" : cpp_type(arg->value_type), i);
    cpp_argument(arg, reference);
    fputs("; ", stdout);
}

static void cpp_method(const Expr *expr) {
    const Expr *receiver = expr->data.call.receiver;
    printf("([&]() -> %s { ", cpp_type(expr->value_type));
    size_t path_length = 0;
    if (lvalue_root(receiver)) path_length = cpp_capture_path(receiver);
    else {
        printf("%s fken_receiver = ", cpp_type(receiver->value_type));
        cpp_expr(receiver);
        fputs("; ", stdout);
    }
    for (size_t i = 0; i < expr->data.call.count; i++) {
        bool ref = expr->data.call.function && expr->data.call.function->params[i+1].by_ref;
        cpp_capture_arg(expr->data.call.args[i], i, ref);
    }
    if (expr->data.call.function) {
        // Resolve self each time: a method can resize the vector containing its receiver.
        const Function *f = expr->data.call.function;
        printf("return fken_fn%zu([&]() -> %s& { return ", f->id, cpp_type(receiver->value_type));
        if (lvalue_root(receiver)) cpp_path(receiver, path_length);
        else fputs("fken_receiver", stdout);
        fputs("; }", stdout);
        for (size_t i = 0; i < expr->data.call.count; ++i) printf(", fken_arg%zu", i);
        fputs("); }())", stdout);
        return;
    }
    bool graph = graph_type(receiver->value_type);
    if (graph) fputs("return ", stdout);
    else printf("return fken::%s(", expr->data.call.name);
    if (lvalue_root(receiver)) cpp_path(receiver, path_length);
    else fputs("fken_receiver", stdout);
    if (graph) printf(".%s(", expr->data.call.name);
    for (size_t i = 0; i < expr->data.call.count; i++)
        printf("%sstd::move(fken_arg%zu)", graph && i == 0 ? "" : ", ", i);
    fputs("); }())", stdout);
}

static void cpp_expr(const Expr *expr) {
    switch (expr->type) {
        case EXPR_TEXT:
            if (expr->value_type == VALUE_CHAR) {
                printf("'\\%03o'", (unsigned char)expr->data.text[0]);
            } else {
                fputs("std::string(\"", stdout);
                for (const unsigned char *p = (const unsigned char *)expr->data.text; *p; ++p)
                    printf("\\%03o", *p);
                fputs("\")", stdout);
            }
            break;
        case EXPR_NUMBER: {
            errno = 0;
            char *end;
            long long value = strtoll(expr->data.number.literal, &end, 10);
            if (!errno && *end == '\0') {
                printf("fken::Int(INT64_C(%lld))", value);
            } else {
                printf("fken::Int::parse(\"%s\")", expr->data.number.literal);
            }
            break;
        }
        case EXPR_VARIABLE:
            cpp_variable(expr);
            break;
        case EXPR_UNARY:
            fputs("(-", stdout);
            cpp_expr(expr->data.unary.operand);
            putchar(')');
            break;
        case EXPR_BINARY: {
            const char *op = NULL;
            switch (expr->data.binary.op) {
                case TK_PLUS: op = "+"; break;
                case TK_MINUS: op = "-"; break;
                case TK_STAR: op = "*"; break;
                case TK_SLASH: op = "/"; break;
                case TK_EQ: op = "=="; break;
                case TK_NE: op = "!="; break;
                case TK_LT: op = "<"; break;
                case TK_LE: op = "<="; break;
                case TK_GT: op = ">"; break;
                case TK_GE: op = ">="; break;
                default: error_at(NULL, "unsupported binary operator");
            }
            ValueType operand = expr->data.binary.left->value_type;
            const char *type = operand == VALUE_CHAR ? "unsigned char" : cpp_type(operand);
            printf("([&]() { %s fken_left = ", type);
            cpp_expr(expr->data.binary.left);
            printf("; %s fken_right = ", type);
            cpp_expr(expr->data.binary.right);
            if (operand != VALUE_INT && expr->value_type == VALUE_INT)
                printf("; return fken::Int(fken_left %s fken_right); }())", op);
            else printf("; return fken_left %s fken_right; }())", op);
            break;
        }
        case EXPR_CALL: {
            if (expr->data.call.receiver) { cpp_method(expr); break; }
            Function *function = expr->data.call.function;
            size_t count = expr->data.call.count;
            if (count >= 2) {
                printf("([&]() -> %s { ", cpp_type(expr->value_type));
                for (size_t i = 0; i < count; i++) {
                    bool by_ref = function && function->params[i].by_ref;
                    cpp_capture_arg(expr->data.call.args[i], i, by_ref);
                }
                fputs("return ", stdout);
            }
            if (function) printf("fken_fn%zu(", function->id);
            else printf("fken::%s(", expr->data.call.name);
            for (size_t i = 0; i < count; i++) {
                if (i) fputs(", ", stdout);
                if (count >= 2) {
                    if (function && function->params[i].by_ref) printf("fken_arg%zu", i);
                    else printf("std::move(fken_arg%zu)", i);
                } else {
                    cpp_argument(expr->data.call.args[i], function && function->params[i].by_ref);
                }
            }
            putchar(')');
            if (count >= 2) fputs("; }())", stdout);
            break;
        }
        case EXPR_ARRAY:
            printf("%s{", cpp_type(expr->value_type));
            for (size_t i = 0; i < expr->data.array.count; i++) {
                if (i) fputs(", ", stdout);
                cpp_expr(expr->data.array.items[i]);
            }
            putchar('}');
            break;
        case EXPR_RECORD:
            printf("([&]() { %s fken_record{}; ", cpp_type(expr->value_type));
            for (size_t i = 0; i < expr->data.record.count; ++i) {
                printf("fken_record.f%zu = ", expr->data.record.indices[i]);
                cpp_expr(expr->data.record.values[i]); fputs("; ", stdout);
            }
            fputs("return fken_record; }())", stdout); break;
        case EXPR_FIELD:
            putchar('('); cpp_expr(expr->data.field.owner); putchar(')');
            if (expr->data.field.owner->value_type == VALUE_EDGE || expr->data.field.owner->value_type == VALUE_PAIR || expr->data.field.owner->value_type == VALUE_DIAMETER) printf(".%s", expr->data.field.name);
            else printf(".f%zu", expr->data.field.index);
            break;
        case EXPR_CONSTRUCT:
            if (graph_type(expr->value_type)) {
                printf("%s(", cpp_type(expr->value_type)); cpp_expr(expr->data.construct.sizes[0]); putchar(')');
                break;
            }
            if (!expr->data.construct.count) { printf("%s{}", cpp_type(expr->value_type)); break; }
            fputs("([&]() { ", stdout);
            for (size_t i = 0; i < expr->data.construct.count; i++) {
                printf("std::size_t fken_size%zu = (", i);
                cpp_expr(expr->data.construct.sizes[i]);
                fputs(").to_size(\"array size\"); ", stdout);
            }
            printf("return fken::make_vec<%d,%s>({", type_rank(expr->value_type), cpp_type(type_base(expr->value_type)));
            for (size_t i = 0; i < expr->data.construct.count; i++) printf("%sfken_size%zu", i ? ", " : "", i);
            fputs("}); }())", stdout);
            break;
        case EXPR_INDEX: {
            printf("([&]() -> %s { ", cpp_type(expr->value_type));
            if (lvalue_root(expr)) {
                size_t count = cpp_capture_path(expr);
                fputs("return ", stdout);
                cpp_path(expr, count);
                fputs("; }())", stdout);
            } else {
                fputs("auto fken_array = ", stdout);
                cpp_expr(expr->data.index.array);
                fputs("; fken::Int fken_index = ", stdout);
                cpp_expr(expr->data.index.index);
                fputs("; return fken::at(fken_array, fken_index); }())", stdout);
            }
            break;
        }
        default: error_at(NULL, "unsupported expression");
    }
}

static void cpp_assignment(const Statement *stmt) {
    const Expr *target = stmt->data.assign.target;
    fputs("([&]() { ", stdout);
    size_t count = cpp_capture_path(target);
    const char *op = NULL;
    switch (stmt->data.assign.op) {
        case TK_ASSIGN: op = "="; break;
        case TK_PLUS_ASSIGN: case TK_INC: op = "+="; break;
        case TK_MINUS_ASSIGN: case TK_DEC: op = "-="; break;
        case TK_STAR_ASSIGN: op = "*="; break;
        case TK_SLASH_ASSIGN: op = "/="; break;
        default: error_at(stmt->token, "unsupported assignment");
    }
    printf("%s fken_value = ", cpp_type(target->value_type));
    if (stmt->data.assign.op == TK_INC || stmt->data.assign.op == TK_DEC) {
        fputs("fken::Int(1)", stdout);
    } else {
        cpp_expr(stmt->data.assign.value);
    }
    fputs("; ", stdout);
    cpp_path(target, count);
    printf(" %s std::move(fken_value); }())", op);
}

static void cpp_statement(const Statement *stmt) {
    switch (stmt->type) {
        case STMT_LET:
            printf("%s fken_v%d = ", cpp_type(stmt->data.let.value->value_type),
                   -stmt->data.let.offset);
            cpp_expr(stmt->data.let.value);
            fputs(";\n", stdout);
            break;
        case STMT_ASSIGN:
            cpp_assignment(stmt);
            fputs(";\n", stdout);
            break;
        case STMT_EXPR:
            cpp_expr(stmt->data.expr.expression);
            fputs(";\n", stdout);
            break;
        case STMT_BREAK:
            fputs("break;\n", stdout);
            break;
        case STMT_RETURN:
            fputs("return", stdout);
            if (stmt->data.return_stmt.value) {
                putchar(' ');
                cpp_expr(stmt->data.return_stmt.value);
            }
            fputs(";\n", stdout);
            break;
        case STMT_WRITE:
            fputs("std::cout << ", stdout);
            cpp_expr(stmt->data.write.value);
            fputs(";\n", stdout);
            break;
        case STMT_PRINT: {
            bool vector = type_rank(stmt->data.print.expression->value_type) &&
                          stmt->data.print.expression->value_type != VALUE_STRING;
            fputs(vector ? "fken::operator<<(std::cout, " : "std::cout << ", stdout);
            cpp_expr(stmt->data.print.expression);
            if (vector) putchar(')');
            fputs(" << '\\n';\n", stdout);
            break;
        }
        case STMT_DECLARATIONS:
            for (size_t i = 0; i < stmt->data.block.count; ++i) cpp_statement(stmt->data.block.statements[i]);
            break;
        case STMT_BLOCK:
            fputs("{\n", stdout);
            for (size_t i = 0; i < stmt->data.block.count; i++) {
                cpp_statement(stmt->data.block.statements[i]);
            }
            fputs("}\n", stdout);
            break;
        case STMT_IF:
            fputs("if (", stdout);
            cpp_expr(stmt->data.if_stmt.condition);
            fputs(")\n", stdout);
            cpp_statement(stmt->data.if_stmt.then_branch);
            if (stmt->data.if_stmt.else_branch) {
                fputs("else\n", stdout);
                cpp_statement(stmt->data.if_stmt.else_branch);
            }
            break;
        case STMT_WHILE:
            fputs("while (", stdout);
            cpp_expr(stmt->data.while_stmt.condition);
            fputs(")\n", stdout);
            cpp_statement(stmt->data.while_stmt.body);
            break;
        case STMT_EACH:
            fputs("{ auto fken_range = ", stdout); cpp_expr(stmt->data.each.values);
            printf("; for (auto fken_v%d : fken_range) ", -stmt->data.each.offset);
            cpp_statement(stmt->data.each.body); fputs("}\n", stdout); break;
        case STMT_FOR:
            fputs("{\n", stdout);
            if (stmt->data.for_stmt.init) cpp_statement(stmt->data.for_stmt.init);
            fputs("for (; ", stdout);
            if (stmt->data.for_stmt.condition) cpp_expr(stmt->data.for_stmt.condition);
            else fputs("true", stdout);
            fputs("; ", stdout);
            if (stmt->data.for_stmt.step) {
                if (stmt->data.for_stmt.step->type == STMT_EXPR) {
                    cpp_expr(stmt->data.for_stmt.step->data.expr.expression);
                } else if (stmt->data.for_stmt.step->type == STMT_ASSIGN) {
                    cpp_assignment(stmt->data.for_stmt.step);
                } else {
                    fputs("([&]() ", stdout);
                    cpp_statement(stmt->data.for_stmt.step);
                    fputs("())", stdout);
                }
            }
            fputs(")\n", stdout);
            cpp_statement(stmt->data.for_stmt.body);
            fputs("}\n", stdout);
            break;
        default: error_at(NULL, "unsupported statement");
    }
}

static void cpp_signature(const Function *function) {
    printf("%s fken_fn%zu(", cpp_type(function->return_type), function->id);
    for (size_t i = 0; i < function->count; i++) {
        const Parameter *param = &function->params[i];
        if (i) fputs(", ", stdout);
        if (param->by_ref) printf("std::function<%s&()> fken_v%d", cpp_type(param->type), -param->offset);
        else printf("%s %sfken_v%d", cpp_type(param->type), param->by_ref ? "&" : "", -param->offset);
    }
    putchar(')');
}

void generate_cpp(const Program *program) {
    fputs("// Generated by fken_cpl. Compile with GCC/Clang C++20 and Boost headers.\n", stdout);
    fputs(cpp_runtime, stdout);
    putchar('\n');
    for (size_t i = 0; i < program->record_count; ++i) {
        const Record *r = &program->records[i];
        printf("struct fken_record%zu {\n", i);
        for (size_t j = 0; j < r->count; ++j) printf("%s f%zu{};\n", cpp_type(r->fields[j].type), j);
        fputs("};\n", stdout);
    }
    for (size_t i = 0; i < program->function_count; i++) {
        cpp_signature(program->functions[i]);
        fputs(";\n", stdout);
    }
    for (size_t i = 0; i < program->function_count; i++) {
        cpp_signature(program->functions[i]);
        putchar('\n');
        current_function = program->functions[i];
        cpp_statement(current_function->body);
        current_function = NULL;
    }
    fputs("\nint main() {\n"
          "std::ios::sync_with_stdio(false);\n"
          "std::cin.tie(nullptr);\n"
          "try {\n", stdout);
    for (size_t i = 0; i < program->count; i++) {
        cpp_statement(program->statesments[i]);
    }
    fputs("return 0;\n"
          "} catch (const std::exception &error) {\n"
          "std::cerr << \"runtime error: \" << error.what() << '\\n';\n"
          "return 1;\n"
          "}\n}\n", stdout);
}
