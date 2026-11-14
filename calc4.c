

#include "typedef.c"
#include "util.c"
#include "lex.c"
#include "parser.c"
#include "analyze.c"
#include "generate.c"





int main(int argc, char *argv[]) {
    if (argc < 2) {
        return 1;
    }

    Source source;
    source.data = argv[1];
    source.length = strlen(argv[1]);
    source.capacity = source.length + 1;

    TokenArray *tokens = lex(&source);
    Program *program = parse(tokens);
    analyze(program);
    generate(program);
    //free_program(program);

    return 0;
}
