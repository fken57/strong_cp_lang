#include "util.c"

void generate_let(Statement *stmt);
void generate_if(Statement *stmt,int *id);
void generate_block(Statement *stmt,int *id);
void generate_while(Statement *stmt,int *id);
void generate_print(Statement *stmt);
void generate_assign(Statement *stmt);
void generate(Program *program);
void start(Program *program);
void end();

void start(Program *program){
    printf (".data\n"
        "L_fmt:\n"
        "\t.ascii \"%%d\\n\\0\"\n"
        ".text\n"
        ".globl main\n"
        "main:\n"
        "\tpushq %%rbp\n"
        "\tmovq %%rsp, %%rbp\n");
    printf("\tsubq $%d, %%rsp\n", program->required_size);
    printf("\tmovq $0, %%rax\n");
}

void end(){
    printf(
    "\tmovl $0, %%eax\n"
    "\tleave\n"
    "\tret\n");

    return;
}

void generate_binary_operation(ExprType type){
    switch(type) {
        case TK_PLUS:
            printf("\taddq %%rdi, %%rax\n");
            break;
        case TK_MINUS:
            printf("\tsubq %%rdi, %%rax\n");
            break;
        case TK_STAR:
            printf("\timulq %%rdi, %%rax\n");
            break;
        case TK_SLASH:
            printf("\tcqto\n");
            printf("\tidivq %%rdi\n");
            break;
        case TK_EQ:
            printf("\tcmpq %%rdi, %%rax\n");
            printf("\tsete %%al\n");
            printf("\tmovzbq %%al, %%rax\n");
            break;
        case TK_GE:
            printf("\tcmpq %%rdi, %%rax\n");
            printf("\tsetge %%al\n");
            printf("\tmovzbq %%al, %%rax\n");
            break;
        case TK_GT:
            printf("\tcmpq %%rdi, %%rax\n");
            printf("\tsetg %%al\n");
            printf("\tmovzbq %%al, %%rax\n");
            break;
        case TK_LE:
            printf("\tcmpq %%rdi, %%rax\n");
            printf("\tsetle %%al\n");
            printf("\tmovzbq %%al, %%rax\n");
            break;
        case TK_LT:
            printf("\tcmpq %%rdi, %%rax\n");
            printf("\tsetl %%al\n");
            printf("\tmovzbq %%al, %%rax\n");
            break;
        case TK_NE:
            printf("\tcmpq %%rdi, %%rax\n");
            printf("\tsetne %%al\n");
            printf("\tmovzbq %%al, %%rax\n");
            break;
    }
}

void generate_expr(Expr *expr){
    if (expr->type == EXPR_CALL || expr->type == EXPR_ARRAY || expr->type == EXPR_INDEX || expr->type == EXPR_CONSTRUCT) {
        error_at(expr->token, "this feature requires the C++ backend (fken_cpl)");
    }
    if(expr->type == EXPR_BINARY) {
        generate_expr(expr->data.binary.left);
        printf("pushq %%rax\n");
        generate_expr(expr->data.binary.right);
        printf("movq %%rax, %%rdi\n");
        printf("popq %%rax\n");
        generate_binary_operation(expr->data.binary.op);
    }
    if(expr->type == EXPR_UNARY) {
        generate_expr(expr->data.unary.operand);
        printf("negq %%rax\n");
    }
    if(expr->type == EXPR_NUMBER) {
        printf("\tmovq $%lld, %%rax\n", expr->data.number.number);
    }
    if(expr->type == EXPR_VARIABLE) {
        printf("\tmovq %d(%%rbp), %%rax\n", expr->data.variable.offset);
    }
}

void generate_let(Statement *stmt){
    generate_expr(stmt->data.let.value);
    printf("\tmovq %%rax, %d(%%rbp)\n", stmt->data.let.offset);
}

void generate_assign(Statement *stmt){
    if ((stmt->data.assign.target && stmt->data.assign.target->type != EXPR_VARIABLE) || stmt->data.assign.op != TK_ASSIGN) {
        error_at(stmt->token, "this update requires the C++ backend (fken_cpl)");
    }
    generate_expr(stmt->data.assign.value);
    printf("\tmovq %%rax, %d(%%rbp)\n", stmt->data.assign.offset);
}

void generate_if(Statement *stmt,int *id){
    int label=(*id)++;
    generate_expr(stmt->data.if_stmt.condition);
    printf("\tcmpq $0, %%rax\n");
    printf("\tje else_%d\n", label);
    generate_block(stmt->data.if_stmt.then_branch, id);
    printf("jmp end_%d\n", label);
    printf("else_%d:\n", label);
    if(stmt->data.if_stmt.else_branch != NULL){
        generate_block(stmt->data.if_stmt.else_branch, id);
    }
    printf("end_%d:\n", label);
}

void generate_while(Statement *stmt,int *id){
    int label=(*id)++;
    printf("while_%d:\n", label);
    generate_expr(stmt->data.while_stmt.condition);
    printf("\tcmpq $0, %%rax\n");
    printf("\tje end_%d\n", label);
    generate_block(stmt->data.while_stmt.body, id);
    printf("jmp while_%d\n", label);
    printf("end_%d:\n", label);
}

void generate_block(Statement *stmt,int *id){
    for(int i = 0; i < stmt->data.block.count; i++) {
        if(stmt->data.block.statements[i] == NULL) {
            continue;
        }
        if (stmt->data.block.statements[i]->type == STMT_FOR ||
            stmt->data.block.statements[i]->type == STMT_BREAK ||
            stmt->data.block.statements[i]->type == STMT_RETURN || stmt->data.block.statements[i]->type == STMT_EXPR) {
            error_at(stmt->data.block.statements[i]->token, "this statement requires the C++ backend (fken_cpl)");
        }
        if(stmt->data.block.statements[i]->type == STMT_LET) {
            generate_let(stmt->data.block.statements[i]);
        }
        if(stmt->data.block.statements[i]->type == STMT_PRINT) {
            generate_print(stmt->data.block.statements[i]);
        }
        if(stmt->data.block.statements[i]->type == STMT_ASSIGN) {
            generate_assign(stmt->data.block.statements[i]);
        }
        if(stmt->data.block.statements[i]->type == STMT_IF) {
            generate_if(stmt->data.block.statements[i],id);
        }
        if(stmt->data.block.statements[i]->type == STMT_WHILE) {
            generate_while(stmt->data.block.statements[i],id);
        }
        if(stmt->data.block.statements[i]->type == STMT_BLOCK) {
            generate_block(stmt->data.block.statements[i],id);
        }
    }
}

void generate_print(Statement *stmt){
    generate_expr(stmt->data.print.expression);
    printf("\tleaq L_fmt(%%rip), %%rdi\n"
    "\tmovq %%rax, %%rsi\n"
    "\tmovq $0 , %%rax\n"
    "\tcall printf\n");
}

void generate(Program *program){
    if (program->function_count) error_at(program->functions[0]->token, "functions require the C++ backend (fken_cpl)");
    start(program);
    int id = 0;
    for(int i = 0; i < program->count; i++) {
        if(program->statesments[i] == NULL) {
            continue;
        }
        if (program->statesments[i]->type == STMT_FOR ||
            program->statesments[i]->type == STMT_BREAK ||
            program->statesments[i]->type == STMT_RETURN || program->statesments[i]->type == STMT_EXPR) {
            error_at(program->statesments[i]->token, "this statement requires the C++ backend (fken_cpl)");
        }
        if(program->statesments[i]->type == STMT_LET) {
            generate_let(program->statesments[i]);
        }
        if(program->statesments[i]->type == STMT_PRINT) {
            generate_print(program->statesments[i]);
        }
        if(program->statesments[i]->type == STMT_ASSIGN) {
            generate_assign(program->statesments[i]);
        }
        if(program->statesments[i]->type == STMT_IF) {
            generate_if(program->statesments[i],&id);
        }
        if(program->statesments[i]->type == STMT_WHILE) {
            generate_while(program->statesments[i],&id);
        }
        if(program->statesments[i]->type == STMT_BLOCK) {
            generate_block(program->statesments[i],&id);
        }
    }
    end();
}
