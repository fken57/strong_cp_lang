#include "util.c"

void analyze_let(Statement *stmt,int *offset,SymbolTable *symbol_table);
void analyze_if(Statement *stmt,int *offset,SymbolTable *symbol_table);
void analyze_block(Statement *stmt,int *offset,SymbolTable *symbol_table);
void analyze_while(Statement *stmt,int *offset,SymbolTable *symbol_table);
void analyze_print(Statement *stmt,int *offset,SymbolTable *symbol_table);  
void analyze(Program *program);
void analyze_assign(Statement *stmt,int *offset,SymbolTable *symbol_table);
void analyze_expr(Expr *expr,SymbolTable *symbol_table);

void analyze_expr(Expr *expr,SymbolTable *symbol_table){
    if(expr->type == EXPR_BINARY){
        analyze_expr(expr->data.binary.left,symbol_table);
        analyze_expr(expr->data.binary.right,symbol_table);
    } else if(expr->type == EXPR_UNARY){
        analyze_expr(expr->data.unary.operand,symbol_table);
    }
    else if(expr->type == EXPR_VARIABLE){
        bool is_var_exist=false;
        for(size_t i = symbol_table->count; i-- > 0;){
            if(strcmp(symbol_table->symbols[i].name, expr->data.variable.var_name) == 0){
                is_var_exist = true;
                expr->data.variable.offset = symbol_table->symbols[i].offset;
                break;
            }
        }
        if(!is_var_exist){
            // 変数が見つからなかった場合
            error_at(expr->token, "undefined variable '%s'", expr->data.variable.var_name);
        }
    }
}

void analyze_let(Statement *stmt, int *offset,SymbolTable *symbol_table){
    for (size_t i = symbol_table->scope_start; i < symbol_table->count; i++) {
        if (strcmp(symbol_table->symbols[i].name, stmt->data.let.name) == 0) {
            error_at(stmt->token, "variable '%s' is already declared in this scope", stmt->data.let.name);
        }
    }
    analyze_expr(stmt->data.let.value, symbol_table);
    if(symbol_table->count >= symbol_table->capacity){
        symbol_table->capacity *= 2;
        symbol_table->symbols = realloc(symbol_table->symbols, sizeof(Symbol) * symbol_table->capacity);
        if(symbol_table->symbols == NULL){
            fprintf(stderr, "Error: failed to reallocate memory\n");
            exit(1);
        }
    }
    (*offset)-=8;
    stmt->data.let.offset = *offset;
    symbol_table->symbols[symbol_table->count].name = stmt->data.let.name;
    symbol_table->symbols[symbol_table->count].offset = *offset;
    symbol_table->count++;
}

void analyze_while(Statement *stmt,int *offset,SymbolTable *symbol_table){
    analyze_expr(stmt->data.while_stmt.condition,symbol_table);
    analyze_block(stmt->data.while_stmt.body,offset,symbol_table);
}

void analyze_print(Statement *stmt,int *offset,SymbolTable *symbol_table){
    analyze_expr(stmt->data.print.expression,symbol_table);
}

void analyze_assign(Statement *stmt,int *offset,SymbolTable *symbol_table){
    bool is_var_exist = false;
    for(size_t i = symbol_table->count; i-- > 0;){
        if(strcmp(symbol_table->symbols[i].name, stmt->data.assign.name) == 0){
            is_var_exist = true;
            stmt->data.assign.offset = symbol_table->symbols[i].offset;
            break;
        }
    }
    if(!is_var_exist){
        error_at(stmt->token, "undefined variable '%s'", stmt->data.assign.name);
    }
    analyze_expr(stmt->data.assign.value,symbol_table);
}

void analyze_if(Statement *stmt,int *offset,SymbolTable *symbol_table){
    analyze_expr(stmt->data.if_stmt.condition,symbol_table);
    analyze_block(stmt->data.if_stmt.then_branch,offset,symbol_table);
    if(stmt->data.if_stmt.else_branch != NULL){
        analyze_block(stmt->data.if_stmt.else_branch,offset,symbol_table);
    }
}
void analyze_block(Statement *stmt,int *offset,SymbolTable *symbol_table){
    size_t saved_count =symbol_table->count;
    size_t saved_scope = symbol_table->scope_start;
    symbol_table->scope_start = saved_count;
    // TODO: Implement block statement analysis
    for(size_t i = 0; i < stmt->data.block.count; i++){
        switch(stmt->data.block.statements[i]->type){
            case STMT_LET:
                analyze_let(stmt->data.block.statements[i], offset, symbol_table);
                break;
            case STMT_ASSIGN:
                analyze_assign(stmt->data.block.statements[i], offset, symbol_table);
                break;
            case STMT_IF:
                analyze_if(stmt->data.block.statements[i], offset, symbol_table);
                break;
            case STMT_WHILE:
                analyze_while(stmt->data.block.statements[i], offset, symbol_table);
                break;
            case STMT_PRINT:
                analyze_print(stmt->data.block.statements[i], offset, symbol_table);
                break;
            case STMT_BLOCK:
                analyze_block(stmt->data.block.statements[i], offset, symbol_table);
                break;
            default:
                error_at(stmt->token, "unsupported statement");
        }
    }
    symbol_table->count = saved_count;
    symbol_table->scope_start = saved_scope;
}


void analyze(Program *program){
    int offset = 0;
    SymbolTable symbol_table;
     symbol_table.capacity = 8;
    symbol_table.symbols = malloc(sizeof(Symbol) * symbol_table.capacity);
    symbol_table.count = 0;
    symbol_table.scope_start = 0;

    for(size_t i = 0; i < program->count; i++){
        if(program->statesments[i]->type == STMT_LET){
            analyze_let(program->statesments[i], &offset,&symbol_table);
        }
        if(program->statesments[i]->type == STMT_ASSIGN){
            analyze_assign(program->statesments[i],&offset,&symbol_table);
        }
        if(program->statesments[i]->type == STMT_IF){
            analyze_if(program->statesments[i],&offset,&symbol_table);
        }
        if(program->statesments[i]->type == STMT_BLOCK){
            analyze_block(program->statesments[i],&offset,&symbol_table);
        }
        if(program->statesments[i]->type == STMT_WHILE){
            analyze_while(program->statesments[i],&offset,&symbol_table);
        }
        if(program->statesments[i]->type == STMT_PRINT){
            analyze_print(program->statesments[i],&offset,&symbol_table);
        }
    }

    program->required_size = -offset;
    free(symbol_table.symbols);
}
