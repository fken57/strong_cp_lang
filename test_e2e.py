#!/usr/bin/env python3
"""lex → parse → analyze の E2E テスト。実行: python3 calc_compile/test_e2e.py"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parent

# 出力するのは AST に保存された宣言位置。レキサのデバッグ出力とは区別する。
RUNNER = r'''
static void dump_offsets(Statement *stmt) {
    switch (stmt->type) {
        case STMT_LET:
            printf("E2E let %s %d\n", stmt->data.let.name, stmt->data.let.offset);
            break;
        case STMT_BLOCK:
            for (size_t i = 0; i < stmt->data.block.count; i++)
                dump_offsets(stmt->data.block.statements[i]);
            break;
        case STMT_IF:
            dump_offsets(stmt->data.if_stmt.then_branch);
            if (stmt->data.if_stmt.else_branch)
                dump_offsets(stmt->data.if_stmt.else_branch);
            break;
        case STMT_WHILE:
            dump_offsets(stmt->data.while_stmt.body);
            break;
        default:
            break;
    }
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    Source source = {argv[1], strlen(argv[1]), strlen(argv[1]) + 1};
    TokenArray *tokens = lex(&source);
    Program *program = parse(tokens);
    analyze(program);
    for (size_t i = 0; i < program->count; i++)
        dump_offsets(program->statesments[i]);
    puts("E2E OK");
    return 0;
}
'''


class AnalyzeE2E(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="calc-e2e-")
        cls.addClassCleanup(cls.temp.cleanup)
        work = Path(cls.temp.name)
        # 各 .c が util.c / typedef.c を再 include する現在の構成に合わせ、
        # 一時ファイル内でだけ共通定義を一度にまとめる。実装ファイルは変更しない。
        sources = []
        for name in ("typedef.c", "util.c", "lex.c", "parser.c", "analyze.c"):
            source = (ROOT / name).read_text()
            sources.append(f'#line 1 "{ROOT / name}"\n' + re.sub(
                r'^\s*#\s*include\s*"(?:typedef|util)\.c"[^\n]*$',
                '', source, flags=re.MULTILINE))
        combined = work / "test_e2e.c"
        combined.write_text('\n'.join(sources) + '\n' + RUNNER)
        cls.binary = work / "test_e2e"
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-Wall", "-Wextra", "-Werror=implicit-function-declaration",
            "-Werror=incompatible-pointer-types", str(combined), "-o", str(cls.binary),
        ]
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        if result.returncode:
            raise RuntimeError("E2E runner build failed:\n" + result.stderr)

    def run_source(self, source):
        return subprocess.run([str(self.binary), source], capture_output=True,
                              text=True, timeout=3)

    def test_valid_programs(self):
        # スタック位置は8バイトずつ割り当てる現在の方針を検証する。
        cases = [
            ("empty", "", []),
            ("constant", "print(1 + 2 * 3);", []),
            ("one variable", "let a = 3; print(a);", [("a", -8)]),
            ("second variable lookup", "let a = 1; let b = 2; print(b);",
             [("a", -8), ("b", -16)]),
            ("initializer reference", "let a = 1; let b = a + 2; print(b);",
             [("a", -8), ("b", -16)]),
            ("assignment", "let a = 1; a = a + 1; print(a);", [("a", -8)]),
            ("unary reference", "let a = 1; print(-a);", [("a", -8)]),
            ("nested blocks", "{ let a = 1; { let b = a + 1; print(b); } print(a); }",
             [("a", -8), ("b", -16)]),
            ("outer variable survives block", "let a = 1; { let b = 2; } print(a);",
             [("a", -8), ("b", -16)]),
            ("if and else", "let a = 1; if (a) { print(a); } else { print(a); }",
             [("a", -8)]),
            ("while", "let a = 0; while (a < 3) { a = a + 1; } print(a);",
             [("a", -8)]),
            # トップレベルの文数より多くの変数が同時に存在する。
            ("symbol capacity", "{ let a = 1; let b = 2; let c = 3; print(a + b + c); }",
             [("a", -8), ("b", -16), ("c", -24)]),
            ("multiline", "let a = 1;\n\tprint(a);\n", [("a", -8)]),
        ]
        for name, source, offsets in cases:
            with self.subTest(name=name, source=source):
                result = self.run_source(source)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                actual = [line for line in result.stdout.splitlines() if line.startswith("E2E ")]
                expected = [f"E2E let {name} {offset}" for name, offset in offsets] + ["E2E OK"]
                self.assertEqual(actual, expected)

    def test_invalid_programs(self):
        cases = [
            ("undeclared with empty table", "print(missing);"),
            ("undeclared with populated table", "let a = 1; print(missing);"),
            ("undeclared assignment", "missing = 1;"),
            ("undeclared assignment value", "let a = 1; a = missing;"),
            ("undeclared initializer", "let a = missing;"),
            ("undeclared binary operand", "let a = 1; print(a + missing);"),
            ("undeclared unary operand", "print(-missing);"),
            ("undeclared if condition", "if (missing) {}"),
            ("undeclared while condition", "while (missing) {}"),
            ("block local escapes", "{ let a = 1; } print(a);"),
            ("nested local escapes", "{ { let a = 1; } print(a); }"),
            ("if local escapes", "if (1) { let a = 1; } print(a);"),
            ("then local unavailable in else", "if (1) { let a = 1; } else { print(a); }"),
            ("while local escapes", "while (0) { let a = 1; } print(a);"),
            ("missing semicolon", "let a = 1"),
            ("incomplete expression", "print(1 +);"),
            ("missing brace", "if (1) { print(1);"),
        ]
        for name, source in cases:
            with self.subTest(name=name, source=source):
                result = self.run_source(source)
                # シグナルによる異常終了は、正しいエラー検出として数えない。
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertNotIn("E2E OK", result.stdout)
                self.assertTrue(result.stderr.strip(), "error message is missing")


if __name__ == "__main__":
    unittest.main(verbosity=2)

